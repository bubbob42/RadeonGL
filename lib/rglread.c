/*
 * rglread.c -- reading the frame back (2026-11-07, for Reflector's quick
 * frames): glReadPixels, glPixelStorei and MiniGL's mglWriteShotPPM.
 *
 * The frame is in video RAM: the back buffer of a screen or window
 * context (our target bitmap), or the program's bitmap of a bitmap
 * context. Everything drawn so far is finished first (Card_WaitIdle), then
 * the rows are read through a P96 lock of that bitmap -- across the bus at
 * about 4.4 MB/s on the A4000D (MEASURED, BusBench), so 320 x 256 x 32 bit
 * is about 75 ms; the lock holds the board's other drawing for as long.
 * The null card has no pixels: it reads zeros.
 *
 * Linked after the hot code (Makefile RGL_OBJS, as rgllight.c).
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <libraries/Picasso96.h>
#include <proto/Picasso96.h>
#include <libraries/minigl_dispatch.h>

#include "rgl_dispatch.h"
#include "rgl.h"
#include "rglint.h"
#include "coreint.h"
#include "r200reg.h"

extern struct Library *P96Base;

/* One row of the frame as 0xAARRGGBB, from its bytes in video RAM: the
 * R200's own little-endian formats (the aperture does not swap) */
static void RowArgb(ULONG *out, const UBYTE *p, ULONG n, ULONG fmt)
{
    ULONG i;
    ULONG v;
    ULONG r, g, b;

    for (i = 0; i < n; i++)
    {
        if (fmt == R_COLOR_FORMAT_ARGB8888)
        {
            out[i] = ((ULONG) p[3] << 24) | ((ULONG) p[2] << 16) | ((ULONG) p[1] << 8) | p[0];
            p += 4;
            continue;
        }
        v = (ULONG) p[0] | ((ULONG) p[1] << 8);
        p += 2;
        if (fmt == R_COLOR_FORMAT_ARGB1555)
        {
            r = (v >> 10) & 31;
            g = (v >> 5) & 31;
            b = v & 31;
            out[i] = ((v & 0x8000UL) ? 0xff000000UL : 0) | (((r << 3) | (r >> 2)) << 16)
                   | (((g << 3) | (g >> 2)) << 8) | ((b << 3) | (b >> 2));
        }
        else
        {
            r = (v >> 11) & 31;
            g = (v >> 5) & 63;
            b = v & 31;
            out[i] = 0xff000000UL | (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8)
                   | ((b << 3) | (b >> 2));
        }
    }
}

/* Rows [y0, y1) (GL's, from the bottom) of columns [x0, x1) of the frame,
 * each to fn(ud, GL row, its pixels). FALSE when the frame was out of reach
 * (fn is then never called). fn runs inside the lock: no I/O. */
static BOOL ReadRows(struct Rgl *r, LONG x0, LONG x1, LONG y0, LONG y1,
                     void (*fn)(APTR ud, LONG row, const ULONG *argb), APTR ud)
{
    struct Card *k;
    struct CardBm *b;
    struct RenderInfo ri;
    ULONG *buf;
    UBYTE *base;
    ULONG bpr, bpp, fmt, gpu;
    LONG lock;
    LONG row;

    buf = (ULONG *) AllocVec((ULONG) (x1 - x0) * 4, MEMF_ANY);
    if (buf == NULL)
        return FALSE;
    k = &r->rad;
    if (!r->real || k->lost != 0 || P96Base == NULL)
    {
        /* the null card: nothing was drawn */
        for (row = 0; row < x1 - x0; row++)
            buf[row] = 0;
        for (row = y0; row < y1; row++)
            fn(ud, row, buf);
        FreeVec(buf);
        return TRUE;
    }
    (void) Card_WaitIdle(k);
    b = &k->b[(k->kind == CARD_KIND_BITMAP) ? CARD_SCREEN : CARD_TARGET];
    if (b->bm == NULL || b->lock != 0)
    {
        FreeVec(buf);
        return FALSE;
    }
    lock = p96LockBitMap(b->bm, (UBYTE *) &ri, sizeof(ri));
    if (lock == 0 || ri.Memory == NULL || !p96GetBitMapAttr(b->bm, P96BMA_ISONBOARD))
    {
        if (lock != 0)
            p96UnlockBitMap(b->bm, lock);
        FreeVec(buf);
        return FALSE;
    }
    bpp = k->bpp;
    if (k->kind == CARD_KIND_BITMAP)
    {
        base = (UBYTE *) ri.Memory;
        bpr = (ULONG) ri.BytesPerRow;
        fmt = k->show_format;
    }
    else
    {
        /* the colour buffer starts at the bitmap's first 4 KB boundary of
         * the GPU's address space (card.c TargetAt) */
        gpu = k->fb_start + ((ULONG) ri.Memory - (ULONG) k->membase) % k->ap_size;
        base = (UBYTE *) ri.Memory + (((gpu + 4095UL) & ~4095UL) - gpu);
        bpr = k->pitch * bpp;
        fmt = k->color_format;
    }
    for (row = y0; row < y1; row++)
    {
        RowArgb(buf, base + (ULONG) ((LONG) k->height - 1 - row) * bpr + (ULONG) x0 * bpp,
                (ULONG) (x1 - x0), fmt);
        fn(ud, row, buf);
    }
    p96UnlockBitMap(b->bm, lock);
    FreeVec(buf);
    return TRUE;
}

/* --- glReadPixels ------------------------------------------------------------------- */

struct ReadTo
{
    UBYTE *pixels;
    LONG x, y, x0;              /* the request's corner; the first column read */
    ULONG stride;               /* bytes a row, GL_PACK_ALIGNMENT applied */
    ULONG cpp;                  /* bytes a pixel out */
    ULONG format;
    ULONG n;                    /* columns read */
};

static void ToClient(APTR ud, LONG row, const ULONG *argb)
{
    struct ReadTo *t;
    UBYTE *p;
    ULONG i;
    ULONG v;

    t = (struct ReadTo *) ud;
    p = t->pixels + (ULONG) (row - t->y) * t->stride + (ULONG) (t->x0 - t->x) * t->cpp;
    for (i = 0; i < t->n; i++)
    {
        v = argb[i];
        if (t->format == GL_RGBA)
        {
            p[0] = (UBYTE) (v >> 16);
            p[1] = (UBYTE) (v >> 8);
            p[2] = (UBYTE) v;
            p[3] = (UBYTE) (v >> 24);
        }
        else if (t->format == GL_RGB)
        {
            p[0] = (UBYTE) (v >> 16);
            p[1] = (UBYTE) (v >> 8);
            p[2] = (UBYTE) v;
        }
        else if (t->format == GL_ALPHA)
            p[0] = (UBYTE) (v >> 24);
        else                        /* GL_LUMINANCE: GL's R + G + B, clamped */
        {
            v = ((v >> 16) & 0xff) + ((v >> 8) & 0xff) + (v & 0xff);
            p[0] = (UBYTE) (v > 255 ? 255 : v);
        }
        p += t->cpp;
    }
}

void Hook_GLReadPixels(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height,
                       GLenum format, GLenum type, GLvoid *pixels)
{
    struct Rgl *r;
    struct ReadTo t;
    LONG x0, x1, y0, y1;
    ULONG align;

    (void) context;
    r = (struct Rgl *) Rgl_Core();
    if (r == NULL)
        return;
    if (IN_BEGIN(&r->core))
    {
        Core_Error(&r->core, GL_INVALID_OPERATION);
        return;
    }
    if (width < 0 || height < 0)
    {
        Core_Error(&r->core, GL_INVALID_VALUE);
        return;
    }
    if ((format != GL_RGBA && format != GL_RGB && format != GL_ALPHA && format != GL_LUMINANCE)
        || type != GL_UNSIGNED_BYTE)
    {
        Core_Error(&r->core, GL_INVALID_ENUM);      /* the depth buffer is tiled: not read */
        return;
    }
    if (pixels == NULL || width == 0 || height == 0)
        return;
    t.pixels = (UBYTE *) pixels;
    t.x = x;
    t.y = y;
    t.format = format;
    t.cpp = (format == GL_RGBA) ? 4 : (format == GL_RGB) ? 3 : 1;
    align = r->core.pack_align;
    t.stride = ((ULONG) width * t.cpp + align - 1) & ~(align - 1);
    /* the part inside the frame; GL leaves the rest undefined (untouched) */
    x0 = (x < 0) ? 0 : x;
    y0 = (y < 0) ? 0 : y;
    x1 = x + width;
    y1 = y + height;
    if (x1 > (LONG) r->core.target.width)
        x1 = (LONG) r->core.target.width;
    if (y1 > (LONG) r->core.target.height)
        y1 = (LONG) r->core.target.height;
    if (x1 <= x0 || y1 <= y0)
        return;
    t.x0 = x0;
    t.n = (ULONG) (x1 - x0);
    if (!ReadRows(r, x0, x1, y0, y1, ToClient, &t))
        Core_Error(&r->core, GL_OUT_OF_MEMORY);
}

void Hook_GLPixelStorei(GLcontext context, GLenum pname, GLint param)
{
    struct Rgl *r;

    (void) context;
    r = (struct Rgl *) Rgl_Core();
    if (r == NULL)
        return;
    if (param != 1 && param != 2 && param != 4 && param != 8)
    {
        Core_Error(&r->core, GL_INVALID_VALUE);
        return;
    }
    if (pname == GL_PACK_ALIGNMENT)
        r->core.pack_align = (ULONG) param;
    else if (pname == GL_UNPACK_ALIGNMENT)
        r->core.unpack_align = (ULONG) param;  /* stored; texture uploads read tight rows */
    else
        Core_Error(&r->core, GL_INVALID_ENUM);
}

/* --- mglWriteShotPPM: the frame as a binary PPM --------------------------------------- */

struct Shot
{
    UBYTE *rgb;
    LONG w, h;
};

static void ToShot(APTR ud, LONG row, const ULONG *argb)
{
    struct Shot *s;
    UBYTE *p;
    LONG i;

    s = (struct Shot *) ud;
    p = s->rgb + (ULONG) (s->h - 1 - row) * (ULONG) s->w * 3;   /* PPM: top row first */
    for (i = 0; i < s->w; i++)
    {
        p[0] = (UBYTE) (argb[i] >> 16);
        p[1] = (UBYTE) (argb[i] >> 8);
        p[2] = (UBYTE) argb[i];
        p += 3;
    }
}

static ULONG Digits(char *d, ULONG v)
{
    char t[12];
    ULONG n;
    ULONG i;

    n = 0;
    do
    {
        t[n++] = (char) ('0' + v % 10);
        v /= 10;
    }
    while (v != 0);
    for (i = 0; i < n; i++)
        d[i] = t[n - 1 - i];
    return n;
}

void Hook_MGLWriteShotPPM(GLcontext context, char *filename)
{
    struct Rgl *r;
    struct Shot s;
    char head[40];
    ULONG n;
    BPTR f;

    (void) context;
    r = (struct Rgl *) Rgl_Core();
    if (r == NULL || filename == NULL)
        return;
    s.w = (LONG) r->core.target.width;
    s.h = (LONG) r->core.target.height;
    s.rgb = (UBYTE *) AllocVec((ULONG) s.w * (ULONG) s.h * 3, MEMF_ANY | MEMF_CLEAR);
    if (s.rgb == NULL)
        return;
    if (ReadRows(r, 0, s.w, 0, s.h, ToShot, &s))
    {
        /* the file after the unlock */
        n = 0;
        head[n++] = 'P';
        head[n++] = '6';
        head[n++] = '\n';
        n += Digits(head + n, (ULONG) s.w);
        head[n++] = ' ';
        n += Digits(head + n, (ULONG) s.h);
        head[n++] = '\n';
        head[n++] = '2';
        head[n++] = '5';
        head[n++] = '5';
        head[n++] = '\n';
        f = Open((STRPTR) filename, MODE_NEWFILE);
        if (f != 0)
        {
            Write(f, head, (LONG) n);
            Write(f, s.rgb, (LONG) ((ULONG) s.w * (ULONG) s.h * 3));
            Close(f);
        }
    }
    FreeVec(s.rgb);
}
