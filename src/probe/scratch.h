/*
 * scratch.h -- a block of the Radeon's VRAM for a probe to write: openpci
 * DMA memory (pinned; needs GRANTDIRECTACCESS=Yes on the P96 monitor
 * icon), else a Picasso96 bitmap on the board, locked only while it is
 * used (P96 may move an unlocked bitmap; a lock is held well under a
 * second). Shared by BusBench and CPRing.
 */

#ifndef PROBE_SCRATCH_H
#define PROBE_SCRATCH_H

#include <exec/types.h>
#include <graphics/gfx.h>
#include "pcicard.h"

/* Exactly one of dma / bm is set. */
struct Scratch
{
    APTR dma;                    /* openpci DMA memory (68k address) */
    ULONG dmaSize;
    struct BitMap *bm;           /* or a P96 bitmap on the board ... */
    LONG lock;                   /* ... locked while a run uses it */
    volatile ULONG *mem;         /* valid between Scratch_Lock/Unlock */
};

extern struct Library *P96Base;

/* openpci DMA memory of bytes (+64). FALSE and a log line when refused. */
BOOL Scratch_FromDma(struct Scratch *s, struct PciCard *card, ULONG bytes);

/* A 256-pixel-wide 32-bit P96 bitmap of at least bytes, on the board.
 * Opens Picasso96API.library. FALSE and a log line when it cannot. */
BOOL Scratch_FromP96(struct Scratch *s, ULONG bytes);

/* s->mem valid until Scratch_Unlock (a P96 lock for a bitmap). */
BOOL Scratch_Lock(struct Scratch *s);
void Scratch_Unlock(struct Scratch *s);

/* Unlock, free, close Picasso96API.library. */
void Scratch_Free(struct Scratch *s, struct PciCard *card);

#endif /* PROBE_SCRATCH_H */
