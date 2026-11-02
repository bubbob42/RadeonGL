/*
 * card.c -- see card.h.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <devices/timer.h>
#include <utility/tagitem.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/modeid.h>
#include <libraries/Picasso96.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/timer.h>
#include <proto/Picasso96.h>

#include "card.h"
#include "r200reg.h"
#include "r2pm4.h"

struct Library *P96Base = NULL;
struct Device *TimerBase = NULL;

#define FENCE_REG     R_SCRATCH_REG2
#define TIMEOUT_US    500000UL      /* every wait of a flush */
#define CHUNK_DW      4096UL        /* commands copied into the ring at a time */
#define PAL_FILL      0x80000080UL  /* PACKET2 both ways round (CPRing) */
#define CANARY_VALUE  0xA5C3C3A5UL
#define PCI_MIRROR    0x0f00UL      /* the chip's PCI configuration (MEASURED, LockBench) */

/* RBBM_SOFT_RESET: the 3D blocks only, never the 2D engine or the render
 * backend, which are Radeon.card's too (Linux r100 names) */
#define SOFT_RESET_SE (1UL << 2)
#define SOFT_RESET_RE (1UL << 3)
#define SOFT_RESET_PP (1UL << 4)

/* --- small helpers ------------------------------------------------------------ */

static ULONG Align4k(ULONG a)
{
    return (a + 4095UL) & ~4095UL;
}

static void Why(char *why, ULONG len, const char *s)
{
    ULONG i;

    if (why == NULL || len == 0)
        return;
    for (i = 0; i + 1 < len && s[i] != 0; i++)
        why[i] = s[i];
    why[i] = 0;
}

static int Lower(int ch)
{
    return (ch >= 'A' && ch <= 'Z') ? ch - 'A' + 'a' : ch;
}

static BOOL SaysRadeon(const char *s)
{
    static const char want[] = "radeon";
    ULONG i;
    ULONG k;

    if (s == NULL)
        return FALSE;
    for (i = 0; s[i] != 0; i++)
    {
        for (k = 0; want[k] != 0 && Lower((UBYTE) s[i + k]) == want[k]; k++)
            ;
        if (want[k] == 0)
            return TRUE;
    }
    return FALSE;
}

/* The R200 family the back end programs: R200, RV250, RV280 (Linux
 * radeon_family: CHIP_R200, CHIP_RV250, CHIP_RV280). */
static BOOL R200Family(ULONG dev)
{
    return (BOOL) ((dev >= 0x5148 && dev <= 0x514f)     /* R200 */
                   || dev == 0x4966 || dev == 0x4967 || dev == 0x4c66 || dev == 0x4c67 /* RV250 */
                   || (dev >= 0x5960 && dev <= 0x5967) || dev == 0x5c61 || dev == 0x5c63); /* RV280 */
}

/* --- the clock: timer.device's E clock --------------------------------------------- */

static ULONG clk_Freq = 709379;

static ULONG Ticks(void)
{
    struct EClockVal ev;

    ReadEClock(&ev);
    return ev.ev_lo;
}

/* seconds first, then the remainder in two steps (eclock.c) */
static ULONG TicksUs(ULONG t)
{
    ULONG s;
    ULONG r;
    ULONG ms;
    ULONG rest;

    s = t / clk_Freq;
    r = t - s * clk_Freq;
    ms = (r * 1000UL) / clk_Freq;
    rest = (r * 1000UL) - ms * clk_Freq;
    return s * 1000000UL + ms * 1000UL + (rest * 1000UL) / clk_Freq;
}

/* --- struct R2Hw over the registers --------------------------------------------------- */

static ULONG Swap32(ULONG v)
{
    return ((v >> 24) & 0xffUL) | ((v >> 8) & 0xff00UL) | ((v << 8) & 0xff0000UL) | (v << 24);
}

static ULONG HwRead(APTR hw, ULONG reg)
{
    struct Card *k;
    ULONG v;

    k = (struct Card *) hw;
    v = *(volatile ULONG *) ((UBYTE *) k->mmio + reg);
    return k->mmio_swap ? Swap32(v) : v;
}

static void HwWrite(APTR hw, ULONG reg, ULONG val)
{
    struct Card *k;

    k = (struct Card *) hw;
    *(volatile ULONG *) ((UBYTE *) k->mmio + reg) = k->mmio_swap ? Swap32(val) : val;
}

static ULONG HwReadBack(APTR hw, volatile ULONG *p)
{
    (void) hw;
    return *p;
}

/* microseconds since the flush (or the open) began: small, never wraps */
static ULONG HwClock(APTR hw)
{
    return TicksUs(Ticks() - ((struct Card *) hw)->clock_base);
}

static BOOL Reset3D(struct Card *k)
{
    HwWrite(k, R_RBBM_SOFT_RESET, SOFT_RESET_SE | SOFT_RESET_RE | SOFT_RESET_PP);
    (void) HwRead(k, R_RBBM_SOFT_RESET);
    HwWrite(k, R_RBBM_SOFT_RESET, 0);
    (void) HwRead(k, R_RBBM_SOFT_RESET);
    return (BOOL) (R2Cp_WaitIdle(&k->hw, TIMEOUT_US) == R2_OK);
}

/* --- the bitmaps -------------------------------------------------------------------- */

/* Lock bitmap i: mem, bpr and (once the chip is known) gpu. TRUE when it is
 * on the board, or with any_place also when P96 has it elsewhere (the
 * texel writes of a heap off the board go to its backup). */
static BOOL LockBm(struct Card *k, ULONG i, BOOL any_place)
{
    struct CardBm *b;
    struct RenderInfo ri;

    b = &k->b[i];
    if (b->bm == NULL || b->lock != 0)
        return FALSE;
    b->lock = p96LockBitMap(b->bm, (UBYTE *) &ri, sizeof(ri));
    if (b->lock == 0)
        return FALSE;
    b->mem = (UBYTE *) ri.Memory;
    b->bpr = (ULONG) ri.BytesPerRow;
    if (b->mem == NULL || (!any_place && !p96GetBitMapAttr(b->bm, P96BMA_ISONBOARD)))
    {
        p96UnlockBitMap(b->bm, b->lock);
        b->lock = 0;
        b->mem = NULL;
        return FALSE;
    }
    if (k->ap_size != 0)
        b->gpu = k->fb_start + ((ULONG) b->mem - (ULONG) k->membase) % k->ap_size;
    return TRUE;
}

static void UnlockBm(struct Card *k, ULONG i)
{
    struct CardBm *b;

    b = &k->b[i];
    if (b->bm != NULL && b->lock != 0)
        p96UnlockBitMap(b->bm, b->lock);
    b->lock = 0;
    b->mem = NULL;
}

static void UnlockAll(struct Card *k)
{
    ULONG i;

    for (i = CARD_NBM; i-- > 0;)
        UnlockBm(k, i);
}

/* A friend bitmap of the screen of at least bytes, rows of CARD_ROW bytes. */
static BOOL AllocData(struct Card *k, ULONG i, ULONG bytes)
{
    struct BitMap *sbm;

    sbm = k->scr->RastPort.BitMap;
    k->b[i].bytes = bytes;
    k->b[i].bm = AllocBitMap(CARD_ROW / k->bpp, (bytes + CARD_ROW - 1) / CARD_ROW,
                             GetBitMapAttr(sbm, BMA_DEPTH), BMF_MINPLANES, sbm);
    if (k->b[i].bm != NULL && !p96GetBitMapAttr(k->b[i].bm, P96BMA_ISP96))
    {
        FreeBitMap(k->b[i].bm);
        k->b[i].bm = NULL;
    }
    return (BOOL) (k->b[i].bm != NULL);
}

/* Is bitmap i in video RAM now? P96 may keep a bitmap it cannot place on
 * the board (too big, too tall) in fast RAM. */
static BOOL OnBoard(struct Card *k, ULONG i)
{
    BOOL on;

    if (!LockBm(k, i, TRUE))
        return FALSE;
    on = (BOOL) (p96GetBitMapAttr(k->b[i].bm, P96BMA_ISONBOARD) != 0);
    UnlockBm(k, i);
    return on;
}

/* Where target and ring sit in their bitmaps, from this lock: 4 KB aligned
 * (their content does not outlive a flush). The heap's place in its bitmap
 * is fixed at the open (k->heap_delta): its content must survive a move. */

static void TargetAt(struct Card *k, ULONG *color, ULONG *depth)
{
    *color = Align4k(k->b[CARD_TARGET].gpu);
    *depth = Align4k(*color + k->color_bytes);
}

static UBYTE *HeapCpu(struct Card *k)
{
    return k->b[CARD_HEAP].mem + k->heap_delta;
}

/* --- the checker's index reads: the stream is in staging until the flush ----- */

static LONG Peek(APTR ctx, ULONG gpu, ULONG *val)
{
    struct Card *k;
    ULONG off;

    k = (struct Card *) ctx;
    off = gpu - k->heap_gpu;
    if (gpu < k->heap_gpu || off < k->core->stream_off
        || off + 4 > k->core->stream_off + k->core->stream_size)
        return -1;
    *val = k->staging[(off - k->core->stream_off) / 4];
    return 0;
}

/* --- the command buffer --------------------------------------------------------------- */

static void ResetCs(struct Card *k)
{
    k->cs.ptr = k->cmd;
    k->cs.end = k->cmd + CARD_CMD_DW - CARD_RESERVE;
    k->cs.error = R2_OK;
    k->cs.immd = NULL;
}

/* The buffer is full: run it (between packets, as R2Cs promises). */
static LONG Need(struct R2Cs *cs, ULONG ndw)
{
    struct Card *k;

    k = (struct Card *) cs->owner;
    if (ndw > (CARD_CMD_DW - CARD_RESERVE) / 2)
        return R2_ETOOBIG;
    if (k->in_flush)
        return R2_EFULL;
    (void) Card_Flush(k, FALSE);
    return R2_OK;
}

/* What was built is dropped: the GPU never saw it, so the core emits its
 * whole state again (and follows moved bitmaps). */
static void Drop(struct Card *k)
{
    ResetCs(k);
    Core_Rebase(k->core, k->heap_gpu, k->color_gpu, k->depth_gpu);
    R2Chk_Reset(&k->chk);
    k->copied = 0;
    k->gen = k->core->stream_gen;
    k->dropped++;
}

/* The commands into the ring, in chunks that end at packet boundaries (the
 * ring's wrap filler must never land inside a packet). */
static LONG CopyCommands(struct Card *k, ULONG n)
{
    ULONG i;
    ULONG c;
    ULONG h;
    ULONG len;
    ULONG *p;
    ULONG j;

    i = 0;
    while (i < n)
    {
        c = 0;
        while (i + c < n)
        {
            h = Swap32(k->cmd[i + c]);          /* stored byte-reversed */
            len = (R2_PKT_TYPE(h) == 2) ? 1 : R2_PKT_COUNT(h) + 2;
            if (c != 0 && c + len > CHUNK_DW)
                break;
            c += len;
        }
        if (i + c > n)
            return R2_EARG;                     /* a packet past the end */
        p = R2Cs_Room(&k->ring.cs, c);
        if (p == NULL)
            return k->ring.cs.error;
        for (j = 0; j < c; j++)
            p[j] = k->cmd[i + j];
        k->ring.cs.ptr = p + c;
        i += c;
    }
    return R2_OK;
}

/* --- the flush ------------------------------------------------------------------------ */

LONG Card_Flush(struct Card *k, BOOL present)
{
    struct CoreScreen s;
    volatile ULONG *dst;
    ULONG color;
    ULONG depth;
    ULONG heap;
    ULONG ring_gpu;
    ULONG *ring_cpu;
    ULONG n;
    ULONG i;
    ULONG t0;
    ULONG tf;
    ULONG us;
    ULONG seq;
    ULONG reset;
    BOOL scr_ok;
    LONG e;

    if (k->in_flush)
        return R2_OK;
    n = (ULONG) (k->cs.ptr - k->cmd);
    if (k->lost != 0 || (n == 0 && !present))
    {
        ResetCs(k);
        return (k->lost != 0) ? R2_ETIMEOUT : R2_OK;
    }
    k->in_flush = 1;
    k->clock_base = Ticks();
    k->flushes++;
    e = R2_OK;
    scr_ok = FALSE;

    /* 1. the locks: target, ring, heap, on the board */
    if (!LockBm(k, CARD_TARGET, FALSE) || !LockBm(k, CARD_RING, FALSE)
        || !LockBm(k, CARD_HEAP, FALSE))
    {
        UnlockAll(k);
        k->offboard++;
        Drop(k);
        k->in_flush = 0;
        return R2_OK;
    }

    /* 2. did a bitmap move since this frame was built? */
    TargetAt(k, &color, &depth);
    heap = k->b[CARD_HEAP].gpu + k->heap_delta;
    if (color != k->color_gpu || depth != k->depth_gpu || heap != k->heap_gpu)
    {
        UnlockAll(k);
        k->moved++;
        k->color_gpu = color;
        k->depth_gpu = depth;
        k->heap_gpu = heap;
        if ((heap & 31) != 0)
            k->lost = CARD_LOST_CP;             /* textures can no longer be aligned */
        Drop(k);
        k->in_flush = 0;
        return R2_OK;
    }

    /* 3. the present, at the screen bitmap's address of this lock */
    if (present)
    {
        scr_ok = LockBm(k, CARD_SCREEN, FALSE);
        if (scr_ok && (k->b[CARD_SCREEN].gpu & 15) == 0 && (k->b[CARD_SCREEN].bpr / k->bpp) % 8 == 0)
        {
            s.gpu = k->b[CARD_SCREEN].gpu;
            s.pitch = k->b[CARD_SCREEN].bpr / k->bpp;
            s.format = k->color_format;
            s.width = k->width;
            s.height = k->height;
            k->cs.end = k->cmd + CARD_CMD_DW;   /* the reserve */
            Core_Present(k->core, &s, 0, 0);
            k->presents++;
        }
        else
        {
            if (scr_ok)
                UnlockBm(k, CARD_SCREEN);
            scr_ok = FALSE;
            k->noscreen++;
        }
    }
    n = (ULONG) (k->cs.ptr - k->cmd);
    if (k->cs.error != R2_OK)
    {
        k->lost = CARD_LOST_CHECKER;
        k->err_code = k->cs.error;
        goto unlock;
    }

    /* 4. every packet through the checker before the CP sees one */
    if (k->check)
    {
        k->cfg.range[0].gpu = k->color_gpu;
        k->cfg.range[0].size = k->color_bytes;
        k->cfg.range[0].kinds = R2K_COLOR | R2K_TEXTURE;    /* the present reads it */
        k->cfg.range[1].gpu = k->depth_gpu;
        k->cfg.range[1].size = k->depth_bytes;
        k->cfg.range[1].kinds = R2K_DEPTH;
        k->cfg.range[2].gpu = k->heap_gpu;
        k->cfg.range[2].size = k->heap_size;
        k->cfg.range[2].kinds = R2K_TEXTURE | R2K_VERTEX | R2K_INDEX;
        k->cfg.nranges = 3;
        if (scr_ok)
        {
            k->cfg.range[3].gpu = k->b[CARD_SCREEN].gpu;
            k->cfg.range[3].size = k->b[CARD_SCREEN].bpr * k->height;
            k->cfg.range[3].kinds = R2K_COLOR;
            k->cfg.nranges = 4;
        }
        e = R2Chk_Run(&k->cfg, &k->chk, k->cmd, n, 1, 0, &k->err);
        if (e != R2CE_OK)
        {
            k->err_code = e;
            k->lost = CARD_LOST_CHECKER;
            goto unlock;
        }
    }

    /* 5. the CP, on the ring at this lock's address */
    if (R2Cp_WaitIdle(&k->hw, TIMEOUT_US) != R2_OK)
    {
        k->lost = CARD_LOST_CP;
        goto unlock;
    }
    ring_gpu = Align4k(k->b[CARD_RING].gpu);
    ring_cpu = (ULONG *) (k->b[CARD_RING].mem + (ring_gpu - k->b[CARD_RING].gpu));
    if (R2Cp_Start(&k->hw, ring_gpu, CARD_RING_DW, 0) != R2_OK
        || R2Ring_Init(&k->ring, ring_cpu, ring_gpu, CARD_RING_DW, &k->hw, 1, FENCE_REG,
                       TIMEOUT_US) != R2_OK)
    {
        k->lost = CARD_LOST_CP;
        goto stop;
    }
    k->ring.kick = 2048;

    /* 6. the stream's new part into video RAM, then the commands */
    if (k->core->stream_gen != k->gen)
    {
        k->gen = k->core->stream_gen;
        k->copied = 0;
    }
    if (k->core->stream_pos > k->copied)
    {
        dst = (volatile ULONG *) (HeapCpu(k) + k->core->stream_off + k->copied);
        for (i = k->copied / 4; i < k->core->stream_pos / 4; i++)
            *dst++ = k->staging[i];
        k->copied = k->core->stream_pos;
    }
    e = CopyCommands(k, n);
    if (e == R2_OK)
        e = R2Ring_Fence(&k->ring, &seq);
    t0 = HwClock(k);
    if (e == R2_OK)
        e = R2Ring_WaitFence(&k->ring, seq);
    us = HwClock(k) - t0;
    if (us > k->fence_us_max)
        k->fence_us_max = us;
    k->dwords += n;
    if (e != R2_OK)
        k->lost = CARD_LOST_FENCE;

stop:
    /* 7. the CP off again, everything as found; the 3D blocks reset only if
     * the engine stays busy */
    tf = R2Cp_Stop(&k->hw, &k->saved, TIMEOUT_US, &reset);
    if (reset)
        k->cp_resets++;
    if (tf != R2_OK && !Reset3D(k))
        k->lost = CARD_LOST_CP;
unlock:
    UnlockAll(k);
    us = HwClock(k);
    if (us > k->lock_us_max)
        k->lock_us_max = us;
    k->lock_us_sum += us;
    ResetCs(k);
    k->in_flush = 0;
    return (k->lost != 0) ? R2_ETIMEOUT : R2_OK;
}

/* --- the core's hooks ----------------------------------------------------------------- */

LONG Card_WaitIdle(struct Card *k)
{
    return Card_Flush(k, FALSE);
}

ULONG *Card_VramBegin(struct Card *k)
{
    if (k->in_flush)
        return (k->b[CARD_HEAP].mem != NULL) ? (ULONG *) HeapCpu(k) : NULL;
    if (!LockBm(k, CARD_HEAP, TRUE))
        return NULL;
    k->vram_lock = 1;
    k->vram_locks++;
    return (ULONG *) HeapCpu(k);
}

void Card_VramEnd(struct Card *k)
{
    if (k->vram_lock)
    {
        UnlockBm(k, CARD_HEAP);
        k->vram_lock = 0;
    }
}

/* --- opening -------------------------------------------------------------------------- */

static ULONG BestMode(ULONG w, ULONG h, ULONG depth)
{
    struct TagItem t[5];

    t[0].ti_Tag = P96BIDTAG_NominalWidth;   t[0].ti_Data = w;
    t[1].ti_Tag = P96BIDTAG_NominalHeight;  t[1].ti_Data = h;
    t[2].ti_Tag = P96BIDTAG_Depth;          t[2].ti_Data = depth;
    t[3].ti_Tag = P96BIDTAG_FormatsAllowed;
    t[3].ti_Data = (depth == 16) ? RGBFF_R5G6B5PC : RGBFF_B8G8R8A8;
    t[4].ti_Tag = TAG_DONE;                 t[4].ti_Data = 0;
    return p96BestModeIDTagList(t);
}

static BOOL BoardIsRadeon(ULONG id)
{
    struct TagItem t[3];
    char *chip;
    char *board;
    ULONG num;

    chip = NULL;
    board = NULL;
    num = p96GetModeIDAttr(id, P96IDA_BOARDNUMBER);
    t[0].ti_Tag = P96BD_ChipName;   t[0].ti_Data = (ULONG) &chip;
    t[1].ti_Tag = P96BD_BoardName;  t[1].ti_Data = (ULONG) &board;
    t[2].ti_Tag = TAG_DONE;         t[2].ti_Data = 0;
    p96GetBoardDataTagList(num, t);
    return (BOOL) (SaysRadeon(chip) || SaysRadeon(board));
}

/* Inside the open's lock of all three data bitmaps: the chip, the
 * apertures, the CP, and CPRing's canary as a self-test. NULL, or what is
 * wrong. */
static const char *ChipCheck(struct Card *k)
{
    ULONG v;
    ULONG i;
    ULONG ap;
    ULONG surf;
    ULONG swp;
    ULONG reset;
    ULONG tf;
    ULONG rptr;
    ULONG t0;
    ULONG *p;
    ULONG ring_gpu;
    ULONG *ring_cpu;
    LONG e;

    /* the register byte order and the chip, from its PCI mirror */
    v = *(volatile ULONG *) ((UBYTE *) k->mmio + PCI_MIRROR);
    if ((v & 0xffffUL) == 0x1002UL)
        k->mmio_swap = 0;
    else if ((Swap32(v) & 0xffffUL) == 0x1002UL)
        k->mmio_swap = 1;
    else
        return "the board's registers do not show an ATI chip";
    k->pci_id = HwRead(k, PCI_MIRROR);
    if (!R200Family(k->pci_id >> 16))
        return "the Radeon is not an R200-family chip (R200, RV250, RV280)";
    k->fb_start = (HwRead(k, R_MC_FB_LOCATION) & 0xffffUL) << 16;
    k->ap_size = HwRead(k, R_CONFIG_APER_SIZE);
    if (k->ap_size == 0 || (k->ap_size & (k->ap_size - 1)) != 0)
        return "CONFIG_APER_SIZE is not a power of two";
    /* the GPU addresses of this lock */
    for (i = 0; i < CARD_SCREEN; i++)
        k->b[i].gpu = k->fb_start + ((ULONG) k->b[i].mem - (ULONG) k->membase) % k->ap_size;

    /* the CPU must see our bitmaps as stored: choice (a), the 68k swaps */
    surf = HwRead(k, R_SURFACE_CNTL);
    for (i = 0; i < CARD_SCREEN; i++)
    {
        ap = ((ULONG) k->b[i].mem - (ULONG) k->membase) / k->ap_size;
        if (ap == 0)
            swp = surf & ((k->bpp == 2) ? R_NONSURF_AP0_SWP_16BPP : R_NONSURF_AP0_SWP_32BPP);
        else
            swp = surf & ((k->bpp == 2) ? R_NONSURF_AP1_SWP_16BPP : R_NONSURF_AP1_SWP_32BPP);
        if (swp != 0)
            return "the aperture byte-swaps this screen's bitmaps (not supported yet)";
    }

    /* the CP: off (a crashed run may have left it on), the microcode */
    R2Cp_Save(&k->hw, &k->saved);
    if (R2Cp_InUse(&k->saved))
    {
        if (R2Cp_Reclaim(&k->hw, &k->saved, TIMEOUT_US, &reset) != R2_OK)
            return "a command processor left running would not stop";
        k->cp_resets++;
    }
    if (R2Cp_WaitIdle(&k->hw, TIMEOUT_US) != R2_OK)
        return "the engine does not go idle";
    R2Cp_LoadMicrocode(&k->hw, R2Cp_Microcode);
    if (R2Cp_CheckMicrocode(&k->hw, R2Cp_Microcode) != 0)
        return "the CP microcode does not read back";

    /* the canary: a packet harmless in both byte orders must write
     * PP_TFACTOR_0 (CPRing) */
    tf = HwRead(k, R_PP_TFACTOR_0);
    HwWrite(k, R_PP_TFACTOR_0, 0);
    ring_gpu = Align4k(k->b[CARD_RING].gpu);
    ring_cpu = (ULONG *) (k->b[CARD_RING].mem + (ring_gpu - k->b[CARD_RING].gpu));
    for (i = 0; i < 64; i++)
        ring_cpu[i] = PAL_FILL;
    e = R2Cp_Start(&k->hw, ring_gpu, CARD_RING_DW, 0);
    if (e == R2_OK)
        e = R2Ring_Init(&k->ring, ring_cpu, ring_gpu, CARD_RING_DW, &k->hw, 1, FENCE_REG,
                        TIMEOUT_US);
    rptr = 0;
    if (e == R2_OK)
    {
        k->ring.kick = 0;
        p = R2Cs_Room(&k->ring.cs, 16);
        if (p != NULL)
        {
            p[0] = R2_OUT(&k->ring.cs, R2_PKT0(R_PP_TFACTOR_0, 0));
            p[1] = R2_OUT(&k->ring.cs, CANARY_VALUE);
            for (i = 2; i < 16; i++)
                p[i] = PAL_FILL;
            k->ring.cs.ptr = p + 16;
            (void) R2Ring_Commit(&k->ring);
            t0 = HwClock(k);
            do
                rptr = HwRead(k, R_CP_RB_RPTR);
            while (rptr != 16 && HwClock(k) - t0 < 100000UL);
        }
    }
    v = HwRead(k, R_PP_TFACTOR_0);
    if (R2Cp_Stop(&k->hw, &k->saved, TIMEOUT_US, &reset) != R2_OK)
        (void) Reset3D(k);
    HwWrite(k, R_PP_TFACTOR_0, tf);
    if (e != R2_OK || rptr != 16 || v != CANARY_VALUE)
        return "the command processor's self-test (the canary) failed";
    return NULL;
}

ULONG Card_Open(struct Card *k, ULONG w, ULONG h, ULONG depth, ULONG check, char *why,
                ULONG whylen)
{
    static struct Card zero;
    struct TagItem t[12];
    struct EClockVal ev;
    struct BitMap *sbm;
    const char *bad;
    ULONG id;
    ULONG fmt;
    ULONG size;
    ULONG want;

    *k = zero;
    Why(why, whylen, "");
    P96Base = OpenLibrary("Picasso96API.library", 2);
    if (P96Base == NULL)
        return CARD_NOT_RADEON;
    want = (depth == 32) ? 32UL : 16UL;
    id = BestMode(w, h, want);
    if (id == (ULONG) INVALID_ID)
        id = BestMode(w, h, (want == 16) ? 32UL : 16UL);
    if (id == (ULONG) INVALID_ID || !BoardIsRadeon(id))
    {
        Card_Close(k);
        return CARD_NOT_RADEON;
    }

    /* the clock (the waits are bounded in microseconds) */
    k->tport = CreateMsgPort();
    k->treq = (k->tport != NULL)
        ? (struct timerequest *) CreateIORequest(k->tport, sizeof(struct timerequest)) : NULL;
    if (k->treq == NULL || OpenDevice((STRPTR) TIMERNAME, UNIT_MICROHZ, (struct IORequest *) k->treq, 0) != 0)
    {
        if (k->treq != NULL)
            DeleteIORequest((struct IORequest *) k->treq);
        k->treq = NULL;
        Why(why, whylen, "timer.device did not open");
        Card_Close(k);
        return CARD_FAILED;
    }
    TimerBase = k->treq->tr_node.io_Device;
    clk_Freq = ReadEClock(&ev);
    if (clk_Freq == 0)
        clk_Freq = 709379;
    k->clock_base = Ticks();

    /* the GL screen and its input window */
    fmt = p96GetModeIDAttr(id, P96IDA_RGBFORMAT);
    t[0].ti_Tag = P96SA_DisplayID;  t[0].ti_Data = id;
    t[1].ti_Tag = P96SA_Width;      t[1].ti_Data = w;
    t[2].ti_Tag = P96SA_Height;     t[2].ti_Data = h;
    t[3].ti_Tag = P96SA_RGBFormat;  t[3].ti_Data = fmt;
    t[4].ti_Tag = P96SA_Depth;      t[4].ti_Data = 8;
    t[5].ti_Tag = P96SA_Title;      t[5].ti_Data = (ULONG) "RadeonGL";
    t[6].ti_Tag = P96SA_ShowTitle;  t[6].ti_Data = FALSE;
    t[7].ti_Tag = P96SA_Quiet;      t[7].ti_Data = TRUE;
    t[8].ti_Tag = P96SA_AutoScroll; t[8].ti_Data = FALSE;
    t[9].ti_Tag = TAG_DONE;         t[9].ti_Data = 0;
    k->scr = p96OpenScreenTagList(t);
    if (k->scr == NULL)
    {
        Why(why, whylen, "the GL screen did not open");
        Card_Close(k);
        return CARD_FAILED;
    }
    sbm = k->scr->RastPort.BitMap;
    k->b[CARD_SCREEN].bm = sbm;
    k->bpp = p96GetBitMapAttr(sbm, P96BMA_BYTESPERPIXEL);
    fmt = p96GetBitMapAttr(sbm, P96BMA_RGBFORMAT);
    if (!((k->bpp == 2 && fmt == RGBFB_R5G6B5PC) || (k->bpp == 4 && fmt == RGBFB_B8G8R8A8)))
    {
        Why(why, whylen, "the GL screen is neither R5G6B5PC nor B8G8R8A8");
        Card_Close(k);
        return CARD_FAILED;
    }
    k->color_format = (k->bpp == 2) ? R_COLOR_FORMAT_RGB565 : R_COLOR_FORMAT_ARGB8888;
    k->width = w;
    k->height = h;
    t[0].ti_Tag = WA_CustomScreen;  t[0].ti_Data = (ULONG) k->scr;
    t[1].ti_Tag = WA_Left;          t[1].ti_Data = 0;
    t[2].ti_Tag = WA_Top;           t[2].ti_Data = 0;
    t[3].ti_Tag = WA_Width;         t[3].ti_Data = w;
    t[4].ti_Tag = WA_Height;        t[4].ti_Data = h;
    t[5].ti_Tag = WA_Backdrop;      t[5].ti_Data = TRUE;
    t[6].ti_Tag = WA_Borderless;    t[6].ti_Data = TRUE;
    t[7].ti_Tag = WA_Activate;      t[7].ti_Data = TRUE;
    t[8].ti_Tag = WA_RMBTrap;       t[8].ti_Data = TRUE;
    t[9].ti_Tag = WA_SimpleRefresh; t[9].ti_Data = TRUE;
    t[10].ti_Tag = WA_NoCareRefresh; t[10].ti_Data = TRUE;
    t[11].ti_Tag = TAG_DONE;        t[11].ti_Data = 0;
    k->win = OpenWindowTagList(NULL, t);
    if (k->win == NULL)
    {
        Why(why, whylen, "the input window did not open");
        Card_Close(k);
        return CARD_FAILED;
    }

    /* video RAM: the target (colour, then Z16), the ring, the heap */
    k->pitch = (w + 15) & ~15UL;            /* rows a multiple of 32 bytes (the present) */
    k->color_bytes = k->pitch * h * k->bpp;
    k->depth_bytes = k->pitch * h * 2;
    if (!AllocData(k, CARD_TARGET, Align4k(k->color_bytes) + k->depth_bytes + 8192)
        || !AllocData(k, CARD_RING, CARD_RING_DW * 4 + 4096))
    {
        Why(why, whylen, "no memory for the back buffer and the ring bitmaps");
        Card_Close(k);
        return CARD_FAILED;
    }
    if (!OnBoard(k, CARD_TARGET))
    {
        Why(why, whylen, "P96 did not put the back buffer bitmap on the board");
        Card_Close(k);
        return CARD_FAILED;
    }
    if (!OnBoard(k, CARD_RING))
    {
        Why(why, whylen, "P96 did not put the ring bitmap on the board");
        Card_Close(k);
        return CARD_FAILED;
    }
    /* the heap: 16 MB, halving to 4 MB, until P96 places it on the board
     * (a bitmap too tall or too big for the board stays in fast RAM) */
    for (size = 16UL << 20; size >= (4UL << 20); size >>= 1)
    {
        if (AllocData(k, CARD_HEAP, size + 4096) && OnBoard(k, CARD_HEAP))
            break;
        if (k->b[CARD_HEAP].bm != NULL)
        {
            FreeBitMap(k->b[CARD_HEAP].bm);
            k->b[CARD_HEAP].bm = NULL;
            k->heap_offboard++;
        }
    }
    if (k->b[CARD_HEAP].bm == NULL)
    {
        Why(why, whylen, "no texture heap of 4 MB or more on the board");
        Card_Close(k);
        return CARD_FAILED;
    }
    k->heap_size = size;

    /* buffers in fast RAM */
    k->cmd = (ULONG *) AllocVec(CARD_CMD_DW * 4, MEMF_ANY);
    k->staging = (ULONG *) AllocVec(CORE_STREAM_BYTES, MEMF_ANY | MEMF_CLEAR);
    if (k->cmd == NULL || k->staging == NULL)
    {
        Why(why, whylen, "no fast RAM for the command buffer");
        Card_Close(k);
        return CARD_FAILED;
    }

    /* the chip, inside one lock of the three */
    if (!LockBm(k, CARD_TARGET, FALSE) || !LockBm(k, CARD_RING, FALSE)
        || !LockBm(k, CARD_HEAP, FALSE))
    {
        UnlockAll(k);
        Why(why, whylen, "the bitmaps are not on the board");
        Card_Close(k);
        return CARD_FAILED;
    }
    k->mmio = (APTR) p96GetBitMapAttr(k->b[CARD_TARGET].bm, P96BMA_BOARDMEMIOBASE);
    k->membase = (APTR) p96GetBitMapAttr(k->b[CARD_TARGET].bm, P96BMA_BOARDMEMBASE);
    k->hw.read_reg = HwRead;
    k->hw.write_reg = HwWrite;
    k->hw.read_back = HwReadBack;
    k->hw.clock_us = HwClock;
    k->hw.hw = k;
    bad = (k->mmio == NULL || k->membase == NULL) ? "P96 gives no register base" : ChipCheck(k);
    if (bad == NULL)
    {
        TargetAt(k, &k->color_gpu, &k->depth_gpu);
        k->heap_delta = Align4k(k->b[CARD_HEAP].gpu) - k->b[CARD_HEAP].gpu;
        k->heap_gpu = k->b[CARD_HEAP].gpu + k->heap_delta;
    }
    UnlockAll(k);
    if (bad != NULL)
    {
        Why(why, whylen, bad);
        Card_Close(k);
        return CARD_FAILED;
    }

    R2Cs_InitBuffer(&k->cs, k->cmd, CARD_CMD_DW - CARD_RESERVE, 1);
    k->cs.need = Need;
    k->cs.owner = k;
    k->check = check;
    k->cfg.fence_reg = FENCE_REG;
    k->cfg.peek = Peek;
    k->cfg.peek_ctx = k;
    R2Chk_Reset(&k->chk);
    return CARD_OK;
}

void Card_Target(const struct Card *k, struct CoreTarget *t)
{
    t->width = k->width;
    t->height = k->height;
    t->color_gpu = k->color_gpu;
    t->color_pitch = k->pitch;
    t->color_format = k->color_format;
    t->depth_gpu = k->depth_gpu;
    t->depth_pitch = k->pitch;
}

void Card_Attach(struct Card *k, struct Core *c)
{
    k->core = c;
    k->gen = c->stream_gen;
    k->copied = 0;
}

void Card_Close(struct Card *k)
{
    ULONG i;

    UnlockAll(k);
    k->vram_lock = 0;
    WaitBlit();
    for (i = 0; i < CARD_SCREEN; i++)
        if (k->b[i].bm != NULL)
        {
            FreeBitMap(k->b[i].bm);
            k->b[i].bm = NULL;
        }
    k->b[CARD_SCREEN].bm = NULL;
    if (k->win != NULL)
    {
        ModifyIDCMP(k->win, 0);
        CloseWindow(k->win);
        k->win = NULL;
    }
    if (k->scr != NULL)
    {
        p96CloseScreen(k->scr);
        k->scr = NULL;
    }
    if (k->cmd != NULL)
        FreeVec(k->cmd);
    if (k->staging != NULL)
        FreeVec(k->staging);
    k->cmd = NULL;
    k->staging = NULL;
    if (k->treq != NULL)
    {
        CloseDevice((struct IORequest *) k->treq);
        DeleteIORequest((struct IORequest *) k->treq);
        k->treq = NULL;
    }
    if (k->tport != NULL)
    {
        DeleteMsgPort(k->tport);
        k->tport = NULL;
    }
    TimerBase = NULL;
    if (P96Base != NULL)
    {
        CloseLibrary(P96Base);
        P96Base = NULL;
    }
}
