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
#include <intuition/intuitionbase.h>
#include <graphics/gfx.h>
#include <graphics/modeid.h>
#include <graphics/displayinfo.h>
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

/* W3 (2026-11-08): several contexts, each a card of its own, one Radeon.
 * Everything that touches the chip holds card_sem (a task's own calls
 * nest); a frame another card left running is settled first; with more
 * than one card no frame is left running past a flush, and each flush
 * leaves its core to send its whole state again, since another card may
 * load the engine before its next one. */
static struct SignalSemaphore card_sem;
static ULONG card_users;            /* cards open */
static ULONG card_p96;              /* our opens of Picasso96API.library */
static struct Card *card_running;   /* the card whose kicked frame may still run */

void Card_Init(void)
{
    InitSemaphore(&card_sem);
}

void Card_Obtain(void)
{
    ObtainSemaphore(&card_sem);
}

void Card_Release(void)
{
    ReleaseSemaphore(&card_sem);
}

/* P96Base stays while any card has it open */
static BOOL P96Open(struct Card *k)
{
    struct Library *b;

    b = OpenLibrary("Picasso96API.library", 2);
    if (b == NULL)
        return FALSE;
    P96Base = b;
    k->p96open = 1;
    card_p96++;
    return TRUE;
}

static void P96Release(struct Card *k)
{
    if (!k->p96open)
        return;
    k->p96open = 0;
    CloseLibrary(P96Base);
    if (--card_p96 == 0)
        P96Base = NULL;
}

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

/* why[] and " ($id, w x h)" after it: the mode a failure was about */
static void WhyMode(char *why, ULONG len, const char *s, ULONG id, ULONG w, ULONG h)
{
    char tail[40];
    char num[12];
    ULONG i, j, n, v;

    Why(why, len, s);
    if (why == NULL || len == 0)
        return;
    j = 0;
    tail[j++] = ' ';
    tail[j++] = '(';
    tail[j++] = '$';
    for (i = 0; i < 8; i++)
        tail[j++] = "0123456789ABCDEF"[(id >> (28 - 4 * i)) & 15];
    tail[j++] = ',';
    for (n = 0; n < 2; n++)
    {
        v = (n == 0) ? w : h;
        tail[j++] = ' ';
        i = 0;
        do
        {
            num[i++] = (char) ('0' + v % 10);
            v /= 10;
        } while (v != 0 && i < 10);
        while (i > 0)
            tail[j++] = num[--i];
        if (n == 0)
        {
            tail[j++] = ' ';
            tail[j++] = 'x';
        }
    }
    tail[j++] = ')';
    tail[j] = 0;
    for (i = 0; why[i] != 0; i++)
        ;
    for (j = 0; i + 1 < len && tail[j] != 0; i++, j++)
        why[i] = tail[j];
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

static BOOL VsyncWindow(struct Card *k, ULONG *crtc, ULONG *lines)
{
    ULONG v;

    if (HwRead(k, R_CRTC_GEN_CNTL) & R_CRTC_EN)
    {
        *crtc = 0;
        v = HwRead(k, R_CRTC_V_TOTAL_DISP);
    }
    else if (HwRead(k, R_CRTC2_GEN_CNTL) & R_CRTC_EN)
    {
        *crtc = 1;
        v = HwRead(k, R_CRTC2_V_TOTAL_DISP);
    }
    else
        return FALSE;
    *lines = ((v >> R_CRTC_V_DISP_SHIFT) & R_CRTC_V_DISP_MASK) + 1;
    return (BOOL) (*lines >= 2 && *lines <= R_TRIG_VLINE_LINE_MASK);
}

static void DisplayPriority(struct Card *k)
{
    ULONG v;
    ULONG want;

    v = HwRead(k, R_GRPH_BUFFER_CNTL);
    if (k->flushes == 1)
        k->grph_seen = v;
    if (!k->dprio_on)
        return;
    want = (v & ~(R_GRPH_CRITICAL_POINT_MASK | R_GRPH_CRITICAL_CNTL | R_GRPH_CRITICAL_AT_SOF | R_GRPH_STOP_CNTL))
         | ((k->dprio_point << R_GRPH_CRITICAL_POINT_SHIFT) & R_GRPH_CRITICAL_POINT_MASK);
    if (v == want)
        return;
    if (!k->grph_saved)
    {
        k->grph_seen = v;
        k->grph_saved = 1;
    }
    HwWrite(k, R_GRPH_BUFFER_CNTL, want);
    k->dprio_writes++;
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
#if defined(RGL_TRAPCATCH) && defined(RGL_CPU_040)
    rgl_bm[i] = (ULONG) b->mem;
#endif
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

    sbm = k->friend;
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

    sbm = k->friend;
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

/* The present's vertical sync (mglEnableSync): which CRTC shows the screen
 * and how many lines it displays, so that the CP can wait until the beam has
 * left them (R2E_WaitVline); the copy then outruns the beam. CRTC if it
 * runs, else CRTC2; FALSE with neither -- a stopped CRTC never lets the CP
 * go, so then no wait. Read in the flush's lock. */
static BOOL VsyncWindow(struct Card *k, ULONG *crtc, ULONG *lines);

/* The display FIFO's priority, in the flush's lock (RadeonGLDisplayPriority):
 * MEASURED 2026-11-07 on the A4000D, Radeon.card leaves GRPH_BUFFER_CNTL at
 * $20205C5C in every mode -- critical point 32 octawords, about 1.2 us of
 * pixels at 1920 x 1080 -- and the user saw sparkles and DVI drop-outs at
 * large modes while GLQuake drew. xf86-video-ati's "DisplayPriority HIGH"
 * sets the critical point to 0 (high priority always). */
static void DisplayPriority(struct Card *k);

/* Where target and ring sit in their bitmaps, from this lock: 4 KB aligned
 * (their content does not outlive a flush). The heap's place in its bitmap
 * is fixed at the open (k->heap_delta): its content must survive a move. */

static void TargetAt(struct Card *k, ULONG *color, ULONG *depth)
{
    if (k->kind == CARD_KIND_BITMAP)
    {
        /* W1: the program's bitmap is the colour buffer, ours the depth */
        *color = k->b[CARD_SCREEN].gpu;
        *depth = Align4k(k->b[CARD_TARGET].gpu);
        return;
    }
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
#if defined(RGL_TRAPCATCH) && defined(RGL_CPU_040)
volatile ULONG rgl_where, rgl_where2, rgl_pending;
volatile ULONG rgl_bm[CARD_NBM];
#endif
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

/* --- copies into video RAM -------------------------------------------------------------- */

/* 2026-11-07: MOVE16 (docs/library-asm-research.md, src/probe/move16bench.c).
 * The 060 build with GCC only: the 68030 build has no MOVE16, SAS/C no
 * inline assembler, and both keep the longword loop. */
#if defined(__GNUC__) && defined(__mc68060__)
#define CARD_MOVE16 1

/* lines 16-byte lines from src to dst, both 16-byte aligned, lines > 0 */
static void Move16Lines(volatile ULONG *dst, const ULONG *src, ULONG lines)
{
    __asm__ __volatile__ (
        "1:\tmove16 %0@+,%1@+\n\t"
        "subql #1,%2\n\t"
        "bnes 1b"
        : "+a" (src), "+a" (dst), "+d" (lines)
        :
        : "memory", "cc");
}
#endif

#ifdef CARD_MOVE16
/* The fallback for a bus that takes MOVE16 but garbles it (2026-11-07: which
 * accelerators burst is barely documented -- the CyberStorm MK I and the
 * A4000T's SCSI do, the BFG9060 does not): once, at the first flush with
 * move16 on and inside its lock, one line into a 16-byte block of the heap
 * of its own, read back. A different line turns move16 off for the
 * context. A bus that HANGS on a burst cannot be caught here: that is why
 * move16 is off unless RadeonGLMove16 is set. TRUE when the line came back. */
static BOOL Move16SelfTest(struct Card *k)
{
    ULONG buf[8];
    ULONG *src;
    volatile ULONG *dst;
    ULONG off;
    ULONG i;
    BOOL ok;

    off = Heap_Alloc(k->core, 16, 16);
    if (off == 0xffffffffUL)
    {
        k->m16_failed = 2;
        return FALSE;
    }
    src = (ULONG *) (((ULONG) buf + 15UL) & ~15UL);
    for (i = 0; i < 4; i++)
        src[i] = 0x52474c30UL + i * 0x01010101UL;
    CacheClearE(src, 16, CACRF_ClearD);
    dst = (volatile ULONG *) (HeapCpu(k) + off);
    Move16Lines(dst, src, 1);
    ok = TRUE;
    for (i = 0; i < 4; i++)
        if (dst[i] != src[i])
            ok = FALSE;
    if (!ok)
        k->m16_failed = 1;
    return ok;
}
#endif

/* n dwords from fast RAM into video RAM (inside the lock): longword stores;
 * with RadeonGLMove16 the whole 16-byte lines between as MOVE16 bursts,
 * when source and destination share their place in a line (the upload
 * cache's mirror and the staged stream do, their bases aligned; the ring
 * position is anywhere) */
static void ToVram(struct Card *k, volatile ULONG *dst, const ULONG *src, ULONG n)
{
#ifdef CARD_MOVE16
    ULONG lines;

    if (k->move16 != 0 && n >= 8 && (((ULONG) dst ^ (ULONG) src) & 15UL) == 0)
    {
        while (((ULONG) dst & 15UL) != 0)
        {
            *dst++ = *src++;
            n--;
            k->m16_plain++;
        }
        lines = n >> 2;
        if (k->move16 == 1)
            CacheClearE((APTR) src, lines * 16, CACRF_ClearD);
        Move16Lines(dst, src, lines);
        dst += lines * 4;
        src += lines * 4;
        n &= 3;
        k->m16_lines += lines * 4;
    }
    k->m16_plain += n;
#else
    (void) k;
#endif
    while (n != 0)
    {
        *dst++ = *src++;
        n--;
    }
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
        ToVram(k, p, src, c);
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
    ULONG off;
    ULONG len;

    c = k->core;
    for (r = 0; r < (c->udirty_all ? 1UL : c->udirty_n); r++)
    {
        off = c->udirty_all ? 0 : c->udirty_off[r];
        len = c->udirty_all ? c->ucache_size : c->udirty_len[r];
        dst = (volatile ULONG *) (HeapCpu(k) + c->ucache_off + off);
        src = c->ushadow + off / 4;
        ToVram(k, dst, src, len / 4);
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

    WHERE(40, k->pend_seq);
    k->pending = 0;
    if (!R2Ring_FenceDone(&k->ring, k->pend_seq))
        k->ov_waits++;
    t0 = HwClock(k);
    if (R2Ring_WaitFence(&k->ring, k->pend_seq) != R2_OK)
        k->lost = CARD_LOST_FENCE;
    us = HwClock(k) - t0;
    if (us > k->fence_us_max)
        k->fence_us_max = us;
    WHERE(41, us);
    if (StopCp(k, closing) && !closing)
        k->drop_next = 1;
    WHERE(42, 0);
}

/* Settle outside a flush, under a lock of its own (the ring's, wherever
 * P96 has it). */
static void SettleAlone(struct Card *k, BOOL closing)
{
    BOOL locked;

    if (!k->pending)
        return;
    WHERE(45, closing);
    locked = LockBm(k, CARD_RING, TRUE);
    k->clock_base = Ticks();
    Settle(k, closing);
    if (locked)
        UnlockBm(k, CARD_RING);
}

/* The window kind: the inner area of the window, in its own coordinates
 * (a GimmeZeroZero window's RastPort is the inner area already). */
static void InnerArea(const struct Window *win, LONG *x, LONG *y, LONG *w, LONG *h)
{
    if (win->Flags & WFLG_GIMMEZEROZERO)
    {
        *x = 0;
        *y = 0;
        *w = win->GZZWidth;
        *h = win->GZZHeight;
    }
    else
    {
        *x = win->BorderLeft;
        *y = win->BorderTop;
        *w = win->Width - win->BorderLeft - win->BorderRight;
        *h = win->Height - win->BorderTop - win->BorderBottom;
    }
}

void Card_InnerSize(const struct Card *k, ULONG *w, ULONG *h)
{
    LONG x, y, iw, ih;

    *w = 0;
    *h = 0;
    if (k->win == NULL)
        return;
    InnerArea(k->win, &x, &y, &iw, &ih);
    if (iw > 0 && ih > 0)
    {
        *w = (ULONG) iw;
        *h = (ULONG) ih;
    }
}

/* The window kind, after the flush's unlock (D4): the show bitmap into the
 * window by Radeon.card's blitter, which clips to the window's layer. The
 * flush waited for the Radeon first (CardFlush), so the copy is complete.
 * A window the program made smaller than the context gets what fits. */
static void ShowInWindow(struct Card *k)
{
    LONG x, y, w, h;

    InnerArea(k->win, &x, &y, &w, &h);
    if (w <= 0 || h <= 0)
        return;
    if ((ULONG) w < k->width || (ULONG) h < k->height)
        k->blit_cut++;
    if ((ULONG) w > k->width)
        w = (LONG) k->width;
    if ((ULONG) h > k->height)
        h = (LONG) k->height;
    BltBitMapRastPort(k->b[CARD_SCREEN].bm, 0, 0, k->win->RPort, x, y, w, h, 0xc0);
    k->blits++;
}

/* Under card_sem: another card's kicked frame finished before this card
 * touches the chip */
static void SettleOthers(struct Card *k)
{
    if (card_running != NULL && card_running != k && card_running->pending)
        SettleAlone(card_running, FALSE);
}

/* One flush. wait: the old way, the Radeon done before the unlock (always
 * without the overlap, and with more than one card); else the frame is
 * kicked and left running. Card_Flush holds card_sem around it. */
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
    /* a window's frame is blitted, a bitmap's read by the program, right
     * after this: the Radeon must have finished the copy (no overlap) */
    if (present && k->kind != CARD_KIND_SCREEN)
        wait = TRUE;

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
    WHERE(1, n);

    /* 1. the locks: target, ring, heap (and a bitmap context's colour
     * buffer, the program's bitmap), on the board */
    if (!LockBm(k, CARD_TARGET, FALSE) || !LockBm(k, CARD_RING, FALSE)
        || !LockBm(k, CARD_HEAP, FALSE)
        || (k->kind == CARD_KIND_BITMAP && !LockBm(k, CARD_SCREEN, FALSE)))
    {
        UnlockAll(k);
        SettleAlone(k, FALSE);
        k->offboard++;
        Drop(k);
        k->drop_next = 0;
        k->in_flush = 0;
        return R2_OK;
    }

    WHERE(2, k->pending);
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
    DisplayPriority(k);

    /* 1c. 2026-11-07: a GL screen behind another screen is not drawn -- no
     * one sees it, and its 3D work and copy load the board's memory while
     * the screen in front is shown (the user: the 1920 x 1080 Workbench
     * went black and back while GLQuake ran behind it). The frame is
     * dropped like one whose bitmaps moved (the uploads it carried are
     * forgotten, Core_Rebase), and the program waits a little, so that a
     * hidden game does not spin. */
    if (present && k->kind == CARD_KIND_SCREEN && k->scr != NULL
        && IntuitionBase->FirstScreen != k->scr)
    {
        UnlockAll(k);
        Drop(k);
        k->dropped--;               /* counted on its own */
        k->hidden++;
        k->in_flush = 0;
        Delay(2);
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
    if (present && k->kind == CARD_KIND_BITMAP)
        k->presents++;              /* drawn into the program's bitmap already: nothing to copy */
    else if (present)
    {
        scr_ok = LockBm(k, CARD_SCREEN, FALSE);
        if (scr_ok && (k->b[CARD_SCREEN].gpu & 15) == 0 && (k->b[CARD_SCREEN].bpr / k->bpp) % 8 == 0)
        {
            s.gpu = k->b[CARD_SCREEN].gpu;
            s.pitch = k->b[CARD_SCREEN].bpr / k->bpp;
            s.format = k->show_format;
            s.width = k->show_w;
            s.height = k->show_h;
            k->cs.end = k->cmd + CARD_CMD_DW;   /* the reserve */
            if (k->core->sync)
            {
                if (VsyncWindow(k, &k->vsync_crtc, &k->vsync_lines))
                {
                    /* the frame drawn first, then the beam: else a frame
                     * still drawing would start the copy late */
                    R2E_Reg(&k->cs, R_RB3D_DSTCACHE_CTLSTAT, R_RB3D_DC_FLUSH_ALL);
                    R2E_Reg(&k->cs, R_WAIT_UNTIL, R_WAIT_3D_IDLECLEAN);
                    R2E_WaitVline(&k->cs, k->vsync_crtc, 0, k->vsync_lines);
                    k->vsyncs++;
                }
                else
                    k->vsync_off++;
            }
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
        k->cfg.range[3].size = k->b[CARD_SCREEN].bpr * k->show_h;
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
    WHERE(5, present);
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

#ifdef CARD_MOVE16
    if (k->move16 != 0 && !k->m16_checked)
    {
        k->m16_checked = 1;
        if (!Move16SelfTest(k))
            k->move16 = 0;      /* the fallback: longword stores (card.h) */
    }
#endif

    WHERE(6, k->core->stream_pos);
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
        ToVram(k, dst, src, (k->core->stream_pos - k->copied) / 4);
        k->copied = k->core->stream_pos;
    }
    /* the upload cache's new entries, from its mirror */
    if (k->core->ustaged && (k->core->udirty_n != 0 || k->core->udirty_all))
        CopyUCache(k);
    WHERE(7, n);
    e = CopyCommands(k, n);
    if (e == R2_OK)
        e = R2Ring_Fence(&k->ring, &seq);
    WHERE(8, e);
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
    WHERE(9, k->pending);
    UnlockAll(k);
#if defined(RGL_TRAPCATCH) && defined(RGL_CPU_040)
    rgl_pending = k->pending;
#endif
    WHERE(10, k->pending);
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
    if (scr_ok && k->kind == CARD_KIND_WINDOW && k->lost == 0)
        ShowInWindow(k);
    return (k->lost != 0) ? R2_ETIMEOUT : R2_OK;
}

/* --- the core's hooks ----------------------------------------------------------------- */

LONG Card_Flush(struct Card *k, BOOL present)
{
    LONG e;
    ULONG ran;

    Card_Obtain();
    SettleOthers(k);
    ran = k->flushes;
    e = CardFlush(k, present, (BOOL) (!k->overlap || card_users > 1));
    if (k->pending)
        card_running = k;
    /* several cards: the next commands may follow another card's on the
     * engine -- they carry the whole state (the dropped-frame path) */
    if (card_users > 1 && k->flushes != ran && k->core != NULL)
        Core_Forget(k->core);
    Card_Release();
    return e;
}

/* Everything built drawn, the Radeon idle: a texture hazard, a stream
 * wrap, glFinish. */
LONG Card_WaitIdle(struct Card *k)
{
    LONG e;
    ULONG ran;

    if (k->in_flush)
        return R2_OK;
    Card_Obtain();
    SettleOthers(k);
    ran = k->flushes;
    e = CardFlush(k, FALSE, TRUE);
    SettleAlone(k, FALSE);
    if (card_users > 1 && k->flushes != ran && k->core != NULL)
        Core_Forget(k->core);
    Card_Release();
    return (k->lost != 0) ? R2_ETIMEOUT : e;
}

ULONG *Card_VramBegin(struct Card *k)
{
    Card_Obtain();                  /* Card_VramEnd releases it */
    if (k->in_flush)
    {
        if (k->b[CARD_HEAP].mem != NULL)
            return (ULONG *) HeapCpu(k);
        Card_Release();
        return NULL;
    }
    SettleOthers(k);
    OvLock(k, CARD_OV_VRAM);
    WHERE(20, k->pending);
    if (!LockBm(k, CARD_HEAP, TRUE))
    {
        Card_Release();
        return NULL;
    }
    if (k->pending)
    {
        /* the kicked frame may still read what is about to be written */
        k->clock_base = Ticks();
        Settle(k, FALSE);
    }
    k->vram_lock = 1;
    k->vram_locks++;
    WHERE(21, k->b[CARD_HEAP].mem);
    return (ULONG *) HeapCpu(k);
}

void Card_VramEnd(struct Card *k)
{
    if (k->vram_lock)
    {
        UnlockBm(k, CARD_HEAP);
        k->vram_lock = 0;
    }
    WHERE(22, 0);
    Card_Release();
}

/* --- opening -------------------------------------------------------------------------- */

/* Every Picasso96 mode of a Radeon board in the format want, each to fn
 * until it returns TRUE: that id, else INVALID_ID. P96Base open.
 *
 * graphics.library's display database, not Picasso96's lists:
 * p96AllocModeListTagList stops at 256 modes (docs/p96-mode-list-256.md,
 * for Thor), which on the A4000D left out 1680 x 1050 to 1920 x 1440 -- the
 * Workbench's own 1920 x 1080 among them. The database has them all.
 * MEASURED 2026-11-07: it also has a set of P96 ids $FFF01xxx, one 320 x 200
 * in each format, ahead of the real ones; p96OpenScreenTagList refuses them
 * and ModeNotAvailable says so (8192). Taken for 320 x 200 they made the GL
 * screen fail to open: an id must be available. */
static BOOL BoardIsRadeon(ULONG id);

static ULONG WalkModes(ULONG want, BOOL (*fn)(ULONG id, void *ud), void *ud)
{
    ULONG id;

    for (id = NextDisplayInfo((ULONG) INVALID_ID); id != (ULONG) INVALID_ID; id = NextDisplayInfo(id))
    {
        if (!p96GetModeIDAttr(id, P96IDA_ISP96)
            || p96GetModeIDAttr(id, P96IDA_RGBFORMAT) != want
            || ModeNotAvailable(id) != 0
            || !BoardIsRadeon(id))
            continue;
        if (fn(id, ud))
            return id;
    }
    return (ULONG) INVALID_ID;
}

struct ExactWalk
{
    ULONG w, h;
};

static BOOL IsExact(ULONG id, void *ud)
{
    struct ExactWalk *ex;

    ex = (struct ExactWalk *) ud;
    return (BOOL) (p96GetModeIDAttr(id, P96IDA_WIDTH) == ex->w
                   && p96GetModeIDAttr(id, P96IDA_HEIGHT) == ex->h);
}

static ULONG BestMode(ULONG w, ULONG h, ULONG depth)
{
    struct TagItem t[5];
    struct ExactWalk ex;
    ULONG id;

    /* a mode of exactly that size first, from all of them (WalkModes) */
    ex.w = w;
    ex.h = h;
    id = WalkModes((depth == 16) ? RGBFB_R5G6B5PC : RGBFB_B8G8R8A8, IsExact, &ex);
    if (id != (ULONG) INVALID_ID)
        return id;

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

/* --- the GL screen ------------------------------------------------------------------ */

/* The input window: backdrop and borderless over the whole GL screen. */
static BOOL OpenInputWindow(struct Card *k)
{
    struct TagItem t[12];

    t[0].ti_Tag = WA_CustomScreen;  t[0].ti_Data = (ULONG) k->scr;
    t[1].ti_Tag = WA_Left;          t[1].ti_Data = 0;
    t[2].ti_Tag = WA_Top;           t[2].ti_Data = 0;
    t[3].ti_Tag = WA_Width;         t[3].ti_Data = k->width;
    t[4].ti_Tag = WA_Height;        t[4].ti_Data = k->height;
    t[5].ti_Tag = WA_Backdrop;      t[5].ti_Data = TRUE;
    t[6].ti_Tag = WA_Borderless;    t[6].ti_Data = TRUE;
    t[7].ti_Tag = WA_Activate;      t[7].ti_Data = TRUE;
    t[8].ti_Tag = WA_RMBTrap;       t[8].ti_Data = TRUE;
    t[9].ti_Tag = WA_SimpleRefresh; t[9].ti_Data = TRUE;
    t[10].ti_Tag = WA_NoCareRefresh; t[10].ti_Data = TRUE;
    t[11].ti_Tag = TAG_DONE;        t[11].ti_Data = 0;
    k->win = OpenWindowTagList(NULL, t);
    return (BOOL) (k->win != NULL);
}

/* The GL screen in mode id, w x h, and its input window. bpp: the bytes a
 * pixel the screen must have (a resize keeps the format), 0 for either of
 * R5G6B5PC and B8G8R8A8. FALSE with why[]; what opened stays for the
 * caller to close. */
static BOOL OpenGLScreen(struct Card *k, ULONG id, ULONG w, ULONG h, ULONG bpp, char *why,
                       ULONG whylen)
{
    struct TagItem t[10];
    struct BitMap *sbm;
    ULONG fmt;

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
        WhyMode(why, whylen, "the GL screen did not open", id, w, h);
        return FALSE;
    }
    sbm = k->scr->RastPort.BitMap;
    k->b[CARD_SCREEN].bm = sbm;
    fmt = p96GetBitMapAttr(sbm, P96BMA_RGBFORMAT);
    if (bpp == 0)
        bpp = p96GetBitMapAttr(sbm, P96BMA_BYTESPERPIXEL);
    if (p96GetBitMapAttr(sbm, P96BMA_BYTESPERPIXEL) != bpp
        || !((bpp == 2 && fmt == RGBFB_R5G6B5PC) || (bpp == 4 && fmt == RGBFB_B8G8R8A8)))
    {
        Why(why, whylen, "the GL screen is neither R5G6B5PC nor B8G8R8A8 (or not the format before)");
        return FALSE;
    }
    k->bpp = bpp;
    k->color_format = (k->bpp == 2) ? R_COLOR_FORMAT_RGB565 : R_COLOR_FORMAT_ARGB8888;
    k->width = w;
    k->height = h;
    k->friend = sbm;
    k->show_format = k->color_format;
    k->show_w = w;
    k->show_h = h;
    if (!OpenInputWindow(k))
    {
        Why(why, whylen, "the input window did not open");
        return FALSE;
    }
    return TRUE;
}

/* The back buffer for k->width x k->height: colour, then Z16, on the board.
 *
 * The R200 keeps its depth buffer tiled, always: Z16 in tiles of 64 x 16
 * pixels, two to a 4 KB page, and it counts a row in WHOLE tiles (Mesa
 * radeon_fbo.c get_depth_z16: pitch >> 7 bytes). With a pitch that is not a
 * multiple of 64 pixels the last, partial tile of each band lands on the
 * next band's second tile. MEASURED 2026-11-07, 800 x 600 (12.5 tiles):
 * x 768-799 of each 16-row band and x 64-95 of the band below shared their
 * depth, the surfaces behind the other's depth failed the test, and GLQuake
 * (no colour clear) showed 32-pixel columns of an old frame there. So the
 * depth pitch is its own, in whole tiles, and the rows whole bands. */
static BOOL AllocTarget(struct Card *k, char *why, ULONG whylen)
{
    ULONG colour;

    k->depth_pitch = (k->width + 63) & ~63UL;
    k->depth_bytes = Align4k(k->depth_pitch * ((k->height + 15) & ~15UL) * 2);
    if (k->kind == CARD_KIND_BITMAP)
    {
        /* W1: the colour buffer is the program's bitmap (k->pitch from its
         * rows, Card_OpenBitMap); ours holds the depth buffer only */
        k->color_bytes = k->pitch * k->show_h * k->bpp;
        colour = 0;
    }
    else
    {
        k->pitch = (k->width + 15) & ~15UL;     /* rows a multiple of 32 bytes (the present) */
        k->color_bytes = k->pitch * k->height * k->bpp;
        colour = Align4k(k->color_bytes);
    }
    if (!AllocData(k, CARD_TARGET, colour + k->depth_bytes + 8192))
    {
        Why(why, whylen, "not enough memory for the back buffer (colour and depth)");
        return FALSE;
    }
    if (!OnBoard(k, CARD_TARGET))
    {
        Why(why, whylen, "the back buffer did not fit on the graphics board");
        return FALSE;
    }
    return TRUE;
}

/* The input window and the GL screen closed (and the back buffer freed);
 * FALSE, with nothing closed, while a visitor window holds the screen. */
static BOOL CloseGLScreen(struct Card *k)
{
    if (k->win != NULL)
    {
        ModifyIDCMP(k->win, 0);
        CloseWindow(k->win);
        k->win = NULL;
    }
    if (k->scr != NULL && !p96CloseScreen(k->scr))
    {
        (void) OpenInputWindow(k);
        return FALSE;
    }
    k->scr = NULL;
    k->b[CARD_SCREEN].bm = NULL;
    FreeBm(k, CARD_TARGET);
    return TRUE;
}

static BOOL AllocShow(struct Card *k, char *why, ULONG whylen);
static const char *ShowCheck(struct Card *k);

/* Card_Resize for the window and bitmap kinds: the back buffer (and the
 * window's show bitmap) anew; on failure the old size again. */
static ULONG ResizeOffscreen(struct Card *k, ULONG w, ULONG h, char *why, ULONG whylen)
{
    const char *bad;
    ULONG oldw, oldh;
    BOOL ok;

    if (k->kind == CARD_KIND_BITMAP && (w > k->show_w || h > k->show_h))
    {
        Why(why, whylen, "larger than the program's bitmap");
        return CARD_FAILED;
    }
    oldw = k->width;
    oldh = k->height;
    UnlockAll(k);
    SettleAlone(k, FALSE);
    WaitBlit();
    FreeBm(k, CARD_TARGET);
    if (k->kind == CARD_KIND_WINDOW)
        FreeBm(k, CARD_SCREEN);
    k->width = w;
    k->height = h;
    ok = AllocTarget(k, why, whylen) && (k->kind != CARD_KIND_WINDOW || AllocShow(k, why, whylen));
    if (!ok)
    {
        FreeBm(k, CARD_TARGET);
        if (k->kind == CARD_KIND_WINDOW)
            FreeBm(k, CARD_SCREEN);
        k->width = oldw;
        k->height = oldh;
        if (!AllocTarget(k, why, whylen) || (k->kind == CARD_KIND_WINDOW && !AllocShow(k, why, whylen)))
        {
            k->lost = CARD_LOST_SCREEN;
            return CARD_FAILED;
        }
    }
    if (!LockBm(k, CARD_TARGET, FALSE)
        || (k->kind == CARD_KIND_BITMAP && !LockBm(k, CARD_SCREEN, FALSE)))
    {
        UnlockAll(k);
        Why(why, whylen, "the back buffer is not on the board");
        k->lost = CARD_LOST_SCREEN;
        return CARD_FAILED;
    }
    TargetAt(k, &k->color_gpu, &k->depth_gpu);
    UnlockAll(k);
    if (k->kind == CARD_KIND_WINDOW && (bad = ShowCheck(k)) != NULL)
    {
        Why(why, whylen, bad);
        k->lost = CARD_LOST_SCREEN;
        return CARD_FAILED;
    }
    return ok ? CARD_OK : CARD_FAILED;
}

static ULONG CardResize(struct Card *k, ULONG w, ULONG h, char *why, ULONG whylen);

ULONG Card_Resize(struct Card *k, ULONG w, ULONG h, char *why, ULONG whylen)
{
    ULONG res;

    Card_Obtain();
    SettleOthers(k);
    res = CardResize(k, w, h, why, whylen);
    Card_Release();
    return res;
}

static ULONG CardResize(struct Card *k, ULONG w, ULONG h, char *why, ULONG whylen)
{
    ULONG id, oldid, oldw, oldh, bpp;
    BOOL ok;

    Why(why, whylen, "");
    if (k->lost == 0 && P96Base != NULL && k->kind != CARD_KIND_SCREEN)
    {
        if (w == k->width && h == k->height)
            return CARD_OK;
        return ResizeOffscreen(k, w, h, why, whylen);
    }
    if (k->lost != 0 || k->scr == NULL || P96Base == NULL)
    {
        Why(why, whylen, "the context is lost");
        return CARD_FAILED;
    }
    if (w == k->width && h == k->height)
        return CARD_OK;
    bpp = k->bpp;
    id = BestMode(w, h, (bpp == 4) ? 32UL : 16UL);
    if (id == (ULONG) INVALID_ID || !BoardIsRadeon(id)
        || p96GetModeIDAttr(id, P96IDA_BOARDNUMBER) != k->board)
    {
        Why(why, whylen, "no mode of that size on the Radeon of this context");
        return CARD_FAILED;
    }
    oldid = GetVPModeID(&k->scr->ViewPort);
    oldw = k->width;
    oldh = k->height;

    /* 1. the last frame drawn, nothing locked */
    UnlockAll(k);
    SettleAlone(k, FALSE);
    WaitBlit();

    /* 2. the old screen, window and back buffer away */
    if (!CloseGLScreen(k))
    {
        Why(why, whylen, "a window on the GL screen keeps it open");
        return CARD_FAILED;
    }

    /* 3. the new ones -- or the old size again */
    ok = OpenGLScreen(k, id, w, h, bpp, why, whylen) && AllocTarget(k, why, whylen);
    if (!ok)
    {
        (void) CloseGLScreen(k);
        if (!OpenGLScreen(k, oldid, oldw, oldh, bpp, why, whylen) || !AllocTarget(k, why, whylen))
        {
            k->lost = CARD_LOST_SCREEN;
            return CARD_FAILED;
        }
    }

    /* 4. the back buffer's address, from a lock */
    if (!LockBm(k, CARD_TARGET, FALSE))
    {
        Why(why, whylen, "the back buffer is not on the board");
        k->lost = CARD_LOST_SCREEN;
        return CARD_FAILED;
    }
    TargetAt(k, &k->color_gpu, &k->depth_gpu);
    UnlockBm(k, CARD_TARGET);
    return ok ? CARD_OK : CARD_FAILED;
}

/* Picasso96API for a call without a context: the context's if one is open,
 * else opened here (P96Close undoes only that). */
static struct Library *P96Own(void)
{
    struct Library *own;

    if (P96Base != NULL)
        return NULL;
    own = OpenLibrary("Picasso96API.library", 2);
    P96Base = own;
    return own;
}

static void P96Close(struct Library *own)
{
    if (own != NULL)
    {
        CloseLibrary(own);
        P96Base = NULL;
    }
}

/* Card_EachMode's walker: each mode to the client (struct CardMode) */
struct EachWalk
{
    BOOL (*fn)(const struct CardMode *m, void *ud);
    void *ud;
};

static BOOL EachToClient(ULONG id, void *ud)
{
    struct EachWalk *ew;
    struct NameInfo ni;
    struct CardMode m;
    ULONG i;

    ew = (struct EachWalk *) ud;
    m.id = id;
    m.width = p96GetModeIDAttr(id, P96IDA_WIDTH);
    m.height = p96GetModeIDAttr(id, P96IDA_HEIGHT);
    m.depth = p96GetModeIDAttr(id, P96IDA_DEPTH);
    m.name[0] = 0;
    if (GetDisplayInfoData(NULL, (UBYTE *) &ni, sizeof(ni), DTAG_NAME, id) > 0)
    {
        for (i = 0; i < sizeof(m.name) - 1 && ni.Name[i] != 0; i++)
            m.name[i] = ni.Name[i];
        m.name[i] = 0;
    }
    return ew->fn(&m, ew->ud);
}

ULONG Card_EachMode(ULONG depth, BOOL (*fn)(const struct CardMode *m, void *ud), void *ud)
{
    struct Library *own;
    struct EachWalk ew;
    ULONG found;

    found = CARD_BESTMODE;
    Card_Obtain();
    own = P96Own();
    if (P96Base == NULL)
    {
        Card_Release();
        return found;
    }
    ew.fn = fn;
    ew.ud = ud;
    found = WalkModes((depth == 32) ? RGBFB_B8G8R8A8 : RGBFB_R5G6B5PC, EachToClient, &ew);
    if (found == (ULONG) INVALID_ID)
        found = CARD_BESTMODE;
    P96Close(own);
    Card_Release();
    return found;
}

BOOL Card_ModeSize(ULONG id, ULONG *w, ULONG *h)
{
    struct Library *own;
    BOOL ok;

    ok = FALSE;
    Card_Obtain();
    own = P96Own();
    if (P96Base != NULL && id != 0 && id != CARD_BESTMODE
        && p96GetModeIDAttr(id, P96IDA_ISP96) && BoardIsRadeon(id))
    {
        *w = p96GetModeIDAttr(id, P96IDA_WIDTH);
        *h = p96GetModeIDAttr(id, P96IDA_HEIGHT);
        ok = (BOOL) (*w != 0 && *h != 0);
    }
    P96Close(own);
    Card_Release();
    return ok;
}

/* The clock: the waits are bounded in microseconds. */
static BOOL OpenTimer(struct Card *k, char *why, ULONG whylen)
{
    struct EClockVal ev;

    k->tport = CreateMsgPort();
    k->treq = (k->tport != NULL)
        ? (struct timerequest *) CreateIORequest(k->tport, sizeof(struct timerequest)) : NULL;
    if (k->treq == NULL || OpenDevice((STRPTR) TIMERNAME, UNIT_MICROHZ, (struct IORequest *) k->treq, 0) != 0)
    {
        if (k->treq != NULL)
            DeleteIORequest((struct IORequest *) k->treq);
        k->treq = NULL;
        Why(why, whylen, "timer.device did not open");
        return FALSE;
    }
    TimerBase = k->treq->tr_node.io_Device;
    clk_Freq = ReadEClock(&ev);
    if (clk_Freq == 0)
        clk_Freq = 709379;
    k->clock_base = Ticks();
    return TRUE;
}

/* What every kind of context needs once its target is there: the ring, the
 * texture heap, the fast-RAM buffers, and the chip checked inside one lock.
 * CARD_OK, or CARD_FAILED with why[] (the caller closes). */
static ULONG OpenRest(struct Card *k, ULONG check, char *why, ULONG whylen)
{
    const char *bad;
    ULONG size;
    ULONG top;
    ULONG step;
    BOOL ok;

    if (!AllocData(k, CARD_RING, CARD_RING_DW * 4 + 4096))
    {
        Why(why, whylen, "no memory for the ring bitmap");
        return CARD_FAILED;
    }
    if (!OnBoard(k, CARD_RING))
    {
        Why(why, whylen, "P96 did not put the ring bitmap on the board");
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
     * stays for investigation only (RadeonGLHeapNarrow=1).
     * W1: a window or bitmap context starts smaller (k->heap_top, 8 MB by
     * default) and goes down to 2 MB, a friend of the window's screen or of
     * the program's bitmap; the Workbench way is the screen kind's only. */
    top = k->heap_top;
    step = 2UL << 20;
    ok = FALSE;
    for (size = top; !ok && !Card_HeapNarrow && size >= k->heap_min; size -= step)
    {
        if (HeapTry(k, size, CARD_HEAP_WIDE) || HeapTry(k, size, CARD_HEAP_BLIT)
            || (k->kind == CARD_KIND_SCREEN && size == top && !Card_NoHeap32
                && HeapTry(k, size, CARD_HEAP_WB32)))
            ok = TRUE;
        if (size < step)
            break;
    }
    for (size = top; !ok && Card_HeapNarrow && size >= k->heap_min; size >>= 1)
    {
        if (HeapTry(k, size, CARD_HEAP_NARROW))
            ok = TRUE;
    }
    if (!ok)
    {
        Why(why, whylen, (k->heap_min >= (4UL << 20)) ? "no texture heap of 4 MB or more on the board"
                                                      : "no texture heap of 2 MB or more on the board");
        return CARD_FAILED;
    }

    /* buffers in fast RAM */
    /* 16-byte aligned (MOVE16 lines; AllocVec gives 4 past 8) */
    k->cmd_mem = AllocVec(CARD_CMD_DW * 4 + 16, MEMF_ANY);
    k->staging_mem = AllocVec(CORE_STREAM_BYTES + 16, MEMF_ANY | MEMF_CLEAR);
    k->cmd = (k->cmd_mem != NULL) ? (ULONG *) (((ULONG) k->cmd_mem + 15UL) & ~15UL) : NULL;
    k->staging = (k->staging_mem != NULL) ? (ULONG *) (((ULONG) k->staging_mem + 15UL) & ~15UL) : NULL;
    if (k->cmd == NULL || k->staging == NULL)
    {
        Why(why, whylen, "no fast RAM for the command buffer");
        return CARD_FAILED;
    }

    /* the chip, inside one lock of the three */
    if (!LockBm(k, CARD_TARGET, FALSE) || !LockBm(k, CARD_RING, FALSE)
        || !LockBm(k, CARD_HEAP, FALSE))
    {
        UnlockAll(k);
        Why(why, whylen, "the bitmaps are not on the board");
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
    if (bad == NULL && k->kind != CARD_KIND_SCREEN)
        bad = ShowCheck(k);
    if (bad == NULL && k->kind == CARD_KIND_BITMAP)
        k->color_gpu = k->b[CARD_SCREEN].gpu;     /* from ShowCheck's lock */
    if (bad != NULL)
    {
        Why(why, whylen, bad);
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

/* The opens, under card_sem; a card that opened counts (card_users) */
static ULONG Counted(struct Card *k, ULONG res)
{
    if (res == CARD_OK)
    {
        k->counted = 1;
        card_users++;
    }
    Card_Release();
    return res;
}

static ULONG CardOpen(struct Card *k, ULONG w, ULONG h, ULONG depth, ULONG modeid, ULONG check,
                      char *why, ULONG whylen);
static ULONG CardOpenWindow(struct Card *k, struct Window *win, const struct CardOpts *o, ULONG check,
                            char *why, ULONG whylen);
static ULONG CardOpenOwnWindow(struct Card *k, LONG x, LONG y, ULONG w, ULONG h, const struct CardOpts *o,
                               ULONG check, char *why, ULONG whylen);
static ULONG CardOpenBitMap(struct Card *k, struct BitMap *bm, const struct CardOpts *o, ULONG check,
                            char *why, ULONG whylen);

ULONG Card_Open(struct Card *k, ULONG w, ULONG h, ULONG depth, ULONG modeid, ULONG check,
                char *why, ULONG whylen)
{
    Card_Obtain();
    SettleOthers(k);
    return Counted(k, CardOpen(k, w, h, depth, modeid, check, why, whylen));
}

ULONG Card_OpenWindow(struct Card *k, struct Window *win, const struct CardOpts *o, ULONG check,
                      char *why, ULONG whylen)
{
    Card_Obtain();
    SettleOthers(k);
    return Counted(k, CardOpenWindow(k, win, o, check, why, whylen));
}

ULONG Card_OpenOwnWindow(struct Card *k, LONG x, LONG y, ULONG w, ULONG h, const struct CardOpts *o,
                         ULONG check, char *why, ULONG whylen)
{
    Card_Obtain();
    SettleOthers(k);
    return Counted(k, CardOpenOwnWindow(k, x, y, w, h, o, check, why, whylen));
}

ULONG Card_OpenBitMap(struct Card *k, struct BitMap *bm, const struct CardOpts *o, ULONG check,
                      char *why, ULONG whylen)
{
    Card_Obtain();
    SettleOthers(k);
    return Counted(k, CardOpenBitMap(k, bm, o, check, why, whylen));
}

static ULONG CardOpen(struct Card *k, ULONG w, ULONG h, ULONG depth, ULONG modeid, ULONG check,
                      char *why, ULONG whylen)
{
    static struct Card zero;
    ULONG id;
    ULONG want;
    ULONG res;

    *k = zero;
    Why(why, whylen, "");
    if (!P96Open(k))
        return CARD_NOT_RADEON;
    want = (depth == 32) ? 32UL : 16UL;
    /* the mode asked for (MGLCreateContextFromID), if it is a Radeon's;
     * else the best one for the size */
    id = (ULONG) INVALID_ID;
    if (modeid != 0 && modeid != CARD_BESTMODE && p96GetModeIDAttr(modeid, P96IDA_ISP96)
        && BoardIsRadeon(modeid))
        id = modeid;
    if (id == (ULONG) INVALID_ID)
        id = BestMode(w, h, want);
    if (id == (ULONG) INVALID_ID)
        id = BestMode(w, h, (want == 16) ? 32UL : 16UL);
    if (id == (ULONG) INVALID_ID || !BoardIsRadeon(id))
    {
        Card_Close(k);
        return CARD_NOT_RADEON;
    }
    k->kind = CARD_KIND_SCREEN;
    k->heap_top = (Card_HeapMax >= 4 && Card_HeapMax < 16) ? Card_HeapMax << 20 : 16UL << 20;
    k->heap_min = 4UL << 20;

    /* the clock, the GL screen and its input window, the back buffer, the rest */
    res = CARD_FAILED;
    if (OpenTimer(k, why, whylen) && OpenGLScreen(k, id, w, h, 0, why, whylen)
        && AllocTarget(k, why, whylen))
        res = OpenRest(k, check, why, whylen);
    if (res != CARD_OK)
        Card_Close(k);
    return res;
}

/* --- W1: windows and the program's bitmaps ---------------------------------------- */

/* The present's colour format for a P96 bitmap the R200 writes as it is
 * stored -- the "PC" formats are the chip's own little-endian ones -- and
 * the back buffer's bytes a pixel (2 or 4); 0 for any other bitmap. */
static ULONG ShowFormat(struct BitMap *bm, ULONG *bpp)
{
    ULONG fmt;

    *bpp = 0;
    if (bm == NULL || !p96GetBitMapAttr(bm, P96BMA_ISP96))
        return 0;
    fmt = p96GetBitMapAttr(bm, P96BMA_RGBFORMAT);
    if (fmt == RGBFB_R5G6B5PC && p96GetBitMapAttr(bm, P96BMA_BYTESPERPIXEL) == 2)
    {
        *bpp = 2;
        return R_COLOR_FORMAT_RGB565;
    }
    if (fmt == RGBFB_R5G5B5PC && p96GetBitMapAttr(bm, P96BMA_BYTESPERPIXEL) == 2)
    {
        *bpp = 2;
        return R_COLOR_FORMAT_ARGB1555;
    }
    if (fmt == RGBFB_B8G8R8A8 && p96GetBitMapAttr(bm, P96BMA_BYTESPERPIXEL) == 4)
    {
        *bpp = 4;
        return R_COLOR_FORMAT_ARGB8888;
    }
    return 0;
}

static ULONG StrLen(const char *s)
{
    ULONG n;

    for (n = 0; s[n] != 0; n++)
        ;
    return n;
}

/* Why a bitmap's format cannot take GL: its format named (the A4000D's
 * 24-bit Workbench, 2026-11-07, was the first one refused). */
static void FormatWhy(char *why, ULONG len, const char *whose, struct BitMap *bm)
{
    ULONG fmt;
    const char *name;

    fmt = p96GetBitMapAttr(bm, P96BMA_ISP96) ? p96GetBitMapAttr(bm, P96BMA_RGBFORMAT) : RGBFB_NONE;
    name = (fmt == RGBFB_CLUT) ? " is 8 bit (CLUT)"
         : (fmt == RGBFB_R8G8B8 || fmt == RGBFB_B8G8R8) ? " is 24 bit (R8G8B8 or B8G8R8)"
         : (fmt == RGBFB_NONE) ? " is not a Picasso96 bitmap (planar)"
         : " has a byte order the Radeon does not render";
    Why(why, len, whose);
    Why(why + StrLen(why), len - StrLen(why), name);
    Why(why + StrLen(why), len - StrLen(why), ": the Radeon renders 15, 16 and 32 bit (R5G5B5PC, R5G6B5PC, B8G8R8A8)");
}

/* The window kind's show bitmap: a friend of the window's screen at the
 * context's size, where the present copies the frame and from where it is
 * blitted into the window. */
static BOOL AllocShow(struct Card *k, char *why, ULONG whylen)
{
    struct CardBm *b;

    b = &k->b[CARD_SCREEN];
    k->own_show = 1;
    b->p96 = 0;
    b->bpp = k->bpp;
    b->bytes = 0;
    b->bm = AllocBitMap((k->width + 15) & ~15UL, k->height, GetBitMapAttr(k->friend, BMA_DEPTH),
                        BMF_MINPLANES, k->friend);
    if (b->bm == NULL || !p96GetBitMapAttr(b->bm, P96BMA_ISP96))
    {
        FreeBm(k, CARD_SCREEN);
        Why(why, whylen, "not enough memory for the window's frame bitmap");
        return FALSE;
    }
    if (!OnBoard(k, CARD_SCREEN))
    {
        FreeBm(k, CARD_SCREEN);
        Why(why, whylen, "the window's frame bitmap did not fit on the graphics board");
        return FALSE;
    }
    k->show_w = k->width;
    k->show_h = k->height;
    return TRUE;
}

/* Can the present write into b[CARD_SCREEN] (a window's show bitmap, the
 * program's bitmap)? On the board, on the same board as ours, its first
 * pixel 16-byte aligned and its rows a multiple of 8 pixels (the R200's
 * colour pitch, Core_Present). NULL, or why not. Outside any lock. */
static const char *ShowCheck(struct Card *k)
{
    struct CardBm *b;
    const char *bad;

    b = &k->b[CARD_SCREEN];
    if (!LockBm(k, CARD_SCREEN, FALSE))
        return (k->kind == CARD_KIND_BITMAP) ? "the program's bitmap is not on the graphics board"
                                             : "the window's frame bitmap is not on the graphics board";
    bad = NULL;
    if ((APTR) p96GetBitMapAttr(b->bm, P96BMA_BOARDMEMBASE) != k->membase)
        bad = "the bitmap is on another graphics board than the Radeon's";
    else if ((b->gpu & 15) != 0 || (b->bpr % b->bpp) != 0 || ((b->bpr / b->bpp) % 8) != 0)
        bad = "the bitmap's rows do not suit the Radeon (alignment, pitch)";
    UnlockBm(k, CARD_SCREEN);
    return bad;
}

/* Is there a Radeon board at all (a mode of one)? */
static BOOL TakeAny(ULONG id, void *ud)
{
    (void) id;
    (void) ud;
    return TRUE;
}

static BOOL AnyRadeon(void)
{
    return (BOOL) (WalkModes(RGBFB_R5G6B5PC, TakeAny, NULL) != (ULONG) INVALID_ID
                   || WalkModes(RGBFB_B8G8R8A8, TakeAny, NULL) != (ULONG) INVALID_ID);
}

/* The common part of the window kind. win stays the program's unless own. */
static ULONG OpenWindowKind(struct Card *k, struct Window *win, ULONG own, const struct CardOpts *o,
                            ULONG check, char *why, ULONG whylen)
{
    struct Screen *scr;
    struct BitMap *sbm;
    LONG id;
    ULONG bpp;
    ULONG w, h;
    ULONG res;

    k->kind = CARD_KIND_WINDOW;
    k->win = win;
    k->own_win = own;
    scr = win->WScreen;
    sbm = scr->RastPort.BitMap;
    id = (LONG) GetVPModeID(&scr->ViewPort);
    if (id == (LONG) INVALID_ID || !p96GetModeIDAttr((ULONG) id, P96IDA_ISP96)
        || !BoardIsRadeon((ULONG) id))
    {
        if (AnyRadeon())
        {
            Why(why, whylen, "the window's screen is not on the Radeon's board");
            return CARD_FAILED;
        }
        return CARD_NOT_RADEON;
    }
    k->show_format = ShowFormat(sbm, &bpp);
    if (k->show_format == 0)
    {
        FormatWhy(why, whylen, "the window's screen", sbm);
        return CARD_FAILED;
    }
    Card_InnerSize(k, &w, &h);
    if (w == 0 || h == 0 || w > 2048 || h > 2048)
    {
        Why(why, whylen, "the window's inner area is empty or larger than 2048");
        return CARD_FAILED;
    }
    k->friend = sbm;
    k->board = p96GetModeIDAttr((ULONG) id, P96IDA_BOARDNUMBER);
    k->compat = 0;
    k->bpp = bpp;
    k->color_format = (bpp == 2) ? R_COLOR_FORMAT_RGB565 : R_COLOR_FORMAT_ARGB8888;
    k->width = w;
    k->height = h;
    k->heap_top = (o != NULL && o->heap_top >= (2UL << 20)) ? o->heap_top : (8UL << 20);
    if (Card_HeapMax >= 2 && (Card_HeapMax << 20) < k->heap_top)
        k->heap_top = Card_HeapMax << 20;
    if (k->heap_top > (16UL << 20))
        k->heap_top = 16UL << 20;
    k->heap_top &= ~((2UL << 20) - 1);
    k->heap_min = 2UL << 20;
    res = CARD_FAILED;
    if (OpenTimer(k, why, whylen) && AllocTarget(k, why, whylen) && AllocShow(k, why, whylen))
        res = OpenRest(k, check, why, whylen);
    return res;
}

static ULONG CardOpenWindow(struct Card *k, struct Window *win, const struct CardOpts *o, ULONG check,
                            char *why, ULONG whylen)
{
    static struct Card zero;
    ULONG res;

    *k = zero;
    Why(why, whylen, "");
    if (win == NULL)
    {
        Why(why, whylen, "no window");
        return CARD_FAILED;
    }
    if (!P96Open(k))
        return CARD_NOT_RADEON;
    res = OpenWindowKind(k, win, 0, o, check, why, whylen);
    if (res != CARD_OK)
        Card_Close(k);
    return res;
}

static ULONG CardOpenOwnWindow(struct Card *k, LONG x, LONG y, ULONG w, ULONG h, const struct CardOpts *o,
                               ULONG check, char *why, ULONG whylen)
{
    static struct Card zero;
    struct TagItem t[14];
    struct Screen *scr;
    struct Window *win;
    ULONG res;

    *k = zero;
    Why(why, whylen, "");
    if (!P96Open(k))
        return CARD_NOT_RADEON;
    scr = LockPubScreen(NULL);
    if (scr == NULL)
    {
        Why(why, whylen, "no default public screen");
        Card_Close(k);
        return CARD_FAILED;
    }
    t[0].ti_Tag = WA_PubScreen;     t[0].ti_Data = (ULONG) scr;
    t[1].ti_Tag = WA_Left;          t[1].ti_Data = (ULONG) ((x > 0) ? x : 30);
    t[2].ti_Tag = WA_Top;           t[2].ti_Data = (ULONG) ((y > 0) ? y : 30);
    t[3].ti_Tag = WA_InnerWidth;    t[3].ti_Data = w;
    t[4].ti_Tag = WA_InnerHeight;   t[4].ti_Data = h;
    t[5].ti_Tag = WA_Title;         t[5].ti_Data = (ULONG) "RadeonGL";
    t[6].ti_Tag = WA_DragBar;       t[6].ti_Data = TRUE;
    t[7].ti_Tag = WA_DepthGadget;   t[7].ti_Data = TRUE;
    t[8].ti_Tag = WA_Activate;      t[8].ti_Data = TRUE;
    t[9].ti_Tag = WA_RMBTrap;       t[9].ti_Data = TRUE;
    t[10].ti_Tag = WA_SimpleRefresh; t[10].ti_Data = TRUE;
    t[11].ti_Tag = WA_NoCareRefresh; t[11].ti_Data = TRUE;
    t[12].ti_Tag = WA_AutoAdjust;   t[12].ti_Data = TRUE;
    t[13].ti_Tag = TAG_DONE;        t[13].ti_Data = 0;
    win = OpenWindowTagList(NULL, t);
    UnlockPubScreen(NULL, scr);
    if (win == NULL)
    {
        Why(why, whylen, "the window did not open");
        Card_Close(k);
        return CARD_FAILED;
    }
    res = OpenWindowKind(k, win, 1, o, check, why, whylen);
    if (res != CARD_OK)
        Card_Close(k);              /* closes the window too (own_win) */
    return res;
}

/* Is bm on a Radeon's board? The screens tell: a screen on a Radeon board
 * whose bitmap has the same board memory base. FALSE also when there is no
 * screen on a Radeon at all. */
#define CARD_SCAN 16
static BOOL BitMapOnRadeon(struct BitMap *bm, ULONG *board)
{
    struct Screen *s;
    APTR base[CARD_SCAN];
    ULONG id[CARD_SCAN];
    ULONG n;
    ULONG lock;
    ULONG i;
    APTR mine;

    /* collected under the lock, looked at after it (P96 calls may need
     * Intuition) */
    n = 0;
    lock = LockIBase(0);
    for (s = IntuitionBase->FirstScreen; s != NULL && n < CARD_SCAN; s = s->NextScreen)
    {
        base[n] = (APTR) s->RastPort.BitMap;
        id[n] = GetVPModeID(&s->ViewPort);
        n++;
    }
    UnlockIBase(lock);
    mine = (APTR) p96GetBitMapAttr(bm, P96BMA_BOARDMEMBASE);
    for (i = 0; i < n; i++)
    {
        if (id[i] == (ULONG) INVALID_ID || !p96GetModeIDAttr(id[i], P96IDA_ISP96)
            || !BoardIsRadeon(id[i]))
            continue;
        if ((APTR) p96GetBitMapAttr((struct BitMap *) base[i], P96BMA_BOARDMEMBASE) == mine)
        {
            *board = p96GetModeIDAttr(id[i], P96IDA_BOARDNUMBER);
            return TRUE;
        }
    }
    return FALSE;
}

/* The program's bitmap as the colour buffer: on the board, its first
 * pixel 16-byte aligned, its rows a whole number of pixels and a multiple
 * of 8 (the R200's colour pitch); k->pitch from them. The GPU address and
 * the board are checked once the chip is known (ShowCheck). */
static BOOL BitMapRows(struct Card *k, char *why, ULONG whylen)
{
    struct CardBm *b;
    BOOL ok;

    b = &k->b[CARD_SCREEN];
    if (!LockBm(k, CARD_SCREEN, FALSE))
    {
        Why(why, whylen, "the program's bitmap is not on the graphics board");
        return FALSE;
    }
    ok = (BOOL) (((ULONG) b->mem & 15) == 0 && (b->bpr % b->bpp) == 0 && ((b->bpr / b->bpp) % 8) == 0);
    k->pitch = b->bpr / b->bpp;
    UnlockBm(k, CARD_SCREEN);
    if (!ok)
        Why(why, whylen, "the bitmap's rows do not suit the Radeon (alignment, pitch)");
    return ok;
}

static ULONG CardOpenBitMap(struct Card *k, struct BitMap *bm, const struct CardOpts *o, ULONG check,
                            char *why, ULONG whylen)
{
    static struct Card zero;
    ULONG bpp;
    ULONG w, h;
    ULONG res;

    *k = zero;
    Why(why, whylen, "");
    if (bm == NULL)
    {
        Why(why, whylen, "no bitmap");
        return CARD_FAILED;
    }
    if (!P96Open(k))
        return CARD_NOT_RADEON;
    k->kind = CARD_KIND_BITMAP;
    res = CARD_FAILED;
    if (!p96GetBitMapAttr(bm, P96BMA_ISP96))
        Why(why, whylen, "the bitmap is not a Picasso96 bitmap");
    else if ((k->show_format = ShowFormat(bm, &bpp)) == 0)
        FormatWhy(why, whylen, "the bitmap", bm);
    else if (!BitMapOnRadeon(bm, &k->board))
    {
        if (AnyRadeon())
            Why(why, whylen, "the bitmap is not on the Radeon's board");
        else
            res = CARD_NOT_RADEON;
    }
    else
    {
        w = GetBitMapAttr(bm, BMA_WIDTH);
        h = GetBitMapAttr(bm, BMA_HEIGHT);
        if (w == 0 || h == 0 || w > 2048 || h > 2048)
            Why(why, whylen, "the bitmap is empty or larger than 2048");
        else
        {
            k->friend = bm;
            k->b[CARD_SCREEN].bm = bm;
            k->b[CARD_SCREEN].bpp = bpp;
            k->own_show = 0;
            k->show_w = w;
            k->show_h = h;
            k->compat = 0;
            k->bpp = bpp;
            k->color_format = k->show_format;   /* drawn into as it is, 15-bit too */
            k->width = w;
            k->height = h;
            k->heap_top = (o != NULL && o->heap_top >= (2UL << 20)) ? o->heap_top : (8UL << 20);
            if (Card_HeapMax >= 2 && (Card_HeapMax << 20) < k->heap_top)
                k->heap_top = Card_HeapMax << 20;
            if (k->heap_top > (16UL << 20))
                k->heap_top = 16UL << 20;
            k->heap_top &= ~((2UL << 20) - 1);
            k->heap_min = 2UL << 20;
            if (BitMapRows(k, why, whylen) && OpenTimer(k, why, whylen) && AllocTarget(k, why, whylen))
                res = OpenRest(k, check, why, whylen);
        }
    }
    if (res != CARD_OK)
        Card_Close(k);
    return res;
}

void Card_Target(const struct Card *k, struct CoreTarget *t)
{
    t->width = k->width;
    t->height = k->height;
    t->color_gpu = k->color_gpu;
    t->color_pitch = k->pitch;
    t->color_format = k->color_format;
    t->depth_gpu = k->depth_gpu;
    t->depth_pitch = k->depth_pitch;
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

    Card_Obtain();
    UnlockAll(k);
    k->vram_lock = 0;
    SettleAlone(k, TRUE);           /* the last frame drawn, the CP off */
    WaitBlit();
    /* Radeon.card's display FIFO setting back (RadeonGLDisplayPriority) */
    if (k->grph_saved && k->b[CARD_TARGET].bm != NULL && LockBm(k, CARD_TARGET, FALSE))
    {
        HwWrite(k, R_GRPH_BUFFER_CNTL, k->grph_seen);
        UnlockBm(k, CARD_TARGET);
        k->grph_saved = 0;
    }
    for (i = 0; i < CARD_SCREEN; i++)
        FreeBm(k, i);
    if (k->own_show)
        FreeBm(k, CARD_SCREEN);     /* the window kind's show bitmap */
    k->b[CARD_SCREEN].bm = NULL;    /* else the screen's or the program's */
    if (k->win != NULL)
    {
        /* the program's window (FromWindow) stays as it is */
        if (k->kind == CARD_KIND_SCREEN || k->own_win)
        {
            ModifyIDCMP(k->win, 0);
            CloseWindow(k->win);
        }
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
    if (k->cmd_mem != NULL)
        FreeVec(k->cmd_mem);
    if (k->staging_mem != NULL)
        FreeVec(k->staging_mem);
    k->cmd_mem = NULL;
    k->staging_mem = NULL;
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
    if (k->counted)
    {
        k->counted = 0;
        card_users--;
    }
    if (card_running == k)
        card_running = NULL;
    if (card_users == 0)
        TimerBase = NULL;           /* (every card's timer.device is the same base) */
    P96Release(k);
    Card_Release();
}
