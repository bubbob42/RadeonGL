/*
 * GroupFrame -- BOOPSI subclass of layout.gadget drawing its own titled group
 * frame in an application-chosen font. See groupframe.h for the rationale.
 *
 * The superclass stays completely frameless (BVS_NONE, no LAYOUT_Label), so
 * it never touches bevel.image's prefs-driven label font; it still performs
 * ALL child management. This class only (a) reserves the title row by forcing
 * LAYOUT_TopSpacing at OM_NEW, (b) widens GM_DOMAIN minimums so the title
 * always fits, and (c) paints the etched frame + title after the superclass
 * has rendered.
 *
 * Compiles with both SAS/C 6.58 and m68k-amigaos-gcc; the dispatcher glue
 * is the one unavoidable compiler-specific spot (see the #if below).
 */

#include <string.h>
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <intuition/gadgetclass.h>
#include <intuition/screens.h>     /* DrawInfo pen numbers */
#include <graphics/gfxbase.h>
#include <graphics/text.h>
#include <graphics/rastport.h>
#include <utility/tagitem.h>
#include <utility/hooks.h>
#include <gadgets/layout.h>
#include <reaction/reaction_prefs.h>   /* struct UIPrefs (REACTION-PREFS) */

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/layout.h>
#include <proto/diskfont.h>

#include <clib/alib_protos.h>   /* DoSuperMethodA() */

#include "groupframe.h"

/* Register-argument dispatcher glue: a BOOPSI dispatcher is called by
 * Intuition with the class in a0, the object in a2 and the message in a1. */
#if defined(__SASC)
# define DISP_QUAL __saveds __asm
# define REG(r, a) register __##r a
#elif defined(__GNUC__)
# define DISP_QUAL
# define REG(r, a) a __asm(#r)
#else
# error "GroupFrame dispatcher needs register-argument support (SAS/C or GCC)."
#endif

/* Air between the title text and the ridge gap ends / the children below. */
#define GF_TITLE_PAD   4

/* Per-instance data, appended after the layout.gadget instance. */
struct GroupFrameData
{
    STRPTR           gfd_Label;    /* application-owned title text (may be NULL) */
    struct TextFont *gfd_Font;     /* our own OpenFont reference (may be NULL)   */
    UWORD            gfd_Style;    /* GROUPFRAME_STYLE_*                         */
    UWORD            gfd_TitleH;   /* reserved title row height (pixels)         */
    UWORD            gfd_PrefsLook; /* title colour/3D from ReAction prefs       */
};


/*
 * Fetch the title pen (a DrawInfo pen INDEX, as bevel.image interprets
 * cap_LabelPen) and the 3D-label flag from the live prefs semaphore.
 * Returns FALSE (leaving the outputs untouched) when no prefs are published.
 * The prefs FONT (cap_LabelAttr) is deliberately never read -- that is the
 * whole point of this class.
 *
 * struct UIPrefs, its fields and RAPREFSSEMAPHORE are all public NDK, from
 * <reaction/reaction_prefs.h>. "CLASSACT-PREFS" is the pre-ReAction name of
 * the same semaphore, tried as a fallback for older systems.
 */
static BOOL GroupFrame_PrefsLook(UWORD *penIndexOut, UWORD *label3DOut)
{
    struct UIPrefs *up;

    Forbid();
    up = (struct UIPrefs *)FindSemaphore((STRPTR)RAPREFSSEMAPHORE);
    if (up == NULL)
    {
        up = (struct UIPrefs *)FindSemaphore((STRPTR)"CLASSACT-PREFS");
    }
    if (up != NULL)
    {
        ObtainSemaphoreShared((struct SignalSemaphore *)up);
    }
    Permit();

    if (up == NULL)
    {
        return FALSE;
    }
    *penIndexOut = (UWORD)up->cap_LabelPen;
    *label3DOut = (UWORD)up->cap_3DLabel;
    ReleaseSemaphore((struct SignalSemaphore *)up);
    return TRUE;
}


/* Draw a 1px outline rectangle. */
static void GroupFrame_Rect(struct RastPort *rp, WORD x1, WORD y1,
                            WORD x2, WORD y2)
{
    Move(rp, x1, y1);
    Draw(rp, x2, y1);
    Draw(rp, x2, y2);
    Draw(rp, x1, y2);
    Draw(rp, x1, y1);
}


/* Resolve a DrawInfo pen with a sane fallback. */
static UWORD GroupFrame_Pen(struct DrawInfo *di, UWORD which, UWORD fallback)
{
    if (di != NULL && which < di->dri_NumPens)
    {
        return (UWORD)di->dri_Pens[which];
    }
    return fallback;
}


/*
 * Paint the frame and title into the gadget's box, after the superclass has
 * rendered the children. The ridge's top line runs at half the title height,
 * so the title straddles it like the stock group bevel; the gap behind the
 * title is cleared with the background pen.
 */
static void GroupFrame_Render(Class *cl, Object *o, struct gpRender *gpr)
{
    struct GroupFrameData *data;
    struct RastPort *rp;
    struct DrawInfo *di;
    struct TextFont *oldFont;
    struct Gadget *g;
    UWORD shadow;
    UWORD shine;
    UWORD text;
    UWORD back;
    WORD  x;
    WORD  y;
    WORD  w;
    WORD  h;
    WORD  ridgeTop;
    WORD  titleW;
    WORD  tx;
    ULONG len;
    UWORD penIdx;
    UWORD label3d;
    BOOL  use3d;

    data = INST_DATA(cl, o);
    rp = gpr->gpr_RPort;
    if (rp == NULL)
    {
        return;
    }
    di = (gpr->gpr_GInfo != NULL) ? gpr->gpr_GInfo->gi_DrInfo : NULL;

    g = (struct Gadget *)o;
    x = g->LeftEdge;
    y = g->TopEdge;
    w = g->Width;
    h = g->Height;
    if (w < 8 || h < 8)
    {
        return;   /* degenerate box -- nothing sensible to draw */
    }

    shadow = GroupFrame_Pen(di, SHADOWPEN, 1);
    shine  = GroupFrame_Pen(di, SHINEPEN, 2);
    text   = GroupFrame_Pen(di, TEXTPEN, 1);
    back   = GroupFrame_Pen(di, BACKGROUNDPEN, 0);

    /* Optionally take the title colour and 3D style from the ReAction prefs
     * (never the font): cap_LabelPen is a DrawInfo pen index, exactly as
     * bevel.image resolves it. */
    use3d = FALSE;
    if (data->gfd_PrefsLook)
    {
        penIdx = TEXTPEN;
        label3d = 0;
        if (GroupFrame_PrefsLook(&penIdx, &label3d))
        {
            text = GroupFrame_Pen(di, penIdx, text);
            use3d = (BOOL)(label3d != 0);
        }
    }

    /* Title font: the chosen one, else the screen font (via DrawInfo) --
     * never whatever font a child renderer left in the RastPort. */
    oldFont = rp->Font;
    if (data->gfd_Font != NULL)
    {
        SetFont(rp, data->gfd_Font);
    }
    else if (di != NULL && di->dri_Font != NULL)
    {
        SetFont(rp, di->dri_Font);
    }

    /* The ridge runs at half the title height so the centred label sits ON
     * it; a label-less frame has no title row, so its ridge hugs the top. */
    if (data->gfd_Label != NULL && data->gfd_Label[0] != '\0')
    {
        ridgeTop = (WORD)(y + (rp->TxHeight / 2));
    }
    else
    {
        ridgeTop = y;
    }
    if (ridgeTop > (WORD)(y + h - 4))
    {
        ridgeTop = (WORD)(y + h - 4);
    }

    SetDrMd(rp, JAM1);

    /* Etched ridge: a shadow outline with a shine outline shifted (1,1). */
    if (data->gfd_Style == GROUPFRAME_STYLE_LINES)
    {
        /* A single top line pair carrying the title (the BVS_SBAR_VERT look:
         * no bottom line, no sides). */
        SetAPen(rp, shadow);
        Move(rp, x, ridgeTop);
        Draw(rp, (WORD)(x + w - 1), ridgeTop);
        SetAPen(rp, shine);
        Move(rp, x, (WORD)(ridgeTop + 1));
        Draw(rp, (WORD)(x + w - 1), (WORD)(ridgeTop + 1));
    }
    else
    {
        SetAPen(rp, shadow);
        GroupFrame_Rect(rp, x, ridgeTop,
                        (WORD)(x + w - 2), (WORD)(y + h - 2));
        SetAPen(rp, shine);
        GroupFrame_Rect(rp, (WORD)(x + 1), (WORD)(ridgeTop + 1),
                        (WORD)(x + w - 1), (WORD)(y + h - 1));
    }

    /* Centered title over the top ridge, the ridge blanked behind it. */
    if (data->gfd_Label != NULL && data->gfd_Label[0] != '\0')
    {
        len = (ULONG)strlen((const char *)data->gfd_Label);
        titleW = (WORD)TextLength(rp, data->gfd_Label, len);
        if (titleW > (WORD)(w - 2 * (GF_TITLE_PAD + 4)))
        {
            /* Clamping the WIDTH was never enough: Text() honours no
             * box of ours, so the whole title still painted -- past
             * the frame and over whatever sat beside it, which is what
             * a long German section title did to its neighbours.
             *
             * TextFit answers how many characters fit the clamped
             * width; what does not fit is not drawn, and the ridge is
             * blanked from the same number, so the cleared strip and
             * the ink cannot disagree. */
            struct TextExtent te;
            WORD maxW;
            ULONG fit;

            maxW = (WORD)(w - 2 * (GF_TITLE_PAD + 4));
            if (maxW < 0)
                maxW = 0;
            fit = (maxW > 0)
                      ? TextFit(rp, data->gfd_Label, len, &te, NULL, 1,
                                (ULONG)maxW, 32767)
                      : 0;
            len = fit;
            titleW = (fit > 0)
                         ? (WORD)TextLength(rp, data->gfd_Label, fit) : 0;
        }
        tx = (WORD)(x + (w - titleW) / 2);

        SetAPen(rp, back);
        /* RectFill with xmax below xmin is undefined, and at exactly
         * 8 pixels of title the arithmetic above produces it. Guarded
         * rather than clamped: at that width there is nothing to clear
         * anyway. */
        if (tx + titleW + GF_TITLE_PAD - 1 >= tx - GF_TITLE_PAD)
            RectFill(rp, (WORD)(tx - GF_TITLE_PAD), y,
                     (WORD)(tx + titleW + GF_TITLE_PAD - 1),
                     (WORD)(y + rp->TxHeight - 1));

        /* Prefs 3D label style: a SHADOWPEN pass offset (+1,+1) under the
         * main text, as bevel.image draws it. */
        if (use3d)
        {
            SetAPen(rp, shadow);
            Move(rp, (WORD)(tx + 1), (WORD)(y + rp->TxBaseline + 1));
            Text(rp, data->gfd_Label, len);
        }

        SetAPen(rp, text);
        Move(rp, tx, (WORD)(y + rp->TxBaseline));
        Text(rp, data->gfd_Label, len);
    }

    SetFont(rp, oldFont);
}


/****** groupframe.gadget/--background-- *********************************
*
*   NAME
*       groupframe.gadget -- a titled group-frame layout gadget.
*
*   FUNCTION
*       GroupFrame is a private BOOPSI subclass of layout.gadget that draws
*       its own titled group frame (an etched ridge plus a title) in the
*       font the application chooses, instead of the built-in bevel.image
*       frame. bevel.image renders BVS_GROUP / BVS_SBAR_VERT labels in the
*       system-wide ReAction prefs Label font whenever one is configured,
*       overriding the supplied IA_Font with no per-instance opt-out; this
*       subclass keeps the layout.gadget child management untouched (the
*       superclass stays frameless) and paints the frame and title itself.
*
*       It reserves the title row by forcing LAYOUT_TopSpacing at OM_NEW,
*       widens the GM_DOMAIN minimum so the title always fits, and paints
*       the etched frame and title after the superclass has rendered. A
*       label-less instance reserves nothing and draws no frame.
*
*   CREATION
*       Use it exactly like a section layout, but instead of
*       LAYOUT_BevelStyle / LAYOUT_Label pass GROUPFRAME_Label,
*       GROUPFRAME_TextAttr and GROUPFRAME_Style. Do not pass
*       LAYOUT_BevelStyle or LAYOUT_Label -- a superclass label would draw a
*       second frame. layout.gadget (LayoutBase) must be open;
*       diskfont.library is used for the title font when available.
*
*   SEE ALSO
*       GroupFrame_MakeClass(), groupframe.gadget/Attributes,
*       groupframe.gadget/Methods
*
**************************************************************************
*
*/

/****** groupframe.gadget/Attributes *************************************
*
*   NAME
*       GroupFrame gadget attributes
*
*   FUNCTION
*       The tag identifiers a host uses with NewObject(). The applicability
*       column is (I) settable at OM_NEW. All are OM_NEW only -- sections
*       are rebuilt, never restyled.
*
*   ATTRIBUTES
*       GROUPFRAME_Label (STRPTR) -- (I)
*           The frame title; must stay valid for the object's lifetime. A
*           NULL or empty title draws no frame.
*
*       GROUPFRAME_TextAttr (struct TextAttr *) -- (I)
*           The title font; NULL means the screen font. The class opens its
*           own reference (OpenDiskFont / OpenFont) and closes it on
*           dispose.
*
*       GROUPFRAME_Style (ULONG) -- (I)
*           STYLE_BOX (full etched box, default) or STYLE_LINES (top and
*           bottom lines).
*
*       GROUPFRAME_PrefsLook (BOOL) -- (I)
*           Take the title colour and 3D style from the ReAction prefs
*           (cap_LabelPen / cap_3DLabel), read live at render time. The
*           prefs font is always ignored. Default FALSE.
*
*   SEE ALSO
*       groupframe.gadget/--background--, groupframe.gadget/Methods
*
**************************************************************************
*
*/

/****** groupframe.gadget/Methods ****************************************
*
*   NAME
*       GroupFrame gadget methods
*
*   FUNCTION
*       The BOOPSI methods the subclass intercepts. All others fall through
*       to layout.gadget.
*
*   METHODS
*       OM_NEW -- read the attributes, open the title font and reserve the
*           title row via LAYOUT_TopSpacing.
*       OM_DISPOSE -- close the title font.
*       GM_DOMAIN -- after the superclass, widen the minimum / nominal width
*           so the title always fits.
*       GM_RENDER -- after the superclass, paint the etched frame and title.
*
*   SEE ALSO
*       groupframe.gadget/--background--, groupframe.gadget/Attributes
*
**************************************************************************
*
*/
DISP_QUAL ULONG GroupFrame_Dispatch(REG(a0, Class *cl), REG(a2, Object *o),
                                    REG(a1, Msg msg))
{
    struct GroupFrameData *data;
    ULONG result;

    switch (msg->MethodID)
    {
        case OM_NEW:
            result = DoSuperMethodA(cl, o, msg);
            if (result != 0)
            {
                struct TagItem *tags;
                struct TextAttr *ta;
                Object *newobj;

                newobj = (Object *)result;
                tags = ((struct opSet *)msg)->ops_AttrList;
                data = INST_DATA(cl, newobj);

                data->gfd_Label =
                    (STRPTR)GetTagData(GROUPFRAME_Label, 0, tags);
                data->gfd_Style =
                    (UWORD)GetTagData(GROUPFRAME_Style,
                                      GROUPFRAME_STYLE_BOX, tags);
                data->gfd_PrefsLook =
                    (UWORD)GetTagData(GROUPFRAME_PrefsLook, FALSE, tags);

                /* Open our own reference to the title font, so the pointer
                 * stays valid however the application juggles its fonts. */
                ta = (struct TextAttr *)GetTagData(GROUPFRAME_TextAttr,
                                                   0, tags);
                data->gfd_Font = NULL;
                if (ta != NULL)
                {
                    if (DiskfontBase != NULL)
                    {
                        data->gfd_Font = OpenDiskFont(ta);
                    }
                    else
                    {
                        data->gfd_Font = OpenFont(ta);
                    }
                }

                if (data->gfd_Font != NULL)
                {
                    data->gfd_TitleH = data->gfd_Font->tf_YSize;
                }
                else if (GfxBase != NULL && GfxBase->DefaultFont != NULL)
                {
                    data->gfd_TitleH = GfxBase->DefaultFont->tf_YSize;
                }
                else
                {
                    data->gfd_TitleH = 8;
                }

                /* Reserve the title row inside the superclass's own geometry
                 * (children start below it; left/right/bottom spacing stay
                 * the caller's). A label-less frame reserves nothing -- the
                 * caller's own top spacing then applies below the ridge. */
                if (data->gfd_Label != NULL && data->gfd_Label[0] != '\0')
                {
                    SetAttrs(newobj,
                        LAYOUT_TopSpacing,
                            (ULONG)(data->gfd_TitleH + GF_TITLE_PAD),
                        TAG_END);
                }
                else
                {
                    data->gfd_TitleH = 0;
                }
            }
            break;

        case OM_DISPOSE:
            data = INST_DATA(cl, o);
            if (data->gfd_Font != NULL)
            {
                CloseFont(data->gfd_Font);
                data->gfd_Font = NULL;
            }
            result = DoSuperMethodA(cl, o, msg);
            break;

        case GM_DOMAIN:
            result = DoSuperMethodA(cl, o, msg);
            {
                struct gpDomain *gpd;

                /* Make sure the title always fits: minimum/nominal width at
                 * least the title plus its gap and some frame air. */
                gpd = (struct gpDomain *)msg;
                data = INST_DATA(cl, o);
                if (gpd->gpd_Which != GDOMAIN_MAXIMUM
                    && data->gfd_Label != NULL && data->gfd_Label[0] != '\0'
                    && gpd->gpd_RPort != NULL)
                {
                    struct RastPort trp;
                    WORD need;

                    trp = *gpd->gpd_RPort;   /* local copy: SetFont is safe */
                    if (data->gfd_Font != NULL)
                    {
                        SetFont(&trp, data->gfd_Font);
                    }
                    need = (WORD)(TextLength(&trp, data->gfd_Label,
                               (ULONG)strlen((const char *)data->gfd_Label))
                               + 2 * (GF_TITLE_PAD + 8));
                    if (gpd->gpd_Domain.Width < need)
                    {
                        gpd->gpd_Domain.Width = need;
                    }
                }
            }
            break;

        case GM_RENDER:
            result = DoSuperMethodA(cl, o, msg);
            GroupFrame_Render(cl, o, (struct gpRender *)msg);
            break;

        default:
            result = DoSuperMethodA(cl, o, msg);
            break;
    }

    return result;
}


/****** groupframe/GroupFrame_MakeClass **********************************
*
*   NAME
*       GroupFrame_MakeClass -- create the GroupFrame gadget class.
*
*   SYNOPSIS
*       class = GroupFrame_MakeClass();
*
*       Class *GroupFrame_MakeClass(void);
*
*   FUNCTION
*       Builds the private GroupFrame class as a subclass of layout.gadget.
*       Call once before creating any instance. intuition.library,
*       utility.library, graphics.library and gadgets/layout.gadget
*       (LayoutBase) must already be open; diskfont.library is used when
*       available.
*
*   RESULT
*       A pointer to the new Class, or NULL on failure. Make instances with
*       NewObject(class, NULL, ...). Free the class with
*       GroupFrame_FreeClass() only after every instance has been disposed.
*
*   NOTES
*       The superclass is fetched with LAYOUT_GetClass(), so MakeClass
*       returns NULL if layout.gadget is not open. The class is private; the
*       host owns it and uses it directly with NewObject().
*
*   SEE ALSO
*       GroupFrame_FreeClass(), groupframe.gadget/--background--
*
**************************************************************************
*
*/
Class *GroupFrame_MakeClass(void)
{
    Class *super;
    Class *cl;

    super = LAYOUT_GetClass();
    if (super == NULL)
    {
        return NULL;
    }

    cl = MakeClass(NULL, NULL, super, sizeof(struct GroupFrameData), 0);
    if (cl != NULL)
    {
        cl->cl_Dispatcher.h_Entry = (ULONG (*)())GroupFrame_Dispatch;
        cl->cl_Dispatcher.h_SubEntry = NULL;
        cl->cl_Dispatcher.h_Data = NULL;
    }

    return cl;
}


/****** groupframe/GroupFrame_FreeClass **********************************
*
*   NAME
*       GroupFrame_FreeClass -- free the GroupFrame gadget class.
*
*   SYNOPSIS
*       GroupFrame_FreeClass(class);
*
*       void GroupFrame_FreeClass(Class *class);
*
*   FUNCTION
*       Frees a class made by GroupFrame_MakeClass(). A NULL class is
*       ignored. Dispose of every instance first -- FreeClass() fails while
*       instances still exist.
*
*   INPUTS
*       class
*           the Class returned by GroupFrame_MakeClass(), or NULL.
*
*   SEE ALSO
*       GroupFrame_MakeClass()
*
**************************************************************************
*
*/
void GroupFrame_FreeClass(Class *cl)
{
    if (cl != NULL)
    {
        FreeClass(cl);
    }
}
