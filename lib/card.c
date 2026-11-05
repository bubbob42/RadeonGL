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
#include <proto/dos.h>
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
static ULONG TicksUs(ULONG t);

/* The overlap statistic (card.h): an unlock after a fence arms it, the
 * next lock of any bitmap reads it. */
#define CARD_OV_FRAME 0         /* the next lock is a frame's flush */
#define CARD_OV_FLUSH 1         /* a flush without a present: a texture hazard, a stream wrap */
#define CARD_OV_VRAM  2         /* a heap lock: a texture upload, the direct stream */

#ifdef CARD_OVERLAP_STAT
static void OvUnlock(struct Card *k, ULONG tail)
{
    k->ov_unlock = Ticks();
    k->ov_tail = tail;
    k->ov_armed = 1;
}

static void OvLock(struct Card *k, ULONG kind)
{
    ULONG gap;

    if (!k->ov_armed)
        return;
    k->ov_armed = 0;
    gap = TicksUs(Ticks() - k->ov_unlock);
    k->ov_n++;
    k->ov_tail_sum += k->ov_tail;
    k->ov_gap_sum += gap;
    k->ov_save_sum += gap < k->ov_tail ? gap : k->ov_tail;
    k->ov_by[kind]++;
    if (k->ov_gap_sum > 0x40000000UL || k->ov_tail_sum > 0x40000000UL)
    {
        /* hours of play, or a long pause: halve everything, the means stay */
        k->ov_n /= 2;
        k->ov_tail_sum /= 2;
        k->ov_gap_sum /= 2;
        k->ov_save_sum /= 2;
        k->ov_by[0] /= 2;
        k->ov_by[1] /= 2;
        k->ov_by[2] /= 2;
    }
}
#else
/* the release build: nothing in the frame's path (2026-11-05, "GLQuake must
 * not get slower") */
#define OvUnlock(k, tail) ((void) (k), (void) (tail))
#define OvLock(k, kind) ((void) (k))
#endif

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
    ULONG t;

    HwWrite(k, R_RBBM_SOFT_RESET, SOFT_RESET_SE | SOFT_RESET_RE | SOFT_RESET_PP);
    (void) HwRead(k, R_RBBM_SOFT_RESET);
    t = HwClock(k);
    while (HwClock(k) - t < 200)
        ;                           /* held 200 us, as Linux does */
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
    k->b[i].bpp = k->bpp;
    k->b[i].p96 = 0;
    k->b[i].bm = AllocBitMap(CARD_ROW / k->bpp, (bytes + CARD_ROW - 1) / CARD_ROW,
                             GetBitMapAttr(sbm, BMA_DEPTH), BMF_MINPLANES, sbm);
    if (k->b[i].bm != NULL && !p96GetBitMapAttr(k->b[i].bm, P96BMA_ISP96))
    {
        FreeBitMap(k->b[i].bm);
        k->b[i].bm = NULL;
    }
    return (BOOL) (k->b[i].bm != NULL);
}

/* The heap as a 32-bit bitmap 2048 pixels wide (8 KB a row), a friend of
 * the Workbench screen's bitmap. MEASURED (BmFit, A4000D and uaegfx): P96
 * puts a bitmap on the board at allocation only when its friend is there;
 * without a friend every bitmap starts in fast RAM (8 MB as well), and a
 * 32-bit friend of the 32-bit Workbench goes on the board up to 16 MB in
 * any shape tried. A friend of the 16-bit GL screen stayed off at 16 MB.
 * Only where the Workbench is B8G8R8A8 on the GL screen's board, and the
 * GL screen's mode holds that format beside itself without reconfiguring
 * the aperture. */
static BOOL AllocHeap32(struct Card *k, ULONG bytes)
{
    struct CardBm *b;
    struct Screen *wb;
    struct BitMap *friend;
    LONG id;

    b = &k->b[CARD_HEAP];
    b->bm = NULL;
    if (!(k->compat & RGBFF_B8G8R8A8))
        return FALSE;
    wb = LockPubScreen(NULL);
    if (wb == NULL)
        return FALSE;
    friend = wb->RastPort.BitMap;
    id = GetVPModeID(&wb->ViewPort);
    if (id != INVALID_ID && p96GetModeIDAttr((ULONG) id, P96IDA_ISP96)
        && p96GetModeIDAttr((ULONG) id, P96IDA_BOARDNUMBER) == k->board
        && p96GetBitMapAttr(friend, P96BMA_RGBFORMAT) == RGBFB_B8G8R8A8)
    {
        b->bytes = bytes;
        b->bpp = 4;
        b->p96 = 1;
        b->bm = p96AllocBitMap(2048, (bytes + 8191) / 8192, 32, BMF_MINPLANES, friend, RGBFB_NONE);
    }
    UnlockPubScreen(NULL, wb);
    return (BOOL) (b->bm != NULL);
}

/* The heap in the GL screen's format, rows of row bytes: a friend of the
 * GL screen, or (blit) a bitmap without a friend that a 1 x 1 blit from the
 * GL screen moves onto the board (MEASURED for 32 bit, BmFit way B). For a
 * Workbench that is not 32-bit: a reporter's 8-bit Workbench (2026-11-05)
 * left RadeonGL 0.4 with 8 MB, P96 keeping the 16 MB friend of 2048 x 4096
 * off the board, and GLQuake's demo1 needs about 8.7 MB. */
static void FreeBm(struct Card *k, ULONG i);
static BOOL OnBoard(struct Card *k, ULONG i);

static BOOL AllocHeapScreen(struct Card *k, ULONG bytes, ULONG row, BOOL blit)
{
    struct BitMap *sbm;
    struct CardBm *b;
    ULONG rows;

    sbm = k->scr->RastPort.BitMap;
    b = &k->b[CARD_HEAP];
    rows = (bytes + row - 1) / row;
    b->bytes = bytes;
    b->bpp = k->bpp;
    b->p96 = blit ? 1 : 0;
    if (blit)
        b->bm = p96AllocBitMap(row / k->bpp, rows, k->bpp * 8, BMF_MINPLANES, NULL,
                               p96GetBitMapAttr(sbm, P96BMA_RGBFORMAT));
    else
        b->bm = AllocBitMap(row / k->bpp, rows, GetBitMapAttr(sbm, BMA_DEPTH), BMF_MINPLANES, sbm);
    if (b->bm != NULL && !p96GetBitMapAttr(b->bm, P96BMA_ISP96))
    {
        FreeBm(k, CARD_HEAP);
        return FALSE;
    }
    if (b->bm != NULL && blit)
    {
        BltBitMap(sbm, 0, 0, b->bm, 0, 0, 1, 1, 0xc0, 0xff, NULL);
        WaitBlit();
    }
    return (BOOL) (b->bm != NULL);
}

/* One try at the heap: size bytes one way. TRUE when it is on the board
 * (then heap_size and heap_way are set); every try goes to the log. */
static BOOL HeapTry(struct Card *k, ULONG size, ULONG way)
{
    BOOL ok;

    if (way == CARD_HEAP_WB32)
        ok = AllocHeap32(k, size + 8192);
    else if (way == CARD_HEAP_NARROW)
        ok = AllocData(k, CARD_HEAP, size + 4096);
    else
        ok = AllocHeapScreen(k, size + 8192, 8192, (BOOL) (way == CARD_HEAP_BLIT));
    if (ok)
        ok = OnBoard(k, CARD_HEAP);
    if (k->heap_try_n < CARD_HEAP_TRIES)
        k->heap_try[k->heap_try_n++] = (UBYTE) (((size >> 20) << 3) | (way << 1) | (ok ? 1 : 0));
    if (!ok)
    {
        if (k->b[CARD_HEAP].bm != NULL)
            k->heap_offboard++;
        FreeBm(k, CARD_HEAP);
        return FALSE;
    }
    k->heap_size = size;
    k->heap_way = way;
    return TRUE;
}

static void FreeBm(struct Card *k, ULONG i)
{
    if (k->b[i].bm == NULL)
        return;
    if (k->b[i].p96)
        p96FreeBitMap(k->b[i].bm);
    else
        FreeBitMap(k->b[i].bm);
    k->b[i].bm = NULL;
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
    /* the upload cache's region: its mirror holds what the flush copies */
    if (gpu >= k->heap_gpu && k->core->ustaged && off >= k->core->ucache_off
        && off + 4 <= k->core->ucache_off + k->core->ucache_size)
    {
        *val = k->core->ushadow[(off - k->core->ucache_off) / 4];
        return 0;
    }
    if (gpu < k->heap_gpu || off < k->core->stream_off
        || off + 4 > k->core->stream_off + k->core->stream_size)
        return -1;
    *val = k->staging[(off - k->core->stream_off) / 4];
    return 0;
}

/* GL screens a visitor window kept open at Card_Close (for the log). */
ULONG Card_ScreensLeft = 0;
ULONG Card_NoHeap32 = 0;
ULONG Card_HeapMax = 0;
ULONG Card_HeapNarrow = 0;

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
 * ring's wrap filler must never land inside a packet), and never between
 * a DRAW_INDX_2 and its INDX_BUFFER: a kick's NOP padding would land
 * there, a layout the checker refuses (audit 2026-11-03). */
static LONG CopyCommands(struct Card *k, ULONG n)
{
    ULONG i;
    ULONG c;
    ULONG h;
    ULONG len;
    ULONG *p;
    const ULONG *src;
    ULONG j;
    ULONG last;

    i = 0;
    while (i < n)
    {
        c = 0;
        last = 0;
        while (i + c < n)
        {
            h = Swap32(k->cmd[i + c]);          /* stored byte-reversed */
            len = (R2_PKT_TYPE(h) == 2) ? 1 : R2_PKT_COUNT(h) + 2;
            if (c != 0 && c + len > CHUNK_DW)
            {
                /* the end of a chunk: never right after a DRAW_INDX_2
                 * (looked at here only, not for every packet: this runs
                 * inside the lock) */
                h = Swap32(k->cmd[i + last]);
                if (!(R2_PKT_TYPE(h) == 3 && R2_PKT3_OPCODE(h) == R2_OP_3D_DRAW_INDX_2))
                    break;
                h = Swap32(k->cmd[i + c]);
            }
            last = c;
            c += len;
        }
        if (i + c > n)
            return R2_EARG;                     /* a packet past the end */
        p = R2Cs_Room(&k->ring.cs, c);
        if (p == NULL)
            return k->ring.cs.error;
        /* the source in a local: through k the compiler reloaded it for
         * every dword (MEASURED in the A4000D's profile) */
        src = k->cmd + i;
        for (j = 0; j < c; j++)
            p[j] = src[j];
        k->ring.cs.ptr = p + c;
        i += c;
    }
    return R2_OK;
}

/* The upload cache's dirty ranges from its fast-RAM mirror into the heap
 * (staged stream; inside the flush's lock). */
static void CopyUCache(struct Card *k)
{
    struct Core *c;
    volatile ULONG *dst;
    const ULONG *src;
    ULONG r;
    ULONG i;
    ULONG off;
    ULONG len;

    c = k->core;
    for (r = 0; r < (c->udirty_all ? 1UL : c->udirty_n); r++)
    {
        off = c->udirty_all ? 0 : c->udirty_off[r];
        len = c->udirty_all ? c->ucache_size : c->udirty_len[r];
        dst = (volatile ULONG *) (HeapCpu(k) + c->ucache_off + off);
        src = c->ushadow + off / 4;
        for (i = len / 4; i > 0; i--)
            *dst++ = *src++;
    }
    Core_UCacheCopied(c);
}

/* --- the flush ------------------------------------------------------------------------ */

/* The CP off again, everything as found; the 3D blocks reset only if the
 * engine stays busy. TRUE when a reset discarded the 3D state: the core
 * sends everything again (not while closing: the core is gone), the
 * checker starts over, and the next lock loads the microcode before the
 * CP starts. */
static BOOL StopCp(struct Card *k, BOOL closing)
{
    ULONG tf;
    ULONG reset;

    tf = R2Cp_Stop(&k->hw, &k->saved, TIMEOUT_US, &reset);
    if (reset)
        k->cp_resets++;
    if (tf != R2_OK && !Reset3D(k))
        k->lost = CARD_LOST_CP;
    if (!reset && tf == R2_OK)
        return FALSE;
    if (!closing)
    {
        Core_Forget(k->core);
        R2Chk_Reset(&k->chk);
    }
    k->reload_ucode = 1;
    return TRUE;
}

/* The frame the last flush kicked (k->pending): its fence, then the CP
 * off. The caller holds a lock, so P96 draws nothing meanwhile. After a
 * reset the commands built since that flush assumed state the engine has
 * lost: the next flush drops them (drop_next). */
static void Settle(struct Card *k, BOOL closing)
{
    ULONG t0;
    ULONG us;

    k->pending = 0;
    if (!R2Ring_FenceDone(&k->ring, k->pend_seq))
        k->ov_waits++;
    t0 = HwClock(k);
    if (R2Ring_WaitFence(&k->ring, k->pend_seq) != R2_OK)
        k->lost = CARD_LOST_FENCE;
    us = HwClock(k) - t0;
    if (us > k->fence_us_max)
        k->fence_us_max = us;
    if (StopCp(k, closing) && !closing)
        k->drop_next = 1;
}

/* Settle outside a flush, under a lock of its own (the ring's, wherever
 * P96 has it). */
static void SettleAlone(struct Card *k, BOOL closing)
{
    BOOL locked;

    if (!k->pending)
        return;
    locked = LockBm(k, CARD_RING, TRUE);
    k->clock_base = Ticks();
    Settle(k, closing);
    if (locked)
        UnlockBm(k, CARD_RING);
}

/* One flush. wait: the old way, the Radeon done before the unlock (always
 * without the overlap); else the frame is kicked and left running. */
static LONG CardFlush(struct Card *k, BOOL present, BOOL wait)
{
    struct CoreScreen s;
    volatile ULONG *dst;
    const ULONG *src;
    ULONG color;
    ULONG depth;
    ULONG heap;
    ULONG ring_gpu;
    ULONG *ring_cpu;
    ULONG n;
    ULONG i;
    ULONG t0;
    ULONG us;
    ULONG seq;
    ULONG tail;
    BOOL scr_ok;
    LONG e;

    if (k->in_flush)
        return R2_OK;
    if (k->vram_lock)
        return R2_OK;               /* inside Card_VramBegin: the heap is held (never today) */
    n = (ULONG) (k->cs.ptr - k->cmd);
    if (k->lost != 0 || (n == 0 && !present))
    {
        ResetCs(k);
        return (k->lost != 0) ? R2_ETIMEOUT : R2_OK;
    }
    k->in_flush = 1;
    k->flushes++;
    e = R2_OK;
    scr_ok = FALSE;
    tail = ~0UL;

    /* 0. every packet through the checker before the CP sees one -- outside
     * the lock (the GUI waits while it is held), against the addresses the
     * frame was built with; a frame whose bitmaps moved is dropped below */
    if (k->check && n != 0)
    {
        /* a stream written straight into video RAM cannot be read here
         * (outside the lock): the indices' declared bounds are checked, the
         * values were checked by the core against the lock */
        k->cfg.peek = k->core->stream_direct ? NULL : Peek;
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
        e = R2Chk_Run(&k->cfg, &k->chk, k->cmd, n, 1, 0, &k->err);
        if (e != R2CE_OK)
        {
            k->err_code = e;
            k->lost = CARD_LOST_CHECKER;
            ResetCs(k);
            k->in_flush = 0;
            return R2_ETIMEOUT;
        }
    }
    k->clock_base = Ticks();
    OvLock(k, present ? CARD_OV_FRAME : CARD_OV_FLUSH);

    /* 1. the locks: target, ring, heap, on the board */
    if (!LockBm(k, CARD_TARGET, FALSE) || !LockBm(k, CARD_RING, FALSE)
        || !LockBm(k, CARD_HEAP, FALSE))
    {
        UnlockAll(k);
        SettleAlone(k, FALSE);
        k->offboard++;
        Drop(k);
        k->drop_next = 0;
        k->in_flush = 0;
        return R2_OK;
    }

    /* 1b. the frame the last flush kicked: done before anything is written
     * (the lock has waited for the 3D engine already, probe B) */
    if (k->pending)
        Settle(k, FALSE);
    if (k->lost != 0)
        goto unlock;
    if (k->drop_next)
    {
        UnlockAll(k);
        k->drop_next = 0;
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
            k->lost = CARD_LOST_HEAP;           /* textures can no longer be aligned */
        Drop(k);
        k->in_flush = 0;
        return R2_OK;
    }

    /* 3. the present, at the screen bitmap's address of this lock */
    i = n;
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
        k->lost = CARD_LOST_EMIT;
        k->err_code = k->cs.error;
        goto unlock;
    }

    /* 4. the present's packets through the checker too, with the screen */
    if (k->check && n > i)
    {
        k->cfg.range[0].gpu = k->color_gpu;
        k->cfg.range[0].size = k->color_bytes;
        k->cfg.range[0].kinds = R2K_COLOR | R2K_TEXTURE;
        k->cfg.range[1].gpu = k->depth_gpu;
        k->cfg.range[1].size = k->depth_bytes;
        k->cfg.range[1].kinds = R2K_DEPTH;
        k->cfg.range[2].gpu = k->heap_gpu;
        k->cfg.range[2].size = k->heap_size;
        k->cfg.range[2].kinds = R2K_TEXTURE | R2K_VERTEX | R2K_INDEX;
        k->cfg.range[3].gpu = k->b[CARD_SCREEN].gpu;
        k->cfg.range[3].size = k->b[CARD_SCREEN].bpr * k->height;
        k->cfg.range[3].kinds = R2K_COLOR;
        k->cfg.nranges = 4;
        e = R2Chk_Run(&k->cfg, &k->chk, k->cmd + i, n - i, 1, i, &k->err);
        if (e != R2CE_OK)
        {
            k->err_code = e;
            k->lost = CARD_LOST_CHECKER;
            goto unlock;
        }
    }

    /* the flush ends no indexed draw half-way (its indices would never come) */
    if (k->check && R2Chk_End(&k->chk, &k->err) != R2CE_OK)
    {
        k->err_code = R2CE_INDX;
        k->lost = CARD_LOST_CHECKER;
        goto unlock;
    }

    /* 5. the CP, on the ring at this lock's address (its microcode again
     * after a reset) */
    if (R2Cp_WaitIdle(&k->hw, TIMEOUT_US) != R2_OK)
    {
        k->lost = CARD_LOST_CP;
        goto unlock;
    }
    if (k->reload_ucode)
    {
        R2Cp_LoadMicrocode(&k->hw, R2Cp_Microcode);
        if (R2Cp_CheckMicrocode(&k->hw, R2Cp_Microcode) != 0)
        {
            k->lost = CARD_LOST_CP;
            goto unlock;
        }
        k->reload_ucode = 0;
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
    if (!k->core->stream_direct && k->core->stream_pos > k->copied)
    {
        /* bounds and source in locals: through k (volatile stores in
         * between) they were reloaded for every dword */
        dst = (volatile ULONG *) (HeapCpu(k) + k->core->stream_off + k->copied);
        src = k->staging + k->copied / 4;
        for (i = (k->core->stream_pos - k->copied) / 4; i > 0; i--)
            *dst++ = *src++;
        k->copied = k->core->stream_pos;
    }
    /* the upload cache's new entries, from its mirror */
    if (k->core->ustaged && (k->core->udirty_n != 0 || k->core->udirty_all))
        CopyUCache(k);
    e = CopyCommands(k, n);
    if (e == R2_OK)
        e = R2Ring_Fence(&k->ring, &seq);
    if (k->dwords + n < k->dwords)
        k->dwords_hi++;
    k->dwords += n;
    if (e == R2_OK && !wait)
    {
        /* 7. the overlap: the CP gets all of it, the locks go, and the
         * Radeon draws on while the 68k builds the next frame; the next
         * lock settles it */
        e = R2Ring_Commit(&k->ring);
        if (e == R2_OK)
        {
            k->pending = 1;
            k->pend_seq = seq;
            k->ov_frames++;
            goto unlock;
        }
    }
    t0 = HwClock(k);
    if (e == R2_OK)
        e = R2Ring_WaitFence(&k->ring, seq);
    us = HwClock(k) - t0;
    if (us > k->fence_us_max)
        k->fence_us_max = us;
    if (e == R2_OK)
        tail = us;
    if (e != R2_OK)
        k->lost = CARD_LOST_FENCE;

stop:
    /* 7. without the overlap: the CP off again before the unlock */
    (void) StopCp(k, FALSE);
unlock:
    UnlockAll(k);
    if (tail != ~0UL)
        OvUnlock(k, tail);
    us = HwClock(k);
    if (us > k->lock_us_max)
        k->lock_us_max = us;
    if (k->lock_us_sum + us < k->lock_us_sum)
    {
        /* hours of play: halve both, the mean stays */
        k->lock_us_sum /= 2;
        k->lock_flushes /= 2;
    }
    k->lock_us_sum += us;
    k->lock_flushes++;
    ResetCs(k);
    k->in_flush = 0;
    return (k->lost != 0) ? R2_ETIMEOUT : R2_OK;
}

/* --- the core's hooks ----------------------------------------------------------------- */

LONG Card_Flush(struct Card *k, BOOL present)
{
    return CardFlush(k, present, (BOOL) !k->overlap);
}

/* Everything built drawn, the Radeon idle: a texture hazard, a stream
 * wrap, glFinish. */
LONG Card_WaitIdle(struct Card *k)
{
    LONG e;

    if (k->in_flush)
        return R2_OK;
    e = CardFlush(k, FALSE, TRUE);
    SettleAlone(k, FALSE);
    return (k->lost != 0) ? R2_ETIMEOUT : e;
}

ULONG *Card_VramBegin(struct Card *k)
{
    if (k->in_flush)
        return (k->b[CARD_HEAP].mem != NULL) ? (ULONG *) HeapCpu(k) : NULL;
    OvLock(k, CARD_OV_VRAM);
    if (!LockBm(k, CARD_HEAP, TRUE))
        return NULL;
    if (k->pending)
    {
        /* the kicked frame may still read what is about to be written */
        k->clock_base = Ticks();
        Settle(k, FALSE);
    }
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
            swp = surf & ((k->b[i].bpp == 2) ? R_NONSURF_AP0_SWP_16BPP : R_NONSURF_AP0_SWP_32BPP);
        else
            swp = surf & ((k->b[i].bpp == 2) ? R_NONSURF_AP1_SWP_16BPP : R_NONSURF_AP1_SWP_32BPP);
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
    ULONG top;
    ULONG want;
    BOOL ok;

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
    k->compat = p96GetModeIDAttr(id, P96IDA_COMPATIBLEFORMATS);
    k->board = p96GetModeIDAttr(id, P96IDA_BOARDNUMBER);
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
    /* the heap, until P96 places one on the board (a bitmap it cannot or
     * will not place stays in fast RAM): the GL screen's format, 16 MB down
     * to 4 MB in 2 MB steps, each with wide rows as a friend of the GL
     * screen, then moved on by a blit. The Workbench has no say: only where
     * neither gets the full size does a 32-bit friend of a 32-bit Workbench
     * get a try at it (RadeonGL 0.4/0.5 tried that first; the wide friend
     * got 16 MB on the board on the A4000D's 8- and 32-bit Workbench alike,
     * 2026-11-05). Nothing else: RadeonGL 0.4's 4096-byte rows (16, 8, 4 MB) froze
     * GLQuake on the A4000D's 8-bit Workbench every time, at 8 MB, where
     * the same 8 MB with 8192-byte rows ran demo1 through (2026-11-05,
     * 0.4 and this code alike); a refusal beats a freeze. The narrow heap
     * stays for investigation only (RadeonGLHeapNarrow=1). */
    top = (Card_HeapMax >= 4 && Card_HeapMax < 16) ? Card_HeapMax << 20 : 16UL << 20;
    ok = FALSE;
    for (size = top; !ok && !Card_HeapNarrow && size >= (4UL << 20); size -= 2UL << 20)
    {
        if (HeapTry(k, size, CARD_HEAP_WIDE) || HeapTry(k, size, CARD_HEAP_BLIT)
            || (size == top && !Card_NoHeap32 && HeapTry(k, size, CARD_HEAP_WB32)))
            ok = TRUE;
    }
    for (size = top; !ok && Card_HeapNarrow && size >= (4UL << 20); size >>= 1)
    {
        if (HeapTry(k, size, CARD_HEAP_NARROW))
            ok = TRUE;
    }
    if (!ok)
    {
        Why(why, whylen, "no texture heap of 4 MB or more on the board");
        Card_Close(k);
        return CARD_FAILED;
    }
    size = k->heap_size;

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
    SettleAlone(k, TRUE);           /* the last frame drawn, the CP off */
    WaitBlit();
    for (i = 0; i < CARD_SCREEN; i++)
        FreeBm(k, i);
    k->b[CARD_SCREEN].bm = NULL;
    if (k->win != NULL)
    {
        ModifyIDCMP(k->win, 0);
        CloseWindow(k->win);
        k->win = NULL;
    }
    if (k->scr != NULL)
    {
        /* a visitor window (a requester, a tool) keeps it open: give it
         * five seconds, then leave the screen (the next log says so) */
        for (i = 0; i < 25 && !p96CloseScreen(k->scr); i++)
            Delay(10);
        if (i == 25)
            Card_ScreensLeft++;
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
