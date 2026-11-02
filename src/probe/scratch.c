/*
 * scratch.c -- a block of the Radeon's VRAM for a probe (scratch.h).
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <libraries/Picasso96.h>
#include <libraries/openpci.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/openpci.h>
#include <proto/Picasso96.h>

#include "scratch.h"
#include "plog.h"

struct Library *P96Base = NULL;

BOOL Scratch_Lock(struct Scratch *s)
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

void Scratch_Unlock(struct Scratch *s)
{
    if (s->bm && s->lock)
    {
        p96UnlockBitMap(s->bm, s->lock);
        s->lock = 0;
    }
    s->mem = NULL;
}

BOOL Scratch_FromDma(struct Scratch *s, struct PciCard *card, ULONG bytes)
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

BOOL Scratch_FromP96(struct Scratch *s, ULONG bytes)
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

void Scratch_Free(struct Scratch *s, struct PciCard *card)
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

