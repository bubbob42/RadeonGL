/*
 * rstatview.c -- RadeonStat's readouts (rstatview.h).
 *
 * Public API only. Gauges and the graph fill in FILLPEN inside a recessed
 * frame, text in TEXTPEN; the value is always written as text as well, so
 * nothing depends on colour (on a 4-colour Workbench FILLPEN is the blue).
 */

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <intuition/gadgetclass.h>
#include <intuition/screens.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <utility/tagitem.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>

#include <clib/alib_protos.h>

#include "rstatview.h"

#if defined(__SASC)
# define DISP_QUAL __saveds __asm
# define REG(r, a) register __##r a
#elif defined(__GNUC__)
# define DISP_QUAL
# define REG(r, a) a __asm(#r)
#endif

#define TEXTMAX 64
#define HIST    1024        /* samples the graph keeps: wider than any screen's window */
#define COLW    2           /* a sample's column in the graph, pixels */

struct RSVData
{
    UBYTE kind;
    UBYTE chars;
    UBYTE drawn;            /* a GM_RENDER set pens and font */
    UBYTE stale;            /* the graph missed an update: draw all at the next */
    UWORD value;            /* gauge: 0-1000 */
    WORD shownFill;         /* gauge: the filled pixels on the window */
    char text[TEXTMAX], shown[TEXTMAX];
    char bold[TEXTMAX], shownBold[TEXTMAX];
    struct TextFont *font;  /* the screen's */
    UWORD *pens;            /* the screen's, from the last GM_RENDER */
    UWORD head, count;      /* graph: the next slot, samples held */
    UBYTE hist[HIST];
};

static Class *rsv_Class;    /* the update calls find the instance data */

static void Copy(char *to, CONST_STRPTR from)
{
    LONG i;

    i = 0;
    if (from != NULL)
        for (; i < TEXTMAX - 1 && from[i]; i++)
            to[i] = from[i];
    to[i] = 0;
}

static BOOL Same(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        a++;
        b++;
    }
    return (BOOL) (*a == *b);
}

static UWORD Len(const char *s)
{
    UWORD n;

    for (n = 0; s[n]; n++)
        ;
    return n;
}

/* --- drawing ------------------------------------------------------------ */

struct Box
{
    WORD x0, y0, x1, y1;    /* the gadget, inclusive */
    WORD ix0, iy0, ix1, iy1; /* inside the frame */
};

static void GetBox(struct Gadget *g, struct Box *b)
{
    b->x0 = g->LeftEdge;
    b->y0 = g->TopEdge;
    b->x1 = (WORD) (g->LeftEdge + g->Width - 1);
    b->y1 = (WORD) (g->TopEdge + g->Height - 1);
    b->ix0 = (WORD) (b->x0 + 2);    /* two-pixel sides, as the system's frames */
    b->iy0 = (WORD) (b->y0 + 1);
    b->ix1 = (WORD) (b->x1 - 2);
    b->iy1 = (WORD) (b->y1 - 1);
}

static void Fill(struct RastPort *rp, UWORD pen, WORD x0, WORD y0, WORD x1, WORD y1)
{
    if (x1 < x0 || y1 < y0)
        return;
    SetAPen(rp, pen);
    RectFill(rp, x0, y0, x1, y1);
}

static void Recessed(struct RastPort *rp, UWORD *pens, struct Box *b)
{
    if (b->x1 - b->x0 < 4 || b->y1 - b->y0 < 2)
        return;
    SetAPen(rp, pens[SHADOWPEN]);
    Move(rp, b->x0, b->y1);
    Draw(rp, b->x0, b->y0);
    Draw(rp, (WORD) (b->x1 - 1), b->y0);
    Move(rp, (WORD) (b->x0 + 1), (WORD) (b->y1 - 1));
    Draw(rp, (WORD) (b->x0 + 1), (WORD) (b->y0 + 1));
    SetAPen(rp, pens[SHINEPEN]);
    Move(rp, b->x1, b->y0);
    Draw(rp, b->x1, b->y1);
    Draw(rp, (WORD) (b->x0 + 1), b->y1);
    Move(rp, (WORD) (b->x1 - 1), (WORD) (b->y0 + 1));
    Draw(rp, (WORD) (b->x1 - 1), (WORD) (b->y1 - 1));
}

static WORD FillWidth(struct RSVData *d, struct Box *b)
{
    LONG w;

    w = b->ix1 - b->ix0 + 1;
    if (w <= 0)
        return 0;
    return (WORD) (w * d->value / 1000);
}

static void DrawBar(struct RastPort *rp, struct RSVData *d, struct Box *b)
{
    struct TextExtent te;
    WORD fw, w, h, tx, ty;
    UWORD n;

    fw = FillWidth(d, b);
    Fill(rp, d->pens[FILLPEN], b->ix0, b->iy0, (WORD) (b->ix0 + fw - 1), b->iy1);
    Fill(rp, d->pens[BACKGROUNDPEN], (WORD) (b->ix0 + fw), b->iy0, b->ix1, b->iy1);
    w = (WORD) (b->ix1 - b->ix0 + 1);
    h = (WORD) (b->iy1 - b->iy0 + 1);
    n = Len(d->text);
    if (n > 0 && w > 0 && h >= d->font->tf_YSize)
    {
        n = (UWORD) TextFit(rp, (STRPTR) d->text, n, &te, NULL, 1, (UWORD) w, (UWORD) h);
        tx = (WORD) (b->ix0 + (w - TextLength(rp, (STRPTR) d->text, n)) / 2);
        ty = (WORD) (b->iy0 + (h - d->font->tf_YSize) / 2 + d->font->tf_Baseline);
        SetAPen(rp, d->pens[TEXTPEN]);
        SetDrMd(rp, JAM1);
        Move(rp, tx, ty);
        Text(rp, (STRPTR) d->text, n);
    }
    d->shownFill = fw;
    Copy(d->shown, d->text);
}

/* n characters at x; where the next may start: past the pen position and
 * past the glyphs' extent (a bold glyph reaches a pixel further) */
static WORD Glyphs(struct RastPort *rp, const char *s, UWORD n, struct TextExtent *te, WORD x, WORD ty)
{
    WORD end;

    Move(rp, x, ty);
    Text(rp, (STRPTR) s, n);
    end = (WORD) (x + te->te_Extent.MaxX + 1);
    return (WORD) (rp->cp_x > end ? rp->cp_x : end);
}

/* Text in JAM2 over the background, then only what is right of it and
 * above and below the line cleared: no flicker when a value changes */
static void DrawText(struct RastPort *rp, struct RSVData *d, struct Box *b)
{
    struct TextExtent te;
    WORD x, ty, top, h;
    UWORD n;
    ULONG enable;

    h = (WORD) (b->y1 - b->y0 + 1);
    top = (WORD) (b->y0 + (h - d->font->tf_YSize) / 2);
    if (top < b->y0)
        top = b->y0;
    ty = (WORD) (top + d->font->tf_Baseline);
    x = b->x0;
    SetAPen(rp, d->pens[TEXTPEN]);
    SetBPen(rp, d->pens[BACKGROUNDPEN]);
    SetDrMd(rp, JAM2);
    enable = AskSoftStyle(rp);
    SetSoftStyle(rp, 0, enable);
    n = Len(d->text);
    if (n > 0 && x <= b->x1)
    {
        n = (UWORD) TextFit(rp, (STRPTR) d->text, n, &te, NULL, 1, (UWORD) (b->x1 - x + 1), (UWORD) h);
        if (n > 0)
            x = Glyphs(rp, d->text, n, &te, x, ty);
    }
    n = Len(d->bold);
    if (n > 0 && x <= b->x1)
    {
        SetSoftStyle(rp, FSF_BOLD, enable);
        n = (UWORD) TextFit(rp, (STRPTR) d->bold, n, &te, NULL, 1, (UWORD) (b->x1 - x + 1), (UWORD) h);
        if (n > 0)
            x = Glyphs(rp, d->bold, n, &te, x, ty);
        SetSoftStyle(rp, 0, enable);
    }
    Fill(rp, d->pens[BACKGROUNDPEN], x, b->y0, b->x1, b->y1);
    if (x > b->x0)
    {
        Fill(rp, d->pens[BACKGROUNDPEN], b->x0, b->y0, (WORD) (x - 1), (WORD) (top - 1));
        Fill(rp, d->pens[BACKGROUNDPEN], b->x0, (WORD) (top + d->font->tf_YSize), (WORD) (x - 1), b->y1);
    }
    Copy(d->shown, d->text);
    Copy(d->shownBold, d->bold);
}

/* One sample's column of the graph, COLW pixels from x: v per cent of the
 * height filled from below */
static void Column(struct RastPort *rp, struct RSVData *d, struct Box *b, WORD x, UBYTE v)
{
    WORD ih, h;

    ih = (WORD) (b->iy1 - b->iy0 + 1);
    h = (WORD) ((ih * (LONG) v + 50) / 100);
    if (h > ih)
        h = ih;
    Fill(rp, d->pens[BACKGROUNDPEN], x, b->iy0, (WORD) (x + COLW - 1), (WORD) (b->iy1 - h));
    Fill(rp, d->pens[FILLPEN], x, (WORD) (b->iy1 - h + 1), (WORD) (x + COLW - 1), b->iy1);
}

/* The newest sample's column ends at the right edge; what is left of the
 * oldest sample (or of a whole column) stays background */
static void DrawGraph(struct RastPort *rp, struct RSVData *d, struct Box *b)
{
    WORD cols, c, x;

    cols = (WORD) ((b->ix1 - b->ix0 + 1) / COLW);
    if (cols <= 0 || b->iy1 < b->iy0)
        return;
    if (cols > (WORD) d->count)
        cols = (WORD) d->count;
    x = (WORD) (b->ix1 + 1 - cols * COLW);      /* the oldest column shown */
    Fill(rp, d->pens[BACKGROUNDPEN], b->ix0, b->iy0, (WORD) (x - 1), b->iy1);
    for (c = 0; c < cols; c++)                  /* c columns from the newest */
        Column(rp, d, b, (WORD) (b->ix1 + 1 - (c + 1) * COLW), d->hist[(d->head + HIST - 1 - c) % HIST]);
    d->stale = 0;
}

static void Render(struct Gadget *g, struct RSVData *d, struct RastPort *rp)
{
    struct Box b;

    GetBox(g, &b);
    SetFont(rp, d->font);
    if (d->kind == RSVK_TEXT)
        DrawText(rp, d, &b);
    else
    {
        Recessed(rp, d->pens, &b);
        if (d->kind == RSVK_BAR)
            DrawBar(rp, d, &b);
        else
            DrawGraph(rp, d, &b);
    }
}

/* --- the class -------------------------------------------------------------- */

static struct TextFont *FontOf(struct RSVData *d, struct GadgetInfo *gi)
{
    if (gi != NULL && gi->gi_DrInfo != NULL && gi->gi_DrInfo->dri_Font != NULL)
        return gi->gi_DrInfo->dri_Font;
    if (d->font != NULL)
        return d->font;
    return GfxBase->DefaultFont;
}

static ULONG Domain(struct RSVData *d, struct gpDomain *gpd)
{
    struct TextFont *tf;
    WORD w, h;

    tf = FontOf(d, gpd->gpd_GInfo);
    w = (WORD) (d->chars * tf->tf_XSize);
    if (d->kind == RSVK_BAR)
    {
        w = (WORD) (w + 8);
        h = (WORD) (tf->tf_YSize + 4);
    }
    else if (d->kind == RSVK_TEXT)
        h = tf->tf_YSize;
    else
        h = (WORD) (tf->tf_YSize * 4 + 2);
    if (gpd->gpd_Which == GDOMAIN_MAXIMUM)
    {
        w = 16383;
        if (d->kind == RSVK_GRAPH)
            h = 16383;
    }
    gpd->gpd_Domain.Left = 0;
    gpd->gpd_Domain.Top = 0;
    gpd->gpd_Domain.Width = w;
    gpd->gpd_Domain.Height = h;
    return 1;
}

DISP_QUAL ULONG RSView_Dispatch(REG(a0, Class *cl), REG(a2, Object *o), REG(a1, Msg msg))
{
    struct RSVData *d;
    struct opSet *ops;
    struct gpRender *gpr;
    Object *newobj;

    switch (msg->MethodID)
    {
    case OM_NEW:
        newobj = (Object *) DoSuperMethodA(cl, o, msg);
        if (newobj == NULL)
            return 0;
        d = (struct RSVData *) INST_DATA(cl, newobj);
        ops = (struct opSet *) msg;
        d->kind = (UBYTE) GetTagData(RSV_Kind, RSVK_TEXT, ops->ops_AttrList);
        d->chars = (UBYTE) GetTagData(RSV_Chars, 8, ops->ops_AttrList);
        d->font = (struct TextFont *) GetTagData(RSV_Font, 0, ops->ops_AttrList);
        d->drawn = 0;
        d->stale = 1;
        d->value = 0;
        d->shownFill = -1;
        d->text[0] = d->shown[0] = d->bold[0] = d->shownBold[0] = 0;
        d->pens = NULL;
        d->head = d->count = 0;
        return (ULONG) newobj;

    case GM_DOMAIN:
        return Domain((struct RSVData *) INST_DATA(cl, o), (struct gpDomain *) msg);

    case GM_RENDER:
        gpr = (struct gpRender *) msg;
        if (gpr->gpr_GInfo == NULL || gpr->gpr_GInfo->gi_DrInfo == NULL || gpr->gpr_RPort == NULL)
            return 0;
        d = (struct RSVData *) INST_DATA(cl, o);
        d->pens = gpr->gpr_GInfo->gi_DrInfo->dri_Pens;
        d->font = FontOf(d, gpr->gpr_GInfo);
        d->drawn = 1;
        Render((struct Gadget *) o, d, gpr->gpr_RPort);
        return 1;

    case GM_HITTEST:
        return 0;               /* clicks pass through */

    default:
        return DoSuperMethodA(cl, o, msg);
    }
}

Class *RSView_MakeClass(void)
{
    Class *cl;

    cl = MakeClass(NULL, (STRPTR) GADGETCLASS, NULL, sizeof(struct RSVData), 0);
    if (cl != NULL)
    {
        cl->cl_Dispatcher.h_Entry = (ULONG (*)()) RSView_Dispatch;
        cl->cl_Dispatcher.h_SubEntry = NULL;
        cl->cl_Dispatcher.h_Data = NULL;
    }
    rsv_Class = cl;
    return cl;
}

void RSView_FreeClass(Class *cl)
{
    if (cl != NULL)
        FreeClass(cl);
    rsv_Class = NULL;
}

/* --- updates from the window's task ------------------------------------------ */

/* The window's layer, only tried: a busy one skips this update (the values
 * stay held; the next update or GM_RENDER draws them). A copy of the
 * window's RastPort, so its font and pens stay as they are. */
static BOOL Begin(struct Window *win, struct RSVData *d, struct RastPort *rp)
{
    if (win == NULL || !d->drawn || d->pens == NULL)
        return FALSE;
    if (!AttemptLockLayerRom(win->WLayer))
        return FALSE;
    *rp = *win->RPort;
    SetFont(rp, d->font);
    return TRUE;
}

void RSView_SetBar(Object *o, struct Window *win, ULONG permille, CONST_STRPTR text)
{
    struct RSVData *d;
    struct RastPort rp;
    struct Box b;

    if (o == NULL || rsv_Class == NULL)
        return;
    d = (struct RSVData *) INST_DATA(rsv_Class, o);
    d->value = (UWORD) (permille > 1000 ? 1000 : permille);
    Copy(d->text, text);
    GetBox((struct Gadget *) o, &b);
    if (FillWidth(d, &b) == d->shownFill && Same(d->text, d->shown))
        return;
    if (!Begin(win, d, &rp))
        return;
    DrawBar(&rp, d, &b);
    UnlockLayerRom(win->WLayer);
}

void RSView_SetText(Object *o, struct Window *win, CONST_STRPTR text, CONST_STRPTR bold)
{
    struct RSVData *d;
    struct RastPort rp;
    struct Box b;

    if (o == NULL || rsv_Class == NULL)
        return;
    d = (struct RSVData *) INST_DATA(rsv_Class, o);
    Copy(d->text, text);
    Copy(d->bold, bold);
    if (Same(d->text, d->shown) && Same(d->bold, d->shownBold))
        return;
    if (!Begin(win, d, &rp))
        return;
    GetBox((struct Gadget *) o, &b);
    DrawText(&rp, d, &b);
    UnlockLayerRom(win->WLayer);
}

void RSView_Push(Object *o, struct Window *win, ULONG percent)
{
    struct RSVData *d;
    struct RastPort rp;
    struct Box b;

    if (o == NULL || rsv_Class == NULL)
        return;
    d = (struct RSVData *) INST_DATA(rsv_Class, o);
    d->hist[d->head] = (UBYTE) (percent > 100 ? 100 : percent);
    d->head = (UWORD) ((d->head + 1) % HIST);
    if (d->count < HIST)
        d->count++;
    if (!Begin(win, d, &rp))
    {
        d->stale = 1;
        return;
    }
    GetBox((struct Gadget *) o, &b);
    if (d->stale || b.ix1 - b.ix0 + 1 < 2 * COLW)
        DrawGraph(&rp, d, &b);
    else
    {
        /* one column to the left, the new sample on the right */
        SetBPen(&rp, d->pens[BACKGROUNDPEN]);
        ScrollRaster(&rp, COLW, 0, b.ix0, b.iy0, b.ix1, b.iy1);
        Column(&rp, d, &b, (WORD) (b.ix1 + 1 - COLW), d->hist[(d->head + HIST - 1) % HIST]);
    }
    UnlockLayerRom(win->WLayer);
}

void RSView_Clear(Object *o, struct Window *win)
{
    struct RSVData *d;
    struct RastPort rp;
    struct Box b;

    if (o == NULL || rsv_Class == NULL)
        return;
    d = (struct RSVData *) INST_DATA(rsv_Class, o);
    d->head = d->count = 0;
    d->stale = 1;
    if (!Begin(win, d, &rp))
        return;
    GetBox((struct Gadget *) o, &b);
    DrawGraph(&rp, d, &b);
    UnlockLayerRom(win->WLayer);
}
