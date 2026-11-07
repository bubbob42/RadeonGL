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
};

#endif /* RADEONGL_RGLINT_H */
