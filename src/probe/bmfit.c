/*
 * BmFit -- which bitmaps P96 places on the board, measured.
 *
 * minigl.library's texture heap is one P96 bitmap. On the A4000D P96 put a
 * 16-bit 2048 x 2048 (8 MB) bitmap on the board, but kept both a 16-bit
 * 2048 x 4096 and a 32-bit 2048 x 2048 (16 MB each) in fast RAM. Whether
 * the limit is the width, the height or the size decides how the heap may
 * grow. Each shape below is allocated three ways, never displayable:
 *   A  p96AllocBitMap without a friend;
 *   B  the same, then a 1 x 1 BltBitMap from the Workbench screen into it
 *      (does P96 move a bitmap onto the board when the blitter needs it?);
 *   C  with the Workbench screen's bitmap as the friend (its format: only
 *      the shapes whose pixel size is the Workbench's).
 * Each is locked once to see where it is, then freed. FS-UAE showed every
 * friendless bitmap off the board, 8 MB included, with 29 MB free.
 *
 *   BmFit
 */

#include <exec/types.h>
#include <utility/tagitem.h>
#include <graphics/gfx.h>
#include <libraries/Picasso96.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/Picasso96.h>

static const char ver[] = "$VER: BmFit 0.2 (02.11.2026)";

struct Library *P96Base = NULL;

struct Shape
{
    ULONG w;
    ULONG h;
    ULONG fmt;          /* RGBFB_ */
    ULONG bpp;
};

static const struct Shape shapes[] = {
    { 2048, 2048, RGBFB_R5G6B5PC, 2 },  /*  8 MB, placed before */
    { 1024, 4096, RGBFB_R5G6B5PC, 2 },  /*  8 MB, tall */
    { 4096, 1024, RGBFB_R5G6B5PC, 2 },  /*  8 MB, wide */
    { 2048, 2560, RGBFB_R5G6B5PC, 2 },  /* 10 MB */
    { 2048, 3072, RGBFB_R5G6B5PC, 2 },  /* 12 MB */
    { 2048, 4096, RGBFB_R5G6B5PC, 2 },  /* 16 MB, kept off before */
    { 4096, 2048, RGBFB_R5G6B5PC, 2 },  /* 16 MB, wide */
    { 1024, 2048, RGBFB_B8G8R8A8, 4 },  /*  8 MB at 32 bit */
    { 2048, 1024, RGBFB_B8G8R8A8, 4 },  /*  8 MB at 32 bit, wide */
    { 2048, 1280, RGBFB_B8G8R8A8, 4 },  /* 10 MB */
    { 2048, 1536, RGBFB_B8G8R8A8, 4 },  /* 12 MB */
    { 2048, 2048, RGBFB_B8G8R8A8, 4 },  /* 16 MB, kept off before */
    { 4096, 1024, RGBFB_B8G8R8A8, 4 }   /* 16 MB, wide */
};

static void Board(ULONG *free_kb, ULONG *largest_kb)
{
    struct TagItem t[3];
    ULONG fr;
    ULONG lg;

    fr = 0;
    lg = 0;
    t[0].ti_Tag = P96BD_FreeMemory;         t[0].ti_Data = (ULONG) &fr;
    t[1].ti_Tag = P96BD_LargestFreeMemory;  t[1].ti_Data = (ULONG) &lg;
    t[2].ti_Tag = TAG_DONE;                 t[2].ti_Data = 0;
    p96GetBoardDataTagList(0, t);
    *free_kb = fr >> 10;
    *largest_kb = lg >> 10;
}

/* Where bm is: 'ON' or 'off' (or '--' when there is no bitmap). */
static const char *Where(struct BitMap *bm)
{
    struct RenderInfo ri;
    LONG lock;
    ULONG on;

    if (bm == NULL)
        return "--";
    ri.Memory = NULL;
    lock = p96LockBitMap(bm, (UBYTE *) &ri, sizeof(ri));
    on = (lock != 0) ? p96GetBitMapAttr(bm, P96BMA_ISONBOARD) : 0;
    if (lock != 0)
        p96UnlockBitMap(bm, lock);
    return on ? "ON " : "off";
}

int main(void)
{
    struct Screen *scr;
    struct BitMap *sbm;
    struct BitMap *bm;
    const struct Shape *s;
    const char *a;
    const char *b;
    const char *c;
    ULONG sbpp;
    ULONG i;
    ULONG fr;
    ULONG lg;

    Printf("%s\n", (LONG) (ver + 6));
    P96Base = OpenLibrary("Picasso96API.library", 2);
    if (P96Base == NULL)
    {
        Printf("Picasso96API.library V2 not found\n");
        return RETURN_FAIL;
    }
    scr = LockPubScreen(NULL);
    if (scr == NULL)
    {
        CloseLibrary(P96Base);
        return RETURN_FAIL;
    }
    sbm = scr->RastPort.BitMap;
    sbpp = p96GetBitMapAttr(sbm, P96BMA_BYTESPERPIXEL);
    Board(&fr, &lg);
    Printf("Workbench: %ld bytes a pixel; board free %ld KB, largest %ld KB\n"
           "  A = no friend, B = no friend + a 1 x 1 blit from the screen, C = the screen as friend\n",
           (LONG) sbpp, (LONG) fr, (LONG) lg);
    for (i = 0; i < sizeof(shapes) / sizeof(shapes[0]); i++)
    {
        s = &shapes[i];
        bm = p96AllocBitMap(s->w, s->h, s->bpp * 8, BMF_MINPLANES, NULL, (RGBFTYPE) s->fmt);
        a = Where(bm);
        if (bm != NULL)
        {
            BltBitMap(sbm, 0, 0, bm, 0, 0, 1, 1, 0xc0, 0xff, NULL);
            WaitBlit();
        }
        b = Where(bm);
        if (bm != NULL)
            p96FreeBitMap(bm);
        c = "  ";
        if (s->bpp == sbpp)
        {
            bm = p96AllocBitMap(s->w, s->h, s->bpp * 8, BMF_MINPLANES, sbm, RGBFB_NONE);
            c = Where(bm);
            if (bm != NULL)
                p96FreeBitMap(bm);
        }
        Printf("%4ld x %4ld %s %5ld KB:  A %s  B %s  C %s\n", (LONG) s->w, (LONG) s->h,
               (LONG) (s->bpp == 2 ? "16 bit" : "32 bit"), (LONG) (s->w * s->h * s->bpp >> 10),
               (LONG) a, (LONG) b, (LONG) c);
    }
    UnlockPubScreen(NULL, scr);
    CloseLibrary(P96Base);
    return RETURN_OK;
}
