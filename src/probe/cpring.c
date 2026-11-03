/*
 * CPRing -- the R200 command processor, started for the first time
 * (concept section 11, probe 3).
 *
 * Radeon.card does not use the CP (R200Info on the A4000D: CP_CSQ_CNTL mode
 * 0; its author confirms it writes the registers directly). CPRing loads
 * the microcode, starts the CP on a 16 KB ring in a block of video RAM (a
 * friend bitmap of the Workbench screen, p96board.h), and answers:
 *
 *   - does the CP fetch and execute from a ring in VRAM through the
 *     Mediator, with bus mastering off and no write-back?
 *   - which ring byte order works: the 68k storing each dword byte-reversed
 *     (choice a), or the CP swapping (CP_RB_CNTL BUF_SWAP_32BIT, choice b)?
 *   - what a fence costs: commit, CP executes, scratch register polled.
 *
 * A CP reading byte-reversed packets would execute garbage register writes,
 * so each round starts with a CANARY that is harmless in BOTH byte orders:
 * the ring is filled with 0x80000080 (a PACKET2 filler, the same reversed),
 * and the first packet writes PP_TFACTOR_0, a 3D colour constant nobody
 * else uses: its header 0x00000BB8 reads reversed as a filler too, and its
 * value 0xA5C3C3A5 is a palindrome that also reads as a filler. Only when
 * PP_TFACTOR_0 then holds the value is the byte order right, and only then
 * do the fences (ordinary packets) run. The 3D engine is never set up, the
 * 2D engine (Radeon.card's) never touched. On exit the CP registers,
 * PP_TFACTOR_0 and SCRATCH_REG2 are as found. A CP found running can only
 * be one a crashed run left behind: it is stopped first (R2Cp_Reclaim).
 *
 * The card is found through Picasso96 (its register base from
 * p96GetBitMapAttr), and the whole run, from saving the CP registers to
 * putting them back, happens inside ONE lock of the block: P96 keeps off
 * the chip meanwhile. That lock lasts a few tens of milliseconds.
 *
 * The ring code is the library's own (src/r200: r2cp.c starts the CP,
 * r2ring.c writes the ring, r2emit.c the fence).
 *
 *   CPRing LOG/K,FENCES/N,ONLYA/S
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>

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

static const char ver[] = "$VER: CPRing 0.2 (02.11.2026)";

#define TEMPLATE "LOG/K,FENCES/N,ONLYA/S"

struct Args
{
    STRPTR log;
    LONG *fences;
    LONG onlya;
};

#define RING_DW       4096UL            /* 16 KB */
#define BLOCK_BYTES   (32UL * 1024)     /* the ring, 4 KB aligned inside */
#define FENCE_REG     R_SCRATCH_REG2
#define TIMEOUT_US    100000UL          /* every wait: 100 ms */

#define PAL_FILL      0x80000080UL      /* PACKET2 both ways round */
#define CANARY_VALUE  0xA5C3C3A5UL      /* a palindrome, PACKET2 reversed */
#define MMIO_TEST     0x5A3C3C5AUL

struct Ctx
{
    struct PciCard *card;   /* the bases P96 gave */
    struct P96Board *board; /* the block, locked for the whole run */
    struct R2Hw hw;
    struct R2CpSaved saved;
    ULONG fb_start;         /* GPU address of video RAM's first byte */
    ULONG apSize;           /* CONFIG_APER_SIZE: one aperture */
    ULONG clock_base;
    LONG fences;
};

/* --- struct R2Hw over the probes' MMIO helpers ------------------------------ */

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

/* microseconds since the start (wraps after an hour or so; runs take
 * seconds) */
static ULONG HwClock(APTR hw)
{
    return Clock_Us(Clock_Since(((struct Ctx *)hw)->clock_base));
}

/* --- helpers -------------------------------------------------------------- */

/* Does [off, off + bytes) of VRAM overlap a surface window that swaps or
 * tiles? (Radeon.card's cursor window, SURFACE7 on the A4000D.) */
static BOOL InSurface(struct Ctx *c, ULONG off, ULONG bytes)
{
    ULONG n;
    ULONG lo;
    ULONG hi;
    ULONG inf;

    for (n = 0; n < R_SURFACE_COUNT; n++)
    {
        inf = Mmio_Read(c->card->mmio, R_SURFACE0_INFO + n * R_SURFACE_STRIDE);
        if ((inf & 0x00f70000UL) == 0)
            continue;
        lo = Mmio_Read(c->card->mmio, R_SURFACE0_LOWER_BOUND + n * R_SURFACE_STRIDE);
        hi = Mmio_Read(c->card->mmio, R_SURFACE0_UPPER_BOUND + n * R_SURFACE_STRIDE);
        if (off <= hi && off + bytes - 1 >= lo)
            return TRUE;
    }
    return FALSE;
}

static void LogStats(const struct R2Ring *r)
{
    Log_Printf("  ring: %lu commits, %lu RPTR reads, %lu fence reads, %lu stalls, "
               "%lu wraps, %lu pad dwords\n",
               r->stats.commits, r->stats.rptr_reads, r->stats.fence_reads,
               r->stats.stalls, r->stats.wraps, r->stats.pad_dw);
}

/* One round: start the CP with flags, canary, fences, stop. Returns TRUE
 * when the canary showed the byte order right. */
static BOOL Round(struct Ctx *c, ULONG flags, CONST_STRPTR title)
{
    struct R2Ring ring;
    volatile ULONG *mem;
    ULONG *base;
    ULONG off;
    ULONG gpu;
    ULONG *p;
    ULONG i;
    ULONG t0;
    ULONG dt;
    ULONG rptr;
    ULONG tf;
    ULONG seq;
    ULONG tmin;
    ULONG tmax;
    ULONG tsum;
    ULONG done;
    ULONG reset;
    LONG err;
    BOOL right;

    Log_Section(title);
    right = FALSE;
    mem = (volatile ULONG *)c->board->mem;
    base = (ULONG *)(((ULONG)mem + 4095UL) & ~4095UL);
    /* the video RAM offset: the CPU sees the memory twice (AP0, AP1) */
    off = ((ULONG)base - (ULONG)c->card->aper) % c->apSize;
    gpu = c->fb_start + off;
    Log_Printf("ring: 68k 0x%08lx, VRAM offset 0x%08lx, GPU 0x%08lx, %lu dwords\n",
               (ULONG)base, off, gpu, RING_DW);
    if ((ULONG)base + RING_DW * 4 > (ULONG)mem + BLOCK_BYTES)
    {
        Log_Printf("the ring does not fit the block: round skipped\n");
        return FALSE;
    }
    if (InSurface(c, off, RING_DW * 4))
    {
        Log_Printf("the ring overlaps a swapping surface window: round skipped\n");
        return FALSE;
    }
    for (i = 0; i < RING_DW; i++)
        base[i] = PAL_FILL;
    (void)HwReadBack(c, &base[RING_DW - 1]);
    /* the canary must change PP_TFACTOR_0: start every round from another
     * value (round A leaves the canary value there) */
    HwWrite(c, R_PP_TFACTOR_0, 0);
    if (HwRead(c, R_PP_TFACTOR_0) != 0)
    {
        Log_Printf("PP_TFACTOR_0 does not clear: round skipped\n");
        return FALSE;
    }

    err = R2Cp_Start(&c->hw, gpu, RING_DW, flags);
    Log_Printf("CP started: %ld; CP_RB_CNTL 0x%08lx, CSQ_CNTL 0x%08lx, RPTR %lu, WPTR %lu\n",
               err, HwRead(c, R_CP_RB_CNTL), HwRead(c, R_CP_CSQ_CNTL),
               HwRead(c, R_CP_RB_RPTR), HwRead(c, R_CP_RB_WPTR));
    if (err != R2_OK)
        goto stop;
    err = R2Ring_Init(&ring, base, gpu, RING_DW, &c->hw,
                      (flags & R2CP_GPU_SWAP) ? 0UL : 1UL, FENCE_REG, TIMEOUT_US);
    if (err != R2_OK)
    {
        Log_Printf("R2Ring_Init: %ld\n", err);
        goto stop;
    }
    ring.kick = 0;

    /* the canary: 16 dwords, one write-pointer step, no padding */
    p = R2Cs_Room(&ring.cs, 16);
    if (p == NULL)
    {
        Log_Printf("no ring room: %ld\n", ring.cs.error);
        goto stop;
    }
    p[0] = R2_OUT(&ring.cs, R2_PKT0(R_PP_TFACTOR_0, 0));
    p[1] = R2_OUT(&ring.cs, CANARY_VALUE);
    for (i = 2; i < 16; i++)
        p[i] = PAL_FILL;
    ring.cs.ptr = p + 16;
    t0 = HwClock(c);
    err = R2Ring_Commit(&ring);
    do
    {
        rptr = HwRead(c, R_CP_RB_RPTR);
        dt = HwClock(c) - t0;
    } while (rptr != 16 && dt < TIMEOUT_US);
    tf = HwRead(c, R_PP_TFACTOR_0);
    Log_Printf("canary: commit %ld, RPTR %lu after %lu us; PP_TFACTOR_0 0x%08lx (want 0x%08lx)\n",
               err, rptr, dt, tf, CANARY_VALUE);
    if (rptr != 16)
    {
        Log_Printf("  the CP did NOT consume the ring: it does not fetch from here\n");
        goto stop;
    }
    if (tf != CANARY_VALUE)
    {
        Log_Printf("  consumed, but the write did not happen: this byte order is WRONG\n"
                   "  (the CP read only fillers; nothing else was written)\n");
        goto stop;
    }
    Log_Printf("  executed: this byte order is RIGHT\n");
    right = TRUE;

    /* fences: commit, wait until SCRATCH_REG2 shows the number */
    tmin = 0xffffffffUL;
    tmax = 0;
    tsum = 0;
    done = 0;
    for (i = 0; i < (ULONG)c->fences; i++)
    {
        t0 = HwClock(c);
        err = R2Ring_Fence(&ring, &seq);
        if (err == R2_OK)
            err = R2Ring_WaitFence(&ring, seq);
        dt = HwClock(c) - t0;
        if (err != R2_OK)
        {
            Log_Printf("fence %lu: error %ld after %lu us (SCRATCH_REG2 0x%08lx, RPTR %lu)\n",
                       i, err, dt, HwRead(c, FENCE_REG), HwRead(c, R_CP_RB_RPTR));
            break;
        }
        if (dt < tmin)
            tmin = dt;
        if (dt > tmax)
            tmax = dt;
        tsum += dt;
        done++;
    }
    if (done != 0)
        Log_Printf("fences: %lu of %ld, round trip min %lu us, mean %lu us, max %lu us\n",
                   done, c->fences, tmin, tsum / done, tmax);
    Log_Printf("after: RPTR %lu, WPTR %lu, committed %lu, SCRATCH_REG2 0x%08lx, RBBM_STATUS 0x%08lx\n",
               HwRead(c, R_CP_RB_RPTR), HwRead(c, R_CP_RB_WPTR), ring.committed,
               HwRead(c, FENCE_REG), HwRead(c, R_RBBM_STATUS));
    LogStats(&ring);

stop:
    err = R2Cp_Stop(&c->hw, &c->saved, TIMEOUT_US, &reset);
    Log_Printf("CP stopped: %ld%s; CSQ_CNTL 0x%08lx, RBBM_STATUS 0x%08lx\n", err,
               reset ? (CONST_STRPTR)" (engine stayed busy: the CP was soft-reset)"
                     : (CONST_STRPTR)"",
               HwRead(c, R_CP_CSQ_CNTL), HwRead(c, R_RBBM_STATUS));
    return right;
}

/* Everything that touches the chip, inside one lock of the block. rc as
 * main returns it. */
static int Run(struct Ctx *c, BOOL onlya)
{
    ULONG tf_orig;
    ULONG scr_orig;
    ULONG v;
    ULONG bad;
    ULONG reset;
    LONG err;
    BOOL a;
    BOOL b;

    if (R200_ReportEndian(c->card) == MMIO_UNDECIDED)
    {
        Log_Printf("CPRing: register byte order undecided: the CP is not touched\n");
        return RETURN_FAIL;
    }
    if (!R200_CheckFamily(c->card))
    {
        Log_Printf("CPRing: not an R200-family chip: the R200 microcode is not loaded\n");
        return RETURN_FAIL;
    }

    Log_Section("Before");
    R2Cp_Save(&c->hw, &c->saved);
    Log_Printf("CSQ_CNTL 0x%08lx, CSQ_MODE 0x%08lx, RB_CNTL 0x%08lx, RB_BASE 0x%08lx,\n"
               "RPTR_ADDR 0x%08lx, WPTR_DELAY 0x%08lx, SCRATCH_UMSK 0x%08lx\n",
               c->saved.csq_cntl, c->saved.csq_mode, c->saved.rb_cntl, c->saved.rb_base,
               c->saved.rb_rptr_addr, c->saved.wptr_delay, c->saved.scratch_umsk);
    if (R2Cp_InUse(&c->saved))
    {
        /* Radeon.card never runs the CP: a run that crashed left it on */
        Log_Printf("the CP is RUNNING (CSQ mode %lu): left over from a run that crashed\n",
                   c->saved.csq_cntl >> R_CSQ_MODE_SHIFT);
        err = R2Cp_Reclaim(&c->hw, &c->saved, TIMEOUT_US, &reset);
        Log_Printf("  stopped: %ld%s; CSQ_CNTL 0x%08lx, RBBM_STATUS 0x%08lx\n", err,
                   reset ? (CONST_STRPTR)" (the CP was soft-reset)" : (CONST_STRPTR)"",
                   HwRead(c, R_CP_CSQ_CNTL), HwRead(c, R_RBBM_STATUS));
        if (err != R2_OK)
            return RETURN_FAIL;
    }
    v = HwRead(c, R_MC_FB_LOCATION);
    c->fb_start = (v & 0xffffUL) << 16;
    c->apSize = HwRead(c, R_CONFIG_APER_SIZE);
    Log_Printf("MC_FB_LOCATION 0x%08lx: VRAM at GPU 0x%08lx; CONFIG_APER_SIZE %lu MB\n", v,
               c->fb_start, c->apSize >> 20);
    if (c->apSize == 0 || (c->apSize & (c->apSize - 1)) != 0)
    {
        Log_Printf("CPRing: CONFIG_APER_SIZE is not a power of two: the CP is not started\n");
        return RETURN_FAIL;
    }

    /* the canary needs PP_TFACTOR_0 to read back what was written */
    tf_orig = HwRead(c, R_PP_TFACTOR_0);
    scr_orig = HwRead(c, FENCE_REG);
    HwWrite(c, R_PP_TFACTOR_0, MMIO_TEST);
    v = HwRead(c, R_PP_TFACTOR_0);
    HwWrite(c, R_PP_TFACTOR_0, tf_orig);
    Log_Printf("PP_TFACTOR_0 0x%08lx; written 0x%08lx over MMIO, read 0x%08lx\n",
               tf_orig, MMIO_TEST, v);
    if (v != MMIO_TEST)
    {
        Log_Printf("CPRing: PP_TFACTOR_0 does not read back: no canary, the CP is not started\n");
        return RETURN_FAIL;
    }

    Log_Section("Microcode");
    if (R2Cp_WaitIdle(&c->hw, TIMEOUT_US) != R2_OK)
    {
        Log_Printf("CPRing: the engine is not idle (RBBM_STATUS 0x%08lx): the CP is not started\n",
                   HwRead(c, R_RBBM_STATUS));
        return RETURN_FAIL;
    }
    R2Cp_LoadMicrocode(&c->hw, R2Cp_Microcode);
    bad = R2Cp_CheckMicrocode(&c->hw, R2Cp_Microcode);
    Log_Printf("loaded %lu words; read back through CP_ME_RAM_RADDR: %lu differ (diagnostic)\n",
               (ULONG)R2CP_UCODE_WORDS, bad);

    a = Round(c, 0, (CONST_STRPTR)"Round A: the 68k swaps (BUF_SWAP off)");
    b = FALSE;
    if (!onlya)
        b = Round(c, R2CP_GPU_SWAP, (CONST_STRPTR)"Round B: the CP swaps (BUF_SWAP_32BIT)");

    HwWrite(c, R_PP_TFACTOR_0, tf_orig);
    HwWrite(c, FENCE_REG, scr_orig);
    Log_Section("Result");
    Log_Printf("byte order (a), the 68k swaps: %s\n",
               a ? (CONST_STRPTR)"WORKS" : (CONST_STRPTR)"does not work");
    if (!onlya)
        Log_Printf("byte order (b), the CP swaps:  %s\n",
                   b ? (CONST_STRPTR)"WORKS" : (CONST_STRPTR)"does not work");
    Log_Printf("as found again: CSQ_CNTL 0x%08lx, PP_TFACTOR_0 0x%08lx, SCRATCH_REG2 0x%08lx\n",
               HwRead(c, R_CP_CSQ_CNTL), HwRead(c, R_PP_TFACTOR_0), HwRead(c, FENCE_REG));
    return (a || b) ? RETURN_OK : RETURN_WARN;
}

int main(void)
{
    static struct P96Board board;     /* zero: P96Board_Close is right before Open */
    struct RDArgs *rda;
    struct Args args;
    struct PciCard card;
    struct Ctx c;
    ULONG t0;
    int rc;

    if (((struct Process *) FindTask(NULL))->pr_CLI == 0)
        return RETURN_FAIL;     /* the Workbench: no console to report to */
    args.log = NULL;
    args.fences = NULL;
    args.onlya = 0;
    rda = ReadArgs(TEMPLATE, (LONG *)&args, NULL);
    if (rda == NULL)
    {
        PrintFault(IoErr(), "CPRing");
        return RETURN_ERROR;
    }
    if (!Log_Open(args.log))
        Log_Printf("CPRing: cannot write %s, console only\n", args.log);
    Log_Printf("%s -- the R200 command processor, first start\n", ver + 6);

    rc = RETURN_FAIL;
    c.card = &card;
    c.board = &board;
    c.hw.read_reg = HwRead;
    c.hw.write_reg = HwWrite;
    c.hw.read_back = HwReadBack;
    c.hw.clock_us = HwClock;
    c.hw.hw = &c;
    c.fences = (args.fences && *args.fences > 0) ? *args.fences : 200;
    c.fb_start = 0;
    c.apSize = 0;

    if (!Clock_Open())
    {
        Log_Printf("CPRing: timer.device did not open\n");
        goto out;
    }
    c.clock_base = Clock_Now();
    Log_Section("VRAM block");
    if (!P96Board_Open(&board, BLOCK_BYTES))
        goto out;
    P96Board_Report(&board);
    if (!P96Board_IsRadeon(&board) || board.memIoBase == NULL)
    {
        Log_Printf("CPRing: the Workbench's board is not a Radeon: the CP is not touched\n");
        goto out;
    }
    R200_CardFromP96(&card, &board);
    if (!P96Board_Lock(&board))
    {
        Log_Printf("CPRing: the block is not on the board: the CP is not touched\n");
        goto out;
    }
    t0 = Clock_Now();
    rc = Run(&c, (BOOL)(args.onlya != 0));
    t0 = Clock_Us(Clock_Since(t0));
    P96Board_Unlock(&board);
    Log_Printf("\n(the board was locked for %lu ms)\n", t0 / 1000);

out:
    P96Board_Close(&board);
    Clock_Close();
    Log_Printf("\nCPRing: done (rc %ld)\n", (LONG)rc);
    Log_Close();
    FreeArgs(rda);
    return rc;
}
