/*
 * cardview.c -- R4, GL_RGL_bitmap_view on the Radeon (2026-11-14;
 * docs/concept-bitmap-texture.md, D1-D5 decided by the user): up to
 * CARD_NVIEW board bitmaps a context draws from as textures (src/core/
 * texview.c makes the texture, lib/rglview.h is the program's side).
 *
 * Open checks a view the way the targets are checked (card.c BitMapRows):
 * on the board, R5G6B5PC / R5G5B5PC / B8G8R8A8 (the formats the R200 reads
 * with the byte order the library uses for textures), the rows and the
 * rectangle's first texel 32-byte aligned (PP_TXOFFSET's low bits are
 * flags, PP_TXPITCH counts 32 bytes), the rectangle inside the bitmap.
 *
 * At every flush the views' bitmaps are locked with the targets (CardFlush
 * step 1) and their addresses compared with the ones the frame was built
 * with (step 2): off the board, or moved, the frame is dropped and counted
 * (D4), and the next one draws from the new place. Each flush also marks
 * the view textures for a new PP_TXOFFSET at their next draw, so what the
 * CPU or the blitter drew into a view's bitmap since is read, not the
 * texture cache's old texels.
 *
 * Linked behind the hot code: card.c calls in only while a view is open.
 */

#include <exec/types.h>
#include <graphics/gfx.h>
#include <intuition/intuition.h>
#include <libraries/Picasso96.h>
#include <proto/exec.h>
#include <proto/Picasso96.h>
#include <libraries/minigl_dispatch.h>

#include "rgl_dispatch.h"
#include "backend.h"
#include "core.h"
#include "coreint.h"
#include "r2ring.h"
#include "nullcard.h"
#include "card.h"
#include "cardflip.h"               /* Rgl_LogPut */
#include "rglint.h"
#include "rglview.h"

extern struct Library *P96Base;

static void Why(struct Card *k, const char *s)
{
    ULONG i;

    for (i = 0; s[i] != 0 && i + 1 < sizeof(k->view_why); i++)
        k->view_why[i] = s[i];
    k->view_why[i] = 0;
    k->view_refused++;
}

/* A bitmap's memory as the GPU sees it, from a lock (as card.c LockBm) */
static ULONG GpuOf(struct Card *k, const UBYTE *mem)
{
    return k->fb_start + ((ULONG) mem - (ULONG) k->membase) % k->ap_size;
}

LONG CardView_Open(struct Card *k, const void *view, ULONG w, ULONG h, ULONG rgba,
                   ULONG *gpu, ULONG *pitch, ULONG *fmt, ULONG *handle)
{
    const struct RglBitMapView *v;
    struct CardView *cv;
    struct RenderInfo ri;
    struct BitMap *bm;
    ULONG rgb;
    ULONG bpp;
    ULONG g;
    LONG lock;
    ULONG i;

    (void) rgba;
    v = (const struct RglBitMapView *) view;
    bm = v->bm;
    for (i = 0; i < CARD_NVIEW && k->view[i].bm != NULL; i++)
        ;
    if (i == CARD_NVIEW)
    {
        Why(k, "more than 4 bitmap views");
        return 1;
    }
    cv = &k->view[i];
    if (bm == NULL || k->ap_size == 0 || !p96GetBitMapAttr(bm, P96BMA_ISP96))
    {
        Why(k, "a bitmap view not of Picasso96");
        return 1;
    }
    rgb = p96GetBitMapAttr(bm, P96BMA_RGBFORMAT);
    if (rgb == RGBFB_R5G6B5PC)
        *fmt = CORE_VIEW_RGB565;
    else if (rgb == RGBFB_R5G5B5PC)
        *fmt = CORE_VIEW_ARGB1555;
    else if (rgb == RGBFB_B8G8R8A8)
        *fmt = CORE_VIEW_ARGB8888;
    else
    {
        Why(k, "a bitmap view in a format the Radeon does not read");
        return 1;
    }
    bpp = (*fmt == CORE_VIEW_ARGB8888) ? 4 : 2;
    if (v->x < 0 || v->y < 0
        || (ULONG) v->x + w > p96GetBitMapAttr(bm, P96BMA_WIDTH)
        || (ULONG) v->y + h > p96GetBitMapAttr(bm, P96BMA_HEIGHT))
    {
        Why(k, "a bitmap view outside its bitmap");
        return 1;
    }
    lock = p96LockBitMap(bm, (UBYTE *) &ri, sizeof(ri));
    if (lock == 0)
    {
        Why(k, "a bitmap view that could not be locked");
        return 1;
    }
    if (ri.Memory == NULL || !p96GetBitMapAttr(bm, P96BMA_ISONBOARD))
    {
        p96UnlockBitMap(bm, lock);
        Why(k, "a bitmap view not on the Radeon's board");
        return 1;
    }
    g = GpuOf(k, (const UBYTE *) ri.Memory) + (ULONG) v->y * (ULONG) ri.BytesPerRow + (ULONG) v->x * bpp;
    p96UnlockBitMap(bm, lock);
    if ((g & 31) != 0 || ((ULONG) ri.BytesPerRow & 31) != 0)
    {
        Why(k, "a bitmap view not 32-byte aligned");
        return 1;
    }
    cv->bm = bm;
    cv->x = v->x;
    cv->y = v->y;
    cv->w = w;
    cv->h = h;
    cv->bpp = bpp;
    cv->bpr = (ULONG) ri.BytesPerRow;
    cv->gpu = g;
    cv->lock = 0;
    cv->mem = NULL;
    k->nview++;
    k->view_opened++;
    *gpu = g;
    *pitch = cv->bpr;
    *handle = i + 1;
    return 0;
}

void CardView_Close(struct Card *k, ULONG handle)
{
    struct CardView *cv;

    if (handle < 1 || handle > CARD_NVIEW)
        return;
    cv = &k->view[handle - 1];
    if (cv->bm == NULL)
        return;
    if (cv->lock != 0)
        p96UnlockBitMap(cv->bm, cv->lock);
    cv->bm = NULL;
    cv->lock = 0;
    if (k->nview != 0)
        k->nview--;
}

ULONG CardView_Ranges(struct Card *k, struct R2ChkCfg *cfg, ULONG n)
{
    ULONG i;

    for (i = 0; i < CARD_NVIEW && n < R2CHK_MAX_RANGES; i++)
        if (k->view[i].bm != NULL)
        {
            cfg->range[n].gpu = k->view[i].gpu;
            /* whole rows: the checker counts an NPOT texture as pitch x
             * height (MEASURED on the card, GLView: a view 32 pixels in was
             * refused by the rectangle's last-row tail) */
            cfg->range[n].size = k->view[i].h * k->view[i].bpr;
            cfg->range[n].kinds = R2K_TEXTURE;
            n++;
        }
    return n;
}

BOOL CardView_Lock(struct Card *k)
{
    struct RenderInfo ri;
    struct CardView *cv;
    ULONG i;

    for (i = 0; i < CARD_NVIEW; i++)
    {
        cv = &k->view[i];
        if (cv->bm == NULL || cv->lock != 0)
            continue;
        cv->lock = p96LockBitMap(cv->bm, (UBYTE *) &ri, sizeof(ri));
        if (cv->lock == 0)
            goto off;
        cv->mem = (UBYTE *) ri.Memory;
        if (cv->mem == NULL || !p96GetBitMapAttr(cv->bm, P96BMA_ISONBOARD))
            goto off;
    }
    return TRUE;
off:
    k->view_dropped++;
    return FALSE;
}

BOOL CardView_Moved(struct Card *k)
{
    struct CardView *cv;
    ULONG moved;
    ULONG g;
    ULONG i;

    moved = 0;
    for (i = 0; i < CARD_NVIEW; i++)
    {
        cv = &k->view[i];
        if (cv->bm == NULL || cv->mem == NULL)
            continue;
        g = GpuOf(k, cv->mem) + (ULONG) cv->y * cv->bpr + (ULONG) cv->x * cv->bpp;
        if (g != cv->gpu)
        {
            cv->gpu = g;
            moved = 1;
        }
        /* moved or not: the next draw writes PP_TXOFFSET again, so the
         * texture cache drops what the bitmap held before this flush */
        Core_TexViewMoved(k->core, i + 1, cv->gpu);
    }
    if (moved)
        k->view_moved++;
    return (BOOL) (moved != 0);
}

void CardView_Unlock(struct Card *k)
{
    struct CardView *cv;
    ULONG i;

    for (i = 0; i < CARD_NVIEW; i++)
    {
        cv = &k->view[i];
        if (cv->bm != NULL && cv->lock != 0)
            p96UnlockBitMap(cv->bm, cv->lock);
        cv->lock = 0;
        cv->mem = NULL;
    }
}

/* the log's line (rgllog.c) */
static void LogU(struct LogOut *f, ULONG v)
{
    char b[12];
    int i;

    i = 11;
    b[i] = 0;
    do
    {
        b[--i] = (char) ('0' + v % 10);
        v /= 10;
    } while (v != 0 && i > 0);
    Rgl_LogPut(f, b + i);
}

void CardView_Log(struct LogOut *f, struct Card *k)
{
    if (k->view_opened == 0 && k->view_refused == 0)
        return;
    Rgl_LogPut(f, "bitmap views (GL_RGL_bitmap_view): made ");
    LogU(f, k->view_opened);
    Rgl_LogPut(f, ", refused ");
    LogU(f, k->view_refused);
    if (k->view_refused != 0)
    {
        Rgl_LogPut(f, " (last: ");
        Rgl_LogPut(f, k->view_why);
        Rgl_LogPut(f, ")");
    }
    Rgl_LogPut(f, "; frames dropped, a view off the board ");
    LogU(f, k->view_dropped);
    Rgl_LogPut(f, ", a view moved ");
    LogU(f, k->view_moved);
    Rgl_LogPut(f, "\n");
}

/* --- the context's side (rgl.c calls Rgl_ViewsAttach after Core_Init) --- */

static LONG RealOpen(APTR env, const void *view, ULONG w, ULONG h, ULONG rgba,
                     ULONG *gpu, ULONG *pitch, ULONG *fmt, ULONG *handle)
{
    return CardView_Open(&((struct Rgl *) env)->rad, view, w, h, rgba, gpu, pitch, fmt, handle);
}

static void RealClose(APTR env, ULONG handle)
{
    CardView_Close(&((struct Rgl *) env)->rad, handle);
}

/* The null card (no Radeon: tests, an emulator): a view is taken if it
 * names a bitmap, and drawn from the start of the null heap -- the
 * checker sees an ordinary texture there */
static ULONG null_views;

static LONG NullOpen(APTR env, const void *view, ULONG w, ULONG h, ULONG rgba,
                     ULONG *gpu, ULONG *pitch, ULONG *fmt, ULONG *handle)
{
    struct Rgl *r;

    (void) rgba;
    r = (struct Rgl *) env;
    if (((const struct RglBitMapView *) view)->bm == NULL
        || ((w * 4 + 31) & ~31UL) * h > r->core.heap.size)
        return 1;
    *gpu = r->core.heap.gpu;
    *pitch = (w * 4 + 31) & ~31UL;
    *fmt = CORE_VIEW_ARGB8888;
    *handle = ++null_views;
    return 0;
}

static void NullClose(APTR env, ULONG handle)
{
    (void) env;
    (void) handle;
}

void Rgl_ViewsAttach(struct Rgl *r)
{
    r->core.view_open = r->real ? RealOpen : NullOpen;
    r->core.view_close = r->real ? RealClose : NullClose;
}
