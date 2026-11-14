/*
 * rstatgui.c -- RadeonStat's window (rstatgui.h), 2026-11-08.
 *
 *   +- Engine -----------+ +- Card memory --------------------+
 *   | Busy    [##  37 %] | | Used          [ 16.5 of 64.0 MB] |
 *   |                    | | Free          47.5 MB            |
 *   | 3D      [#   31 %] | | Largest block 47.5 MB            |
 *   |                    | +- Display ------------------------+
 *   | 2D      [     2 %] | | Screen        Workbench Screen   |
 *   |                    | | Mode          1920 x 1080, 50 Hz |
 *   | Command [#   35 %] | | Pixel clock   123.7 MHz DVI may..|
 *   | Queue   1536 dwords| |                                  |
 *   +--------------------+ +----------------------------------+
 *   +- Busy history ------------------------------------------+
 *   |                                     ..||.|||||.||||||||| |
 *   +---------------------------------------------------------+
 *   Interval [1 s]                                    [Pause]
 *
 * The engine's rows spread over their group's height (spacers between
 * them); a taller window gives the room to the history graph. The live
 * readouts are rstatview.c gadgets: the window's task draws their values
 * under a tried layer lock, never through SetGadgetAttrs().
 *
 * A timer.device request paces the sampling (50 bursts a second, as the
 * Shell lines); the window answers between ticks. Pause sends no more
 * requests, so no lock is taken while paused.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <devices/timer.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/chooser.h>
#include <gadgets/space.h>
#include <images/label.h>
#include <images/bevel.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/chooser.h>
#include <proto/space.h>
#include <proto/label.h>

#include <clib/alib_protos.h>

#include "rstat.h"
#include "rstatgui.h"
#include "rstatview.h"

struct Library *WindowBase = NULL;
struct Library *LayoutBase = NULL;
struct Library *ButtonBase = NULL;
struct Library *ChooserBase = NULL;
struct Library *SpaceBase = NULL;
struct Library *LabelBase = NULL;
struct Library *UtilityBase = NULL;

#define CLASS_VERSION   44

#define GID_INTERVAL    1
#define GID_PAUSE       2

#define MENU_ABOUT      1
#define MENU_QUIT       2

/* the readouts */
#define V_BUSY      0
#define V_3D        1
#define V_2D        2
#define V_CP        3
#define V_QUEUE     4
#define V_USED      5
#define V_FREE      6
#define V_LARGEST   7
#define V_SCREEN    8
#define V_MODE      9
#define V_CLOCK     10
#define V_GRAPH     11
#define V_N         12

#define DVI_LIMIT   1100    /* pixel clock in 1/10 MHz above which DVI drops out (docs/radeon-card-dvi-clock.md) */

static STRPTR intervalLabels[] = { (STRPTR) "0.5 s", (STRPTR) "1 s", (STRPTR) "2 s", (STRPTR) "5 s", NULL };
static const UBYTE intervalTenths[] = { 5, 10, 20, 50 };

static struct NewMenu menus[] =
{
    { NM_TITLE, (STRPTR) "Project",  NULL,          0, 0, NULL },
    { NM_ITEM,  (STRPTR) "About...", NULL,          0, 0, (APTR) MENU_ABOUT },
    { NM_ITEM,  NM_BARLABEL,         NULL,          0, 0, NULL },
    { NM_ITEM,  (STRPTR) "Quit",     (STRPTR) "Q",  0, 0, (APTR) MENU_QUIT },
    { NM_END,   NULL,                NULL,          0, 0, NULL }
};

struct Gui
{
    struct RStat *s;
    CONST_STRPTR version;
    struct Screen *scr;
    struct DrawInfo *dri;
    Class *viewClass;
    Object *win;
    struct Window *w;           /* while open */
    Object *view[V_N];
    Object *pause;
    struct MsgPort *tport;
    struct timerequest *treq;
    BOOL tdev, tpending, paused;
    UWORD choice;               /* intervalLabels index */
    ULONG perInterval, ticks;
    struct RStatFront front;
    struct RStatMode mode;
};

/* --- text ----------------------------------------------------------------- */

static char *PutS(char *p, CONST_STRPTR s)
{
    while (*s)
        *p++ = *s++;
    *p = 0;
    return p;
}

static char *PutU(char *p, ULONG v)
{
    char t[12];
    int n;

    n = 0;
    do
    {
        t[n++] = (char) ('0' + v % 10);
        v /= 10;
    } while (v != 0);
    while (n > 0)
        *p++ = t[--n];
    *p = 0;
    return p;
}

static char *PutTenths(char *p, ULONG v)
{
    p = PutU(p, v / 10);
    *p++ = '.';
    *p++ = (char) ('0' + v % 10);
    *p = 0;
    return p;
}

/* bytes as MB with one decimal */
static char *PutMB(char *p, ULONG bytes)
{
    return PutTenths(p, (bytes / 1024UL) * 10UL / 1024UL);
}

/* --- building the window ---------------------------------------------------- */

#define MAXTAGS 64

/* A layout's tag list and the objects it will own. Any object that could
 * not be made marks the build bad: then the objects made are disposed of
 * here, and no layout is made (a layout that fails disposes its children
 * itself). */
struct Build
{
    struct TagItem t[MAXTAGS];
    Object *own[MAXTAGS];
    WORD n, no;
    BOOL bad;
};

static void B_Init(struct Build *b)
{
    b->n = 0;
    b->no = 0;
    b->bad = FALSE;
}

static void B_Tag(struct Build *b, ULONG tag, ULONG data)
{
    if (b->n >= MAXTAGS - 1)
    {
        b->bad = TRUE;
        return;
    }
    b->t[b->n].ti_Tag = tag;
    b->t[b->n].ti_Data = data;
    b->n++;
}

static void B_Obj(struct Build *b, ULONG tag, Object *o)
{
    if (o == NULL || b->no >= MAXTAGS)
    {
        b->bad = TRUE;
        if (o != NULL)
            DisposeObject(o);
        return;
    }
    b->own[b->no++] = o;
    B_Tag(b, tag, (ULONG) o);
}

static Object *B_Layout(struct Build *b)
{
    WORD i;

    if (b->bad)
    {
        for (i = 0; i < b->no; i++)
            DisposeObject(b->own[i]);
        return NULL;
    }
    b->t[b->n].ti_Tag = TAG_DONE;
    b->t[b->n].ti_Data = 0;
    return NewObjectA(LAYOUT_GetClass(), NULL, b->t);
}

static Object *Label(CONST_STRPTR text)
{
    return NewObject(LABEL_GetClass(), NULL, LABEL_Text, (ULONG) text, TAG_DONE);
}

static Object *Spacer(void)
{
    return NewObject(SPACE_GetClass(), NULL, TAG_DONE);
}

static Object *View(struct Gui *g, ULONG kind, ULONG chars)
{
    return NewObject(g->viewClass, NULL,
                     RSV_Kind, kind,
                     RSV_Chars, chars,
                     RSV_Font, (ULONG) g->dri->dri_Font,
                     TAG_DONE);
}

/* a titled group of labelled rows, top-aligned */
static void B_Group(struct Build *b, CONST_STRPTR title)
{
    B_Init(b);
    B_Tag(b, LAYOUT_Orientation, LAYOUT_ORIENT_VERT);
    B_Tag(b, LAYOUT_BevelStyle, BVS_GROUP);
    B_Tag(b, LAYOUT_Label, (ULONG) title);
    B_Tag(b, LAYOUT_SpaceOuter, TRUE);
    B_Tag(b, LAYOUT_VertAlignment, LALIGN_TOP);
}

/* a labelled row that keeps its own height */
static void B_Row(struct Build *b, Object *o, CONST_STRPTR label)
{
    B_Obj(b, LAYOUT_AddChild, o);
    B_Obj(b, CHILD_Label, Label(label));
    B_Tag(b, CHILD_WeightedHeight, 0);
}

static Object *EngineGroup(struct Gui *g)
{
    struct Build b;

    B_Group(&b, (CONST_STRPTR) "Engine");
    B_Row(&b, g->view[V_BUSY] = View(g, RSVK_BAR, 12), (CONST_STRPTR) "Busy");
    B_Obj(&b, LAYOUT_AddChild, Spacer());
    B_Row(&b, g->view[V_3D] = View(g, RSVK_BAR, 12), (CONST_STRPTR) "3D");
    B_Obj(&b, LAYOUT_AddChild, Spacer());
    B_Row(&b, g->view[V_2D] = View(g, RSVK_BAR, 12), (CONST_STRPTR) "2D");
    B_Obj(&b, LAYOUT_AddChild, Spacer());
    B_Row(&b, g->view[V_CP] = View(g, RSVK_BAR, 12), (CONST_STRPTR) "Command");
    B_Obj(&b, LAYOUT_AddChild, Spacer());
    B_Row(&b, g->view[V_QUEUE] = View(g, RSVK_TEXT, 12), (CONST_STRPTR) "Queue");
    return B_Layout(&b);
}

static Object *MemoryGroup(struct Gui *g)
{
    struct Build b;

    B_Group(&b, (CONST_STRPTR) "Card memory");
    B_Row(&b, g->view[V_USED] = View(g, RSVK_BAR, 17), (CONST_STRPTR) "Used");
    B_Row(&b, g->view[V_FREE] = View(g, RSVK_TEXT, 10), (CONST_STRPTR) "Free");
    B_Row(&b, g->view[V_LARGEST] = View(g, RSVK_TEXT, 10), (CONST_STRPTR) "Largest block");
    return B_Layout(&b);
}

static Object *DisplayGroup(struct Gui *g)
{
    struct Build b;

    B_Group(&b, (CONST_STRPTR) "Display");
    B_Row(&b, g->view[V_SCREEN] = View(g, RSVK_TEXT, 20), (CONST_STRPTR) "Screen");
    B_Row(&b, g->view[V_MODE] = View(g, RSVK_TEXT, 20), (CONST_STRPTR) "Mode");
    B_Row(&b, g->view[V_CLOCK] = View(g, RSVK_TEXT, 27), (CONST_STRPTR) "Pixel clock");
    return B_Layout(&b);
}

static Object *RightColumn(struct Gui *g)
{
    struct Build b;
    Object *mem, *disp;

    mem = MemoryGroup(g);
    disp = DisplayGroup(g);
    /* one label column across both groups */
    if (mem != NULL && disp != NULL)
    {
        SetAttrs(mem, LAYOUT_AlignLabels, (ULONG) disp, TAG_DONE);
        SetAttrs(disp, LAYOUT_AlignLabels, (ULONG) mem, TAG_DONE);
    }
    B_Init(&b);
    B_Tag(&b, LAYOUT_Orientation, LAYOUT_ORIENT_VERT);
    B_Tag(&b, LAYOUT_VertAlignment, LALIGN_TOP);
    B_Obj(&b, LAYOUT_AddChild, mem);
    B_Tag(&b, CHILD_WeightedHeight, 0);
    B_Obj(&b, LAYOUT_AddChild, disp);
    B_Tag(&b, CHILD_WeightedHeight, 0);
    return B_Layout(&b);
}

static Object *TopRow(struct Gui *g)
{
    struct Build b;

    B_Init(&b);
    B_Tag(&b, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ);
    B_Obj(&b, LAYOUT_AddChild, EngineGroup(g));
    B_Obj(&b, LAYOUT_AddChild, RightColumn(g));
    return B_Layout(&b);
}

static Object *HistoryGroup(struct Gui *g)
{
    struct Build b;

    B_Init(&b);
    B_Tag(&b, LAYOUT_Orientation, LAYOUT_ORIENT_VERT);
    B_Tag(&b, LAYOUT_BevelStyle, BVS_GROUP);
    B_Tag(&b, LAYOUT_Label, (ULONG) "Busy history");
    B_Tag(&b, LAYOUT_SpaceOuter, TRUE);
    B_Obj(&b, LAYOUT_AddChild, g->view[V_GRAPH] = View(g, RSVK_GRAPH, 24));
    return B_Layout(&b);
}

static Object *BottomRow(struct Gui *g)
{
    struct Build b;

    B_Init(&b);
    B_Tag(&b, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ);
    B_Tag(&b, LAYOUT_VertAlignment, LALIGN_CENTER);
    B_Obj(&b, LAYOUT_AddImage, Label((CONST_STRPTR) "Interval"));
    B_Tag(&b, CHILD_WeightedWidth, 0);
    B_Obj(&b, LAYOUT_AddChild, NewObject(CHOOSER_GetClass(), NULL,
                                         GA_ID, GID_INTERVAL,
                                         GA_RelVerify, TRUE,
                                         CHOOSER_PopUp, TRUE,
                                         CHOOSER_LabelArray, (ULONG) intervalLabels,
                                         CHOOSER_Selected, (ULONG) g->choice,
                                         TAG_DONE));
    B_Tag(&b, CHILD_WeightedWidth, 0);
    B_Obj(&b, LAYOUT_AddChild, Spacer());
    B_Obj(&b, LAYOUT_AddChild, g->pause = NewObject(BUTTON_GetClass(), NULL,
                                                    GA_ID, GID_PAUSE,
                                                    GA_RelVerify, TRUE,
                                                    GA_Text, (ULONG) "_Pause",
                                                    BUTTON_PushButton, TRUE,
                                                    TAG_DONE));
    B_Tag(&b, CHILD_WeightedWidth, 0);
    return B_Layout(&b);
}

static Object *Root(struct Gui *g)
{
    struct Build b;

    B_Init(&b);
    B_Tag(&b, LAYOUT_Orientation, LAYOUT_ORIENT_VERT);
    B_Tag(&b, LAYOUT_SpaceOuter, TRUE);
    B_Tag(&b, LAYOUT_DeferLayout, TRUE);
    B_Obj(&b, LAYOUT_AddChild, TopRow(g));
    B_Tag(&b, CHILD_WeightedHeight, 0);
    B_Obj(&b, LAYOUT_AddChild, HistoryGroup(g));
    B_Obj(&b, LAYOUT_AddChild, BottomRow(g));
    B_Tag(&b, CHILD_WeightedHeight, 0);
    return B_Layout(&b);
}

static BOOL MakeWindow(struct Gui *g, const struct RStatGuiCfg *c)
{
    struct TagItem t[24];
    Object *root;
    int n;

    root = Root(g);
    if (root == NULL)
        return FALSE;
    n = 0;
    t[n].ti_Tag = WA_Title;         t[n++].ti_Data = (ULONG) "RadeonStat";
    t[n].ti_Tag = WA_ScreenTitle;   t[n++].ti_Data = (ULONG) g->version;
    t[n].ti_Tag = WA_PubScreen;     t[n++].ti_Data = (ULONG) g->scr;
    if (c->left >= 0 && c->top >= 0)
    {
        t[n].ti_Tag = WA_Left;      t[n++].ti_Data = (ULONG) c->left;
        t[n].ti_Tag = WA_Top;       t[n++].ti_Data = (ULONG) c->top;
    }
    else
    {
        t[n].ti_Tag = WINDOW_Position; t[n++].ti_Data = WPOS_CENTERSCREEN;
    }
    t[n].ti_Tag = WA_Activate;      t[n++].ti_Data = TRUE;
    t[n].ti_Tag = WA_DragBar;       t[n++].ti_Data = TRUE;
    t[n].ti_Tag = WA_DepthGadget;   t[n++].ti_Data = TRUE;
    t[n].ti_Tag = WA_CloseGadget;   t[n++].ti_Data = TRUE;
    t[n].ti_Tag = WA_SizeGadget;    t[n++].ti_Data = TRUE;
    t[n].ti_Tag = WA_SizeBBottom;   t[n++].ti_Data = TRUE;
    t[n].ti_Tag = WA_SmartRefresh;  t[n++].ti_Data = TRUE;  /* the graph scrolls inside the window */
    t[n].ti_Tag = WA_NewLookMenus;  t[n++].ti_Data = TRUE;
    t[n].ti_Tag = WA_IDCMP;         t[n++].ti_Data = IDCMP_RAWKEY;  /* the button's underscore key */
    t[n].ti_Tag = WINDOW_NewMenu;   t[n++].ti_Data = (ULONG) menus;
    t[n].ti_Tag = WINDOW_Layout;    t[n++].ti_Data = (ULONG) root;
    t[n].ti_Tag = TAG_DONE;         t[n].ti_Data = 0;
    g->win = NewObjectA(WINDOW_GetClass(), NULL, t);
    if (g->win == NULL)
    {
        DisposeObject(root);
        return FALSE;
    }
    return TRUE;
}

/* --- the readings ------------------------------------------------------------- */

static void Bar(struct Gui *g, int v, ULONG pct)
{
    char t[16], *p;

    p = PutU(t, pct);
    PutS(p, (CONST_STRPTR) " %");
    RSView_SetBar(g->view[v], g->w, pct * 10, (CONST_STRPTR) t);
}

static void ShowMemory(struct Gui *g)
{
    char t[40], *p;
    ULONG total, fr, lg, used;

    RStat_Memory(g->s, &total, &fr, &lg);
    used = total > fr ? total - fr : 0;
    p = PutMB(t, used);
    p = PutS(p, (CONST_STRPTR) " of ");
    p = PutMB(p, total);
    PutS(p, (CONST_STRPTR) " MB");
    RSView_SetBar(g->view[V_USED], g->w, total >= 1000 ? used / (total / 1000) : 0, (CONST_STRPTR) t);
    PutS(PutMB(t, fr), (CONST_STRPTR) " MB");
    RSView_SetText(g->view[V_FREE], g->w, (CONST_STRPTR) t, NULL);
    PutS(PutMB(t, lg), (CONST_STRPTR) " MB");
    RSView_SetText(g->view[V_LARGEST], g->w, (CONST_STRPTR) t, NULL);
}

/* The front screen; the card's mode is timed again only when it changed
 * (that holds the lock about 100 ms) */
static void ShowDisplay(struct Gui *g, BOOL force)
{
    char t[48], *p;

    if (RStat_Front(&g->front) || force)
        RStat_Mode(g->s, &g->mode);
    RSView_SetText(g->view[V_SCREEN], g->w, (CONST_STRPTR) g->front.title, NULL);
    p = PutU(t, (ULONG) g->front.width);
    p = PutS(p, (CONST_STRPTR) " x ");
    p = PutU(p, (ULONG) g->front.height);
    if (g->mode.hz10 != 0)
    {
        p = PutS(p, (CONST_STRPTR) ", ");
        p = PutTenths(p, g->mode.hz10);
        PutS(p, (CONST_STRPTR) " Hz");
    }
    RSView_SetText(g->view[V_MODE], g->w, (CONST_STRPTR) t, NULL);
    if (g->mode.hz10 != 0)
    {
        PutS(PutTenths(t, g->mode.mhz10), (CONST_STRPTR) " MHz ");
        RSView_SetText(g->view[V_CLOCK], g->w, (CONST_STRPTR) t,
                       g->mode.mhz10 > DVI_LIMIT ? (CONST_STRPTR) "DVI may drop out" : NULL);
    }
    else
        RSView_SetText(g->view[V_CLOCK], g->w, (CONST_STRPTR) "not measured", NULL);
}

static void ShowLoad(struct Gui *g, BOOL push)
{
    struct RStatLoad l;
    char t[24], *p;

    RStat_Take(g->s, &l);
    Bar(g, V_BUSY, l.gpu);
    Bar(g, V_3D, l.d3);
    Bar(g, V_2D, l.d2);
    Bar(g, V_CP, l.cp);
    p = PutU(t, l.ring);
    PutS(p, (CONST_STRPTR) " dwords");
    RSView_SetText(g->view[V_QUEUE], g->w, (CONST_STRPTR) t, NULL);
    if (push)
        RSView_Push(g->view[V_GRAPH], g->w, l.gpu);
}

/* --- the timer ------------------------------------------------------------------ */

static BOOL OpenTimer(struct Gui *g)
{
    g->tport = CreateMsgPort();
    if (g->tport == NULL)
        return FALSE;
    g->treq = (struct timerequest *) CreateIORequest(g->tport, sizeof(struct timerequest));
    if (g->treq == NULL)
        return FALSE;
    if (OpenDevice((CONST_STRPTR) TIMERNAME, UNIT_MICROHZ, (struct IORequest *) g->treq, 0) != 0)
        return FALSE;
    g->tdev = TRUE;
    return TRUE;
}

static void Arm(struct Gui *g)
{
    if (!g->tdev || g->tpending)
        return;
    g->treq->tr_node.io_Command = TR_ADDREQUEST;
    g->treq->tr_time.tv_secs = 0;
    g->treq->tr_time.tv_micro = 1000000UL / RSTAT_TICKS;
    SendIO((struct IORequest *) g->treq);
    g->tpending = TRUE;
}

static void CloseTimer(struct Gui *g)
{
    if (g->tpending)
    {
        AbortIO((struct IORequest *) g->treq);
        WaitIO((struct IORequest *) g->treq);
        g->tpending = FALSE;
    }
    if (g->tdev)
        CloseDevice((struct IORequest *) g->treq);
    g->tdev = FALSE;
    if (g->treq != NULL)
        DeleteIORequest((struct IORequest *) g->treq);
    g->treq = NULL;
    if (g->tport != NULL)
        DeleteMsgPort(g->tport);
    g->tport = NULL;
}

/* --- requesters and the event loop ------------------------------------------------ */

static void Tell(struct Window *w, CONST_STRPTR text, ULONG arg)
{
    struct EasyStruct es;

    es.es_StructSize = sizeof(es);
    es.es_Flags = 0;
    es.es_Title = (STRPTR) "RadeonStat";
    es.es_TextFormat = (STRPTR) text;
    es.es_GadgetFormat = (STRPTR) "OK";
    EasyRequest(w, &es, NULL, arg);
}

/* A new interval; the counting starts anew */
static void SetInterval(struct Gui *g, UWORD choice)
{
    struct RStatLoad dropped;

    if (choice >= sizeof(intervalTenths))
        return;
    g->choice = choice;
    g->perInterval = (ULONG) intervalTenths[choice] * RSTAT_TICKS / 10;
    g->ticks = 0;
    RStat_Take(g->s, &dropped);
}

static void Tick(struct Gui *g)
{
    RStat_Sample(g->s);
    if (++g->ticks >= g->perInterval)
    {
        g->ticks = 0;
        ShowLoad(g, TRUE);
        ShowMemory(g);
        ShowDisplay(g, FALSE);
    }
}

/* The menu numbers of one pick; FALSE for Quit. The next pick is read
 * before an item's action (a closing window frees its menu strip). */
static BOOL Menus(struct Gui *g, UWORD code)
{
    struct MenuItem *item;
    struct Menu *strip;
    UWORD next;

    strip = g->w ? g->w->MenuStrip : NULL;
    while (code != MENUNULL && strip != NULL)
    {
        item = ItemAddress(strip, code);
        if (item == NULL)
            break;
        next = item->NextSelect;
        switch ((ULONG) GTMENUITEM_USERDATA(item))
        {
        case MENU_ABOUT:
            Tell(g->w, (CONST_STRPTR) "%s\n\nWhat the Radeon (R200 family) is doing:\n"
                 "engine load, card memory and the display.\n"
                 "Read-only: nothing is written to the card.\n\nPart of RadeonGL.",
                 (ULONG) g->version);
            break;
        case MENU_QUIT:
            return FALSE;
        }
        code = next;
    }
    return TRUE;
}

static void Loop(struct Gui *g)
{
    ULONG wsig, tsig, sig, result, sel;
    UWORD code;
    BOOL run;

    wsig = 0;
    GetAttr(WINDOW_SigMask, g->win, &wsig);
    tsig = 1UL << g->tport->mp_SigBit;
    Arm(g);
    run = TRUE;
    while (run)
    {
        sig = Wait(wsig | tsig | SIGBREAKF_CTRL_C);
        if (sig & SIGBREAKF_CTRL_C)
            break;
        if (sig & tsig)
        {
            if (GetMsg(g->tport) != NULL)
            {
                g->tpending = FALSE;
                if (!g->paused)
                {
                    Tick(g);
                    Arm(g);
                }
            }
        }
        if (sig & wsig)
        {
            while ((result = DoMethod(g->win, WM_HANDLEINPUT, (ULONG) &code)) != WMHI_LASTMSG)
            {
                switch (result & WMHI_CLASSMASK)
                {
                case WMHI_CLOSEWINDOW:
                    run = FALSE;
                    break;
                case WMHI_GADGETUP:
                    switch (result & WMHI_GADGETMASK)
                    {
                    case GID_INTERVAL:
                        SetInterval(g, code);
                        RSView_Clear(g->view[V_GRAPH], g->w);
                        break;
                    case GID_PAUSE:
                        sel = 0;
                        GetAttr(GA_Selected, g->pause, &sel);
                        g->paused = (BOOL) (sel != 0);
                        if (!g->paused)
                        {
                            SetInterval(g, g->choice);  /* start counting anew */
                            Arm(g);
                        }
                        break;
                    }
                    break;
                case WMHI_MENUPICK:
                    if (!Menus(g, (UWORD) (result & WMHI_MENUMASK)))
                        run = FALSE;
                    break;
                }
                if (!run)
                    break;
            }
        }
    }
}

static UWORD ChoiceOf(ULONG tenths)
{
    UWORD i;

    for (i = 0; i < sizeof(intervalTenths); i++)
        if (intervalTenths[i] >= tenths)
            return i;
    return (UWORD) (sizeof(intervalTenths) - 1);
}

static struct Library *OpenClass(CONST_STRPTR name, CONST_STRPTR *missing)
{
    struct Library *base;

    base = OpenLibrary(name, CLASS_VERSION);
    if (base == NULL && *missing == NULL)
        *missing = name;
    return base;
}

int RStatGui_Run(struct RStat *s, const struct RStatGuiCfg *c, CONST_STRPTR version)
{
    static struct Gui gui;
    struct Gui *g;
    CONST_STRPTR missing;
    LONG rc;
    int i;

    g = &gui;
    g->s = s;
    g->version = version;
    for (i = 0; i < V_N; i++)
        g->view[i] = NULL;
    rc = RETURN_FAIL;
    missing = NULL;
    UtilityBase = OpenLibrary((CONST_STRPTR) "utility.library", 39);
    WindowBase = OpenClass((CONST_STRPTR) "window.class", &missing);
    LayoutBase = OpenClass((CONST_STRPTR) "gadgets/layout.gadget", &missing);
    ButtonBase = OpenClass((CONST_STRPTR) "gadgets/button.gadget", &missing);
    ChooserBase = OpenClass((CONST_STRPTR) "gadgets/chooser.gadget", &missing);
    SpaceBase = OpenClass((CONST_STRPTR) "gadgets/space.gadget", &missing);
    LabelBase = OpenClass((CONST_STRPTR) "images/label.image", &missing);
    if (missing != NULL || UtilityBase == NULL)
    {
        Tell(NULL, (CONST_STRPTR) "RadeonStat needs %s\n(ReAction, AmigaOS 3.2).",
             (ULONG) (missing ? missing : (CONST_STRPTR) "utility.library"));
        goto out;
    }
    switch (RStat_Open(s))
    {
    case RSTAT_OK:
        break;
    case RSTAT_NORADEON:
        Tell(NULL, (CONST_STRPTR) "The Workbench screen is not on a Radeon.\n"
             "RadeonStat watches an R200-family Radeon\n(8500 to 9250) under Picasso96.", 0);
        goto out;
    case RSTAT_NOLOCK:
        Tell(NULL, (CONST_STRPTR) "RadeonStat could not lock its bitmap\non the board.", 0);
        goto out;
    default:
        Tell(NULL, (CONST_STRPTR) "The Workbench screen is not on a\nPicasso96 board.", 0);
        goto out;
    }
    g->scr = LockPubScreen((CONST_STRPTR) "Workbench");
    if (g->scr == NULL)
        goto out;
    g->dri = GetScreenDrawInfo(g->scr);
    g->viewClass = RSView_MakeClass();
    if (g->dri == NULL || g->viewClass == NULL || !OpenTimer(g))
        goto out;
    SetInterval(g, ChoiceOf(c->interval10));
    g->paused = FALSE;
    g->front.scr = NULL;
    if (!MakeWindow(g, c))
    {
        Tell(NULL, (CONST_STRPTR) "RadeonStat could not make its window.", 0);
        goto out;
    }
    /* the first values before the window opens: its first GM_RENDER draws them */
    ShowLoad(g, FALSE);
    ShowMemory(g);
    ShowDisplay(g, TRUE);
    g->w = (struct Window *) DoMethod(g->win, WM_OPEN, NULL);
    if (g->w == NULL)
    {
        Tell(NULL, (CONST_STRPTR) "RadeonStat could not open its window.", 0);
        goto out;
    }
    rc = RETURN_OK;
    Loop(g);

out:
    CloseTimer(g);
    if (g->win != NULL)
        DisposeObject(g->win);      /* closes the window; the layout disposes the gadgets */
    g->win = NULL;
    g->w = NULL;
    RSView_FreeClass(g->viewClass);
    g->viewClass = NULL;
    if (g->dri != NULL)
        FreeScreenDrawInfo(g->scr, g->dri);
    g->dri = NULL;
    if (g->scr != NULL)
        UnlockPubScreen(NULL, g->scr);
    g->scr = NULL;
    CloseLibrary(LabelBase);
    CloseLibrary(SpaceBase);
    CloseLibrary(ChooserBase);
    CloseLibrary(ButtonBase);
    CloseLibrary(LayoutBase);
    CloseLibrary(WindowBase);
    CloseLibrary(UtilityBase);
    LabelBase = SpaceBase = ChooserBase = ButtonBase = LayoutBase = WindowBase = UtilityBase = NULL;
    return (int) rc;
}
