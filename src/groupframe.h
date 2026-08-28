#ifndef APP_GROUPFRAME_H
#define APP_GROUPFRAME_H

/*
 * GroupFrame -- a BOOPSI subclass of layout.gadget that draws its own titled
 * group frame (etched ridge + title) instead of the built-in bevel.image one.
 *
 * Why: EMPIRICALLY, whenever a "Label" font is configured in the system-wide
 * ReAction preferences, bevel.image draws BVS_GROUP / BVS_SBAR_VERT labels in
 * THAT font and ignores the one the application supplies through IA_Font /
 * REACTION_TextAttr. No tag was found that opts a single instance out of it.
 *
 * Reproduce it: set a distinctive Label font in the ReAction prefs, open a
 * window whose section layouts pass LAYOUT_Label with an explicit
 * REACTION_TextAttr, and the titles come up in the prefs font while every
 * other gadget honours the application's.
 *
 * This subclass keeps the layout.gadget child management untouched (the
 * superclass stays frameless: BVS_NONE, no label) and paints the frame and
 * title itself, in the font the application chooses.
 *
 * Usage: exactly like a section layout, but instead of
 *     LAYOUT_BevelStyle, BVS_GROUP, LAYOUT_Label, (ULONG)"Title",
 * pass
 *     GROUPFRAME_Label,    (ULONG)"Title",
 *     GROUPFRAME_TextAttr, (ULONG)ta,     -- may be NULL: screen font
 *     GROUPFRAME_Style,    GROUPFRAME_STYLE_BOX,
 * Do NOT pass LAYOUT_BevelStyle or LAYOUT_Label (a label would make the
 * superclass draw its own frame again). The class reserves the title row by
 * forcing LAYOUT_TopSpacing; left/right/bottom spacing remain the caller's.
 */

#include <exec/types.h>
#include <intuition/classusr.h>
#include <utility/tagitem.h>

/* Custom attributes (OM_NEW only; sections are rebuilt, never restyled). */
#define GROUPFRAME_TAGBASE   (TAG_USER + 0x47460000UL)   /* 'GF' marker */
#define GROUPFRAME_Label     (GROUPFRAME_TAGBASE + 1)  /* (STRPTR) title; must
                                                        * stay valid for the
                                                        * object's lifetime */
#define GROUPFRAME_TextAttr  (GROUPFRAME_TAGBASE + 2)  /* (struct TextAttr *)
                                                        * title font; NULL =
                                                        * screen font */
#define GROUPFRAME_Style     (GROUPFRAME_TAGBASE + 3)  /* (ULONG) STYLE_* */
#define GROUPFRAME_PrefsLook (GROUPFRAME_TAGBASE + 4)  /* (BOOL) take the title
                                                        * COLOUR and 3D style
                                                        * from the ReAction
                                                        * prefs (UIPrefs
                                                        * cap_LabelPen /
                                                        * cap_3DLabel), read
                                                        * live at render time.
                                                        * The prefs FONT is
                                                        * always ignored. */

/* Frame styles, mirroring the two bevel looks the app used. */
#define GROUPFRAME_STYLE_BOX   0   /* full etched box (was BVS_GROUP)       */
#define GROUPFRAME_STYLE_LINES 1   /* top+bottom lines (was BVS_SBAR_VERT)  */

/*
 * Create the GroupFrame class as a private subclass of layout.gadget.
 * intuition.library, utility.library, graphics.library and
 * gadgets/layout.gadget (LayoutBase) must already be open; diskfont.library
 * is used when available. Returns NULL on failure.
 */
Class *GroupFrame_MakeClass(void);

/* Free a class made by GroupFrame_MakeClass(). Safe to call with NULL. */
void GroupFrame_FreeClass(Class *cl);

#endif /* APP_GROUPFRAME_H */
