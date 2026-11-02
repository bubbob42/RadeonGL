/*
 * p96board.h -- the RTG board under the Workbench screen, as
 * Picasso96API.library hands it out. The rules come from Picasso96's answers of 2026-11-02 (LockBench)
 * and the Picasso96API autodoc (reference/P96API_digest.md):
 *
 *  - The card is found through P96, not on the PCI bus. A bitmap on the
 *    board is locked; p96GetBitMapAttr then gives the board's memory base,
 *    its I/O base (BAR1 on a Radeon) and its memory-mapped registers (BAR2).
 *    The chip name P96 reports is checked before any register is touched.
 *  - While a bitmap of the board is locked, P96 keeps off the chip: every
 *    register access of a probe happens between P96Board_Lock and
 *    P96Board_Unlock. Locks are short, and nothing reaches a console
 *    meanwhile (the lock holds plog's console output).
 *  - Video RAM for a probe is a friend bitmap of the screen, allocated with
 *    graphics.library's AllocBitMap, never displayable. It may move, or
 *    leave the board, whenever it is unlocked: its address is valid only
 *    inside a lock and is checked at every lock.
 */

#ifndef PROBE_P96BOARD_H
#define PROBE_P96BOARD_H

#include <exec/types.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <libraries/Picasso96.h>

struct P96Board
{
    struct Screen *scr;          /* the Workbench, locked as a public screen */
    struct BitMap *scrbm;        /* its bitmap */
    ULONG modeId;
    ULONG board;                 /* P96 board number */
    STRPTR boardName;
    STRPTR chipName;
    ULONG totalMem;
    ULONG freeMem;
    ULONG largestFree;
    ULONG formats;               /* RGBFF_ mask: what the board supports */
    ULONG scrFormat;             /* RGBFB_ of the screen */
    ULONG compatFormats;         /* P96IDA_COMPATIBLEFORMATS of the screen's mode */

    struct BitMap *bm;           /* the block: a friend bitmap of the screen */
    ULONG bytes;                 /* asked for */
    ULONG width;                 /* pixels a row */
    ULONG rows;
    LONG lock;

    /* inside a lock of the block (P96Board_Lock) */
    UBYTE *mem;                  /* its first byte, 68k address */
    ULONG bpr;                   /* bytes a row */
    ULONG bpp;                   /* bytes a pixel */
    ULONG format;                /* RGBFB_ */

    /* the board's bases, read at the first lock (they do not change) */
    APTR memBase;                /* P96BMA_BOARDMEMBASE */
    APTR ioBase;                 /* P96BMA_BOARDIOBASE: BAR1 on a Radeon */
    APTR memIoBase;              /* P96BMA_BOARDMEMIOBASE: BAR2, the registers */

    ULONG locks;                 /* locks of the block so far */
    ULONG moves;                 /* how often mem differed from the lock before */
    UBYTE *lastMem;
};

extern struct Library *P96Base;

/* Opens Picasso96API.library V2, describes the Workbench's board, and when
 * bytes is not 0 allocates a block of at least that many bytes (rows of
 * 1024 bytes, the screen's format) and locks it once to read the bases.
 * FALSE and a log line when any of that fails; P96Board_Close is right
 * either way. */
BOOL P96Board_Open(struct P96Board *b, ULONG bytes);

/* Locks the block. TRUE with mem, bpr, bpp and format set when it is on
 * the board; FALSE, nothing held, when it could not be locked or is not on
 * the board now. */
BOOL P96Board_Lock(struct P96Board *b);
void P96Board_Unlock(struct P96Board *b);

/* p96LockBitMap / p96UnlockBitMap of any bitmap, holding the console output
 * between them. The lock, or 0. */
LONG P96Board_LockBitMap(struct BitMap *bm, struct RenderInfo *ri);
void P96Board_UnlockBitMap(struct BitMap *bm, LONG lock);

/* Unlocks and frees the block, releases the screen, closes the library. */
void P96Board_Close(struct P96Board *b);

/* TRUE when P96's chip name (or board name) says Radeon: only then may a
 * probe read Radeon registers at memIoBase. */
BOOL P96Board_IsRadeon(const struct P96Board *b);

/* The board and the block as report lines (outside a lock). */
void P96Board_Report(const struct P96Board *b);

/* An RGBFB_ value's name. */
CONST_STRPTR P96Board_FormatName(ULONG rgbfb);

#endif /* PROBE_P96BOARD_H */
