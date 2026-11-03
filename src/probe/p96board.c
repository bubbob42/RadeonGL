/*
 * p96board.c -- see p96board.h.
 */

#include <exec/types.h>
#include <utility/tagitem.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/modeid.h>
#include <libraries/Picasso96.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/Picasso96.h>

#include "p96board.h"
#include "plog.h"

struct Library *P96Base = NULL;

#define ROW_BYTES 1024UL

static const char *const fmt_Names[] = {
    "NONE (planar)", "CLUT", "R8G8B8", "B8G8R8", "R5G6B5PC", "R5G5B5PC",
    "A8R8G8B8", "A8B8G8R8", "R8G8B8A8", "B8G8R8A8", "R5G6B5", "R5G5B5",
    "B5G6R5PC", "B5G5R5PC", "YUV422CGX", "YUV411", "YUV411PC", "YUV422",
    "YUV422PC", "YUV422PA", "YUV422PAPC"
};

CONST_STRPTR P96Board_FormatName(ULONG rgbfb)
{
    if (rgbfb < sizeof(fmt_Names) / sizeof(fmt_Names[0]))
        return (CONST_STRPTR)fmt_Names[rgbfb];
    return (CONST_STRPTR)"(unknown)";
}

static int Lower(int ch)
{
    return (ch >= 'A' && ch <= 'Z') ? ch - 'A' + 'a' : ch;
}

/* Does s contain "radeon", in any case? */
static BOOL SaysRadeon(CONST_STRPTR s)
{
    static const char want[] = "radeon";
    ULONG i;
    ULONG k;

    if (s == NULL)
        return FALSE;
    for (i = 0; s[i] != 0; i++)
    {
        for (k = 0; want[k] != 0 && Lower((UBYTE)s[i + k]) == want[k]; k++)
            ;
        if (want[k] == 0)
            return TRUE;
    }
    return FALSE;
}

BOOL P96Board_IsRadeon(const struct P96Board *b)
{
    return (BOOL)(SaysRadeon(b->chipName) || SaysRadeon(b->boardName));
}

LONG P96Board_LockBitMap(struct BitMap *bm, struct RenderInfo *ri)
{
    LONG lock;

    Log_Hold(TRUE);
    lock = p96LockBitMap(bm, (UBYTE *)ri, sizeof(*ri));
    if (lock == 0)
        Log_Hold(FALSE);
    return lock;
}

void P96Board_UnlockBitMap(struct BitMap *bm, LONG lock)
{
    p96UnlockBitMap(bm, lock);
    Log_Hold(FALSE);
}

BOOL P96Board_Lock(struct P96Board *b)
{
    struct RenderInfo ri;

    if (b->bm == NULL || b->lock != 0)
        return FALSE;
    b->lock = P96Board_LockBitMap(b->bm, &ri);
    if (b->lock == 0)
        return FALSE;
    b->mem = (UBYTE *)ri.Memory;
    if (b->mem == NULL || !p96GetBitMapAttr(b->bm, P96BMA_ISONBOARD))
    {
        /* off the board now (P96 needed the video RAM): nothing to use */
        P96Board_Unlock(b);
        return FALSE;
    }
    b->bpr = (ULONG)ri.BytesPerRow;
    b->bpp = p96GetBitMapAttr(b->bm, P96BMA_BYTESPERPIXEL);
    b->format = (ULONG)ri.RGBFormat;
    if (b->memIoBase == NULL)
    {
        b->memBase = (APTR)p96GetBitMapAttr(b->bm, P96BMA_BOARDMEMBASE);
        b->ioBase = (APTR)p96GetBitMapAttr(b->bm, P96BMA_BOARDIOBASE);
        b->memIoBase = (APTR)p96GetBitMapAttr(b->bm, P96BMA_BOARDMEMIOBASE);
    }
    if (b->locks != 0 && b->mem != b->lastMem)
        b->moves++;
    b->lastMem = b->mem;
    b->locks++;
    return TRUE;
}

void P96Board_Unlock(struct P96Board *b)
{
    if (b->bm != NULL && b->lock != 0)
    {
        P96Board_UnlockBitMap(b->bm, b->lock);
        b->lock = 0;
    }
    b->mem = NULL;
}

/* p96GetBoardDataTagList, one board. */
static void BoardData(struct P96Board *b)
{
    struct TagItem tags[8];

    tags[0].ti_Tag = P96BD_BoardName;
    tags[0].ti_Data = (ULONG)&b->boardName;
    tags[1].ti_Tag = P96BD_ChipName;
    tags[1].ti_Data = (ULONG)&b->chipName;
    tags[2].ti_Tag = P96BD_TotalMemory;
    tags[2].ti_Data = (ULONG)&b->totalMem;
    tags[3].ti_Tag = P96BD_FreeMemory;
    tags[3].ti_Data = (ULONG)&b->freeMem;
    tags[4].ti_Tag = P96BD_LargestFreeMemory;
    tags[4].ti_Data = (ULONG)&b->largestFree;
    tags[5].ti_Tag = P96BD_RGBFormats;
    tags[5].ti_Data = (ULONG)&b->formats;
    tags[6].ti_Tag = TAG_DONE;
    tags[6].ti_Data = 0;
    p96GetBoardDataTagList(b->board, tags);
}

BOOL P96Board_Open(struct P96Board *b, ULONG bytes)
{
    static struct P96Board zero;
    LONG id;
    ULONG depth;
    ULONG sbpp;

    *b = zero;
    P96Base = OpenLibrary("Picasso96API.library", 2);
    if (P96Base == NULL)
    {
        Log_Printf("Picasso96API.library V2 not found: no RTG board\n");
        return FALSE;
    }
    b->scr = LockPubScreen(NULL);
    if (b->scr == NULL)
    {
        Log_Printf("no public screen\n");
        return FALSE;
    }
    b->scrbm = b->scr->RastPort.BitMap;
    id = GetVPModeID(&b->scr->ViewPort);
    b->modeId = (ULONG)id;
    if (id == INVALID_ID || !p96GetModeIDAttr(b->modeId, P96IDA_ISP96)
        || !p96GetBitMapAttr(b->scrbm, P96BMA_ISP96))
    {
        Log_Printf("the Workbench screen (mode 0x%08lx) is not an RTG screen\n", b->modeId);
        return FALSE;
    }
    b->board = p96GetModeIDAttr(b->modeId, P96IDA_BOARDNUMBER);
    b->scrFormat = p96GetBitMapAttr(b->scrbm, P96BMA_RGBFORMAT);
    b->compatFormats = p96GetModeIDAttr(b->modeId, P96IDA_COMPATIBLEFORMATS);
    BoardData(b);
    if (bytes == 0)
        return TRUE;

    /* the block: a friend of the screen's bitmap gets the screen's format
     * and lands on the board when there is room. Rows of ROW_BYTES, so the
     * size does not depend on the pixel size (a 16-bit screen gives 512
     * pixels a row). Not BMF_DISPLAYABLE: the CRTC never shows it. */
    sbpp = p96GetBitMapAttr(b->scrbm, P96BMA_BYTESPERPIXEL);
    if (sbpp == 0 || ROW_BYTES % sbpp != 0)
    {
        Log_Printf("the screen has %lu bytes a pixel: no block\n", sbpp);
        return FALSE;
    }
    depth = GetBitMapAttr(b->scrbm, BMA_DEPTH);
    b->bytes = bytes;
    b->width = ROW_BYTES / sbpp;
    b->rows = (bytes + ROW_BYTES - 1) / ROW_BYTES;
    b->bm = AllocBitMap(b->width, b->rows, depth, BMF_MINPLANES, b->scrbm);
    if (b->bm == NULL || !p96GetBitMapAttr(b->bm, P96BMA_ISP96))
    {
        Log_Printf("AllocBitMap(%lu x %lu, depth %lu, friend: the screen): %s\n",
                   b->width, b->rows, depth,
                   b->bm == NULL ? (CONST_STRPTR)"NULL" : (CONST_STRPTR)"not an RTG bitmap");
        return FALSE;
    }
    if (!P96Board_Lock(b))
    {
        Log_Printf("the block (%lu x %lu) could not be locked on the board\n", b->width, b->rows);
        return FALSE;
    }
    P96Board_Unlock(b);
    if (b->bpr * b->rows < bytes)
    {
        Log_Printf("the block has %lu bytes a row x %lu rows: less than %lu bytes\n",
                   b->bpr, b->rows, bytes);
        return FALSE;
    }
    return TRUE;
}

void P96Board_Close(struct P96Board *b)
{
    P96Board_Unlock(b);
    if (b->bm != NULL)
    {
        FreeBitMap(b->bm);
        b->bm = NULL;
    }
    if (b->scr != NULL)
    {
        UnlockPubScreen(NULL, b->scr);
        b->scr = NULL;
    }
    if (P96Base != NULL)
    {
        CloseLibrary(P96Base);
        P96Base = NULL;
    }
}

void P96Board_Report(const struct P96Board *b)
{
    Log_Section("The board through Picasso96API");
    Log_Printf("Picasso96API.library %ld.%ld\n",
               (LONG)P96Base->lib_Version, (LONG)P96Base->lib_Revision);
    Log_Printf("Workbench mode 0x%08lx on board %lu: board \"%s\", chip \"%s\"%s\n",
               b->modeId, b->board,
               b->boardName ? b->boardName : (STRPTR)"?", b->chipName ? b->chipName : (STRPTR)"?",
               P96Board_IsRadeon(b) ? (CONST_STRPTR)" (a Radeon)" : (CONST_STRPTR)"");
    Log_Printf("video RAM %lu KB, free %lu KB, largest free %lu KB (the last two move)\n",
               b->totalMem >> 10, b->freeMem >> 10, b->largestFree >> 10);
    Log_Printf("screen format %s; board formats 0x%08lx; compatible with the screen 0x%08lx\n",
               P96Board_FormatName(b->scrFormat), b->formats, b->compatFormats);
    if (b->bm == NULL)
        return;
    Log_Printf("block: %lu x %lu pixels, %lu bytes a row, %lu a pixel, %s; last at 0x%08lx\n",
               b->width, b->rows, b->bpr, b->bpp, P96Board_FormatName(b->format),
               (ULONG)b->lastMem);
    Log_Printf("bases (p96GetBitMapAttr inside a lock):\n"
               "  BOARDMEMBASE   0x%08lx\n"
               "  BOARDIOBASE    0x%08lx  (BAR1 on a Radeon)\n"
               "  BOARDMEMIOBASE 0x%08lx  (BAR2: the registers)\n",
               (ULONG)b->memBase, (ULONG)b->ioBase, (ULONG)b->memIoBase);
    Log_Printf("locks %lu, the address moved %lu times\n", b->locks, b->moves);
}
