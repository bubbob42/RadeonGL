/*
 * BusBench -- what the Radeon costs to talk to from this 68k, measured
 * through the Mediator (concept section 11, probe 2):
 *
 *   MMIO register read and write latency, and writes into a 3D register
 *     through the command FIFO (PP_TFACTOR_0), with and without the FIFO
 *     check a driver that writes registers directly needs: the like-for-
 *     like answer to "a ring costs the same as direct register writes"
 *   aperture reads from a block of video RAM: a longword loop and
 *     CopyMemQuick
 *   aperture writes into the block: longword stores, CPU byte-swapped
 *     stores (a ring dword), CopyMemQuick (movem bursts), word stores, and
 *     stores interleaved with fast-RAM work (does the bridge post writes?)
 *   fast RAM to fast RAM, the same size, for scale
 *
 * The card and the block come from Picasso96 (p96board.h): a friend bitmap
 * of the Workbench screen, locked for each timed run only (well under a
 * second). Every register access happens inside such a lock, which keeps
 * Radeon.card off the chip. Without a Radeon the register tests are
 * skipped. BusBench writes two registers: SCRATCH_REG5 with the value it
 * read from it, and PP_TFACTOR_0 (a 3D constant colour nobody else uses),
 * put back afterwards.
 *
 *   BusBench LOG/K,KB/N,NOWRITE/S
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
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

static const char ver[] = "$VER: BusBench 0.2 (02.11.2026)";

#define TEMPLATE "LOG/K,KB/N,NOWRITE/S"

struct Args
{
    STRPTR log;
    LONG *kb;
    LONG nowrite;
};

#define MMIO_OPS 20000UL
#define FIFO_STEP 32UL           /* writes between FIFO checks */
#define REPS 3


struct Bench
{
    struct PciCard *card;        /* the bases P96 gave (r200probe's record) */
    struct P96Board *board;
    ULONG bytes;
    ULONG longs;
    ULONG *ram;                  /* fast RAM, bytes */
    ULONG *ram2;                 /* fast RAM, bytes */
    volatile ULONG *vram;        /* the block, inside the lock of the current run */
    ULONG sum;                   /* keeps every read alive */
    ULONG scratchVal;            /* SCRATCH_REG5 as found */
    ULONG fifoWaits;             /* FIFO checks that found fewer free slots than a step */
};

typedef void (*BenchFn)(struct Bench *);

/* --- the tests ----------------------------------------------------------- */

static void T_MmioRead(struct Bench *b)
{
    ULONG i;
    ULONG sum;

    sum = 0;
    for (i = 0; i < MMIO_OPS; i++)
        sum += Mmio_Read(b->card->mmio, R_RBBM_STATUS);
    b->sum += sum;
}

static void T_MmioWrite(struct Bench *b)
{
    ULONG i;

    for (i = 0; i < MMIO_OPS; i++)
        Mmio_Write(b->card->mmio, R_SCRATCH_REG5, b->scratchVal);
}

/* Writes to a 3D register: they go through the command FIFO into the 3D
 * engine, as a driver that programs the registers directly writes state
 * and vertex data. Without a check (an idle engine drains the FIFO far
 * faster than the bus fills it) ... */
static void T_Mmio3D(struct Bench *b)
{
    ULONG i;

    for (i = 0; i < MMIO_OPS; i++)
        Mmio_Write(b->card->mmio, R_PP_TFACTOR_0, i);
}

/* ... and as such a driver must: before every FIFO_STEP writes, wait until
 * RBBM_STATUS shows that many free slots. */
static void T_Mmio3DChecked(struct Bench *b)
{
    ULONG i;
    ULONG k;

    for (i = 0; i < MMIO_OPS; i += FIFO_STEP)
    {
        while ((Mmio_Read(b->card->mmio, R_RBBM_STATUS) & R_RBBM_FIFOCNT_MASK) < FIFO_STEP)
            b->fifoWaits++;
        for (k = 0; k < FIFO_STEP; k++)
            Mmio_Write(b->card->mmio, R_PP_TFACTOR_0, i + k);
    }
}

static void T_AperRead(struct Bench *b)
{
    ULONG i;
    ULONG sum;

    sum = 0;
    for (i = 0; i < b->longs; i++)
        sum += b->vram[i];
    b->sum += sum;
}

static void T_AperReadCopy(struct Bench *b)
{
    CopyMemQuick((APTR)b->vram, b->ram, b->bytes);
    b->sum += b->ram[0];
}

static void T_RamCopy(struct Bench *b)
{
    CopyMemQuick(b->ram, b->ram2, b->bytes);
    b->sum += b->ram2[0];
}

static void T_RamStores(struct Bench *b)
{
    ULONG i;
    ULONG *r;

    r = b->ram;
    for (i = 0; i < b->longs; i++)
    {
        r[(i * 4) & 1023] = i;
        r[(i * 4 + 1) & 1023] = i;
        r[(i * 4 + 2) & 1023] = i;
        r[(i * 4 + 3) & 1023] = i;
    }
}

static void T_AperWrite(struct Bench *b)
{
    ULONG i;

    for (i = 0; i < b->longs; i++)
        b->vram[i] = i;
}

static void T_AperWriteSwap(struct Bench *b)
{
    ULONG i;

    for (i = 0; i < b->longs; i++)
        b->vram[i] = Mmio_Swap32(i);
}

static void T_AperWriteCopy(struct Bench *b)
{
    CopyMemQuick(b->ram, (APTR)b->vram, b->bytes);
}

static void T_AperWriteWord(struct Bench *b)
{
    volatile UWORD *p;
    ULONG i;
    ULONG n;

    p = (volatile UWORD *)b->vram;
    n = b->longs * 2;
    for (i = 0; i < n; i++)
        p[i] = (UWORD)i;
}

static void T_AperWriteInterleaved(struct Bench *b)
{
    ULONG i;
    ULONG *r;

    r = b->ram;
    for (i = 0; i < b->longs; i++)
    {
        b->vram[i] = i;
        r[(i * 4) & 1023] = i;
        r[(i * 4 + 1) & 1023] = i;
        r[(i * 4 + 2) & 1023] = i;
        r[(i * 4 + 3) & 1023] = i;
    }
}

/* --- running them -------------------------------------------------------- */

/* Best of REPS runs, in microseconds. With locked, each run holds a lock of
 * the block (vram valid): every register and video-RAM access needs one.
 * Returns 0 when the block could not be locked. */
static ULONG RunTimed(struct Bench *b, BenchFn fn, BOOL locked)
{
    ULONG best;
    ULONG t;
    ULONG us;
    int r;

    best = 0xffffffffUL;
    for (r = 0; r < REPS; r++)
    {
        if (locked)
        {
            if (!P96Board_Lock(b->board))
                return 0;
            b->vram = (volatile ULONG *)b->board->mem;
        }
        t = Clock_Now();
        fn(b);
        us = Clock_Us(Clock_Since(t));
        if (locked)
        {
            P96Board_Unlock(b->board);
            b->vram = NULL;
        }
        if (us < best)
            best = us;
    }
    return best;
}

static void ReportRate(CONST_STRPTR what, ULONG bytes, ULONG us)
{
    ULONG kbps;

    if (us == 0)
    {
        Log_Printf("  %-44s skipped\n", what);
        return;
    }
    kbps = Clock_KBps(bytes, us);
    Log_Printf("  %-44s %7lu us  %6lu KB/s (%lu.%lu MB/s)\n", what, us, kbps,
               kbps / 1024, (kbps % 1024) * 10 / 1024);
}

static void ReportLatency(CONST_STRPTR what, ULONG ops, ULONG us)
{
    /* ns an operation = us * 1000 / ops; us * 1000 fits for runs < 4 s */
    Log_Printf("  %-44s %7lu us  %6lu ns each\n", what, us, (us * 1000UL) / ops);
}

static void Verify(struct Bench *b)
{
    ULONG i;
    ULONG bad;

    if (!P96Board_Lock(b->board))
        return;
    bad = 0;
    for (i = 0; i < 64 && i < b->longs; i++)
        if (((volatile ULONG *)b->board->mem)[i] != i)
            bad++;
    P96Board_Unlock(b->board);
    Log_Printf("  read-back of the last pattern: %s (%lu of 64 longs wrong)\n",
               bad ? (CONST_STRPTR)"MISMATCH" : (CONST_STRPTR)"ok", bad);
}

/* SCRATCH_REG5 and PP_TFACTOR_0 as found; the latter put back after its
 * tests. Inside a lock. */
static void ReadRegs(struct Bench *b, ULONG *tf)
{
    if (!P96Board_Lock(b->board))
        return;
    b->scratchVal = Mmio_Read(b->card->mmio, R_SCRATCH_REG5);
    *tf = Mmio_Read(b->card->mmio, R_PP_TFACTOR_0);
    P96Board_Unlock(b->board);
}

static void PutBack(struct Bench *b, ULONG tf)
{
    ULONG v;
    ULONG t;

    if (!P96Board_Lock(b->board))
        return;
    Mmio_Write(b->card->mmio, R_PP_TFACTOR_0, tf);
    v = Mmio_Read(b->card->mmio, R_SCRATCH_REG5);
    t = Mmio_Read(b->card->mmio, R_PP_TFACTOR_0);
    P96Board_Unlock(b->board);
    Log_Printf("  SCRATCH_REG5 was 0x%08lx, is 0x%08lx%s; PP_TFACTOR_0 back to 0x%08lx%s\n",
               b->scratchVal, v, (v == b->scratchVal) ? (CONST_STRPTR)"" : (CONST_STRPTR)"  CHANGED",
               t, (t == tf) ? (CONST_STRPTR)"" : (CONST_STRPTR)"  NOT RESTORED");
}

int main(void)
{
    static struct P96Board board;     /* zero: P96Board_Close is right before Open */
    struct RDArgs *rda;
    struct Args args;
    struct PciCard card;
    struct Bench b;
    LONG kb;
    ULONG us;
    ULONG v;
    ULONG tf;
    BOOL radeon;
    int rc;

    args.log = NULL;
    args.kb = NULL;
    args.nowrite = 0;
    rda = ReadArgs(TEMPLATE, (LONG *)&args, NULL);
    if (rda == NULL)
    {
        PrintFault(IoErr(), "BusBench");
        return RETURN_ERROR;
    }
    kb = args.kb ? *args.kb : 1024;
    if (kb < 4)
        kb = 4;
    if (kb > 4096)
        kb = 4096;

    if (!Log_Open(args.log))
        Log_Printf("BusBench: cannot write %s, console only\n", args.log);
    Log_Printf("%s -- Mediator/Radeon bus costs, %ld KB blocks, best of %ld\n",
               ver + 6, kb, (LONG)REPS);

    rc = RETURN_FAIL;
    b.card = &card;
    b.board = &board;
    b.bytes = (ULONG)kb << 10;
    b.longs = b.bytes >> 2;
    b.ram = NULL;
    b.ram2 = NULL;
    b.vram = NULL;
    b.sum = 0;
    b.scratchVal = 0;
    b.fifoWaits = 0;
    tf = 0;

    if (!Clock_Open())
    {
        Log_Printf("BusBench: timer.device did not open\n");
        goto out;
    }
    if (!P96Board_Open(&board, b.bytes))
        goto out;
    P96Board_Report(&board);
    radeon = (BOOL)(P96Board_IsRadeon(&board) && board.memIoBase != NULL);
    R200_CardFromP96(&card, &board);
    if (radeon && P96Board_Lock(&board))
    {
        v = R200_ReportEndian(&card);
        P96Board_Unlock(&board);
        if (v == MMIO_UNDECIDED)
            Log_Printf("(register orientation undecided; MMIO figures still valid)\n");
    }

    b.ram = AllocVec(b.bytes, MEMF_FAST | MEMF_PUBLIC);
    b.ram2 = AllocVec(b.bytes, MEMF_FAST | MEMF_PUBLIC);
    if (b.ram == NULL || b.ram2 == NULL)
    {
        Log_Printf("BusBench: no fast RAM for two %ld KB buffers\n", kb);
        goto out;
    }
    for (v = 0; v < b.longs; v++)
        b.ram[v] = v;

    if (radeon)
    {
        Log_Section("MMIO registers (each run inside a lock)");
        ReadRegs(&b, &tf);
        us = RunTimed(&b, T_MmioRead, TRUE);
        ReportLatency("read RBBM_STATUS", MMIO_OPS, us);
        us = RunTimed(&b, T_MmioWrite, TRUE);
        ReportLatency("write SCRATCH_REG5 (same value back)", MMIO_OPS, us);
        us = RunTimed(&b, T_Mmio3D, TRUE);
        ReportLatency("write PP_TFACTOR_0 (3D, through the FIFO)", MMIO_OPS, us);
        us = RunTimed(&b, T_Mmio3DChecked, TRUE);
        ReportLatency("the same, FIFO checked every 32 writes", MMIO_OPS, us);
        Log_Printf("  FIFO checks that had to wait: %lu\n", b.fifoWaits);
        PutBack(&b, tf);
    }
    else
        Log_Printf("\n(not a Radeon: no register tests)\n");

    Log_Section("Fast RAM, for scale");
    us = RunTimed(&b, T_RamCopy, FALSE);
    ReportRate("CopyMemQuick fast RAM -> fast RAM", b.bytes, us);
    us = RunTimed(&b, T_RamStores, FALSE);
    ReportRate("4 fast-RAM stores per step, no VRAM (for scale)", b.bytes * 4, us);

    if (!args.nowrite)
    {
        Log_Section("Aperture writes (CPU -> the block in video RAM)");
        us = RunTimed(&b, T_AperWrite, TRUE);
        ReportRate("longword stores", b.bytes, us);
        us = RunTimed(&b, T_AperWriteSwap, TRUE);
        ReportRate("longword stores, byte-swapped by the CPU", b.bytes, us);
        us = RunTimed(&b, T_AperWriteCopy, TRUE);
        ReportRate("CopyMemQuick fast RAM -> VRAM (movem)", b.bytes, us);
        us = RunTimed(&b, T_AperWriteWord, TRUE);
        ReportRate("word stores", b.bytes, us);
        us = RunTimed(&b, T_AperWriteInterleaved, TRUE);
        ReportRate("longword stores + 4 RAM stores between", b.bytes, us);
        us = RunTimed(&b, T_AperWrite, TRUE);
        ReportRate("longword stores again (pattern for read-back)", b.bytes, us);
        Verify(&b);
    }

    Log_Section("Aperture reads (the block in video RAM -> CPU)");
    us = RunTimed(&b, T_AperRead, TRUE);
    ReportRate("longword loop VRAM -> CPU", b.bytes, us);
    us = RunTimed(&b, T_AperReadCopy, TRUE);
    ReportRate("CopyMemQuick VRAM -> fast RAM", b.bytes, us);

    Log_Printf("\n%lu locks of the block, its address moved %lu times\n", board.locks, board.moves);
    Log_Printf("(checksum of everything read: 0x%08lx)\n", b.sum);
    rc = RETURN_OK;

out:
    if (b.ram)
        FreeVec(b.ram);
    if (b.ram2)
        FreeVec(b.ram2);
    P96Board_Close(&board);
    Clock_Close();
    Log_Printf("BusBench: done (rc %ld)\n", (LONG)rc);
    Log_Close();
    FreeArgs(rda);
    return rc;
}
