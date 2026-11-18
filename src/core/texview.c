/*
 * texview.c -- R4, GL_RGL_bitmap_view (2026-11-14; docs/concept-bitmap-
 * texture.md, decided by the user D1-D5): a texture that IS a rectangle of a
 * board bitmap. No heap block, no upload: the back end checks the view (on
 * its board, one of the three little-endian formats, 32-byte aligned rows
 * and first texel) and gives its address and pitch; the texture is drawn
 * from there, as an NPOT texture (one level, clamped, its own pitch).
 *
 *   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB or GL_RGBA, w, h, 0,
 *                GL_RGL_BITMAP_VIEW, GL_UNSIGNED_BYTE, &view);
 *
 * GL_RGB ignores the bitmap's alpha byte (a Workbench), GL_RGBA uses it (a
 * 32-bit bitmap the program drew alpha into; 16-bit views have no alpha
 * but R5G5B5's bit). The view holds the bitmap until the texture is
 * specified again or deleted. Linked behind the hot code.
 */

#include <stddef.h>
#include "core.h"
#include "coreint.h"
#include "r200reg.h"

void Core_TexView(struct Core *c, struct CoreTex *t, LONG internal, LONG w, LONG h, const void *view)
{
    ULONG base;
    ULONG gpu;
    ULONG pitch;
    ULONG fmt;
    ULONG handle;

    switch (internal)
    {
        case 3: case GL_RGB: case GL_RGB8: case GL_RGB5:
            base = GL_RGB;
            break;
        case 4: case GL_RGBA: case GL_RGBA8:
            base = GL_RGBA;
            break;
        default:
            Core_Error(c, GL_INVALID_ENUM);
            return;
    }
    if (w < 1 || h < 1 || w > CORE_MAX_TEXSIZE || h > CORE_MAX_TEXSIZE)
    {
        Core_Error(c, GL_INVALID_VALUE);
        return;
    }
    if (c->view_open == NULL || view == NULL
        || c->view_open(c->env.env, view, (ULONG) w, (ULONG) h, (base == GL_RGBA) ? 1UL : 0UL,
                        &gpu, &pitch, &fmt, &handle) != 0)
    {
        Core_Error(c, GL_INVALID_OPERATION);    /* the reason is in the log */
        return;
    }
    t->gpu = gpu;
    t->bytes = 0;
    t->w = (ULONG) w;
    t->h = (ULONG) h;
    t->pot = 0;                     /* always the NPOT path: the bitmap's own pitch */
    t->lw = 0;
    t->lh = 0;
    t->lofs[0] = 0;
    t->pitch[0] = pitch;
    t->levels = 1;
    t->base = base;
    switch (fmt)
    {
        case CORE_VIEW_RGB565:
            t->hwfmt = R_TXFORMAT_RGB565;
            t->cpp = 2;
            break;
        case CORE_VIEW_ARGB1555:
            t->hwfmt = R_TXFORMAT_ARGB1555 | ((base == GL_RGBA) ? R_TXFORMAT_ALPHA_IN_MAP : 0);
            t->cpp = 2;
            break;
        default:
            t->hwfmt = R_TXFORMAT_ARGB8888 | ((base == GL_RGBA) ? R_TXFORMAT_ALPHA_IN_MAP : 0);
            t->cpp = 4;
            break;
    }
    t->view = handle;
    c->views++;
}

void Core_TexViewRelease(struct Core *c, struct CoreTex *t)
{
    if (t->view == 0)
        return;
    Core_TexHazard(c, t);           /* the GPU may still read the bitmap */
    if (c->view_close != NULL)
        c->view_close(c->env.env, t->view);
    t->view = 0;
    t->gpu = 0;
    t->bytes = 0;
    t->levels = 0;
    if (c->views != 0)
        c->views--;
}

/* The back end found a view's bitmap at another address (at a flush): every
 * texture of that view takes it, re-emitted at the next draw */
void Core_TexViewMoved(struct Core *c, ULONG handle, ULONG gpu)
{
    ULONG i;
    ULONG u;
    struct CoreTex *t;

    for (i = 1; i < CORE_MAX_TEX; i++)
    {
        t = c->tex[i];
        if (t == NULL || t->view != handle)
            continue;
        t->gpu = gpu;
        for (u = 0; u < CORE_UNITS; u++)
            if (c->bound[u] == t->name)
            {
                DIRTY_UNIT(c, u);
                c->hw_tex[u][6] = 0xffffffffUL;
            }
    }
}
