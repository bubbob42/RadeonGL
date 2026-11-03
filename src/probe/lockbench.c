/*
 * LockBench -- what a Picasso96 bitmap lock does, measured.
 *
 * The measurements (2026-11-02) and the other reading of the Picasso96API disagree about the one thing the back end's design
 * hangs on (LockBench):
 *
 *   one:   while a bitmap of the board is locked, P96 keeps off the chip,
 *            so Intuition cannot render and the GUI waits;
 *   other: a lock a lock does not stop rendering; it blocks only reorganisations of video RAM.
 *
 * LockBench answers it on the running system:
 *
 *  1. The board as P96 describes it: names, video RAM, the screen's format,
 *     and the three bases from p96GetBitMapAttr inside a lock. On a Radeon,
 *     inside the lock: the register byte order, the apertures, which
 *     aperture (and byte swap) the probe's block and the screen bitmap sit
 *     in, and the PCI configuration mirror. With openpci.library present,
 *     its BARs beside P96's bases (a cross-check; the card is not claimed).
 *  2. The cost of a lock: PAIRS lock + unlock pairs of the block and of the
 *     screen bitmap; the task priority inside a lock; whether the address
 *     moved between locks.
 *  3. Does a lock stop other rendering? Two child processes fill, without
 *     pause, one a rectangle in a small window on Workbench, the other a
 *     second off-screen bitmap, alternating two colours (p96RectFill), and
 *     time every fill (one child per target: a child stuck in one fill
 *     cannot speak for the other target). Meanwhile the parent holds a lock for HOLD ms in three
 *     rounds: on its block, on the Workbench screen's bitmap, on its block
 *     again. For each round: the fills that completed during the lock, the
 *     longest fill, and fills that spanned the lock.
 *     On a Radeon the parent also samples the 2D engine's registers
 *     (DP_BRUSH_FRGD_CLR and others, read only) when the lock starts and
 *     just before it ends: unchanged inside every lock while the samples
 *     BETWEEN locks (end of one, start of the next) differ means P96 kept
 *     off the 2D engine while locked, and rendered while unlocked.
 *
 * Every register access happens inside a lock; nothing is written to the
 * card. While the parent holds a lock it prints nothing (plog holds the
 * console's copy) and calls no graphics function.
 *
 *   LockBench LOG/K,HOLD/N,PAIRS/N,NOCHILD/S
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <utility/tagitem.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/rastport.h>
#include <libraries/Picasso96.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/Picasso96.h>

#include "p96board.h"
#include "pcicard.h"
#include "mmio.h"
#include "r200regs.h"
#include "eclock.h"
#include "plog.h"

static const char ver[] = "$VER: LockBench 0.2 (02.11.2026)";

#define TEMPLATE "LOG/K,HOLD/N,PAIRS/N,NOCHILD/S"

struct Args
{
    STRPTR log;
    LONG *hold;
    LONG *pairs;
    LONG nochild;
};

/* A process's entry point under SAS/C's small data model must load A4 */
#if defined(__SASC)
# define CHILD_QUAL __saveds
#else
# define CHILD_QUAL
#endif

#define BLOCK_BYTES  (64UL * 1024)
#define ROUNDS       3
#define TARGETS      2          /* 0: the window, 1: the off-screen bitmap */
#define PHASES       3          /* 0: before the lock, 1: locked, 2: after */
#define FILL_W       160
#define FILL_H       60
#define OFF_W        64
#define OFF_H        64
#define NREGS        8

static CONST_STRPTR round_Names[ROUNDS] = {
    (CONST_STRPTR)"lock on the probe's own block",
    (CONST_STRPTR)"lock on the Workbench screen's bitmap",
    (CONST_STRPTR)"lock on the probe's own block, again"
};
static CONST_STRPTR target_Names[TARGETS] = {
    (CONST_STRPTR)"window on Workbench",
    (CONST_STRPTR)"off-screen bitmap"
};
static CONST_STRPTR phase_Names[PHASES] = {
    (CONST_STRPTR)"before", (CONST_STRPTR)"LOCKED", (CONST_STRPTR)"after"
};

static const ULONG reg_Offs[NREGS] = {
    R_DP_BRUSH_FRGD_CLR, R_DP_SRC_FRGD_CLR, R_DP_GUI_MASTER_CNTL, R_DST_PITCH_OFFSET,
    R_DST_Y_X, R_DP_DATATYPE, R_DP_CNTL, R_DP_WRITE_MASK
};
static CONST_STRPTR reg_Names[NREGS] = {
    (CONST_STRPTR)"DP_BRUSH_FRGD_CLR", (CONST_STRPTR)"DP_SRC_FRGD_CLR",
    (CONST_STRPTR)"DP_GUI_MASTER_CNTL", (CONST_STRPTR)"DST_PITCH_OFFSET",
    (CONST_STRPTR)"DST_Y_X", (CONST_STRPTR)"DP_DATATYPE", (CONST_STRPTR)"DP_CNTL",
    (CONST_STRPTR)"DP_WRITE_MASK"
};

struct Stat
{
    ULONG n;                    /* fills that completed in this phase */
    ULONG maxTicks;             /* the longest of them */
    ULONG spans;                /* of them, fills that started in another phase */
    ULONG maxSpan;
};

/* Parent and children share this. */
struct Shared
{
    struct RastPort *winrp;
    WORD x0;
    WORD y0;
    struct RastPort rp2;        /* over bm2 */
    volatile ULONG stop;
    volatile ULONG phase;
    volatile ULONG round;       /* ROUNDS: no longer counted */
    ULONG fills[TARGETS];
    struct Stat st[ROUNDS][TARGETS][PHASES];
};

/* A child's start-up message: which target it fills. */
struct Start
{
    struct Message msg;
    struct Shared *sh;
    ULONG target;
};

/* --- the child -------------------------------------------------------------- */

static void Fill(struct Shared *sh, ULONG target, ULONG k)
{
    if (target == 0)
        p96RectFill(sh->winrp, (UWORD)sh->x0, (UWORD)sh->y0, (UWORD)(sh->x0 + FILL_W - 1),
                    (UWORD)(sh->y0 + FILL_H - 1), (k & 1) ? 0x00ff2020UL : 0x002020ffUL);
    else
        p96RectFill(&sh->rp2, 0, 0, OFF_W - 1, OFF_H - 1, (k & 1) ? 0x0020ff20UL : 0x00ffff20UL);
}

static void CHILD_QUAL ChildMain(void)
{
    struct Process *me;
    struct Start *start;
    struct Shared *sh;
    struct Stat *s;
    ULONG k;
    ULONG t;
    ULONG r;
    ULONG p0;
    ULONG p1;
    ULONG t0;
    ULONG d;

    me = (struct Process *)FindTask(NULL);
    WaitPort(&me->pr_MsgPort);
    start = (struct Start *)GetMsg(&me->pr_MsgPort);
    sh = start->sh;
    t = start->target;
    for (k = 0; !sh->stop; k++)
    {
        r = sh->round;
        p0 = sh->phase;
        t0 = Clock_Now();
        Fill(sh, t, k);
        d = Clock_Since(t0);
        p1 = sh->phase;
        sh->fills[t]++;
        if (r >= ROUNDS || sh->round != r)
            continue;
        s = &sh->st[r][t][p1];
        s->n++;
        if (d > s->maxTicks)
            s->maxTicks = d;
        if (p0 != p1)
        {
            s->spans++;
            if (d > s->maxSpan)
                s->maxSpan = d;
        }
    }
    /* the parent may unload this code once it has the reply: no switch
     * back here before this task is gone */
    Forbid();
    ReplyMsg(&start->msg);
}

/* --- the parent --------------------------------------------------------------- */

struct Radeon
{
    BOOL ok;
    ULONG how;                   /* MMIO_* */
    ULONG memsize;
    ULONG aper0;
    ULONG aper1;
    ULONG aperSize;
    ULONG fbLoc;
    ULONG surfCntl;
    ULONG pci[3];
    ULONG rbbm;
};

static struct P96Board board;
static struct Radeon radeon;
static ULONG regs[ROUNDS][2][NREGS];

static void SampleRegs(ULONG *out)
{
    ULONG i;

    for (i = 0; i < NREGS; i++)
        out[i] = Mmio_Read(board.memIoBase, reg_Offs[i]);
}

/* Which aperture an address of the board's memory is in, and how the CPU
 * sees it there (SURFACE_CNTL's swap bits for that aperture and size). */
static void Where(CONST_STRPTR what, APTR addr, ULONG bpp)
{
    ULONG off;
    ULONG ap;
    ULONG swp;

    if (!radeon.ok || radeon.aperSize == 0)
        return;
    off = (ULONG)addr - (ULONG)board.memBase;
    ap = off / radeon.aperSize;
    off %= radeon.aperSize;
    if (ap == 0)
        swp = (bpp == 2) ? (radeon.surfCntl & R_NONSURF_AP0_SWP_16BPP)
                         : (radeon.surfCntl & R_NONSURF_AP0_SWP_32BPP);
    else
        swp = (bpp == 2) ? (radeon.surfCntl & R_NONSURF_AP1_SWP_16BPP)
                         : (radeon.surfCntl & R_NONSURF_AP1_SWP_32BPP);
    Log_Printf("  %s: 68k 0x%08lx = aperture %lu, VRAM offset 0x%08lx, GPU 0x%08lx; "
               "%lu bytes a pixel there: %s\n",
               what, (ULONG)addr, ap, off, ((radeon.fbLoc & 0xffffUL) << 16) + off, bpp,
               swp ? (CONST_STRPTR)"the CPU's view is BYTE-SWAPPED"
                   : (CONST_STRPTR)"the CPU's view is as stored (no swap)");
}

/* The Radeon's side, inside a lock of the block (read only). */
static void ReadRadeon(ULONG bar0pci)
{
    radeon.how = Mmio_DetectSwap(board.memIoBase, bar0pci, &radeon.memsize);
    radeon.aper0 = Mmio_Read(board.memIoBase, R_CONFIG_APER_0_BASE);
    radeon.aper1 = Mmio_Read(board.memIoBase, R_CONFIG_APER_1_BASE);
    radeon.aperSize = Mmio_Read(board.memIoBase, R_CONFIG_APER_SIZE);
    radeon.fbLoc = Mmio_Read(board.memIoBase, R_MC_FB_LOCATION);
    radeon.surfCntl = Mmio_Read(board.memIoBase, R_SURFACE_CNTL);
    radeon.pci[0] = Mmio_Read(board.memIoBase, R_PCI_MIRROR);
    radeon.pci[1] = Mmio_Read(board.memIoBase, R_PCI_MIRROR + 4);
    radeon.pci[2] = Mmio_Read(board.memIoBase, R_PCI_MIRROR + 8);
    radeon.rbbm = Mmio_Read(board.memIoBase, R_RBBM_STATUS);
    radeon.ok = TRUE;
}

static void ReportRadeon(const struct PciCard *card, BOOL havePci, APTR scrMem, ULONG scrBpp)
{
    Log_Section("The Radeon, read inside a lock");
    Log_Printf("register byte order: %s (%s)\n",
               radeon.how == MMIO_UNDECIDED ? (CONST_STRPTR)"UNDECIDED"
               : Mmio_Swap ? (CONST_STRPTR)"little-endian, the 68k swaps"
                           : (CONST_STRPTR)"right as read (no swap)",
               radeon.how == MMIO_BY_PCIMIRROR ? (CONST_STRPTR)"by the PCI mirror's vendor ID"
               : radeon.how == MMIO_BY_MEMSIZE ? (CONST_STRPTR)"by CONFIG_MEMSIZE"
               : radeon.how == MMIO_BY_APER  ? (CONST_STRPTR)"by CONFIG_APER_0_BASE against BAR0"
               : radeon.how == MMIO_BY_APERSIZE ? (CONST_STRPTR)"by CONFIG_APER_SIZE"
                                                : (CONST_STRPTR)"no test decided");
    Log_Printf("CONFIG_APER_0_BASE 0x%08lx, APER_1_BASE 0x%08lx, APER_SIZE %lu MB; "
               "MC_FB_LOCATION 0x%08lx; SURFACE_CNTL 0x%08lx; RBBM_STATUS 0x%08lx\n",
               radeon.aper0, radeon.aper1, radeon.aperSize >> 20, radeon.fbLoc,
               radeon.surfCntl, radeon.rbbm);
    Log_Printf("register 0x0f00-0x0f08 (PCI configuration mirror?): 0x%08lx 0x%08lx 0x%08lx\n",
               radeon.pci[0], radeon.pci[1], radeon.pci[2]);
    if (havePci)
        Log_Printf("  openpci says vendor 0x%04lx device 0x%04lx rev 0x%02lx: the mirror %s\n",
                   card->vendor, card->device, card->revision,
                   radeon.pci[0] == ((card->device << 16) | card->vendor)
                       ? (CONST_STRPTR)"MATCHES" : (CONST_STRPTR)"does not match");
    Log_Printf("where the memory is:\n");
    Where((CONST_STRPTR)"the block", board.lastMem, board.bpp);
    Where((CONST_STRPTR)"the screen", scrMem, scrBpp);
}

static void ReportBases(const struct PciCard *card)
{
    Log_Section("P96's bases against openpci's BARs (cross-check)");
    Log_Printf("BOARDMEMBASE   0x%08lx   BAR0 (frame buffer) 0x%08lx, %lu MB: %s\n",
               (ULONG)board.memBase, (ULONG)card->aper, card->aperSize >> 20,
               board.memBase == card->aper ? (CONST_STRPTR)"same" : (CONST_STRPTR)"DIFFERENT");
    Log_Printf("BOARDIOBASE    0x%08lx   BAR1 (I/O)          0x%08lx: %s\n",
               (ULONG)board.ioBase, (ULONG)card->bar1,
               board.ioBase == card->bar1 ? (CONST_STRPTR)"same" : (CONST_STRPTR)"DIFFERENT");
    Log_Printf("BOARDMEMIOBASE 0x%08lx   BAR2 (registers)    0x%08lx: %s\n",
               (ULONG)board.memIoBase, (ULONG)card->mmio,
               board.memIoBase == card->mmio ? (CONST_STRPTR)"same" : (CONST_STRPTR)"DIFFERENT");
}

/* PAIRS lock + unlock pairs of bm; microseconds per pair, *moved: how
 * often the address changed. */
static ULONG LockCost(struct BitMap *bm, ULONG pairs, ULONG *moved, LONG *pri)
{
    struct RenderInfo ri;
    APTR last;
    LONG lock;
    ULONG t0;
    ULONG us;
    ULONG i;

    *moved = 0;
    *pri = FindTask(NULL)->tc_Node.ln_Pri;   /* also when the first lock fails */
    last = NULL;
    t0 = Clock_Now();
    for (i = 0; i < pairs; i++)
    {
        lock = p96LockBitMap(bm, (UBYTE *)&ri, sizeof(ri));
        if (lock == 0)
            break;
        if (i == 0)
            *pri = FindTask(NULL)->tc_Node.ln_Pri;  /* inside the first lock */
        else if (ri.Memory != last)
            (*moved)++;
        last = ri.Memory;
        p96UnlockBitMap(bm, lock);
    }
    us = Clock_Us(Clock_Since(t0));
    if (i != pairs)
        Log_Printf("  p96LockBitMap returned 0 at pair %lu\n", i);
    return (i == 0) ? 0 : (us * 10 / i);    /* tenths of a microsecond */
}

static void ReportStats(struct Shared *sh, const ULONG *held)
{
    struct Stat *s;
    ULONG r;
    ULONG t;
    ULONG p;
    ULONG i;
    ULONG same;
    ULONG diff;

    Log_Section("Rendering while a lock is held");
    Log_Printf("the children did %lu fills in the window, %lu in the off-screen bitmap\n",
               sh->fills[0], sh->fills[1]);
    for (r = 0; r < ROUNDS; r++)
    {
        Log_Printf("round %lu, %s, held %lu ms:\n", r + 1, round_Names[r], held[r] / 1000);
        for (t = 0; t < TARGETS; t++)
            for (p = 0; p < PHASES; p++)
            {
                s = &sh->st[r][t][p];
                Log_Printf("  %-19s %-6s %6lu fills done, longest %7lu us",
                           target_Names[t], phase_Names[p], s->n, Clock_Us(s->maxTicks));
                if (s->spans != 0)
                    Log_Printf(", %lu began earlier (longest %lu us)", s->spans,
                               Clock_Us(s->maxSpan));
                Log_Printf("\n");
            }
        for (t = 0; t < TARGETS; t++)
        {
            s = &sh->st[r][t][1];
            if (s->n > 1)
                Log_Printf("  => %s: rendering WENT ON during the lock\n", target_Names[t]);
            else if (Clock_Us(sh->st[r][t][2].maxSpan) * 2 > held[r])
                Log_Printf("  => %s: rendering WAITED for the unlock\n", target_Names[t]);
            else
                Log_Printf("  => %s: no fill during the lock and none waited (its child starved?)\n",
                           target_Names[t]);
        }
    }
    if (!radeon.ok)
        return;
    Log_Section("The 2D engine's registers, sampled inside each lock");
    for (r = 0; r < ROUNDS; r++)
    {
        same = 0;
        for (i = 0; i < NREGS; i++)
        {
            Log_Printf("  round %lu %-19s start 0x%08lx end 0x%08lx%s\n", r + 1, reg_Names[i],
                       regs[r][0][i], regs[r][1][i],
                       regs[r][0][i] != regs[r][1][i] ? (CONST_STRPTR)"  CHANGED while locked"
                                                      : (CONST_STRPTR)"");
            if (regs[r][0][i] == regs[r][1][i])
                same++;
        }
        Log_Printf("  round %lu: %lu of %lu registers unchanged inside the lock\n", r + 1, same,
                   (ULONG)NREGS);
    }
    for (r = 0; r + 1 < ROUNDS; r++)
    {
        diff = 0;
        for (i = 0; i < NREGS; i++)
            if (regs[r][1][i] != regs[r + 1][0][i])
                diff++;
        Log_Printf("  between round %lu and %lu (unlocked, the child rendering): %lu registers "
                   "changed%s\n", r + 1, r + 2, diff,
                   diff == 0 ? (CONST_STRPTR)" -- the samples cannot tell (registers not readable?)"
                             : (CONST_STRPTR)"");
    }
}

/* The concurrency rounds. FALSE when the child or its targets could not be
 * set up. */
static BOOL Rounds(ULONG holdMs)
{
    struct TagItem wt[12];
    struct TagItem pt[5];
    struct Window *win;
    struct BitMap *bm2;
    struct MsgPort *reply;
    struct Process *child;
    struct Shared *sh;
    struct Start *start;
    struct RenderInfo ri;
    ULONG held[ROUNDS];
    ULONG started;
    ULONG t;
    ULONG r;
    ULONG t0;
    LONG lock;
    BOOL ok;

    ok = FALSE;
    started = 0;
    sh = (struct Shared *)AllocVec(sizeof(*sh), MEMF_PUBLIC | MEMF_CLEAR);
    start = (struct Start *)AllocVec(TARGETS * sizeof(*start), MEMF_PUBLIC | MEMF_CLEAR);
    if (sh == NULL || start == NULL)
    {
        FreeVec(sh);
        FreeVec(start);
        return FALSE;
    }
    reply = CreateMsgPort();
    bm2 = AllocBitMap(OFF_W, OFF_H, GetBitMapAttr(board.scrbm, BMA_DEPTH), BMF_MINPLANES,
                      board.scrbm);
    wt[0].ti_Tag = WA_PubScreen;     wt[0].ti_Data = (ULONG)board.scr;
    wt[1].ti_Tag = WA_Left;          wt[1].ti_Data = 40;
    wt[2].ti_Tag = WA_Top;           wt[2].ti_Data = 40;
    wt[3].ti_Tag = WA_InnerWidth;    wt[3].ti_Data = FILL_W;
    wt[4].ti_Tag = WA_InnerHeight;   wt[4].ti_Data = FILL_H;
    wt[5].ti_Tag = WA_Title;         wt[5].ti_Data = (ULONG)"LockBench";
    wt[6].ti_Tag = WA_DragBar;       wt[6].ti_Data = TRUE;
    wt[7].ti_Tag = WA_DepthGadget;   wt[7].ti_Data = TRUE;
    wt[8].ti_Tag = WA_Activate;      wt[8].ti_Data = FALSE;
    wt[9].ti_Tag = WA_SmartRefresh;  wt[9].ti_Data = TRUE;
    wt[10].ti_Tag = WA_NoCareRefresh; wt[10].ti_Data = TRUE;
    wt[11].ti_Tag = TAG_DONE;        wt[11].ti_Data = 0;
    win = OpenWindowTagList(NULL, wt);
    if (reply == NULL || bm2 == NULL || win == NULL)
    {
        Log_Printf("no reply port, second bitmap or window: no rendering test\n");
        goto out;
    }
    lock = P96Board_LockBitMap(bm2, &ri);
    if (lock != 0)
    {
        r = p96GetBitMapAttr(bm2, P96BMA_ISONBOARD);
        P96Board_UnlockBitMap(bm2, lock);
        Log_Printf("second bitmap %lu x %lu: %s\n", (ULONG)OFF_W, (ULONG)OFF_H,
                   r ? (CONST_STRPTR)"on the board"
                     : (CONST_STRPTR)"NOT on the board (fills are CPU work)");
    }
    sh->winrp = win->RPort;
    sh->x0 = win->BorderLeft;
    sh->y0 = win->BorderTop;
    InitRastPort(&sh->rp2);
    sh->rp2.BitMap = bm2;

    pt[0].ti_Tag = NP_Entry;      pt[0].ti_Data = (ULONG)ChildMain;
    pt[1].ti_Tag = NP_Name;       pt[1].ti_Data = (ULONG)"LockBench child";
    pt[2].ti_Tag = NP_StackSize;  pt[2].ti_Data = 8192;
    pt[3].ti_Tag = NP_Priority;   pt[3].ti_Data = (ULONG)(LONG)FindTask(NULL)->tc_Node.ln_Pri;
    pt[4].ti_Tag = TAG_DONE;      pt[4].ti_Data = 0;
    for (t = 0; t < TARGETS; t++)
    {
        child = CreateNewProc(pt);
        if (child == NULL)
            break;
        start[t].msg.mn_ReplyPort = reply;
        start[t].msg.mn_Length = sizeof(start[t]);
        start[t].sh = sh;
        start[t].target = t;
        PutMsg(&child->pr_MsgPort, &start[t].msg);
        started++;
    }
    if (started != TARGETS)
    {
        Log_Printf("a child process did not start: no rendering test\n");
        sh->round = ROUNDS;
        sh->stop = 1;
        for (t = 0; t < started; t++)
        {
            WaitPort(reply);
            (void)GetMsg(reply);
        }
        goto out;
    }

    for (r = 0; r < ROUNDS; r++)
    {
        sh->phase = 0;
        sh->round = r;
        Delay(15);                                  /* 0.3 s unlocked */
        if (r == 1)
            lock = P96Board_LockBitMap(board.scrbm, &ri);
        else
        {
            lock = P96Board_Lock(&board) ? 1 : 0;
        }
        if (lock == 0)
        {
            Log_Printf("round %lu: the lock failed\n", r + 1);
            held[r] = 0;
            continue;
        }
        sh->phase = 1;
        t0 = Clock_Now();
        if (radeon.ok)
            SampleRegs(regs[r][0]);
        Delay((holdMs + 19) / 20);
        if (radeon.ok)
            SampleRegs(regs[r][1]);
        held[r] = Clock_Us(Clock_Since(t0));
        sh->phase = 2;
        if (r == 1)
            P96Board_UnlockBitMap(board.scrbm, lock);
        else
            P96Board_Unlock(&board);
        Delay(15);
    }
    sh->round = ROUNDS;
    sh->stop = 1;
    for (t = 0; t < TARGETS; t++)
    {
        WaitPort(reply);
        (void)GetMsg(reply);
    }
    ReportStats(sh, held);
    ok = TRUE;

out:
    if (win != NULL)
        CloseWindow(win);
    if (bm2 != NULL)
    {
        WaitBlit();
        FreeBitMap(bm2);
    }
    if (reply != NULL)
        DeleteMsgPort(reply);
    FreeVec(start);
    FreeVec(sh);
    return ok;
}

int main(void)
{
    static struct PciCard card;       /* zero unless openpci fills it */
    struct RDArgs *rda;
    struct Args args;
    struct RenderInfo ri;
    APTR scrMem;
    ULONG scrBpp;
    ULONG pairs;
    ULONG holdMs;
    ULONG tenths;
    ULONG moved;
    LONG pri;
    LONG lock;
    BOOL havePci;
    int rc;

    if (((struct Process *) FindTask(NULL))->pr_CLI == 0)
        return RETURN_FAIL;     /* the Workbench: no console to report to */
    args.log = NULL;
    args.hold = NULL;
    args.pairs = NULL;
    args.nochild = 0;
    rda = ReadArgs(TEMPLATE, (LONG *)&args, NULL);
    if (rda == NULL)
    {
        PrintFault(IoErr(), "LockBench");
        return RETURN_ERROR;
    }
    if (!Log_Open(args.log))
        Log_Printf("LockBench: cannot write %s, console only\n", args.log);
    Log_Printf("%s -- what a Picasso96 bitmap lock does\n", ver + 6);
    holdMs = (args.hold && *args.hold > 0) ? (ULONG)*args.hold : 500UL;
    pairs = (args.pairs && *args.pairs > 0) ? (ULONG)*args.pairs : 1000UL;

    rc = RETURN_FAIL;
    havePci = FALSE;
    if (!Clock_Open())
    {
        Log_Printf("LockBench: timer.device did not open\n");
        goto out;
    }
    if (!P96Board_Open(&board, BLOCK_BYTES))
        goto out;
    scrMem = NULL;
    scrBpp = p96GetBitMapAttr(board.scrbm, P96BMA_BYTESPERPIXEL);

    /* openpci only as a cross-check of P96's bases: read-only, never claimed */
    if (P96Board_IsRadeon(&board))
        havePci = PciCard_Open(&card);

    /* the screen bitmap's address, and on a Radeon the chip's side */
    lock = P96Board_LockBitMap(board.scrbm, &ri);
    if (lock != 0)
    {
        scrMem = ri.Memory;
        P96Board_UnlockBitMap(board.scrbm, lock);
    }
    if (P96Board_IsRadeon(&board) && board.memIoBase != NULL && P96Board_Lock(&board))
    {
        ReadRadeon(havePci ? (card.cfgBar[0] & 0xfffffff0UL) : 0UL);
        P96Board_Unlock(&board);
    }
    P96Board_Report(&board);
    Log_Printf("screen bitmap at 0x%08lx (one lock), %lu bytes a pixel\n", (ULONG)scrMem, scrBpp);
    if (havePci)
        ReportBases(&card);
    if (radeon.ok)
        ReportRadeon(&card, havePci, scrMem, scrBpp);
    else
        Log_Printf("(not a Radeon, or no register base: no register reads)\n");

    Log_Section("The cost of a lock");
    tenths = LockCost(board.bm, pairs, &moved, &pri);
    Log_Printf("block:  %lu lock + unlock pairs, %lu.%lu us a pair, address moved %lu times; "
               "task priority inside the lock %ld (outside %ld)\n",
               pairs, tenths / 10, tenths % 10, moved, pri,
               (LONG)FindTask(NULL)->tc_Node.ln_Pri);
    tenths = LockCost(board.scrbm, pairs, &moved, &pri);
    Log_Printf("screen: %lu lock + unlock pairs, %lu.%lu us a pair, address moved %lu times; "
               "task priority inside the lock %ld\n",
               pairs, tenths / 10, tenths % 10, moved, pri);

    rc = RETURN_OK;
    if (!args.nochild)
    {
        Log_Printf("\nA small window opens on Workbench for a few seconds: rendering is timed.\n");
        if (!Rounds(holdMs))
            rc = RETURN_WARN;
    }

out:
    if (havePci)
        PciCard_Close(&card);
    P96Board_Close(&board);
    Clock_Close();
    Log_Printf("\nLockBench: done (rc %ld)\n", (LONG)rc);
    Log_Close();
    FreeArgs(rda);
    return rc;
}
