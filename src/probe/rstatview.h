/*
 * rstatview.h -- RadeonStat's readouts: one private gadget class for the
 * gauges, the values and the history graph (rstatview.c).
 *
 * The window draws a new value from its own task, straight into the
 * window under a tried layer lock (RSView_Set*, RSView_Push) -- never
 * through SetGadgetAttrs(), which holds Intuition's lock while it waits
 * for the layer and so freezes the pointer while another program owns the
 * layers. GM_RENDER draws everything from the values held.
 */

#ifndef PROBE_RSTATVIEW_H
#define PROBE_RSTATVIEW_H

#include <exec/types.h>
#include <intuition/classes.h>
#include <intuition/intuition.h>
#include <graphics/text.h>
#include <utility/tagitem.h>

#define RSV_Dummy   (TAG_USER + 0x52530000UL)
#define RSV_Kind    (RSV_Dummy + 1)     /* RSVK_: what the gadget shows (OM_NEW) */
#define RSV_Chars   (RSV_Dummy + 2)     /* its least width in characters (OM_NEW) */
#define RSV_Font    (RSV_Dummy + 3)     /* struct TextFont *: the screen's font (OM_NEW) */

#define RSVK_BAR    0   /* a gauge with its value centred on it */
#define RSVK_TEXT   1   /* a value, left-aligned, an optional bold tail */
#define RSVK_GRAPH  2   /* busy per cent over time, newest on the right */

Class *RSView_MakeClass(void);
void RSView_FreeClass(Class *cl);

/* A gauge: permille 0-1000 filled, text centred on it */
void RSView_SetBar(Object *o, struct Window *win, ULONG permille, CONST_STRPTR text);

/* A value: text, then bold (may be NULL) in bold */
void RSView_SetText(Object *o, struct Window *win, CONST_STRPTR text, CONST_STRPTR bold);

/* The graph: a new sample, 0-100 */
void RSView_Push(Object *o, struct Window *win, ULONG percent);

/* The graph: forget the samples (the interval changed) */
void RSView_Clear(Object *o, struct Window *win);

#endif /* PROBE_RSTATVIEW_H */
