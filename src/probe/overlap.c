/*
 * Overlap -- may the R200 keep drawing after the bitmaps are unlocked?
 *
 * RadeonGL waits for its fence and stops the command processor before every
 * unlock, so the CPU and the GPU take turns: 0.9 ms a frame at 320 x 240,
 * 8.3 ms at 1280 x 720 (the A4000D's profile, 2026-11-03). Letting the GPU
 * finish frame N while the CPU builds frame N+1 is safe only if P96 waits
 * for the 3D engine before it touches an unlocked bitmap. Thomas Richter
 * (2026-11-05): P96 calls the board's WaitBlitter() before it migrates a
 * bitmap, and on the Radeon that sync covers the 3D part through a common
 * busy bit. This probe checks that on the card, with Tri's scene and Tri's
 * safety rules (the checker before the CP, the canary, bounded waits, the
 * 3D blocks reset only when the engine stays busy):
 *
 *  A  The busy bit. A job of scene draws, a long run of type-2 fillers
 *     (the CP busy, the 3D engine idle) and one more draw, committed at
 *     once. While its fence has not passed, RBBM_STATUS is sampled: does
 *     RBBM_ACTIVE (bit 31) stay set the whole time? Control: the same
 *     samples after the fence must read idle.
 *  B  The lock. The same job, then the block is unlocked with the CP
 *     still running, and a bitmap is locked at once (B1 another bitmap of
 *     the board, B2 the block itself, as the library would). Does the lock
 *     return only after our work is done? Control B0: the job finished
 *     before the unlock, so the lock costs what LockBench measured.
 *  C  Radeon.card's 2D engine while our CP stays on. p96RectFill and
 *     BltBitMap on another bitmap, read back and compared: C0 with the CP
 *     stopped (control), C1 with the CP on and idle, C2 while the job still
 *     runs (the fill should wait for it). NO2D skips C.
 *
 * Every job draws into a colour buffer filled with a pattern first; the
 * picture is compared with the reference rasteriser's check pixels after
 * each job. The console's copy of the report is held from the first lock
 * until the CP is stopped for good (plog.h): a console draws through the
 * board, and while our CP runs unlocked nothing of ours may draw. Keep the
 * Workbench quiet during the run.
 *
 *   Overlap LOG/K,SCENE/K,DRAWS/N,GAP/N,NO2D/S
 *
 * SCENE=TEX (default) or TRI; DRAWS scene draws before the filler run
 * (default 80, fewer when the ring cannot hold them); GAP filler dwords
 * (default 16384).
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <libraries/Picasso96.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/Picasso96.h>

#include "p96board.h"
#include "pcicard.h"
#include "mmio.h"
#include "r200regs.h"
#include "r200probe.h"
#include "eclock.h"
#include "plog.h"
#include "r2cs.h"
#include "r2pm4.h"
#include "r2ring.h"
#include "r2cp.h"
#include "r2check.h"
#include "r200reg.h"
#include "core.h"
#include "triscene.h"
#include "texscene.h"

static const char ver[] = "$VER: Overlap 0.1 (05.11.2026)";

#define TEMPLATE "LOG/K,SCENE/K,DRAWS/N,GAP/N,NO2D/S"

struct Args
{
    STRPTR log;
    STRPTR scene;
    LONG *draws;
    LONG *gap;
    LONG no2d;
};

#define RING_DW       65536UL      /* 256 KB: a whole job before one commit */
#define RING_BYTES    (RING_DW * 4)
#define COLOR_BYTES   (TRI_W * TRI_H * 4UL)
#define DEPTH_BYTES   (TRI_W * TRI_H * 2UL)
#define HEAP_BYTES    0x10000UL
#define BLOCK_BYTES   (RING_BYTES + COLOR_BYTES + DEPTH_BYTES + HEAP_BYTES + 4096UL)
#define STREAM_DW     8192UL
#define FENCE_REG     R_SCRATCH_REG2
#define TIMEOUT_US    100000UL     /* engine idle, CP start and stop */
#define JOB_US        2000000UL    /* a job's fence */
#define CHUNK_DW      4096UL       /* filler written in pieces of this */

#define MAXPIX        32
#define DEF_DRAWS     80UL
#define DEF_GAP       16384UL
#define CTRL_SAMPLES  200UL
#define PATTERN       0x5A5A5A5AUL /* the colour buffer before a job */
#define PAL_FILL      0x80000080UL /* a type-2 filler in both byte orders */
#define CANARY_VALUE  0xA5C3C3A5UL
#define MMIO_TEST     0x5A3C3C5AUL

#define OTHER_W       256UL        /* the second bitmap, for B1 and C */
#define OTHER_H       64UL
#define STALE         0x5A5A5A5AUL

/* RBBM_SOFT_RESET: the 3D blocks only (Linux r100 names) */
#define SOFT_RESET_SE (1UL << 2)
#define SOFT_RESET_RE (1UL << 3)
#define SOFT_RESET_PP (1UL << 4)

struct Ctx
{
    struct PciCard *card;
    struct P96Board *board;
    struct R2Hw hw;
    struct R2CpSaved saved;
    struct R2Ring ring;
    ULONG fb_start;
    ULONG apSize;
    ULONG tf_orig;
    ULONG scr_orig;
    ULONG clock_base;
    ULONG bpr;
    BOOL locked;            /* the block is locked now */
    BOOL cp_on;             /* we started the CP and have not stopped it */
    BOOL regs_saved;        /* tf_orig and scr_orig hold what was found */
    UBYTE *mem;             /* the block's address at the first lock */
    ULONG *ring_cpu;
    ULONG ring_gpu;
    ULONG color_cpu;
    ULONG *buf;             /* the scene's stream, ring byte order */
    ULONG n;                /* its dwords */
    ULONG draws;
    ULONG gap;
    void (*draw)(struct Core *c);
    struct TriPixel pix[MAXPIX];
    ULONG npix;
    struct BitMap *other;   /* a second friend bitmap of the Workbench */
    ULONG job_us;           /* test A's job, commit to fence */
    ULONG bad;              /* picture or 2D checks that failed */
};

static struct Core *core;
static struct R2ChkState chk;

/* --- struct R2Hw over the probes' MMIO helpers (as Tri) ------------------------ */

static ULONG HwRead(APTR hw, ULONG reg)
{
    return Mmio_Read(((struct Ctx *)hw)->card->mmio, reg);
}

static void HwWrite(APTR hw, ULONG reg, ULONG val)
{
    Mmio_Write(((struct Ctx *)hw)->card->mmio, reg, val);
}

static ULONG HwReadBack(APTR hw, volatile ULONG *p)
{
    (void)hw;
    return *p;
}

static ULONG HwClock(APTR hw)
{
    return Clock_Us(Clock_Since(((struct Ctx *)hw)->clock_base));
}

static APTR EnvAlloc(APTR env, ULONG size)
{
    (void)env;
    return AllocVec(size, MEMF_ANY | MEMF_CLEAR);
}

static void EnvFree(APTR env, APTR mem, ULONG size)
{
    (void)env;
    (void)size;
    FreeVec(mem);
}

static LONG EnvIdle(APTR env)
{
    (void)env;
    return 0;
}

static ULONG Align4k(ULONG a)
{
    return (a + 4095UL) & ~4095UL;
}

static ULONG Gpu(struct Ctx *c, ULONG cpu)
{
    return c->fb_start + (cpu - (ULONG)c->card->aper) % c->apSize;
}

static BOOL InSurface(struct Ctx *c, ULONG off, ULONG bytes)
{
    ULONG n;
    ULONG lo;
    ULONG hi;
    ULONG inf;

    for (n = 0; n < R_SURFACE_COUNT; n++)
    {
        inf = HwRead(c, R_SURFACE0_INFO + n * R_SURFACE_STRIDE);
        if ((inf & 0x00f70000UL) == 0)
            continue;
        lo = HwRead(c, R_SURFACE0_LOWER_BOUND + n * R_SURFACE_STRIDE);
        hi = HwRead(c, R_SURFACE0_UPPER_BOUND + n * R_SURFACE_STRIDE);
        if (off <= hi && off + bytes - 1 >= lo)
            return TRUE;
    }
    return FALSE;
}

static BOOL Reset3D(struct Ctx *c)
{
    HwWrite(c, R_RBBM_SOFT_RESET, SOFT_RESET_SE | SOFT_RESET_RE | SOFT_RESET_PP);
    (void)HwRead(c, R_RBBM_SOFT_RESET);
    HwWrite(c, R_RBBM_SOFT_RESET, 0);
    (void)HwRead(c, R_RBBM_SOFT_RESET);
    return (BOOL)(R2Cp_WaitIdle(&c->hw, TIMEOUT_US) == R2_OK);
}

static BOOL FenceDone(struct Ctx *c, ULONG seq)
{
    return (BOOL)((LONG)(HwRead(c, FENCE_REG) - seq) >= 0);
}

/* --- the block's lock: its address must never change during the run ---------- */

static BOOL LockBlock(struct Ctx *c)
{
    if (c->locked)
        return TRUE;
    if (!P96Board_Lock(c->board))
    {
        Log_Printf("the block could not be locked on the board\n");
        return FALSE;
    }
    c->locked = TRUE;
    if (c->mem != NULL && c->board->mem != c->mem)
    {
        /* P96 moved it while it was unlocked: the ring and the buffers are
         * elsewhere now -- nothing more is written */
        Log_Printf("THE BLOCK MOVED while it was unlocked (0x%08lx -> 0x%08lx)\n",
                   (ULONG)c->mem, (ULONG)c->board->mem);
        return FALSE;
    }
    c->mem = c->board->mem;
    return TRUE;
}

static void UnlockBlock(struct Ctx *c)
{
    if (c->locked)
        P96Board_Unlock(c->board);
    c->locked = FALSE;
}

/* --- the GPU part --------------------------------------------------------------- */

/* Inside the first lock, before anything is written (as Tri). */
static BOOL Prepare(struct Ctx *c)
{
    ULONG v;
    ULONG reset;
    LONG e;

    if (R200_ReportEndian(c->card) == MMIO_UNDECIDED)
    {
        Log_Printf("register byte order undecided: the card is not touched\n");
        return FALSE;
    }
    if (!R200_CheckFamily(c->card))
    {
        Log_Printf("not an R200-family chip: the R200 microcode is not loaded\n");
        return FALSE;
    }
    R2Cp_Save(&c->hw, &c->saved);
    if (R2Cp_InUse(&c->saved))
    {
        Log_Printf("the CP is RUNNING (CSQ_CNTL 0x%08lx): left over from a run that crashed\n",
                   c->saved.csq_cntl);
        e = R2Cp_Reclaim(&c->hw, &c->saved, TIMEOUT_US, &reset);
        Log_Printf("  stopped: %ld%s\n", e,
                   reset ? (CONST_STRPTR)" (the CP was soft-reset)" : (CONST_STRPTR)"");
        if (e != R2_OK)
            return FALSE;
    }
    v = HwRead(c, R_MC_FB_LOCATION);
    c->fb_start = (v & 0xffffUL) << 16;
    c->apSize = HwRead(c, R_CONFIG_APER_SIZE);
    if (c->apSize == 0 || (c->apSize & (c->apSize - 1)) != 0)
    {
        Log_Printf("CONFIG_APER_SIZE 0x%08lx is not a power of two: nothing drawn\n", c->apSize);
        return FALSE;
    }
    c->tf_orig = HwRead(c, R_PP_TFACTOR_0);
    c->scr_orig = HwRead(c, FENCE_REG);
    c->regs_saved = TRUE;
    HwWrite(c, R_PP_TFACTOR_0, MMIO_TEST);
    v = HwRead(c, R_PP_TFACTOR_0);
    HwWrite(c, R_PP_TFACTOR_0, c->tf_orig);
    if (v != MMIO_TEST)
    {
        Log_Printf("PP_TFACTOR_0 does not read back (0x%08lx): no canary, nothing drawn\n", v);
        return FALSE;
    }
    return TRUE;
}

/* Inside a lock: microcode, the CP on the ring, the canary. */
static BOOL CpStart(struct Ctx *c)
{
    ULONG *p;
    ULONG i;
    ULONG t0;
    ULONG rptr;
    ULONG v;
    LONG e;

    if (R2Cp_WaitIdle(&c->hw, TIMEOUT_US) != R2_OK)
    {
        Log_Printf("the engine is not idle (RBBM_STATUS 0x%08lx): the CP is not started\n",
                   HwRead(c, R_RBBM_STATUS));
        return FALSE;
    }
    R2Cp_LoadMicrocode(&c->hw, R2Cp_Microcode);
    v = R2Cp_CheckMicrocode(&c->hw, R2Cp_Microcode);
    if (v != 0)
    {
        Log_Printf("microcode: %lu words differ on read-back: the CP is not started\n", v);
        return FALSE;
    }
    for (i = 0; i < RING_DW; i++)
        c->ring_cpu[i] = PAL_FILL;
    (void)HwReadBack(c, &c->ring_cpu[RING_DW - 1]);
    HwWrite(c, R_PP_TFACTOR_0, 0);
    c->cp_on = TRUE;            /* from here on CpStop puts everything back */
    e = R2Cp_Start(&c->hw, c->ring_gpu, RING_DW, 0);
    if (e == R2_OK)
        e = R2Ring_Init(&c->ring, c->ring_cpu, c->ring_gpu, RING_DW, &c->hw, 1, FENCE_REG,
                        TIMEOUT_US);
    if (e != R2_OK)
    {
        Log_Printf("the CP did not start: %ld\n", e);
        return FALSE;
    }
    c->ring.kick = 0;           /* commit only when asked: a job goes in whole */
    p = R2Cs_Room(&c->ring.cs, 16);
    if (p == NULL)
        return FALSE;
    p[0] = R2_OUT(&c->ring.cs, R2_PKT0(R_PP_TFACTOR_0, 0));
    p[1] = R2_OUT(&c->ring.cs, CANARY_VALUE);
    for (i = 2; i < 16; i++)
        p[i] = PAL_FILL;
    c->ring.cs.ptr = p + 16;
    t0 = HwClock(c);
    (void)R2Ring_Commit(&c->ring);
    do
    {
        rptr = HwRead(c, R_CP_RB_RPTR);
    } while (rptr != 16 && HwClock(c) - t0 < TIMEOUT_US);
    v = HwRead(c, R_PP_TFACTOR_0);
    HwWrite(c, R_PP_TFACTOR_0, c->tf_orig);
    if (rptr != 16 || v != CANARY_VALUE)
    {
        Log_Printf("canary: RPTR %lu, PP_TFACTOR_0 0x%08lx: FAILED, nothing drawn\n", rptr, v);
        return FALSE;
    }
    return TRUE;
}

/* Inside a lock: the CP off again, as found; the 3D blocks reset only if
 * the engine stays busy. */
static void CpStop(struct Ctx *c)
{
    ULONG reset;
    LONG e;

    if (!c->cp_on)
        return;
    c->cp_on = FALSE;
    e = R2Cp_Stop(&c->hw, &c->saved, TIMEOUT_US, &reset);
    Log_Printf("CP stopped: %ld%s; RBBM_STATUS 0x%08lx\n", e,
               reset ? (CONST_STRPTR)" (the CP was soft-reset)" : (CONST_STRPTR)"",
               HwRead(c, R_RBBM_STATUS));
    if (e != R2_OK)
    {
        if (Reset3D(c))
            Log_Printf("the 3D blocks (SE, RE, PP) were reset: the engine is idle\n");
        else
            Log_Printf("THE ENGINE STAYS BUSY (RBBM_STATUS 0x%08lx): save your work and reboot\n",
                       HwRead(c, R_RBBM_STATUS));
    }
}

/* Inside a lock: the colour buffer to the pattern, then the job (draws
 * scenes, the filler run, one scene, the fence) committed at once. */
static BOOL QueueJob(struct Ctx *c, ULONG draws, ULONG gap, ULONG *seq)
{
    ULONG *p;
    ULONG i;
    ULONG d;
    ULONG k;
    ULONG left;

    for (i = 0; i < TRI_H; i++)
        for (k = 0; k < TRI_W; k++)
            ((volatile ULONG *)(c->color_cpu + i * c->bpr))[k] = PATTERN;
    for (d = 0; d <= draws; d++)
    {
        if (d == draws)
        {
            for (left = gap; left > 0; left -= k)
            {
                k = left < CHUNK_DW ? left : CHUNK_DW;
                p = R2Cs_Room(&c->ring.cs, k);
                if (p == NULL)
                    return FALSE;
                for (i = 0; i < k; i++)
                    p[i] = PAL_FILL;
                c->ring.cs.ptr = p + k;
            }
        }
        p = R2Cs_Room(&c->ring.cs, c->n);
        if (p == NULL)
        {
            Log_Printf("no ring room for %lu dwords: %ld\n", c->n, c->ring.cs.error);
            return FALSE;
        }
        for (i = 0; i < c->n; i++)
            p[i] = c->buf[i];
        c->ring.cs.ptr = p + c->n;
    }
    if (R2Ring_Fence(&c->ring, seq) != R2_OK || R2Ring_Commit(&c->ring) != R2_OK)
        return FALSE;
    return TRUE;
}

/* Inside a lock: wait for seq, bounded. The microseconds waited, or ~0. */
static ULONG WaitJob(struct Ctx *c, ULONG seq)
{
    ULONG t0;

    t0 = HwClock(c);
    while (!FenceDone(c, seq))
        if (HwClock(c) - t0 > JOB_US)
            return ~0UL;
    return HwClock(c) - t0;
}

/* Inside a lock, after a job: the check pixels against the reference. */
static BOOL CheckPicture(struct Ctx *c, CONST_STRPTR what)
{
    volatile ULONG *col;
    ULONG i;
    ULONG v;
    ULONG got;
    ULONG bad;
    BOOL ok;

    col = (volatile ULONG *)c->color_cpu;
    bad = 0;
    for (i = 0; i < c->npix; i++)
    {
        v = col[c->pix[i].y * (c->bpr / 4) + c->pix[i].x];
        got = (((v >> 8) & 0xffUL) << 16) | (((v >> 16) & 0xffUL) << 8) | (v >> 24);
        ok = c->pix[i].exact ? (BOOL)(got == c->pix[i].rgb) : (BOOL)(got != c->pix[i].rgb);
        if (!ok)
        {
            if (bad < 4)
                Log_Printf("  picture: (%lu, %lu) 0x%06lx, %s0x%06lx\n", c->pix[i].x, c->pix[i].y,
                           got, c->pix[i].exact ? (CONST_STRPTR)"want " : (CONST_STRPTR)"not ",
                           c->pix[i].rgb);
            bad++;
        }
    }
    Log_Printf("%s: the picture %s (%lu of %lu check pixels right)\n", what,
               bad == 0 ? (CONST_STRPTR)"is right" : (CONST_STRPTR)"IS WRONG", c->npix - bad, c->npix);
    if (bad != 0)
        c->bad++;
    return (BOOL)(bad == 0);
}

/* --- A: the busy bit while work is queued ---------------------------------------- */

static BOOL TestA(struct Ctx *c)
{
    ULONG seq;
    ULONG t0;
    ULONG st;
    ULONG rp;
    ULONG wp;
    ULONG pending;
    ULONG idle;
    ULONG idle_cp;
    ULONG fetched;
    ULONG first_rp;
    ULONG ctrl_idle;
    ULONG i;
    BOOL timeout;

    Log_Section("A: RBBM_ACTIVE while our work is queued (inside the lock)");
    if (!QueueJob(c, c->draws, c->gap, &seq))
        return FALSE;
    t0 = HwClock(c);
    wp = c->ring.committed;
    pending = 0;
    idle = 0;
    idle_cp = 0;
    fetched = 0;
    first_rp = ~0UL;
    timeout = FALSE;
    for (;;)
    {
        st = HwRead(c, R_RBBM_STATUS);
        rp = HwRead(c, R_CP_RB_RPTR);
        if (FenceDone(c, seq))
            break;
        pending++;
        if (rp == wp)
            fetched++;
        if (!(st & R_RBBM_ACTIVE))
        {
            idle++;
            if (!(st & R_CP_CMDSTRM_BUSY))
                idle_cp++;
            if (first_rp == ~0UL)
                first_rp = rp;
        }
        if (HwClock(c) - t0 > JOB_US)
        {
            timeout = TRUE;
            break;
        }
    }
    c->job_us = HwClock(c) - t0;
    if (timeout)
    {
        Log_Printf("the job's fence did not pass within %lu ms\n", JOB_US / 1000);
        return FALSE;
    }
    /* the control: the engine idle now, the bit must read clear */
    ctrl_idle = 0;
    for (i = 0; i < CTRL_SAMPLES; i++)
        if (!(HwRead(c, R_RBBM_STATUS) & R_RBBM_ACTIVE))
            ctrl_idle++;
    Log_Printf("job: %lu draws, %lu filler dwords, 1 draw, fence: %lu us (%lu ring dwords)\n",
               c->draws, c->gap, c->job_us, (c->draws + 1) * c->n + c->gap);
    Log_Printf("samples while queued: %lu; RBBM_ACTIVE clear in %lu (CP_CMDSTRM_BUSY clear "
               "as well in %lu); RPTR at WPTR (all fetched) in %lu\n",
               pending, idle, idle_cp, fetched);
    if (first_rp != ~0UL)
        Log_Printf("  the first clear sample: RPTR %lu of WPTR %lu\n", first_rp, wp);
    Log_Printf("control, the engine idle after the fence: RBBM_ACTIVE clear in %lu of %lu samples\n",
               ctrl_idle, CTRL_SAMPLES);
    (void)CheckPicture(c, "A");
    if (ctrl_idle != CTRL_SAMPLES || pending == 0)
        Log_Printf("A: NO ANSWER (the control did not read idle, or nothing was sampled)\n");
    else if (idle == 0)
        Log_Printf("A: YES -- RBBM_ACTIVE stayed set in all %lu samples while work was queued\n",
                   pending);
    else
        Log_Printf("A: NO -- RBBM_ACTIVE was clear in %lu of %lu samples while work was queued\n",
                   idle, pending);
    return TRUE;
}

/* --- B: does a P96 lock wait for our command processor? -------------------------- */

/* The job queued, the block unlocked with the CP running, then a lock:
 * of the other bitmap (own FALSE) or of the block itself (own TRUE).
 * wait_first: the control, the job finished before the unlock. The block
 * is locked again on return (TRUE). */
static BOOL TestB(struct Ctx *c, BOOL own, BOOL wait_first, CONST_STRPTR name)
{
    struct RenderInfo ri;
    ULONG seq;
    ULONG t0;
    ULONG lock_us;
    ULONG st;
    ULONG rp;
    ULONG rest;
    LONG lock;
    BOOL done;

    Log_Section(name);
    if (!QueueJob(c, c->draws, c->gap, &seq))
        return FALSE;
    if (wait_first && WaitJob(c, seq) == ~0UL)
    {
        Log_Printf("the job's fence did not pass\n");
        return FALSE;
    }
    UnlockBlock(c);                   /* the CP keeps running */
    t0 = Clock_Now();
    if (own)
    {
        if (!LockBlock(c))
            return FALSE;
        lock = 1;
    }
    else
        lock = P96Board_LockBitMap(c->other, &ri);
    lock_us = Clock_Us(Clock_Since(t0));
    if (lock == 0)
    {
        Log_Printf("the other bitmap could not be locked\n");
        (void)LockBlock(c);
        return FALSE;
    }
    /* inside a lock of the board: the registers may be read */
    done = FenceDone(c, seq);
    st = HwRead(c, R_RBBM_STATUS);
    rp = HwRead(c, R_CP_RB_RPTR);
    if (!own)
    {
        P96Board_UnlockBitMap(c->other, lock);
        if (!LockBlock(c))
            return FALSE;
    }
    rest = done ? 0 : WaitJob(c, seq);
    if (rest == ~0UL)
    {
        Log_Printf("the job's fence did not pass after the lock\n");
        return FALSE;
    }
    Log_Printf("the lock took %lu us (test A's job: %lu us); when it returned the fence had "
               "%s (RPTR %lu of %lu, RBBM_STATUS 0x%08lx); %lu us more to the fence\n",
               lock_us, c->job_us, done ? (CONST_STRPTR)"PASSED" : (CONST_STRPTR)"NOT passed",
               rp, c->ring.committed, st, rest);
    (void)CheckPicture(c, name);
    if (wait_first)
        Log_Printf("%s: control, the GPU idle at the unlock: the lock took %lu us\n", name, lock_us);
    else if (done)
        Log_Printf("%s: YES -- the lock waited for our work (%lu us)\n", name, lock_us);
    else
        Log_Printf("%s: NO -- the lock returned after %lu us with our work still running\n",
                   name, lock_us);
    return TRUE;
}

/* --- C: Radeon.card's 2D engine while our CP is on -------------------------------- */

/* Unlocked: the other bitmap to STALE through its lock, two p96RectFills
 * and a BltBitMap through the board's 2D engine, then the pixels read
 * back. TRUE when all are right. */
static BOOL Do2D(struct Ctx *c, ULONG k, CONST_STRPTR name, ULONG *fill_us)
{
    static const ULONG col1[3] = { 0x00C02010UL, 0x00A0F030UL, 0x00E0E000UL };
    static const ULONG col2[3] = { 0x0010C040UL, 0x003050E0UL, 0x0000E0E0UL };
    static const ULONG px[5] = { 10, 200, 160, 230, 64 };
    static const ULONG py[5] = { 10, 10, 48, 48, 60 };
    static const UBYTE which[5] = { 1, 2, 1, 2, 1 };
    struct RenderInfo ri;
    struct RastPort rp;
    volatile ULONG *m;
    ULONG want;
    ULONG got;
    ULONG v;
    ULONG i;
    ULONG t0;
    ULONG bad;
    LONG lock;

    lock = P96Board_LockBitMap(c->other, &ri);
    if (lock == 0 || ri.Memory == NULL)
    {
        if (lock != 0)
            P96Board_UnlockBitMap(c->other, lock);
        Log_Printf("%s: the other bitmap could not be locked\n", name);
        return FALSE;
    }
    m = (volatile ULONG *)ri.Memory;
    for (i = 0; i < OTHER_H * ((ULONG)ri.BytesPerRow / 4); i++)
        m[i] = STALE;
    P96Board_UnlockBitMap(c->other, lock);

    InitRastPort(&rp);
    rp.BitMap = c->other;
    t0 = Clock_Now();
    p96RectFill(&rp, 0, 0, 127, (UWORD)(OTHER_H - 1), 0xFF000000UL | col1[k]);
    *fill_us = Clock_Us(Clock_Since(t0));
    p96RectFill(&rp, 128, 0, 255, (UWORD)(OTHER_H - 1), 0xFF000000UL | col2[k]);
    /* the left half's corner into the right half: (128..191, 32..63) */
    BltBitMap(c->other, 0, 0, c->other, 128, 32, 64, 32, 0xc0, 0xff, NULL);

    lock = P96Board_LockBitMap(c->other, &ri);
    if (lock == 0 || ri.Memory == NULL)
    {
        if (lock != 0)
            P96Board_UnlockBitMap(c->other, lock);
        Log_Printf("%s: the other bitmap could not be locked for the read-back\n", name);
        return FALSE;
    }
    m = (volatile ULONG *)ri.Memory;
    bad = 0;
    for (i = 0; i < 5; i++)
    {
        v = m[py[i] * ((ULONG)ri.BytesPerRow / 4) + px[i]];
        got = (((v >> 8) & 0xffUL) << 16) | (((v >> 16) & 0xffUL) << 8) | (v >> 24);
        want = which[i] == 1 ? col1[k] : col2[k];
        if (got != want)
            bad++;
        Log_Printf("  (%3lu, %2lu) 0x%06lx %s 0x%06lx\n", px[i], py[i], got,
                   got == want ? (CONST_STRPTR)"ok, want" : (CONST_STRPTR)"BAD, want", want);
    }
    P96Board_UnlockBitMap(c->other, lock);
    Log_Printf("%s: Radeon.card's fills and blit %s; the first fill took %lu us\n", name,
               bad == 0 ? (CONST_STRPTR)"are right" : (CONST_STRPTR)"ARE WRONG", *fill_us);
    if (bad != 0)
        c->bad++;
    return (BOOL)(bad == 0);
}

static BOOL TestC(struct Ctx *c)
{
    ULONG seq;
    ULONG fill_us;
    ULONG rest;
    BOOL ok1;
    BOOL ok2;

    /* C0, the control: the CP stopped, as Radeon.card always finds it today */
    Log_Section("C0: Radeon.card's 2D engine, our CP stopped (control)");
    CpStop(c);
    UnlockBlock(c);
    if (!Do2D(c, 0, "C0", &fill_us))
    {
        Log_Printf("C0: the control failed: C1 and C2 are not run\n");
        return LockBlock(c);
    }

    /* C1: the CP on and idle */
    Log_Section("C1: Radeon.card's 2D engine, our CP on and idle");
    if (!LockBlock(c) || !CpStart(c))
        return FALSE;
    if (!QueueJob(c, 1, 0, &seq) || WaitJob(c, seq) == ~0UL)
        return FALSE;
    UnlockBlock(c);
    ok1 = Do2D(c, 1, "C1", &fill_us);

    /* C2: the CP on and drawing */
    Log_Section("C2: Radeon.card's 2D engine while our job runs");
    if (!LockBlock(c))
        return FALSE;
    if (!QueueJob(c, c->draws, c->gap, &seq))
        return FALSE;
    UnlockBlock(c);
    ok2 = Do2D(c, 2, "C2", &fill_us);
    if (!LockBlock(c))
        return FALSE;
    rest = FenceDone(c, seq) ? 0 : WaitJob(c, seq);
    Log_Printf("C2: when the 2D work was done the job %s (test A's job: %lu us)\n",
               rest == 0 ? (CONST_STRPTR)"had finished" : (CONST_STRPTR)"WAS STILL RUNNING",
               c->job_us);
    if (rest == ~0UL)
        return FALSE;
    (void)CheckPicture(c, "C2");
    Log_Printf("C: %s with our CP on and idle, %s while our job ran (the first fill took %lu us)\n",
               ok1 ? (CONST_STRPTR)"right" : (CONST_STRPTR)"WRONG",
               ok2 ? (CONST_STRPTR)"right" : (CONST_STRPTR)"WRONG", fill_us);
    return TRUE;
}

/* --- the run ---------------------------------------------------------------------- */

static void Run(struct Ctx *c, BOOL no2d)
{
    struct R2Cs bcs;
    struct CoreTarget t;
    struct CoreEnv env;
    struct R2ChkCfg cfg;
    struct R2ChkError err;
    ULONG mem;
    ULONG depth_cpu;
    ULONG heap_cpu;
    ULONG max;
    ULONG i;
    ULONG v;
    LONG e;

    if (!LockBlock(c))
        return;
    c->bpr = c->board->bpr;
    if (!Prepare(c))
        goto unlock;

    Log_Section("Layout");
    mem = (ULONG)c->mem;
    c->ring_cpu = (ULONG *)Align4k(mem);
    c->color_cpu = Align4k((ULONG)c->ring_cpu + RING_BYTES);
    depth_cpu = Align4k(c->color_cpu + c->bpr * TRI_H);
    heap_cpu = Align4k(depth_cpu + DEPTH_BYTES);
    c->ring_gpu = Gpu(c, (ULONG)c->ring_cpu);
    t.width = TRI_W;
    t.height = TRI_H;
    t.color_gpu = Gpu(c, c->color_cpu);
    t.color_pitch = c->bpr / 4;
    t.color_format = R_COLOR_FORMAT_ARGB8888;
    t.depth_gpu = Gpu(c, depth_cpu);
    t.depth_pitch = TRI_W;
    Log_Printf("block 0x%08lx, %lu bytes a row; ring GPU 0x%08lx (%lu dwords), colour GPU 0x%08lx\n",
               mem, c->bpr, c->ring_gpu, RING_DW, t.color_gpu);
    if (heap_cpu + HEAP_BYTES > mem + c->bpr * c->board->rows
        || (c->color_cpu - mem) % c->bpr != 0 || c->bpr % 32 != 0 || c->bpr / 4 < TRI_W)
    {
        Log_Printf("the layout does not fit the block: nothing drawn\n");
        goto unlock;
    }
    if (InSurface(c, c->ring_gpu - c->fb_start, heap_cpu + HEAP_BYTES - (ULONG)c->ring_cpu))
    {
        Log_Printf("the block overlaps a swapping surface window: nothing drawn\n");
        goto unlock;
    }

    /* the scene's stream, once, in fast RAM; the checker before the CP */
    R2Cs_InitBuffer(&bcs, c->buf, STREAM_DW, 1UL);
    env.alloc = EnvAlloc;
    env.alloc_raw = NULL;
    env.free = EnvFree;
    env.wait_idle = EnvIdle;
    env.env = NULL;
    v = Core_Init(core, &bcs, &env, &t, Gpu(c, heap_cpu), (ULONG *)heap_cpu, HEAP_BYTES);
    if (v != GL_NO_ERROR)
    {
        Log_Printf("Core_Init: GL error 0x%04lx\n", v);
        Core_Free(core);
        goto unlock;
    }
    c->draw(core);
    c->n = (ULONG)(bcs.ptr - c->buf);
    v = Core_GetError(core);
    Core_Free(core);
    Log_Printf("scene: %lu dwords, stream error %ld, GL error 0x%04lx\n", c->n, bcs.error, v);
    if (bcs.error != R2_OK || v != GL_NO_ERROR || c->n == 0)
        goto unlock;
    for (i = 0; i < sizeof(cfg); i++)
        ((UBYTE *)&cfg)[i] = 0;
    cfg.range[0].gpu = t.color_gpu;
    cfg.range[0].size = c->bpr * TRI_H;
    cfg.range[0].kinds = R2K_COLOR;
    cfg.range[1].gpu = t.depth_gpu;
    cfg.range[1].size = DEPTH_BYTES;
    cfg.range[1].kinds = R2K_DEPTH;
    cfg.range[2].gpu = Gpu(c, heap_cpu);
    cfg.range[2].size = HEAP_BYTES;
    cfg.range[2].kinds = R2K_TEXTURE | R2K_VERTEX | R2K_INDEX;
    cfg.nranges = 3;
    cfg.fence_reg = FENCE_REG;
    R2Chk_Reset(&chk);
    e = R2Chk_Run(&cfg, &chk, c->buf, c->n, 1UL, 0, &err);
    Log_Printf("checker: %s\n", e == R2CE_OK ? (CONST_STRPTR)"clean" : (CONST_STRPTR)R2Chk_ErrorName(e));
    if (e != R2CE_OK)
        goto unlock;

    /* a whole job must fit the ring at once */
    max = (RING_DW - 2048UL - c->gap) / c->n;
    if (max < 2)
    {
        Log_Printf("GAP=%lu leaves no room for the draws: nothing drawn\n", c->gap);
        goto unlock;
    }
    if (c->draws > max - 1)
        c->draws = max - 1;
    Log_Printf("a job: %lu draws, %lu filler dwords, 1 draw, a fence\n", c->draws, c->gap);

    Log_Section("Command processor");
    if (!CpStart(c))
        goto stop;
    Log_Printf("the CP runs (canary passed)\n");

    if (!TestA(c))
        goto stop;
    if (!TestB(c, FALSE, TRUE, "B0: a lock after the job has finished (control)")
        || !TestB(c, FALSE, FALSE, "B1: a lock of another bitmap while our job runs")
        || !TestB(c, TRUE, FALSE, "B2: a lock of the block itself while our job runs"))
        goto stop;
    if (!no2d)
        (void)TestC(c);

stop:
    if (LockBlock(c))
        CpStop(c);
    else if (c->cp_on)
    {
        /* the block moved or is gone: stop the CP inside the other bitmap's lock */
        struct RenderInfo ri;
        LONG lock;

        lock = P96Board_LockBitMap(c->other, &ri);
        if (lock != 0)
        {
            CpStop(c);
            P96Board_UnlockBitMap(c->other, lock);
        }
        else
            Log_Printf("THE CP COULD NOT BE STOPPED (no lock): reboot before the next run\n");
    }
unlock:
    if (c->locked && c->regs_saved)
    {
        HwWrite(c, R_PP_TFACTOR_0, c->tf_orig);
        HwWrite(c, FENCE_REG, c->scr_orig);
    }
    UnlockBlock(c);
}

int main(void)
{
    static struct P96Board board;
    struct RDArgs *rda;
    struct Args args;
    struct PciCard card;
    struct Ctx c;
    ULONG depth;
    ULONG i;
    int rc;

    if (((struct Process *) FindTask(NULL))->pr_CLI == 0)
        return RETURN_FAIL;
    args.log = NULL;
    args.scene = NULL;
    args.draws = NULL;
    args.gap = NULL;
    args.no2d = 0;
    rda = ReadArgs(TEMPLATE, (LONG *)&args, NULL);
    if (rda == NULL)
    {
        PrintFault(IoErr(), "Overlap");
        return RETURN_ERROR;
    }
    if (!Log_Open(args.log))
        Log_Printf("Overlap: cannot write %s, console only\n", args.log);
    Log_Printf("%s -- may the R200 keep drawing after the unlock?\n", ver + 6);

    rc = RETURN_FAIL;
    core = NULL;
    for (i = 0; i < sizeof(c); i++)
        ((UBYTE *)&c)[i] = 0;
    c.card = &card;
    c.board = &board;
    c.hw.read_reg = HwRead;
    c.hw.write_reg = HwWrite;
    c.hw.read_back = HwReadBack;
    c.hw.clock_us = HwClock;
    c.hw.hw = &c;
    if (args.scene != NULL && (args.scene[0] == 'T' || args.scene[0] == 't')
        && (args.scene[1] == 'R' || args.scene[1] == 'r'))
    {
        c.draw = TriScene_Draw;
        for (i = 0; i < TRI_NPIXELS; i++)
            c.pix[i] = TriScene_Pixels[i];
        c.npix = TRI_NPIXELS;
    }
    else
    {
        c.draw = TexScene_Draw;
        TexScene_Pixels(c.pix);
        c.npix = TEX_NPIXELS;
    }
    c.draws = (args.draws && *args.draws > 0) ? (ULONG)*args.draws : DEF_DRAWS;
    c.gap = (args.gap && *args.gap >= 0) ? (ULONG)*args.gap : DEF_GAP;
    if (c.gap > RING_DW / 2)
        c.gap = RING_DW / 2;
    Log_Printf("scene %s, %lu check pixels; %s\n", c.draw == TexScene_Draw ? (CONST_STRPTR)"TEX"
               : (CONST_STRPTR)"TRI", c.npix,
               args.no2d ? (CONST_STRPTR)"tests A and B" : (CONST_STRPTR)"tests A, B and C");

    if (!Clock_Open())
    {
        Log_Printf("Overlap: timer.device did not open\n");
        goto out;
    }
    c.clock_base = Clock_Now();
    Log_Section("VRAM block");
    if (!P96Board_Open(&board, BLOCK_BYTES))
        goto out;
    P96Board_Report(&board);
    if (!P96Board_IsRadeon(&board) || board.memIoBase == NULL)
    {
        Log_Printf("Overlap: the Workbench's board is not a Radeon: the card is not touched\n");
        goto out;
    }
    if (board.bpp != 4)
    {
        Log_Printf("Overlap: the Workbench has %lu bytes a pixel; Overlap needs a 32-bit Workbench\n",
                   board.bpp);
        goto out;
    }
    R200_CardFromP96(&card, &board);
    depth = GetBitMapAttr(board.scrbm, BMA_DEPTH);
    c.other = AllocBitMap(OTHER_W, OTHER_H, depth, BMF_MINPLANES, board.scrbm);
    c.buf = (ULONG *)AllocVec(STREAM_DW * 4, MEMF_ANY | MEMF_CLEAR);
    core = (struct Core *)AllocVec(sizeof(struct Core), MEMF_ANY | MEMF_CLEAR);
    if (c.other == NULL || c.buf == NULL || core == NULL)
    {
        Log_Printf("Overlap: no memory for the other bitmap or the stream\n");
        goto out;
    }
    if (!p96GetBitMapAttr(c.other, P96BMA_ISONBOARD))
    {
        Log_Printf("Overlap: the other bitmap is not on the board\n");
        goto out;
    }

    /* the console's copy stays held until the CP is off for good */
    Log_Hold(TRUE);
    Run(&c, (BOOL)(args.no2d != 0));
    Log_Hold(FALSE);
    Log_Printf("\nchecks that failed: %lu\n", c.bad);
    rc = (c.bad == 0 && c.job_us != 0) ? RETURN_OK : RETURN_WARN;

out:
    if (c.other != NULL)
    {
        WaitBlit();
        FreeBitMap(c.other);
    }
    P96Board_Close(&board);
    if (c.buf != NULL)
        FreeVec(c.buf);
    if (core != NULL)
        FreeVec(core);
    Clock_Close();
    Log_Printf("\nOverlap: done (rc %ld)\n", (LONG)rc);
    Log_Close();
    FreeArgs(rda);
    return rc;
}
