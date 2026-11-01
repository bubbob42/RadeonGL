/*
 * grab.c -- DEV:Test/Grab: read a region of the Workbench screen and write
 * it as a binary PPM (RGB), so a host can LOOK at what an unattended test
 * drew.   Grab X/N/A,Y/N/A,W/N/A,H/N/A,TO/A,SCREEN/K   (negative X/Y: from
 * the right/bottom edge; SCREEN= another public screen than Workbench,
 * BigScreen's HERALDBIG). Pen screens go through GetRGB32; true colour
 * through Picasso96API.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/Picasso96.h>
#include <libraries/Picasso96.h>

struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;
struct Library *P96Base = NULL;

static LONG args[6];

int main(void)
{
    struct RDArgs *rda;
    struct Screen *scr;
    BPTR fh;
    ULONG *pal;
    UBYTE *row;
    LONG x0, y0, w, h, x, y, ncol;
    ULONG c;
    BOOL tc;
    LONG a[2];
    LONG rc;

    rc = 20;
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 39);
    P96Base = OpenLibrary((STRPTR)"Picasso96API.library", 2);
    rda = ReadArgs((STRPTR)"X/N/A,Y/N/A,W/N/A,H/N/A,TO/A,SCREEN/K", args, NULL);
    if (rda == NULL || IntuitionBase == NULL || GfxBase == NULL) goto done;
    scr = LockPubScreen(args[5] != 0 ? (STRPTR)args[5] : (STRPTR)"Workbench");
    if (scr == NULL) goto done;
    x0 = *(LONG *)args[0]; y0 = *(LONG *)args[1];
    w = *(LONG *)args[2]; h = *(LONG *)args[3];
    if (x0 < 0) x0 += scr->Width;
    if (y0 < 0) y0 += scr->Height;
    /* never past the screen: one script grabs "all of it" on a 640x256
     * PAL screen and a 640x480 RTG one alike */
    if (x0 + w > scr->Width) w = scr->Width - x0;
    if (y0 + h > scr->Height) h = scr->Height - y0;
    if (w <= 0 || h <= 0)
    {
        UnlockPubScreen(NULL, scr);
        goto done;
    }
    tc = (BOOL)(P96Base != NULL && p96GetBitMapAttr(scr->RastPort.BitMap, P96BMA_ISP96) &&
                p96GetBitMapAttr(scr->RastPort.BitMap, P96BMA_RGBFORMAT) != RGBFB_CLUT);
    ncol = 1L << GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH);
    if (ncol > 256) ncol = 256;
    pal = (ULONG *)AllocVec(256 * 3 * 4, MEMF_ANY | MEMF_CLEAR);
    row = (UBYTE *)AllocVec(w * 3, MEMF_ANY);
    fh = Open((STRPTR)args[4], MODE_NEWFILE);
    if (pal != NULL && row != NULL && fh != 0)
    {
        if (!tc) GetRGB32(scr->ViewPort.ColorMap, 0, ncol, pal);
        a[0] = w; a[1] = h;
        VFPrintf(fh, (STRPTR)"P6\n%ld %ld\n255\n", a);
        Flush(fh);
        for (y = 0; y < h; y++)
        {
            for (x = 0; x < w; x++)
            {
                if (tc)
                {
                    c = p96ReadPixel(&scr->RastPort, (UWORD)(x0 + x), (UWORD)(y0 + y));
                    row[x * 3] = (UBYTE)(c >> 16);
                    row[x * 3 + 1] = (UBYTE)(c >> 8);
                    row[x * 3 + 2] = (UBYTE)c;
                }
                else
                {
                    c = (ULONG)ReadPixel(&scr->RastPort, x0 + x, y0 + y);
                    if (c >= 256) c = 0;
                    row[x * 3] = (UBYTE)(pal[c * 3] >> 24);
                    row[x * 3 + 1] = (UBYTE)(pal[c * 3 + 1] >> 24);
                    row[x * 3 + 2] = (UBYTE)(pal[c * 3 + 2] >> 24);
                }
            }
            FWrite(fh, row, 1, w * 3);
        }
        rc = 0;
    }
    if (fh != 0) Close(fh);
    FreeVec(row);
    FreeVec(pal);
    UnlockPubScreen(NULL, scr);
done:
    if (rda != NULL) FreeArgs(rda);
    CloseLibrary(P96Base);
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);
    return (int)rc;
}
