/*
 * Move16Bench -- do MOVE16 line bursts reach the Radeon's video RAM through
 * the Mediator, and are they faster than the longword stores RadeonGL uses?
 * (docs/library-asm-research.md, "MOVE16 into video RAM: an untested
 * hardware idea"). 68060 only; GCC only (MOVE16 needs inline assembler).
 *
 * WARNING: A HARDWARE RISK. If the CPU card's bus logic, Buster or the Mediator
 * neither pass a burst through nor make the CPU break it into single
 * cycles, the bus hangs and the machine needs a reset. Run it only with
 * someone at the machine. Every risky step writes the log to disk first
 * (LOG=), so after a reset the log ends at the step that hung.
 *
 * The stages, each on a block of video RAM (a friend bitmap of the
 * Workbench screen, locked for each step only, as BusBench does):
 *   1  one 16-byte line, read back
 *   2  1 KB (64 lines), read back
 *   3  the data cache: MOVE16 straight after the CPU wrote the source
 *      (dirty in the copyback cache) -- does it read the cache or stale
 *      memory? -- and the same after CacheClearE (the push RadeonGL would
 *      otherwise need)
 *   4  timed, best of three, KB= kilobytes: longword stores (RadeonGL's
 *      CopyUCache/CopyCommands today), MOVE16, CopyMemQuick (movem); a
 *      read-back of both ends after the MOVE16 run
 * STEP waits for Return before each stage (Ctrl-C stops).
 *
 *   Move16Bench LOG/K,KB/N,STEP/S,FORCE/S
 *
 * FORCE runs on a 68040 too (some 68040 mask sets have a MOVE16 erratum;
 * not for the A4000D, which has a 68060).
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "p96board.h"
#include "eclock.h"
#include "plog.h"

static const char ver[] = "$VER: Move16Bench 0.1 (07.11.2026)";

#define TEMPLATE "LOG/K,KB/N,STEP/S,FORCE/S"

struct Args
{
    STRPTR log;
    LONG *kb;
    LONG step;
    LONG force;
};

#define REPS 3

extern struct ExecBase *SysBase;

struct Bench
{
    struct P96Board *board;
    ULONG bytes;                 /* a multiple of 16 */
    ULONG longs;
    ULONG *ram;                  /* 16-byte aligned, bytes */
    volatile ULONG *vram;        /* 16-byte aligned, inside the lock of the current run */
};

typedef void (*BenchFn)(struct Bench *);

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

/* the block's first 16-byte aligned longword, inside a lock */
static volatile ULONG *Block(struct Bench *b)
{
    return (volatile ULONG *)(((ULONG)b->board->mem + 15UL) & ~15UL);
}

static void Fill(ULONG *p, ULONG longs, ULONG seed)
{
    ULONG i;

    for (i = 0; i < longs; i++)
        p[i] = seed ^ (i * 0x9E3779B1UL);
}

/* how many of the first longs of the block differ from the source */
static ULONG Compare(struct Bench *b, ULONG first, ULONG longs)
{
    volatile ULONG *v;
    ULONG i;
    ULONG bad;

    if (!P96Board_Lock(b->board))
        return 0xffffffffUL;
    v = Block(b);
    bad = 0;
    for (i = first; i < first + longs; i++)
        if (v[i] != b->ram[i])
            bad++;
    P96Board_Unlock(b->board);
    return bad;
}

/* Return to go on, Ctrl-C to stop */
static BOOL Step(BOOL step, CONST_STRPTR what)
{
    char line[16];

    Log_Printf("\n=== %s\n", what);
    Log_Flush();
    if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
        return FALSE;
    if (!step)
        return TRUE;
    Printf("    press Return to run it (Ctrl-C and Return to stop) ");
    Flush(Output());
    if (FGets(Input(), (STRPTR)line, sizeof(line)) == NULL)
        return FALSE;
    return (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) ? FALSE : TRUE;
}

/* One MOVE16 run of lines from the start of ram into the block, in a lock
 * of its own; push: CacheClearE over the source first. */
static BOOL Move16Once(struct Bench *b, ULONG lines, BOOL push)
{
    if (push)
        CacheClearE(b->ram, lines * 16, CACRF_ClearD);
    if (!P96Board_Lock(b->board))
        return FALSE;
    Move16Lines(Block(b), b->ram, lines);
    P96Board_Unlock(b->board);
    return TRUE;
}

static void T_Longs(struct Bench *b)
{
    volatile ULONG *d;
    const ULONG *s;
    ULONG i;

    d = b->vram;
    s = b->ram;
    for (i = b->longs; i > 0; i--)
        *d++ = *s++;
}

static void T_Move16(struct Bench *b)
{
    Move16Lines(b->vram, b->ram, b->bytes / 16);
}

static void T_CopyMemQuick(struct Bench *b)
{
    CopyMemQuick(b->ram, (APTR)b->vram, b->bytes);
}

/* best of REPS, microseconds; each run in a lock of its own; 0: no lock */
static ULONG RunTimed(struct Bench *b, BenchFn fn)
{
    ULONG best;
    ULONG t;
    ULONG us;
    int r;

    best = 0xffffffffUL;
    for (r = 0; r < REPS; r++)
    {
        CacheClearE(b->ram, b->bytes, CACRF_ClearD);
        if (!P96Board_Lock(b->board))
            return 0;
        b->vram = Block(b);
        t = Clock_Now();
        fn(b);
        us = Clock_Us(Clock_Since(t));
        P96Board_Unlock(b->board);
        b->vram = NULL;
        if (us < best)
            best = us;
    }
    return best;
}

static ULONG ReportRate(CONST_STRPTR what, ULONG bytes, ULONG us)
{
    ULONG kbps;

    if (us == 0)
    {
        Log_Printf("  %-36s skipped (no lock)\n", what);
        return 0;
    }
    kbps = Clock_KBps(bytes, us);
    Log_Printf("  %-36s %7lu us  %6lu KB/s (%lu.%lu MB/s)\n", what, us, kbps,
               kbps / 1024, (kbps % 1024) * 10 / 1024);
    return kbps;
}

int main(void)
{
    static struct P96Board board;     /* zero: P96Board_Close is right before Open */
    struct RDArgs *rda;
    struct Args args;
    struct Bench b;
    APTR mem;
    LONG kb;
    ULONG bad;
    ULONG us;
    ULONG kLongs;
    ULONG kMove;
    int rc;

    if (((struct Process *) FindTask(NULL))->pr_CLI == 0)
        return RETURN_FAIL;     /* the Workbench: no console to report to */
    args.log = NULL;
    args.kb = NULL;
    args.step = 0;
    args.force = 0;
    rda = ReadArgs(TEMPLATE, (LONG *)&args, NULL);
    if (rda == NULL)
    {
        PrintFault(IoErr(), "Move16Bench");
        return RETURN_ERROR;
    }
    kb = args.kb ? *args.kb : 1024;
    if (kb < 4)
        kb = 4;
    if (kb > 4096)
        kb = 4096;

    if (!Log_Open(args.log))
        Log_Printf("Move16Bench: cannot write %s, console only\n", args.log);
    Log_Printf("%s -- MOVE16 into the Radeon's video RAM, %ld KB, best of %ld\n",
               ver + 6, kb, (LONG)REPS);

    rc = RETURN_FAIL;
    mem = NULL;
    b.board = &board;
    b.bytes = (ULONG)kb << 10;
    b.longs = b.bytes >> 2;
    b.ram = NULL;
    b.vram = NULL;

    if (!(SysBase->AttnFlags & AFF_68060))
    {
        if (!(SysBase->AttnFlags & AFF_68040))
        {
            Log_Printf("Move16Bench: no 68040 or 68060, no MOVE16\n");
            goto out;
        }
        if (!args.force)
        {
            Log_Printf("Move16Bench: a 68040 -- FORCE to run anyway (some mask sets have a MOVE16 erratum)\n");
            goto out;
        }
    }
    if (!Clock_Open())
    {
        Log_Printf("Move16Bench: timer.device did not open\n");
        goto out;
    }
    if (!P96Board_Open(&board, b.bytes + 64))
        goto out;
    P96Board_Report(&board);
    if (!P96Board_IsRadeon(&board))
        Log_Printf("(not a Radeon: the bus question is about the Radeon behind the Mediator)\n");

    mem = AllocVec(b.bytes + 16, MEMF_FAST | MEMF_PUBLIC);
    if (mem == NULL)
    {
        Log_Printf("Move16Bench: no fast RAM for %ld KB\n", kb);
        goto out;
    }
    b.ram = (ULONG *)(((ULONG)mem + 15UL) & ~15UL);
    if (P96Board_Lock(&board))
    {
        Log_Printf("source $%08lx, block $%08lx (aligned $%08lx)\n",
                   (ULONG)b.ram, (ULONG)board.mem, (ULONG)Block(&b));
        P96Board_Unlock(&board);
    }

    /* 1: one line, the source pushed out of the cache first */
    if (!Step(args.step, "Stage 1: ONE 16-byte line MOVE16 into video RAM (a hang here = no bursts)"))
        goto stopped;
    Fill(b.ram, 4, 0x11111111UL);
    if (!Move16Once(&b, 1, TRUE))
        goto out;
    bad = Compare(&b, 0, 4);
    Log_Printf("  came back: %s (%lu of 4 longs wrong)\n", bad ? "MISMATCH" : "ok", bad);
    if (bad)
        goto out;

    /* 2: 1 KB */
    if (!Step(args.step, "Stage 2: 1 KB (64 lines) MOVE16, read back"))
        goto stopped;
    Fill(b.ram, 256, 0x22222222UL);
    if (!Move16Once(&b, 64, TRUE))
        goto out;
    bad = Compare(&b, 0, 256);
    Log_Printf("  came back: %s (%lu of 256 longs wrong)\n", bad ? "MISMATCH" : "ok", bad);
    if (bad)
        goto out;

    /* 3: the data cache */
    if (!Step(args.step, "Stage 3: the source dirty in the data cache (no push), then pushed"))
        goto stopped;
    Fill(b.ram, 256, 0x33333333UL);
    if (!Move16Once(&b, 64, FALSE))
        goto out;
    bad = Compare(&b, 0, 256);
    Log_Printf("  without CacheClearE: %s (%lu of 256 longs wrong)%s\n", bad ? "MISMATCH" : "ok", bad,
               bad ? " -- MOVE16 read stale memory: a push is needed first" : "");
    Fill(b.ram, 256, 0x44444444UL);
    if (!Move16Once(&b, 64, TRUE))
        goto out;
    bad = Compare(&b, 0, 256);
    Log_Printf("  with CacheClearE:    %s (%lu of 256 longs wrong)\n", bad ? "MISMATCH" : "ok", bad);

    /* 4: timed */
    if (!Step(args.step, "Stage 4: timed, longword stores against MOVE16 and CopyMemQuick"))
        goto stopped;
    Fill(b.ram, b.longs, 0x55555555UL);
    us = RunTimed(&b, T_Longs);
    kLongs = ReportRate("longword stores (RadeonGL today)", b.bytes, us);
    us = RunTimed(&b, T_CopyMemQuick);
    ReportRate("CopyMemQuick (movem)", b.bytes, us);
    Fill(b.ram, b.longs, 0x66666666UL);
    us = RunTimed(&b, T_Move16);
    kMove = ReportRate("MOVE16", b.bytes, us);
    bad = Compare(&b, 0, 64) + Compare(&b, b.longs - 64, 64);
    Log_Printf("  MOVE16 read-back of both ends: %s (%lu of 128 longs wrong)\n",
               bad ? "MISMATCH" : "ok", bad);
    if (kLongs != 0 && kMove != 0)
        Log_Printf("\nMOVE16 against longword stores: %lu.%02lu x\n",
                   kMove / kLongs, (kMove % kLongs) * 100 / kLongs);
    Log_Printf("%lu locks of the block, its address moved %lu times\n", board.locks, board.moves);
    rc = RETURN_OK;
    goto out;

stopped:
    Log_Printf("Move16Bench: stopped\n");
    rc = RETURN_WARN;

out:
    if (mem)
        FreeVec(mem);
    P96Board_Close(&board);
    Clock_Close();
    Log_Printf("Move16Bench: done (rc %ld)\n", (LONG)rc);
    Log_Close();
    FreeArgs(rda);
    return rc;
}
