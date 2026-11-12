/*
 * VramProbe -- the Radeon's video RAM beyond the part Picasso96 manages,
 * probed in small steps from the host.
 *
 *   VramProbe APER=0 OFFSET=0x4000000 SIZE=4
 *   VramProbe APER=0 OFFSET=0x4000000 SIZE=4096 STRIDE=0x400000 COUNT=16
 *   VramProbe APER=0 OFFSET=0x4100000 SIZE=16 WRITE
 *
 * APER 0 is BAR0 of PCI function 0 (aperture 0), APER 1 BAR0 of function 1
 * (the second aperture). OFFSET and STRIDE are byte offsets into that BAR
 * ($ or 0x for hex). COUNT blocks of SIZE bytes are taken, STRIDE apart.
 *
 * Every block is read, and compared with the blocks it could alias: the
 * P96 area at the same offset modulo the P96 size (rounded up to a power
 * of two), and for APER 1 the same offset in aperture 0.
 * With WRITE, each block is also written with a pattern, read back, the
 * alias candidates read again, and the block restored -- all inside a lock
 * of a P96 bitmap (P96 keeps off the chip) and Forbid(), so nobody renders
 * meanwhile. If the block aliases P96 memory, restoring the block restores
 * that memory too (the same cells). WRITE is refused inside the P96 area.
 * Results are collected inside the lock and printed after it (a console
 * draws through the same board).
 *
 * Nothing is written to any register or to PCI configuration space.
 * Why (2026-11-11): the board's ROM says 256 MB; P96 manages 64 MB.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <utility/tagitem.h>
#include <libraries/openpci.h>
#include <libraries/pcitags.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/openpci.h>

#include "p96board.h"
#include "pcicard.h"
#include "plog.h"

static const char ver[] = "$VER: VramProbe 0.1 (11.11.2026)";

#define TEMPLATE "APER/N,OFFSET/A,SIZE/N,STRIDE/K,COUNT/N,WRITE/S,LOG/K"
#define MAX_SIZE 65536UL
#define MAX_WRITE_SIZE 16384UL
#define MAX_COUNT 64UL
#define PCI_VENDOR_ATI 0x1002UL
#define PCI_CLASS_DISPLAY 3UL

extern struct Library *OpenPciBase;    /* pcicard.c */

struct Args
{
    LONG *aper;
    STRPTR offset;
    LONG *size;
    STRPTR stride;
    LONG *count;
    LONG write;
    STRPTR log;
};

static int parse_num(CONST_STRPTR s, ULONG *out)
{
    ULONG v;
    int digits;
    int hex;
    char c;

    hex = 0;
    if (s[0] == '$')
    {
        hex = 1;
        s++;
    }
    else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    {
        hex = 1;
        s += 2;
    }
    v = 0;
    digits = 0;
    while ((c = *s++) != 0)
    {
        if (c >= '0' && c <= '9')
            v = v * (hex ? 16 : 10) + (ULONG) (c - '0');
        else if (hex && c >= 'a' && c <= 'f')
            v = v * 16 + (ULONG) (c - 'a' + 10);
        else if (hex && c >= 'A' && c <= 'F')
            v = v * 16 + (ULONG) (c - 'A' + 10);
        else
            return 0;
        if (++digits > (hex ? 8 : 10))
            return 0;
    }
    if (digits == 0)
        return 0;
    *out = v;
    return 1;
}

static ULONG attr(struct pci_dev *dev, ULONG tag)
{
    struct TagItem tags[2];
    ULONG value;

    value = 0;
    tags[0].ti_Tag = tag;
    tags[0].ti_Data = (ULONG) &value;
    tags[1].ti_Tag = TAG_DONE;
    tags[1].ti_Data = 0;
    GetBoardAttrsA(dev, tags);
    return value;
}

static void rd(const volatile ULONG *src, ULONG *dst, ULONG n)
{
    while (n-- > 0)
        *dst++ = *src++;
}

static void wr(volatile ULONG *dst, const ULONG *src, ULONG n)
{
    while (n-- > 0)
        *dst++ = *src++;
}

static int same(const ULONG *a, const ULONG *b, ULONG n)
{
    while (n-- > 0)
        if (*a++ != *b++)
            return 0;
    return 1;
}

static ULONG fnv(const ULONG *p, ULONG n)
{
    ULONG h;
    ULONG i;
    const UBYTE *b;

    h = 0x811C9DC5UL;
    b = (const UBYTE *) p;
    for (i = 0; i < n * 4; i++)
        h = (h ^ b[i]) * 0x01000193UL;
    return h;
}

static int uniform(const ULONG *p, ULONG n)
{
    ULONG i;

    for (i = 1; i < n; i++)
        if (p[i] != p[0])
            return 0;
    return 1;
}

int main(void)
{
    static struct P96Board board;
    static struct PciCard card;
    struct RDArgs *rda;
    struct Args args;
    struct TagItem find[3];
    struct pci_dev *dev1;
    UBYTE *aper[2];
    ULONG aperSize[2];
    ULONG which;
    ULONG offset;
    ULONG size;
    ULONG stride;
    ULONG count;
    ULONG words;
    ULONG p96pow2;
    ULONG i;
    ULONG j;
    ULONG off;
    ULONG c1off;
    ULONG *blk;
    ULONG *c1;
    ULONG *c2;
    ULONG *save;
    ULONG *pat;
    ULONG *back;
    ULONG *c1after;
    ULONG *c2after;
    ULONG *restored;
    BOOL haveBoard;
    BOOL havePci;
    BOOL locked;
    BOOL stuck;
    BOOL alias1;
    BOOL alias2;
    BOOL ok;
    int rc;

    if (((struct Process *) FindTask(NULL))->pr_CLI == 0)
        return RETURN_FAIL;
    rc = RETURN_FAIL;
    haveBoard = FALSE;
    havePci = FALSE;
    blk = NULL;
    args.aper = NULL;
    args.offset = NULL;
    args.size = NULL;
    args.stride = NULL;
    args.count = NULL;
    args.write = 0;
    args.log = NULL;
    rda = ReadArgs(TEMPLATE, (LONG *) &args, NULL);
    if (rda == NULL)
    {
        PrintFault(IoErr(), "VramProbe");
        return RETURN_FAIL;
    }
    which = args.aper ? (ULONG) *args.aper : 0;
    size = args.size ? (ULONG) *args.size : 4;
    count = args.count ? (ULONG) *args.count : 1;
    stride = 0;
    if (!parse_num(args.offset, &offset) ||
        (args.stride != NULL && !parse_num(args.stride, &stride)))
    {
        Printf("VramProbe: OFFSET and STRIDE are numbers ($ or 0x for hex)\n");
        goto done;
    }
    size = (size + 3) & ~3UL;
    if (which > 1 || size == 0 || size > MAX_SIZE || count == 0 || count > MAX_COUNT ||
        (offset & 3) != 0 || (stride & 3) != 0 ||
        (args.write && size > MAX_WRITE_SIZE))
    {
        Printf("VramProbe: APER 0 or 1, SIZE 4..%lu (WRITE: ..%lu), COUNT 1..%lu, "
               "OFFSET and STRIDE multiples of 4\n", MAX_SIZE, MAX_WRITE_SIZE, MAX_COUNT);
        goto done;
    }
    words = size / 4;
    Log_Open(args.log);

    haveBoard = TRUE;           /* P96Board_Close is right even after a failed Open */
    if (!P96Board_Open(&board, 4096) || !P96Board_IsRadeon(&board))
    {
        Log_Printf("VramProbe: no Radeon board under the Workbench through P96\n");
        goto done;
    }
    havePci = PciCard_Open(&card);
    if (!havePci)
        goto done;
    aper[0] = (UBYTE *) card.aper;
    aperSize[0] = card.aperSize;
    aper[1] = NULL;
    aperSize[1] = 0;
    find[0].ti_Tag = PRM_Vendor;
    find[0].ti_Data = PCI_VENDOR_ATI;
    find[1].ti_Tag = PRM_Class;
    find[1].ti_Data = PCI_CLASS_DISPLAY;
    find[2].ti_Tag = TAG_DONE;
    find[2].ti_Data = 0;
    dev1 = FindBoardA(card.dev, find);
    if (dev1 != NULL)
    {
        aper[1] = (UBYTE *) attr(dev1, PRM_MemoryAddr0);
        aperSize[1] = attr(dev1, PRM_MemorySize0);
    }
    for (p96pow2 = 1; p96pow2 < board.totalMem && p96pow2 != 0; p96pow2 <<= 1)
        ;

    Log_Printf("P96: %s / %s, %lu KB, memory base $%08lx; area for aliases %lu MB\n",
               (ULONG) board.boardName, (ULONG) board.chipName, board.totalMem >> 10,
               (ULONG) board.memBase, p96pow2 >> 20);
    Log_Printf("aperture 0: $%08lx, %lu MB (function %lu)\n", (ULONG) aper[0], aperSize[0] >> 20,
               card.function);
    if (aper[1] != NULL)
        Log_Printf("aperture 1: $%08lx, %lu MB (function %lu)\n", (ULONG) aper[1],
                   aperSize[1] >> 20, attr(dev1, PRM_FunctionNumber));
    else
        Log_Printf("aperture 1: no second ATI display function found\n");
    if ((UBYTE *) board.memBase != aper[0])
        Log_Printf("note: P96's memory base is not aperture 0\n");

    if (aper[which] == NULL || offset >= aperSize[which] ||
        (count - 1) * stride + size > aperSize[which] - offset)
    {
        Log_Printf("VramProbe: the blocks do not lie inside aperture %lu\n", which);
        goto done;
    }
    if (args.write && offset < p96pow2)
    {
        Log_Printf("VramProbe: WRITE refused below %lu MB (the P96 area or its mirror)\n",
                   p96pow2 >> 20);
        goto done;
    }

    blk = AllocVec(size * 9, MEMF_ANY);
    if (blk == NULL)
    {
        Log_Printf("VramProbe: no memory for the buffers\n");
        goto done;
    }
    c1 = blk + words;
    c2 = c1 + words;
    save = c2 + words;
    pat = save + words;
    back = pat + words;
    c1after = back + words;
    c2after = c1after + words;
    restored = c2after + words;

    Log_Printf("%s aperture %lu: %lu block(s) of %lu bytes from +$%08lx, stride $%lx\n",
               (ULONG) (args.write ? "WRITE+read" : "read"), which, count, size, offset, stride);
    Log_Flush();
    ok = TRUE;
    for (i = 0; i < count; i++)
    {
        off = offset + i * stride;
        c1off = off & (p96pow2 - 1);
        for (j = 0; j < words; j++)
            pat[j] = 0x5A000000UL ^ (off + j * 4) ^ (i << 20);

        locked = P96Board_Lock(&board);
        if (!locked)
        {
            Log_Printf("VramProbe: could not lock a P96 bitmap; stopped\n");
            ok = FALSE;
            break;
        }
        rd((const volatile ULONG *) (aper[which] + off), blk, words);
        rd((const volatile ULONG *) (aper[0] + c1off), c1, words);
        if (which == 1 && off + size <= aperSize[0])
            rd((const volatile ULONG *) (aper[0] + off), c2, words);
        if (args.write)
        {
            Forbid();
            rd((const volatile ULONG *) (aper[which] + off), save, words);
            wr((volatile ULONG *) (aper[which] + off), pat, words);
            rd((const volatile ULONG *) (aper[which] + off), back, words);
            rd((const volatile ULONG *) (aper[0] + c1off), c1after, words);
            if (which == 1 && off + size <= aperSize[0])
                rd((const volatile ULONG *) (aper[0] + off), c2after, words);
            wr((volatile ULONG *) (aper[which] + off), save, words);
            rd((const volatile ULONG *) (aper[which] + off), restored, words);
            Permit();
        }
        P96Board_Unlock(&board);

        Log_Printf("R +$%08lx  hash $%08lx  first $%08lx $%08lx%s  = P96 +$%08lx: %s",
                   off, fnv(blk, words), blk[0], words > 1 ? blk[1] : 0UL,
                   (ULONG) (uniform(blk, words) ? " (uniform)" : ""), c1off,
                   (ULONG) (same(blk, c1, words) ? "SAME" : "differs"));
        if (which == 1 && off + size <= aperSize[0])
            Log_Printf("  = aper0 +$%08lx: %s", off, (ULONG) (same(blk, c2, words) ? "SAME" : "differs"));
        Log_Printf("\n");
        if (args.write)
        {
            stuck = same(back, pat, words) ? TRUE : FALSE;
            alias1 = same(c1after, c1, words) ? FALSE : TRUE;
            alias2 = (which == 1 && off + size <= aperSize[0] && !same(c2after, c2, words)) ? TRUE : FALSE;
            Log_Printf("W +$%08lx  pattern %s  P96 +$%08lx %s%s  restored %s\n",
                       off, (ULONG) (stuck ? "STUCK" : "LOST"), c1off,
                       (ULONG) (alias1 ? "CHANGED (alias)" : "unchanged"),
                       (ULONG) (alias2 ? "  aper0 CHANGED (alias)" : ""),
                       (ULONG) (same(restored, save, words) ? "yes" : "NO"));
            if (!stuck)
                Log_Printf("   read back $%08lx, written $%08lx\n", back[0], pat[0]);
        }
        Log_Flush();
    }
    rc = ok ? RETURN_OK : RETURN_WARN;

done:
    if (blk != NULL)
        FreeVec(blk);
    if (havePci)
        PciCard_Close(&card);
    if (haveBoard)
        P96Board_Close(&board);
    Log_Close();
    FreeArgs(rda);
    return rc;
}
