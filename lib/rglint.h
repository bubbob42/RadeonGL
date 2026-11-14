/*
 * rglint.h -- the library's context (rgl.c), shared with the hooks linked
 * after the hot code (rgllight.c, rglread.c). The core is its first
 * member: Rgl_Core()'s pointer is the context's.
 */

#ifndef RADEONGL_RGLINT_H
#define RADEONGL_RGLINT_H

#include "core.h"
#include "card.h"
#include "r2ring.h"
#include "nullcard.h"
#include "rgl_dispatch.h"

struct RglDL;                   /* rgldlist.c */

struct Rgl
{
    struct Core core;           /* first: a context's pointer is its core's */
    struct Rgl *next;           /* the list of contexts (W3) */
    ULONG real;                 /* the Radeon (rad), else the null card */
    struct Card rad;
    struct R2Ring ring;
    struct NullCard card;
    ULONG *ring_mem;
    ULONG *heap_mem;
    ULONG heap_size;
    struct Window *win;         /* the null card's window */
    ULONG win_app;              /* ... is the program's (FromWindow): not ours to close */
    ULONG kind;                 /* CARD_KIND_* asked for */
    struct Task *owner;
    ULONG frame;
    ULONG frame_dw_max;
    ULONG last_dwords;
    ULONG unimpl[(DISPATCH_SLOTS + 31) / 32];
    char log[160];
    ULONG no_lock;              /* RadeonGLLocked=0: locked arrays the immediate way */
    const char *err_where;      /* the hook that raised the first GL error */
    ULONG err_arg;              /* its enum argument */
    ULONG dump_frame;           /* 0: no dump */
    ULONG dump_written;         /* dwords of packets written, 0 = none */
    char dump_path[160];
    UWORD *ptr_blank;           /* MGLSetPointer: an empty sprite (chip RAM) on the input window */
    ULONG resizes;              /* mglResizeContext calls that changed the size (2026-11-06) */
    ULONG resize_failed;        /* ... that kept the old one */
    char resize_why[96];        /* the last one's reason */
    /* MiniGL's main loop (rglcompat.c, 2026-11-08): the program's handlers */
    IdleFn ml_idle;
    KeyHandlerFn ml_key;
    MouseHandlerFn ml_mouse;
    SpecialHandlerFn ml_special;
    ULONG ml_running;
    ULONG ml_close;             /* the close gadget ends the loop (GLUT, rglglut.c) */
    struct RglDL *dl;           /* display lists (rgldlist.c), made at the first list call */
    /* the task table's state hooks (rgltable.c, 2026-11-12); at the end,
     * so that nothing above moves */
    ULONG tab_armed;            /* the core reports this context's vnorm and pm_mask */
    ULONG tab_hooks;            /* the groups the task's table holds (TAB_LIT, TAB_PM) */
    MGLDispatchTable *tab_table;    /* ... that table */
};

/* rgl.c: every context, newest first (W3) */
extern struct Rgl *rgl_list;

/* rglcompat.c (linked last): glGet with the context's buffering known */
extern const char Rglc_Extensions[];    /* GL_EXTENSIONS (rglcompat.c) */
/* shell.c: the calling task's own dispatch table, NULL on the shared one */
MGLDispatchTable *Shell_TaskTable(void);
/* rgldlist.c: a context's lists freed; glGet's list names; a list being
 * made (its recorders hold the task table's slots) */
void Rgldl_Free(struct Rgl *r);
BOOL Rgldl_Recording(struct Rgl *r);
/* rglglut.c: a task's GLUT state, when the task leaves */
void Rglg_ClientGone(struct Task *task);
/* rglcompat.c: the GLU quadric object (opaque to programs); its settings first */
struct GLUquadricObj_t
{
    struct CoreQuadric q;
    MGLUfuncptr error;          /* GLU_ERROR's callback, or NULL */
};
struct Core *Rglc_TaskCore(void);       /* the calling task's newest context */
BOOL Rgldl_Get(struct Rgl *r, ULONG pname, LONG *out);
void Rglc_GetFloatv(struct Core *c, ULONG pname, float *out);
void Rglc_GetIntegerv(struct Core *c, ULONG pname, LONG *out);

#endif /* RADEONGL_RGLINT_H */
