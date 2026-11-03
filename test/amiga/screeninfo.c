/*
 * screeninfo.c -- DEV:Test/ScreenInfo: what the front public screen is
 * (size, depth), and which window has the keyboard -- a test that types
 * into a window has to know that the keys go there.
 */

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;

int main(void)
{
    struct Screen *scr;
    struct Window *win;
    LONG depth;
    LONG a[7];
    LONG box[6];
    ULONG ilock;
    char title[80];
    int i, rc;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 39);
    if (IntuitionBase == NULL || GfxBase == NULL)
    {
        return 20;
    }
    rc = 5;
    scr = LockPubScreen(NULL);
    if (scr != NULL)
    {
        depth = GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH);
        a[0] = (LONG)scr->Width;
        a[1] = (LONG)scr->Height;
        a[2] = depth;
        a[3] = (LONG)(depth > 8 ? (LONG)"true colour" : (LONG)"palette");
        VPrintf((STRPTR)"screen %ldx%ld, %ld bitplanes (%s)\n", a);
        /* RC 0 = the true-colour looks will really be drawn */
        rc = depth > 8 ? 0 : 5;
        UnlockPubScreen(NULL, scr);
    }
    /* which window has the keyboard: a test that types needs to know */
    /* and its size against its smallest: a layout wider than the screen
     * shows as a minimum the window cannot have; and where it is (a window
     * dragged) */
    title[0] = 0;
    box[0] = box[1] = box[2] = box[3] = box[4] = box[5] = 0;
    ilock = LockIBase(0);
    win = IntuitionBase->ActiveWindow;
    if (win != NULL)
    {
        if (win->Title != NULL)
        {
            for (i = 0; i < 79 && win->Title[i]; i++) title[i] = (char)win->Title[i];
            title[i] = 0;
        }
        box[0] = win->Width;
        box[1] = win->Height;
        box[2] = win->MinWidth;
        box[3] = win->MinHeight;
        box[4] = win->LeftEdge;
        box[5] = win->TopEdge;
    }
    UnlockIBase(ilock);
    a[0] = (LONG)(title[0] ? title : (win != NULL ? "(untitled)" : "(none)"));
    a[1] = box[0];
    a[2] = box[1];
    a[3] = box[2];
    a[4] = box[3];
    a[5] = box[4];
    a[6] = box[5];
    VPrintf((STRPTR)"active window: %s, %ldx%ld, at least %ldx%ld, at %ld,%ld\n", a);
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);
    return rc;
}
