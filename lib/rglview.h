/*
 * rglview.h -- RadeonGL's GL_RGL_bitmap_view (2026-11-14, R4): a texture
 * that IS a rectangle of a Picasso96 bitmap on the Radeon's board. Public:
 * a program includes it, tests "GL_RGL_bitmap_view" in GL_EXTENSIONS, then
 *
 *     struct RglBitMapView v;
 *     v.bm = bitmap; v.x = left; v.y = top;
 *     glBindTexture(GL_TEXTURE_2D, tex);
 *     glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0,
 *                  GL_RGL_BITMAP_VIEW, GL_UNSIGNED_BYTE, &v);
 *
 * w x h is the rectangle. GL_RGB ignores the bitmap's alpha byte (a
 * Workbench), GL_RGBA uses it. Nothing is copied: the Radeon reads the
 * bitmap where it is, at every draw, so what the program or the blitter
 * drew into it before the flush shows. Drawing into a bitmap and reading
 * it as a view in the same flush is undefined (end a pass with glFlush or
 * the context's mglSwitchDisplay first).
 *
 * The bitmap must be on the Radeon's board, in R5G6B5PC, R5G5B5PC or
 * B8G8R8A8, its rows and the rectangle's first pixel 32-byte aligned;
 * else GL_INVALID_OPERATION, the reason in the log. At most 4 views a
 * context. The view keeps the bitmap until the texture is specified again
 * or deleted: free the bitmap only after that. A frame drawn while
 * Picasso96 has moved a view's bitmap off the board, or to another place,
 * is dropped and counted in the log (the next one draws from the new
 * place). One level, clamped to the edge; nearest or linear filtering.
 */

#ifndef RADEONGL_RGLVIEW_H
#define RADEONGL_RGLVIEW_H

#include <exec/types.h>
#include <graphics/gfx.h>

#ifndef GL_RGL_BITMAP_VIEW
#define GL_RGL_BITMAP_VIEW 0x52474c06UL
#endif

struct RglBitMapView
{
    struct BitMap *bm;          /* a Picasso96 bitmap on the Radeon's board */
    LONG x, y;                  /* the rectangle's top left in it */
};

#endif /* RADEONGL_RGLVIEW_H */
