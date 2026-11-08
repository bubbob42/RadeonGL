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
    Card_Obtain();                  /* no other card's frame while this one is read (W3) */
    (void) Card_WaitIdle(k);
    b = &k->b[(k->kind == CARD_KIND_BITMAP) ? CARD_SCREEN : CARD_TARGET];
    if (b->bm == NULL || b->lock != 0)
    {
        Card_Release();
        FreeVec(buf);
        return FALSE;
    }
    lock = p96LockBitMap(b->bm, (UBYTE *) &ri, sizeof(ri));
    if (lock == 0 || ri.Memory == NULL || !p96GetBitMapAttr(b->bm, P96BMA_ISONBOARD))
    {
        if (lock != 0)
            p96UnlockBitMap(b->bm, lock);
        Card_Release();
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
    Card_Release();
    FreeVec(buf);
    return TRUE;
}

/* --- glReadPixels ------------------------------------------------------------------- */

struct ReadTo
{
    UBYTE *pixels;              /* the request's first pixel, GL_PACK_SKIP_* applied */
    LONG x, y, x0;              /* the request's corner; the first column read */
    ULONG stride;               /* bytes a row (GL 1.1 4.3.2: the pack state) */
    ULONG cpp;                  /* bytes a pixel out */
    ULONG format;
    ULONG type, tsize;          /* the component type and its bytes */
    ULONG swap;                 /* GL_PACK_SWAP_BYTES */
    ULONG n;                    /* columns read */
};

/* One component (0..255) as type, GL 1.1 table 2.7's conversion */
static void PutComp(UBYTE *p, ULONG v, ULONG type, ULONG swap)
{
    ULONG u;
    float f;
    UBYTE *fb;

    switch (type)
    {
        case GL_UNSIGNED_BYTE:  p[0] = (UBYTE) v; return;
        case GL_BYTE:           p[0] = (UBYTE) (v >> 1); return;
        case GL_UNSIGNED_SHORT: u = v * 257UL; break;
        case GL_SHORT:          u = (v * 257UL) >> 1; break;
        case GL_UNSIGNED_INT:   u = v * 0x01010101UL; break;
        case GL_INT:            u = (v * 0x01010101UL) >> 1; break;
        default:                /* GL_FLOAT: its bytes, as the 68k keeps them */
            f = (float) v / 255.0f;
            fb = (UBYTE *) &f;
            u = ((ULONG) fb[0] << 24) | ((ULONG) fb[1] << 16) | ((ULONG) fb[2] << 8) | fb[3];
            break;
    }
    if (type == GL_UNSIGNED_SHORT || type == GL_SHORT)
    {
        p[swap ? 1 : 0] = (UBYTE) (u >> 8);
        p[swap ? 0 : 1] = (UBYTE) u;
        return;
    }
    p[swap ? 3 : 0] = (UBYTE) (u >> 24);
    p[swap ? 2 : 1] = (UBYTE) (u >> 16);
    p[swap ? 1 : 2] = (UBYTE) (u >> 8);
    p[swap ? 0 : 3] = (UBYTE) u;
}

static void ToClient(APTR ud, LONG row, const ULONG *argb)
{
    struct ReadTo *t;
    UBYTE *p;
    ULONG i;
    ULONG v;
    ULONG c[4];
    ULONG k;
    ULONG n;
    ULONG s;

    t = (struct ReadTo *) ud;
    s = t->tsize;
    p = t->pixels + (ULONG) (row - t->y) * t->stride + (ULONG) (t->x0 - t->x) * t->cpp;
    for (i = 0; i < t->n; i++)
    {
        v = argb[i];
        n = 1;
        switch (t->format)
        {
            case GL_RGBA:
                c[0] = (v >> 16) & 0xff; c[1] = (v >> 8) & 0xff; c[2] = v & 0xff; c[3] = v >> 24;
                n = 4;
                break;
            case GL_RGB:
                c[0] = (v >> 16) & 0xff; c[1] = (v >> 8) & 0xff; c[2] = v & 0xff;
                n = 3;
                break;
            case GL_RED:    c[0] = (v >> 16) & 0xff; break;
            case GL_GREEN:  c[0] = (v >> 8) & 0xff; break;
            case GL_BLUE:   c[0] = v & 0xff; break;
            case GL_ALPHA:  c[0] = v >> 24; break;
            default:        /* GL_LUMINANCE(_ALPHA): GL's R + G + B, clamped */
                c[0] = ((v >> 16) & 0xff) + ((v >> 8) & 0xff) + (v & 0xff);
                if (c[0] > 255)
                    c[0] = 255;
                if (t->format == GL_LUMINANCE_ALPHA)
                {
                    c[1] = v >> 24;
                    n = 2;
                }
                break;
        }
        if (t->type == GL_UNSIGNED_BYTE)
            for (k = 0; k < n; k++)
                p[k] = (UBYTE) c[k];
        else
            for (k = 0; k < n; k++)
                PutComp(p + k * s, c[k], t->type, t->swap);
        p += t->cpp;
    }
}

static ULONG ReadComps(ULONG format)
{
    switch (format)
    {
        case GL_RGBA:               return 4;
        case GL_RGB:                return 3;
        case GL_LUMINANCE_ALPHA:    return 2;
        case GL_RED:
        case GL_GREEN:
        case GL_BLUE:
        case GL_ALPHA:
        case GL_LUMINANCE:          return 1;
    }
    return 0;
}

static ULONG ReadTypeSize(ULONG type)
{
    switch (type)
    {
        case GL_UNSIGNED_BYTE:
        case GL_BYTE:           return 1;
        case GL_UNSIGNED_SHORT:
        case GL_SHORT:          return 2;
        case GL_UNSIGNED_INT:
        case GL_INT:
        case GL_FLOAT:          return 4;
    }
    return 0;
}

void Hook_GLReadPixels(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height,
                       GLenum format, GLenum type, GLvoid *pixels)
{
    struct Rgl *r;
    struct Core *c;
    struct ReadTo t;
    LONG x0, x1, y0, y1;
    ULONG n, s, l, k, a;

    r = (struct Rgl *) context;
    if (r == NULL)
        return;
    c = &r->core;
    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    if (width < 0 || height < 0)
    {
        Core_Error(c, GL_INVALID_VALUE);
        return;
    }
    /* no depth buffer to read (it is tiled), no colour index */
    if (format == GL_DEPTH_COMPONENT || format == GL_COLOR_INDEX)
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    n = ReadComps(format);
    s = ReadTypeSize(type);
    if (n == 0 || s == 0)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    if (pixels == NULL || width == 0 || height == 0)
        return;
    /* GL 1.1 4.3.2: rows of k elements of s bytes */
    l = c->pack_row ? c->pack_row : (ULONG) width;
    a = c->pack_align;
    if (s >= a)
        k = n * l;
    else
        k = (a / s) * ((s * n * l + a - 1) / a);
    t.stride = s * k;
    t.pixels = (UBYTE *) pixels + c->pack_skip_rows * t.stride + c->pack_skip_px * s * n;
    t.x = x;
    t.y = y;
    t.format = format;
    t.type = type;
    t.tsize = s;
    t.swap = c->pack_swap;
    t.cpp = n * s;
    /* the part inside the frame; GL leaves the rest undefined (untouched) */
    x0 = (x < 0) ? 0 : x;
    y0 = (y < 0) ? 0 : y;
    x1 = x + width;
    y1 = y + height;
    if (x1 > (LONG) c->target.width)
        x1 = (LONG) c->target.width;
    if (y1 > (LONG) c->target.height)
        y1 = (LONG) c->target.height;
    if (x1 <= x0 || y1 <= y0)
        return;
    t.x0 = x0;
    t.n = (ULONG) (x1 - x0);
    if (!ReadRows(r, x0, x1, y0, y1, ToClient, &t))
        Core_Error(c, GL_OUT_OF_MEMORY);
}

void Hook_GLPixelStorei(GLcontext context, GLenum pname, GLint param)
{
    if (context != NULL)
        Core_PixelStore((struct Core *) context, pname, param);   /* compat.c: all twelve */
}

/* --- glCopyTexImage2D, glCopyTexSubImage2D (29.1's, 2026-11-08) ---------------------------
 * A rectangle of the frame into a texture: read through ReadRows (which
 * first draws whatever is pending, Card_WaitIdle) as RGBA bytes, rows from
 * the bottom as a texture's go, then uploaded as glTexImage2D and
 * glTexSubImage2D upload them. Pixels outside the frame are black. */

struct CopyTo
{
    UBYTE *buf;                 /* w x h RGBA */
    LONG x, y, x0;              /* the rectangle's corner; the first column read */
    ULONG w;                    /* the rectangle's width */
    ULONG n;                    /* columns read */
};

static void ToCopy(APTR ud, LONG row, const ULONG *argb)
{
    struct CopyTo *t;
    UBYTE *p;
    ULONG i;
    ULONG v;

    t = (struct CopyTo *) ud;
    p = t->buf + ((ULONG) (row - t->y) * t->w + (ULONG) (t->x0 - t->x)) * 4;
    for (i = 0; i < t->n; i++)
    {
        v = argb[i];
        p[0] = (UBYTE) (v >> 16);
        p[1] = (UBYTE) (v >> 8);
        p[2] = (UBYTE) v;
        p[3] = (UBYTE) (v >> 24);
        p += 4;
    }
}

/* The w x h rectangle at (x, y) as RGBA bytes (AllocVec, w * h * 4); NULL
 * with a GL error raised */
static UBYTE *CopyRect(struct Rgl *r, LONG x, LONG y, LONG w, LONG h)
{
    struct CopyTo t;
    UBYTE *buf;
    LONG x0, x1, y0, y1;

    buf = (UBYTE *) AllocVec((ULONG) w * (ULONG) h * 4, MEMF_ANY | MEMF_CLEAR);
    if (buf == NULL)
    {
        Core_Error(&r->core, GL_OUT_OF_MEMORY);
        return NULL;
    }
    t.buf = buf;
    t.x = x;
    t.y = y;
    t.w = (ULONG) w;
    x0 = (x < 0) ? 0 : x;
    y0 = (y < 0) ? 0 : y;
    x1 = x + w;
    y1 = y + h;
    if (x1 > (LONG) r->core.target.width)
        x1 = (LONG) r->core.target.width;
    if (y1 > (LONG) r->core.target.height)
        y1 = (LONG) r->core.target.height;
    if (x1 > x0 && y1 > y0)
    {
        t.x0 = x0;
        t.n = (ULONG) (x1 - x0);
        if (!ReadRows(r, x0, x1, y0, y1, ToCopy, &t))
        {
            FreeVec(buf);
            Core_Error(&r->core, GL_OUT_OF_MEMORY);
            return NULL;
        }
    }
    return buf;
}

/* GL's checks before the read: GL_INVALID_OPERATION inside glBegin,
 * GL_INVALID_ENUM for another target, GL_INVALID_VALUE for a size the
 * texture code would refuse anyway. TRUE: go on. */
static BOOL CopyArgs(struct Rgl *r, ULONG target, LONG w, LONG h)
{
    if (IN_BEGIN(&r->core))
        Core_Error(&r->core, GL_INVALID_OPERATION);
    else if (target != GL_TEXTURE_2D)
        Core_Error(&r->core, GL_INVALID_ENUM);
    else if (w < 1 || h < 1 || w > CORE_MAX_TEXSIZE || h > CORE_MAX_TEXSIZE)
        Core_Error(&r->core, GL_INVALID_VALUE);
    else
        return TRUE;
    return FALSE;
}

void Hook_GLCopyTexImage2D(GLcontext context, GLenum target, GLint level, GLenum internalformat,
                           GLint x, GLint y, GLsizei width, GLsizei height, GLint border)
{
    struct Rgl *r;
    UBYTE *buf;

    r = (struct Rgl *) context;
    if (r == NULL || !CopyArgs(r, target, width, height))
        return;
    if (border != 0)
    {
        Core_Error(&r->core, GL_INVALID_VALUE);
        return;
    }
    if ((buf = CopyRect(r, x, y, width, height)) == NULL)
        return;
    Core_TexImage2D(&r->core, target, level, (LONG) internalformat, width, height, 0, GL_RGBA,
                    GL_UNSIGNED_BYTE, buf);
    FreeVec(buf);
}

void Hook_GLCopyTexSubImage2D(GLcontext context, GLenum target, GLint level, GLint xoffset,
                              GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height)
{
    struct Rgl *r;
    UBYTE *buf;

    r = (struct Rgl *) context;
    if (r == NULL || !CopyArgs(r, target, width, height))
        return;
    if ((buf = CopyRect(r, x, y, width, height)) == NULL)
        return;
    Core_TexSubImage2D(&r->core, target, level, xoffset, yoffset, width, height, GL_RGBA,
                       GL_UNSIGNED_BYTE, buf);
    FreeVec(buf);
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

    r = (struct Rgl *) context;
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
