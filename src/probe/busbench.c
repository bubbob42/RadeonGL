/*
 * BusBench -- what the Radeon costs to talk to from this 68k, measured
 * through the Mediator with P96 out of the way (concept section 11,
 * probe 2):
 *
 *   MMIO register read and write latency
 *   aperture (BAR0) reads: a longword loop and CopyMemQuick
 *   aperture writes into a scratch block: longword stores, CPU
 *     byte-swapped stores, CopyMemQuick (movem bursts), word stores, and
 *     stores interleaved with fast-RAM work (does the bridge post writes?)
 *   fast RAM to fast RAM, the same size, for scale
 *   AllocateDMAMemoryForBoard(): does P96 hand out pinned VRAM, where, how
 *     much (needs GRANTDIRECTACCESS=Yes on the monitor icon)
 *
 * The scratch block for the write tests is the openpci DMA memory; when
 * that fails, a Picasso96 bitmap on the board, locked for each timed run
 * only (less than a second). Without either, the write tests are skipped
 * and say so. BusBench writes ONE register, SCRATCH_REG5, and only the
 * value it read from it.
 *
 *   BusBench LOG/K,KB/N,NODMA/S,NOP96/S,NOWRITE/S
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <libraries/Picasso96.h>
#include <libraries/openpci.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/openpci.h>
#include <proto/Picasso96.h>

#include "pcicard.h"
#include "mmio.h"
#include "r200regs.h"
#include "r200probe.h"
#include "eclock.h"
#include "plog.h"

static const char ver[] = "$VER: BusBench 0.1 (02.11.2026)";

#define TEMPLATE "LOG/K,KB/N,NODMA/S,NOP96/S,NOWRITE/S"

struct Args
{
    STRPTR log;
    LONG *kb;
    LONG nodma;
    LONG nop96;
    LONG nowrite;
};

#define MMIO_OPS 20000UL
#define REPS 3

struct Library *P96Base = NULL;

/* Where the write tests write. Exactly one of dma / bm is set. */
struct Scratch
{
    APTR dma;                    /* openpci DMA memory (68k address) */
    ULONG dmaSize;
    struct BitMap *bm;           /* or a P96 bitmap on the board ... */
    LONG lock;                   /* ... locked while a timed run uses it */
    volatile ULONG *mem;         /* valid between Scratch_Lock/Unlock */
};

struct Bench
{
    struct PciCard *card;
    ULONG bytes;
    ULONG longs;
    ULONG *ram;                  /* fast RAM, bytes */
    ULONG *ram2;                 /* fast RAM, bytes */
    struct Scratch *scratch;
    volatile ULONG *vram;        /* the write target of the current run */
    ULONG sum;                   /* keeps every read alive */
    ULONG scratchVal;            /* SCRATCH_REG5 as found */
};

typedef void (*BenchFn)(struct Bench *);

/* --- scratch ------------------------------------------------------------- */

static BOOL Scratch_Lock(struct Scratch *s)
{
    struct RenderInfo ri;

    if (s->dma)
    {
        s->mem = (volatile ULONG *)s->dma;
        return TRUE;
    }
    if (s->bm)
    {
        s->lock = p96LockBitMap(s->bm, (UBYTE *)&ri, sizeof(ri));
        if (s->lock == 0)
            return FALSE;
        s->mem = (volatile ULONG *)ri.Memory;
        return TRUE;
    }
    return FALSE;
}

static void Scratch_Unlock(struct Scratch *s)
{
    if (s->bm && s->lock)
    {
        p96UnlockBitMap(s->bm, s->lock);
        s->lock = 0;
    }
    s->mem = NULL;
}

static BOOL Scratch_FromDma(struct Scratch *s, struct PciCard *card, ULONG bytes)
{
    ULONG off;

    s->dmaSize = bytes + 64;
    s->dma = AllocateDMAMemoryForBoard(card->dev, s->dmaSize, MEM_PCI);
    if (s->dma == NULL)
    {
        Log_Printf("AllocateDMAMemoryForBoard(%lu bytes): NULL -- no pinned VRAM through openpci\n"
                   "  (GRANTDIRECTACCESS=Yes on the P96 monitor icon? P96 3.5+?)\n", s->dmaSize);
        return FALSE;
    }
    off = (ULONG)s->dma - (ULONG)card->aper;
    Log_Printf("AllocateDMAMemoryForBoard(%lu bytes) = 0x%08lx, PCI 0x%08lx: %s\n",
               s->dmaSize, (ULONG)s->dma,
               (ULONG)pci_logic_to_physic_addr(s->dma, card->dev),
               (off < card->aperSize) ? (CONST_STRPTR)"inside BAR0 (Radeon VRAM)"
                                      : (CONST_STRPTR)"OUTSIDE BAR0");
    if (off < card->aperSize)
        Log_Printf("  = VRAM offset 0x%08lx (%lu MB into the card)\n", off, off >> 20);
    return TRUE;
}

static BOOL Scratch_FromP96(struct Scratch *s, ULONG bytes)
{
    struct Screen *scr;
    ULONG rows;

    P96Base = OpenLibrary("Picasso96API.library", 2);
    if (P96Base == NULL)
    {
        Log_Printf("Picasso96API.library V2 not found: no P96 scratch\n");
        return FALSE;
    }
    scr = LockPubScreen(NULL);
    if (scr == NULL)
    {
        Log_Printf("no public screen: no P96 scratch\n");
        return FALSE;
    }
    rows = (bytes + 1023) / 1024;
    /* 256 pixels of 32 bits = 1024 bytes a row; a friend of the screen's
     * bitmap gets the screen's format and lands on the board when there is
     * room (Herald's argbraster.c does the same). */
    s->bm = p96AllocBitMap(256, rows, 32, BMF_MINPLANES, scr->RastPort.BitMap, RGBFB_NONE);
    UnlockPubScreen(NULL, scr);
    if (s->bm == NULL)
    {
        Log_Printf("p96AllocBitMap(256 x %lu x 32): NULL\n", rows);
        return FALSE;
    }
    if (!Scratch_Lock(s))
    {
        Log_Printf("p96LockBitMap failed\n");
        p96FreeBitMap(s->bm);
        s->bm = NULL;
        return FALSE;
    }
    if (!p96GetBitMapAttr(s->bm, P96BMA_ISONBOARD))
    {
        Log_Printf("the P96 bitmap is NOT on the board (fast RAM): no P96 scratch\n");
        Scratch_Unlock(s);
        p96FreeBitMap(s->bm);
        s->bm = NULL;
        return FALSE;
    }
    Log_Printf("P96 scratch: bitmap 256 x %lu x 32 on the board at 0x%08lx (this lock), %lu bytes a row\n",
               rows, (ULONG)s->mem, p96GetBitMapAttr(s->bm, P96BMA_BYTESPERROW));
    Scratch_Unlock(s);
    return TRUE;
}

static void Scratch_Free(struct Scratch *s, struct PciCard *card)
{
    Scratch_Unlock(s);
    if (s->dma)
    {
        ReleaseDMAMemoryForBoard(card->dev, s->dma, s->dmaSize);
        s->dma = NULL;
    }
    if (s->bm)
    {
        p96FreeBitMap(s->bm);
        s->bm = NULL;
    }
    if (P96Base)
    {
        CloseLibrary(P96Base);
        P96Base = NULL;
    }
}

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

static void T_AperRead(struct Bench *b)
{
    volatile ULONG *p;
    ULONG i;
    ULONG sum;

    p = (volatile ULONG *)b->card->aper;
    sum = 0;
    for (i = 0; i < b->longs; i++)
        sum += p[i];
    b->sum += sum;
}

static void T_AperReadCopy(struct Bench *b)
{
    CopyMemQuick(b->card->aper, b->ram, b->bytes);
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

/* Best of REPS runs, in microseconds. A write test locks the scratch for
 * each run only. Returns 0 when the scratch could not be locked. */
static ULONG RunTimed(struct Bench *b, BenchFn fn, BOOL needsScratch)
{
    ULONG best;
    ULONG t;
    ULONG us;
    int r;

    best = 0xffffffffUL;
    for (r = 0; r < REPS; r++)
    {
        if (needsScratch)
        {
            if (!Scratch_Lock(b->scratch))
                return 0;
            b->vram = b->scratch->mem;
        }
        t = Clock_Now();
        fn(b);
        us = Clock_Us(Clock_Since(t));
        if (needsScratch)
            Scratch_Unlock(b->scratch);
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

static void DmaLadder(struct PciCard *card)
{
    static const ULONG mb[] = { 1, 4, 16, 32, 48 };
    ULONG i;
    ULONG size;
    APTR p;
    ULONG off;

    Log_Section("AllocateDMAMemoryForBoard sizes");
    for (i = 0; i < sizeof(mb) / sizeof(mb[0]); i++)
    {
        size = mb[i] << 20;
        p = AllocateDMAMemoryForBoard(card->dev, size, MEM_PCI);
        if (p == NULL)
        {
            Log_Printf("  %2lu MB: NULL\n", mb[i]);
            continue;
        }
        off = (ULONG)p - (ULONG)card->aper;
        Log_Printf("  %2lu MB: 0x%08lx %s (VRAM offset 0x%08lx)\n", mb[i], (ULONG)p,
                   (off < card->aperSize) ? (CONST_STRPTR)"in BAR0" : (CONST_STRPTR)"OUTSIDE BAR0",
                   off);
        ReleaseDMAMemoryForBoard(card->dev, p, size);
    }
}

static void Verify(struct Bench *b)
{
    ULONG i;
    ULONG bad;

    if (!Scratch_Lock(b->scratch))
        return;
    bad = 0;
    for (i = 0; i < 64 && i < b->longs; i++)
        if (b->scratch->mem[i] != i)
            bad++;
    Scratch_Unlock(b->scratch);
    Log_Printf("  read-back of the last pattern: %s (%lu of 64 longs wrong)\n",
               bad ? (CONST_STRPTR)"MISMATCH" : (CONST_STRPTR)"ok", bad);
}

int main(void)
{
    struct RDArgs *rda;
    struct Args args;
    struct PciCard card;
    struct Scratch scratch;
    struct Bench b;
    LONG kb;
    ULONG us;
    ULONG v;
    int rc;

    args.log = NULL;
    args.kb = NULL;
    args.nodma = 0;
    args.nop96 = 0;
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
    scratch.dma = NULL;
    scratch.dmaSize = 0;
    scratch.bm = NULL;
    scratch.lock = 0;
    scratch.mem = NULL;
    b.card = &card;
    b.bytes = (ULONG)kb << 10;
    b.longs = b.bytes >> 2;
    b.ram = NULL;
    b.ram2 = NULL;
    b.scratch = &scratch;
    b.vram = NULL;
    b.sum = 0;
    b.scratchVal = 0;

    if (!Clock_Open())
    {
        Log_Printf("BusBench: timer.device did not open\n");
        goto out;
    }
    if (!PciCard_Open(&card))
        goto out;
    PciCard_Report(&card);
    if (R200_ReportEndian(&card) == MMIO_UNDECIDED)
        Log_Printf("(register orientation undecided; MMIO figures still valid)\n");

    b.ram = AllocVec(b.bytes, MEMF_FAST | MEMF_PUBLIC);
    b.ram2 = AllocVec(b.bytes, MEMF_FAST | MEMF_PUBLIC);
    if (b.ram == NULL || b.ram2 == NULL)
    {
        Log_Printf("BusBench: no fast RAM for two %ld KB buffers\n", kb);
        goto out;
    }
    for (v = 0; v < b.longs; v++)
        b.ram[v] = v;

    Log_Section("MMIO registers");
    us = RunTimed(&b, T_MmioRead, FALSE);
    ReportLatency("read RBBM_STATUS", MMIO_OPS, us);
    b.scratchVal = Mmio_Read(card.mmio, R_SCRATCH_REG5);
    us = RunTimed(&b, T_MmioWrite, FALSE);
    ReportLatency("write SCRATCH_REG5 (same value back)", MMIO_OPS, us);
    v = Mmio_Read(card.mmio, R_SCRATCH_REG5);
    Log_Printf("  SCRATCH_REG5 was 0x%08lx, is 0x%08lx%s\n", b.scratchVal, v,
               (v == b.scratchVal) ? (CONST_STRPTR)"" : (CONST_STRPTR)"  CHANGED");

    Log_Section("Aperture reads (BAR0, the visible frame buffer)");
    us = RunTimed(&b, T_AperRead, FALSE);
    ReportRate("longword loop VRAM -> CPU", b.bytes, us);
    us = RunTimed(&b, T_AperReadCopy, FALSE);
    ReportRate("CopyMemQuick VRAM -> fast RAM", b.bytes, us);
    us = RunTimed(&b, T_RamCopy, FALSE);
    ReportRate("CopyMemQuick fast RAM -> fast RAM", b.bytes, us);
    us = RunTimed(&b, T_RamStores, FALSE);
    ReportRate("4 fast-RAM stores per step, no VRAM (for scale)", b.bytes * 4, us);

    if (!args.nowrite)
    {
        Log_Section("Scratch block for writes");
        if ((args.nodma || !Scratch_FromDma(&scratch, &card, b.bytes))
            && (args.nop96 || !Scratch_FromP96(&scratch, b.bytes)))
        {
            Log_Printf("no scratch block: write tests skipped\n");
        }
        else
        {
            Log_Section("Aperture writes (CPU -> VRAM scratch)");
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
    }

    if (!args.nodma)
        DmaLadder(&card);

    Log_Printf("\n(checksum of everything read: 0x%08lx)\n", b.sum);
    rc = RETURN_OK;

out:
    Scratch_Free(&scratch, &card);
    if (b.ram)
        FreeVec(b.ram);
    if (b.ram2)
        FreeVec(b.ram2);
    PciCard_Close(&card);
    Clock_Close();
    Log_Printf("BusBench: done (rc %ld)\n", (LONG)rc);
    Log_Close();
    FreeArgs(rda);
    return rc;
}
