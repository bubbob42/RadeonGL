/*
 * inputpoke.c -- DEV:Test/InputPoke: input for unattended tests, through
 * input.device, so commodities (and Herald's idle stamp) and Intuition see
 * it like real input.
 *
 *   InputPoke                    one relative mouse move of one pixel
 *                                ("somebody is at the machine")
 *   InputPoke KEY=<n> [QUAL=<q>] a raw key (decimal) pressed and released,
 *                                qualifiers as in devices/inputevent.h
 *                                (24 = control + left alt)
 *   InputPoke X=<x> Y=<y> [CLICKS=<n>] [QUAL=<q>]
 *                                the pointer to x, y of the front screen,
 *                                then n left clicks (default 1; 2 inside
 *                                the double-click time; 0 = move only).
 *                                QUAL holds a qualifier down for the
 *                                clicks -- 1 is left shift, which is how
 *                                a shift-click is tested. HOLD=<ticks>
 *                                keeps the button down that long (1/50 s)
 *                                each time: a gadget's repeat while held.
 *                                STEPS=<n> glides there in n moves from
 *                                where the pointer is, 2/50 s apart, as a
 *                                hand would: window.class closes an open
 *                                tooltip only after more than two mouse
 *                                moves, so one jump leaves the last
 *                                gadget's bubble standing.
 *   InputPoke [X=<x> Y=<y>] MENU=<m>
 *                                the MENU button instead of clicks, so a
 *                                test can grab an open menu: the pointer
 *                                to x, y (when given), then 1 presses the
 *                                button and KEEPS it down, 2 only moves
 *                                with it held (another menu, an item), 0
 *                                lets it go. Let go over the bar, away
 *                                from every menu, and nothing is chosen.
 *   InputPoke [X=<x> Y=<y>] SELECT=<m> [STEPS=<n>]
 *                                the same with the left button, for a drag
 *                                (a knob turned): 1 the pointer there, the
 *                                button pressed and KEPT down; 2 glides
 *                                there with it held (STEPS as above); 0
 *                                glides there, then lets it go.
 *   InputPoke WIN=<title> [X=<x> Y=<y> ...] [KEY=<n> ...]
 *                                the same, aimed at the front screen's
 *                                window whose title holds <title>: X and Y
 *                                count from its top left corner (a negative
 *                                Y from its bottom edge, -1 the last line:
 *                                the Installer moves its window from page
 *                                to page, its buttons stay at the bottom),
 *                                and a KEY
 *                                activates it first. Alone it reports the
 *                                window's box. RC 5 when there is no such
 *                                window (nothing sent): a script asks with
 *                                it whether a program's window is up.
 *   ... UNLESS=<title>           nothing at all while such a window is
 *                                open (RC 5): a clicker aimed at one
 *                                program's pages must not land in another
 *                                program's window that opened over them.
 */

#include <exec/types.h>
#include <exec/io.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <dos/dos.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>

#define TEMPLATE "KEY/K/N,QUAL/K/N,X/K/N,Y/K/N,CLICKS/K/N,MENU/K/N,HOLD/K/N,STEPS/K/N,WIN/K,UNLESS/K,SELECT/K/N"

struct IntuitionBase *IntuitionBase = NULL;

static struct IOStdReq *io;

/* What to hold down with a click; QUAL= sets it. */
static UWORD clickQual = 0;

static void Send(struct InputEvent *ie)
{
    io->io_Command = IND_WRITEEVENT;
    io->io_Data = (APTR)ie;
    io->io_Length = sizeof(*ie);
    DoIO((struct IORequest *)io);
}

static void Clear(struct InputEvent *ie)
{
    ULONG i;

    for (i = 0; i < sizeof(*ie); i++) ((UBYTE *)ie)[i] = 0;
}

static void Button(UWORD code)
{
    struct InputEvent ie;

    Clear(&ie);
    ie.ie_Class = IECLASS_RAWMOUSE;
    ie.ie_Code = code;
    ie.ie_Qualifier = (UWORD)(IEQUALIFIER_RELATIVEMOUSE | clickQual);
    Send(&ie);
}

/* Does s hold part? */
static int Holds(CONST_STRPTR s, CONST_STRPTR part)
{
    LONG i, j;

    for (i = 0; s[i]; i++)
    {
        for (j = 0; part[j] && s[i + j] == part[j]; j++) ;
        if (!part[j]) return 1;
    }
    return 0;
}

/* The front screen's first window whose title holds part, its box into
 * box[4] (left, top, width, height); NULL when there is none. */
static struct Window *FindWin(CONST_STRPTR part, LONG *box)
{
    struct Window *w;
    ULONG ilock;

    ilock = LockIBase(0);
    for (w = IntuitionBase->FirstScreen->FirstWindow; w != NULL; w = w->NextWindow)
    {
        if (w->Title != NULL && Holds(w->Title, part))
        {
            box[0] = w->LeftEdge;
            box[1] = w->TopEdge;
            box[2] = w->Width;
            box[3] = w->Height;
            break;
        }
    }
    UnlockIBase(ilock);
    return w;
}

/* The pointer to x, y of the front screen, qual held. */
static void MoveTo(LONG x, LONG y, UWORD qual)
{
    struct InputEvent ie;
    struct IEPointerPixel pp;

    Clear(&ie);
    pp.iepp_Screen = IntuitionBase->FirstScreen;
    pp.iepp_Position.X = (WORD)x;
    pp.iepp_Position.Y = (WORD)y;
    ie.ie_Class = IECLASS_NEWPOINTERPOS;
    ie.ie_SubClass = IESUBCLASS_PIXEL;
    ie.ie_EventAddress = (APTR)&pp;
    ie.ie_Qualifier = qual;
    Send(&ie);
}

/* The pointer to x, y in steps moves, 2/50 s apart, qual held. */
static void Glide(LONG x, LONG y, LONG steps, UWORD qual)
{
    LONG x0 = IntuitionBase->FirstScreen->MouseX, y0 = IntuitionBase->FirstScreen->MouseY, i;

    for (i = 1; i < steps; i++)
    {
        MoveTo(x0 + (x - x0) * i / steps, y0 + (y - y0) * i / steps, qual);
        Delay(2);
    }
    MoveTo(x, y, qual);
}

int main(void)
{
    struct MsgPort *port;
    struct InputEvent ie;
    struct RDArgs *rda;
    struct Window *win = NULL;
    LONG args[11];
    LONG box[4];
    LONG clicks, menu, hold, steps, sel;
    LONG x, y;
    LONG i;
    int rc = 0;

    for (i = 0; i < 11; i++) args[i] = 0;
    box[0] = box[1] = box[2] = box[3] = 0;
    rda = ReadArgs((STRPTR)TEMPLATE, args, NULL);
    if (rda == NULL)
    {
        PrintFault(IoErr(), (STRPTR)"InputPoke");
        return 10;
    }
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 36);
    port = CreateMsgPort();
    io = port != NULL ? (struct IOStdReq *)CreateIORequest(port, sizeof(struct IOStdReq)) : NULL;
    if (io != NULL && IntuitionBase != NULL &&
        OpenDevice((STRPTR)"input.device", 0, (struct IORequest *)io, 0) == 0)
    {
        Clear(&ie);
        if (args[9] != 0 && FindWin((CONST_STRPTR)args[9], box) != NULL)
        {
            rc = 5;             /* UNLESS: that window is up */
        }
        else if (args[8] != 0 && (win = FindWin((CONST_STRPTR)args[8], box)) == NULL)
        {
            rc = 5;             /* WIN: no such window */
        }
        else if (args[8] != 0 && args[0] == 0 && args[2] == 0)
        {
            VPrintf((STRPTR)"window at %ld,%ld, %ldx%ld\n", box);
        }
        else if (args[10] != 0)
        {
            sel = *(LONG *)args[10];
            clickQual = (UWORD)(sel == 1 ? 0 : IEQUALIFIER_LEFTBUTTON);
            if (args[2] != 0 && args[3] != 0)
            {
                Glide(*(LONG *)args[2] + box[0], *(LONG *)args[3] + box[1], args[7] != 0 ? *(LONG *)args[7] : 1, clickQual);
                Delay(5);
            }
            if (sel == 1) Button(IECODE_LBUTTON);
            else if (sel == 0) Button(IECODE_LBUTTON | IECODE_UP_PREFIX);
        }
        else if (args[5] != 0)
        {
            menu = *(LONG *)args[5];
            if (args[2] != 0 && args[3] != 0)
            {
                MoveTo(*(LONG *)args[2], *(LONG *)args[3],
                       (UWORD)(menu == 1 ? 0 : IEQUALIFIER_RBUTTON));
                Delay(5);
            }
            clickQual = (UWORD)(menu == 1 ? 0 : IEQUALIFIER_RBUTTON);
            if (menu == 1) Button(IECODE_RBUTTON);
            else if (menu == 0) Button(IECODE_RBUTTON | IECODE_UP_PREFIX);
        }
        else if (args[0] != 0)
        {
            if (win != NULL)
            {
                ActivateWindow(win);
                Delay(10);
            }
            ie.ie_Class = IECLASS_RAWKEY;
            ie.ie_Code = (UWORD)*(LONG *)args[0];
            ie.ie_Qualifier = (UWORD)(args[1] != 0 ? *(LONG *)args[1] : 0);
            Send(&ie);
            ie.ie_Code |= IECODE_UP_PREFIX;
            Send(&ie);
        }
        else if (args[2] != 0 && args[3] != 0)
        {
            x = *(LONG *)args[2] + box[0];     /* box: 0, or WIN's */
            y = *(LONG *)args[3] + box[1];
            if (box[3] != 0 && *(LONG *)args[3] < 0)    /* WIN's bottom edge */
                y = box[1] + box[3] + *(LONG *)args[3];
            steps = args[7] != 0 ? *(LONG *)args[7] : 1;
            Glide(x, y, steps, 0);
            Delay(5);
            clickQual = (UWORD)(args[1] != 0 ? *(LONG *)args[1] : 0);
            clicks = args[4] != 0 ? *(LONG *)args[4] : 1;
            hold = args[6] != 0 ? *(LONG *)args[6] : 0;
            for (i = 0; i < clicks; i++)
            {
                Button(IECODE_LBUTTON);
                if (hold > 0) Delay(hold);
                Button(IECODE_LBUTTON | IECODE_UP_PREFIX);
                Delay(4);
            }
        }
        else
        {
            ie.ie_Class = IECLASS_RAWMOUSE;
            ie.ie_Code = IECODE_NOBUTTON;
            ie.ie_Qualifier = IEQUALIFIER_RELATIVEMOUSE;
            ie.ie_X = 1;
            Send(&ie);
        }
        CloseDevice((struct IORequest *)io);
    }
    if (io != NULL) DeleteIORequest((struct IORequest *)io);
    if (port != NULL) DeleteMsgPort(port);
    if (IntuitionBase != NULL) CloseLibrary((struct Library *)IntuitionBase);
    FreeArgs(rda);
    return rc;
}
