/*
 * compat.c -- the rest of minigl.library 29.1's calls (2026-11-08, the
 * user: "get on par with Steffen's 29.1 first"): glArrayElement and
 * glMultiDrawArrays, glTexCoord4f, gluPerspective and gluLookAt, the
 * texture environment colour with GL_BLEND and RGBA GL_DECAL, texture
 * uploads in any GL 1.1 layout and component type, the pixel store, the
 * glGet names and enables 29.1 answers, edge flags and colour index,
 * glDrawBuffer and glReadBuffer, texture residency and memory.
 *
 * Linked last, like light.c and arraylit.c: code added to the hot objects
 * moves GLQuake's code and has cost it ~0.8% on the 68060 twice (MEASURED
 * 2026-11-07 and -08). So the library's hooks call the ...X wrappers here
 * in place of the plain calls -- a call of the same size -- and the
 * wrappers handle what is new and pass everything else on. The one change
 * in a hot object is hwstate.c's texture environment switch, which sends
 * GL_DECAL and GL_BLEND here (Core_CombExtra).
 *
 * The behaviour follows GL 1.1 and, where MiniGL has its own rules,
 * MiniGLV3D 29.1 (Steffen Haeuser, Dennis van der Boon): written anew
 * from those rules, nothing copied.
 */

#include "core.h"
#include "coreint.h"
#include "r200reg.h"
#include "arrayfetch.h"

/* GL_EXTENSIONS for Core_GetString (get.c): kept here, behind the hot code. */
const char Core_Extensions[] = CORE_EXTENSIONS;

/* GL's defaults for the state compat.c adds (Fog_Defaults calls it at
 * Core_Init; the rest of that state starts zeroed) */
void Compat_Defaults(struct Core *c)
{
    c->cur_edge = 1;
    c->cur_index = 1.0f;
    c->pm_front = GL_FILL;                  /* glPolygonMode (polymode.c) */
    c->pm_back = GL_FILL;
    c->array_xform = 1;                     /* 29.1 starts with it on */
    c->unpack_plain = 1;
    c->blend_eq_rgb = GL_FUNC_ADD;
    c->blend_eq_a = GL_FUNC_ADD;
    c->draw_buffer = 0;                     /* 0: GL's default, front or back as the */
    c->read_buffer = 0;                     /* context has (single_buffer, the library) */
}

static ULONG DefBuffer(struct Core *c, ULONG mode)
{
    return mode ? mode : c->single_buffer ? GL_FRONT : GL_BACK;
}

/* --- Enables ------------------------------------------------------------------------ */

/* Core_Enable's path for a cap state.c does not know (it checked glBegin):
 * those 29.1 adds, else GL_INVALID_ENUM. GLQuake's caps never come here --
 * a call into this module (linked last) costs the 68060 a cache line, and
 * wrappers in front of every glEnable cost GLQuake 1% (MEASURED 2026-11-08) */
void Compat_Enable(struct Core *c, ULONG cap, ULONG on)
{
    ULONG *flag;

    switch (cap)
    {
        case GL_DEPTH_WRITEMASK:            /* MiniGL's own cap: the depth mask */
            Core_DepthMask(c, on);
            return;
        case GL_POLYGON_OFFSET_LINE:        /* only filled polygons are drawn: stored */
            flag = &c->poly_offset_line;
            break;
        case GL_POLYGON_OFFSET_POINT:
            flag = &c->poly_offset_point;
            break;
        case MGL_ARRAY_TRANSFORMATIONS:     /* stored and answered, as 29.1 does */
            flag = &c->array_xform;
            break;
        case GL_SHARED_TEXTURE_PALETTE_EXT: /* the one palette serves anyway: stored */
            flag = &c->shared_pal;
            break;
        default:
            Core_Error(c, GL_INVALID_ENUM);
            return;
    }
    *flag = on ? 1UL : 0UL;
}

/* Core_IsEnabled's path for the same: the client arrays too */
ULONG Compat_IsEnabled(struct Core *c, ULONG cap)
{
    switch (cap)
    {
        case GL_VERTEX_ARRAY:           return c->va.enabled;
        case GL_COLOR_ARRAY:            return c->ca.enabled;
        case GL_NORMAL_ARRAY:           return c->na.enabled;
        case GL_TEXTURE_COORD_ARRAY:    return c->ta[c->client_unit].enabled;
        case GL_EDGE_FLAG_ARRAY:        return c->ea.enabled;
        case GL_INDEX_ARRAY:            return c->ia.enabled;
        case GL_DEPTH_WRITEMASK:        return c->depth_mask ? 1UL : 0UL;
        case GL_POLYGON_OFFSET_LINE:    return c->poly_offset_line;
        case GL_POLYGON_OFFSET_POINT:   return c->poly_offset_point;
        case MGL_ARRAY_TRANSFORMATIONS: return c->array_xform;
        case GL_SHARED_TEXTURE_PALETTE_EXT: return c->shared_pal;
    }
    Core_Error(c, GL_INVALID_ENUM);
    return 0;
}

/* Core_EnableClientState's path for an array array.c does not know */
void Compat_ClientState(struct Core *c, ULONG array, ULONG on)
{
    if (array == GL_EDGE_FLAG_ARRAY)
        c->ea.enabled = on ? 1UL : 0UL;     /* edge flags and colour index: no effect */
    else if (array == GL_INDEX_ARRAY)
        c->ia.enabled = on ? 1UL : 0UL;
    else
        Core_Error(c, GL_INVALID_ENUM);
}

/* --- Queries ------------------------------------------------------------------------ */

/* An array's type, size and stride, GL's defaults while it was never set */
static ULONG ArrType(const struct CoreArray *a)
{
    return a->type ? a->type : GL_FLOAT;
}

static ULONG ArrSize(const struct CoreArray *a)
{
    return a->size ? a->size : 4UL;
}

/* The names 29.1 answers that get.c does not: how many values went into
 * v, 0 for a name of get.c's (or none) */
static ULONG QueryX(struct Core *c, ULONG pname, float *v)
{
    ULONG f;
    ULONG i;
    const struct CoreArray *ta;

    f = c->target.color_format;
    ta = &c->ta[c->client_unit];
    switch (pname)
    {
        /* the frame buffer */
        case GL_RED_BITS:
        case GL_BLUE_BITS:
            v[0] = (f == R_COLOR_FORMAT_ARGB8888) ? 8.0f : 5.0f;
            return 1;
        case GL_GREEN_BITS:
            v[0] = (f == R_COLOR_FORMAT_ARGB8888) ? 8.0f
                 : (f == R_COLOR_FORMAT_ARGB1555) ? 5.0f : 6.0f;
            return 1;
        case GL_ALPHA_BITS:
            v[0] = (f == R_COLOR_FORMAT_ARGB8888) ? 8.0f
                 : (f == R_COLOR_FORMAT_ARGB1555) ? 1.0f : 0.0f;
            return 1;
        case GL_DEPTH_BITS:         v[0] = 16.0f; return 1;
        case GL_RGBA_MODE:          v[0] = 1.0f; return 1;
        case GL_INDEX_MODE:         v[0] = 0.0f; return 1;
        case GL_DOUBLEBUFFER:       v[0] = c->single_buffer ? 0.0f : 1.0f; return 1;
        case GL_DRAW_BUFFER:        v[0] = (float) DefBuffer(c, c->draw_buffer); return 1;
        case GL_READ_BUFFER:        v[0] = (float) DefBuffer(c, c->read_buffer); return 1;
        case GL_MAX_VIEWPORT_DIMS:
            v[0] = (float) CORE_MAX_TEXSIZE;
            v[1] = (float) CORE_MAX_TEXSIZE;
            return 2;
        /* the matrix stacks */
        case GL_MODELVIEW_STACK_DEPTH:  v[0] = (float) (c->mv_top + 1); return 1;
        case GL_PROJECTION_STACK_DEPTH: v[0] = (float) (c->proj_top + 1); return 1;
        case GL_TEXTURE_STACK_DEPTH:    v[0] = (float) (c->tex_top[c->active_unit] + 1); return 1;
        /* the client arrays */
        case GL_VERTEX_ARRAY:
        case GL_COLOR_ARRAY:
        case GL_NORMAL_ARRAY:
        case GL_TEXTURE_COORD_ARRAY:
        case GL_EDGE_FLAG_ARRAY:
        case GL_INDEX_ARRAY:
            v[0] = (float) Core_IsEnabled(c, pname);
            return 1;
        case GL_VERTEX_ARRAY_SIZE:          v[0] = (float) ArrSize(&c->va); return 1;
        case GL_VERTEX_ARRAY_TYPE:          v[0] = (float) ArrType(&c->va); return 1;
        case GL_VERTEX_ARRAY_STRIDE:        v[0] = (float) c->va.stride; return 1;
        case GL_COLOR_ARRAY_SIZE:           v[0] = (float) ArrSize(&c->ca); return 1;
        case GL_COLOR_ARRAY_TYPE:           v[0] = (float) ArrType(&c->ca); return 1;
        case GL_COLOR_ARRAY_STRIDE:         v[0] = (float) c->ca.stride; return 1;
        case GL_TEXTURE_COORD_ARRAY_SIZE:   v[0] = (float) ArrSize(ta); return 1;
        case GL_TEXTURE_COORD_ARRAY_TYPE:   v[0] = (float) ArrType(ta); return 1;
        case GL_TEXTURE_COORD_ARRAY_STRIDE: v[0] = (float) ta->stride; return 1;
        case GL_NORMAL_ARRAY_TYPE:          v[0] = (float) ArrType(&c->na); return 1;
        case GL_NORMAL_ARRAY_STRIDE:        v[0] = (float) c->na.stride; return 1;
        case GL_INDEX_ARRAY_TYPE:           v[0] = (float) ArrType(&c->ia); return 1;
        case GL_INDEX_ARRAY_STRIDE:         v[0] = (float) c->ia.stride; return 1;
        case GL_EDGE_FLAG_ARRAY_STRIDE:     v[0] = (float) c->ea.stride; return 1;
        /* blending: one equation, one pair of factors for colour and alpha */
        case GL_BLEND_EQUATION:     v[0] = (float) c->blend_eq_rgb; return 1;
        case GL_BLEND_SRC_RGB:      v[0] = (float) c->blend_src; return 1;
        case GL_BLEND_DST_RGB:      v[0] = (float) c->blend_dst; return 1;
        case GL_BLEND_SRC_ALPHA:    v[0] = (float) (c->blend_sep ? c->blend_src_a : c->blend_src); return 1;
        case GL_BLEND_DST_ALPHA:    v[0] = (float) (c->blend_sep ? c->blend_dst_a : c->blend_dst); return 1;
        /* lighting, rasterising */
        case GL_COLOR_MATERIAL_FACE:        v[0] = (float) c->cm_face; return 1;
        case GL_COLOR_MATERIAL_PARAMETER:   v[0] = (float) c->cm_mode; return 1;
        case GL_LINE_WIDTH:         v[0] = c->line_width; return 1;
        case GL_POLYGON_MODE:
            v[0] = (float) c->pm_front;     /* front, back (polymode.c) */
            v[1] = (float) c->pm_back;
            return 2;
        case GL_POLYGON_OFFSET_FACTOR:  v[0] = c->poly_factor; return 1;
        case GL_POLYGON_OFFSET_UNITS:   v[0] = c->poly_units; return 1;
        case GL_FOG_MODE:           v[0] = (float) c->fog_mode; return 1;
        case GL_FOG_DENSITY:        v[0] = c->fog_density; return 1;
        case GL_FOG_START:          v[0] = c->fog_start; return 1;
        case GL_FOG_END:            v[0] = c->fog_end; return 1;
        case GL_FOG_COLOR:
            for (i = 0; i < 4; i++)
                v[i] = c->fog_color[i];
            return 4;
        /* the current values */
        case GL_CURRENT_INDEX:      v[0] = c->cur_index; return 1;
        case GL_EDGE_FLAG:          v[0] = (float) c->cur_edge; return 1;
        /* the pixel store */
        case GL_PACK_ALIGNMENT:     v[0] = (float) c->pack_align; return 1;
        case GL_PACK_ROW_LENGTH:    v[0] = (float) c->pack_row; return 1;
        case GL_PACK_SKIP_PIXELS:   v[0] = (float) c->pack_skip_px; return 1;
        case GL_PACK_SKIP_ROWS:     v[0] = (float) c->pack_skip_rows; return 1;
        case GL_PACK_SWAP_BYTES:    v[0] = (float) c->pack_swap; return 1;
        case GL_PACK_LSB_FIRST:     v[0] = (float) c->pack_lsb; return 1;
        case GL_UNPACK_ALIGNMENT:   v[0] = (float) c->unpack_align; return 1;
        case GL_UNPACK_ROW_LENGTH:  v[0] = (float) c->unpack_row; return 1;
        case GL_UNPACK_SKIP_PIXELS: v[0] = (float) c->unpack_skip_px; return 1;
        case GL_UNPACK_SKIP_ROWS:   v[0] = (float) c->unpack_skip_rows; return 1;
        case GL_UNPACK_SWAP_BYTES:  v[0] = (float) c->unpack_swap; return 1;
        case GL_UNPACK_LSB_FIRST:   v[0] = (float) c->unpack_lsb; return 1;
        /* enables get.c does not list (glGet answers every cap, GL 1.1 6.1.1) */
        case GL_COLOR_MATERIAL:
        case GL_NORMALIZE:
        case GL_POINT_SMOOTH:
        case GL_LINE_SMOOTH:
        case GL_POLYGON_SMOOTH:
        case GL_STENCIL_TEST:
        case GL_LINE_STIPPLE:
        case GL_POLYGON_STIPPLE:
        case GL_COLOR_LOGIC_OP:
        case GL_TEXTURE_GEN_S:
        case GL_TEXTURE_GEN_T:
        case GL_POLYGON_OFFSET_LINE:
        case GL_POLYGON_OFFSET_POINT:
        case MGL_Z_OFFSET:
        case MGL_PERSPECTIVE_MAPPING:
        case MGL_ARRAY_TRANSFORMATIONS:
        case GL_SHARED_TEXTURE_PALETTE_EXT:
            v[0] = (float) Core_IsEnabled(c, pname);
            return 1;
    }
    if (pname >= GL_LIGHT0 && pname <= GL_LIGHT0 + 7)
    {
        v[0] = (float) Core_IsEnabled(c, pname);
        return 1;
    }
    return 0;
}

ULONG Core_GetCountX(ULONG pname)
{
    switch (pname)
    {
        case GL_MAX_VIEWPORT_DIMS:
        case GL_POLYGON_MODE:       return 2;
        case GL_FOG_COLOR:          return 4;
    }
    return Core_GetCount(pname);
}

void Core_GetFloatvX(struct Core *c, ULONG pname, float *out)
{
    float v[4];
    ULONG n;
    ULONG i;

    n = QueryX(c, pname, v);
    if (n == 0)
    {
        Core_GetFloatv(c, pname, out);
        return;
    }
    for (i = 0; i < n; i++)
        out[i] = v[i];
}

void Core_GetIntegervX(struct Core *c, ULONG pname, LONG *out)
{
    float v[4];
    ULONG n;
    ULONG i;

    n = QueryX(c, pname, v);
    if (n == 0)
    {
        Core_GetIntegerv(c, pname, out);
        return;
    }
    for (i = 0; i < n; i++)
    {
        /* a colour maps to the whole integer range (GL 1.1 6.1.2), as in get.c */
        if (pname == GL_FOG_COLOR)
            out[i] = (v[i] >= 1.0f) ? 0x7fffffffL : Core_Floor(v[i] * 2147483647.0f);
        else
            out[i] = Core_Floor(v[i] + 0.5f);
    }
}

void Core_GetPointerv(struct Core *c, ULONG pname, void **out)
{
    const UBYTE *p;

    switch (pname)
    {
        case GL_VERTEX_ARRAY_POINTER:           p = c->va.ptr; break;
        case GL_COLOR_ARRAY_POINTER:            p = c->ca.ptr; break;
        case GL_NORMAL_ARRAY_POINTER:           p = c->na.ptr; break;
        case GL_TEXTURE_COORD_ARRAY_POINTER:    p = c->ta[c->client_unit].ptr; break;
        case GL_INDEX_ARRAY_POINTER:            p = c->ia.ptr; break;
        case GL_EDGE_FLAG_ARRAY_POINTER:        p = c->ea.ptr; break;
        default:
            Core_Error(c, GL_INVALID_ENUM);
            return;
    }
    if (out != NULL)
        *out = (void *) p;
}

/* --- The pixel store ---------------------------------------------------------------- */

/* unpack_plain: the unpack state texture.c reads uploads in -- rows padded
 * to 4 bytes, nothing skipped or swapped -- so that an upload of unsigned
 * bytes goes on at once (Core_TexSubImage2DX: GLQuake's lightmaps, every
 * frame) */
static void PlainState(struct Core *c)
{
    c->unpack_plain = (c->unpack_align == 4 && c->unpack_row == 0 && c->unpack_skip_px == 0
                       && c->unpack_skip_rows == 0 && !c->unpack_swap) ? 1UL : 0UL;
}

void Core_PixelStore(struct Core *c, ULONG pname, LONG param)
{
    ULONG *f;

    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    switch (pname)
    {
        case GL_PACK_ALIGNMENT:
        case GL_UNPACK_ALIGNMENT:
            if (param != 1 && param != 2 && param != 4 && param != 8)
            {
                Core_Error(c, GL_INVALID_VALUE);
                return;
            }
            if (pname == GL_PACK_ALIGNMENT)
                c->pack_align = (ULONG) param;
            else
                c->unpack_align = (ULONG) param;
            PlainState(c);
            return;
        case GL_PACK_SWAP_BYTES:    c->pack_swap = param ? 1UL : 0UL; return;
        case GL_PACK_LSB_FIRST:     c->pack_lsb = param ? 1UL : 0UL; return;   /* bitmaps only */
        case GL_UNPACK_SWAP_BYTES:  c->unpack_swap = param ? 1UL : 0UL; PlainState(c); return;
        case GL_UNPACK_LSB_FIRST:   c->unpack_lsb = param ? 1UL : 0UL; return;
        case GL_PACK_ROW_LENGTH:    f = &c->pack_row; break;
        case GL_PACK_SKIP_PIXELS:   f = &c->pack_skip_px; break;
        case GL_PACK_SKIP_ROWS:     f = &c->pack_skip_rows; break;
        case GL_UNPACK_ROW_LENGTH:  f = &c->unpack_row; break;
        case GL_UNPACK_SKIP_PIXELS: f = &c->unpack_skip_px; break;
        case GL_UNPACK_SKIP_ROWS:   f = &c->unpack_skip_rows; break;
        default:
            Core_Error(c, GL_INVALID_ENUM);
            return;
    }
    if (param < 0)
    {
        Core_Error(c, GL_INVALID_VALUE);
        return;
    }
    *f = (ULONG) param;
    PlainState(c);
}

/* --- Texture uploads in any layout and type ------------------------------------------
 * texture.c reads unsigned bytes (and MiniGL's packed 16-bit layouts) in
 * rows padded to 4 bytes -- GL's default GL_UNPACK_ALIGNMENT. Anything else
 * -- another alignment, ROW_LENGTH, SKIP_*, SWAP_BYTES, the other component
 * types, GL_RED/GREEN/BLUE -- is turned into that first, in a buffer of
 * its own. */

static ULONG TypeBytes(ULONG type)
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

static ULONG FormatComps(ULONG format)
{
    switch (format)
    {
        case GL_RGBA:               return 4;
        case GL_RGB:                return 3;
        case GL_LUMINANCE_ALPHA:    return 2;
        case GL_LUMINANCE:
        case GL_ALPHA:
        case GL_RED:
        case GL_GREEN:
        case GL_BLUE:               return 1;
    }
    return 0;
}

static ULONG Packed16(ULONG format, ULONG type)
{
    return (format == MGL_UNSIGNED_SHORT_4_4_4_4 || type == MGL_UNSIGNED_SHORT_4_4_4_4
            || format == MGL_UNSIGNED_SHORT_5_6_5 || type == MGL_UNSIGNED_SHORT_5_6_5) ? 1UL : 0UL;
}

/* Component k bytes at p, bytes swapped when asked */
static ULONG Word16(const UBYTE *p, ULONG swap)
{
    return swap ? ((ULONG) p[1] << 8) | p[0] : ((ULONG) p[0] << 8) | p[1];
}

static ULONG Word32(const UBYTE *p, ULONG swap)
{
    if (swap)
        return ((ULONG) p[3] << 24) | ((ULONG) p[2] << 16) | ((ULONG) p[1] << 8) | p[0];
    return ((ULONG) p[0] << 24) | ((ULONG) p[1] << 16) | ((ULONG) p[2] << 8) | p[3];
}

/* One component as GL 1.1 converts it (table 2.6, signed values from
 * (2c + 1) / (2^b - 1), clamped to [0, 1]) and then to an unsigned byte */
static ULONG ToByte(const UBYTE *p, ULONG type, ULONG swap)
{
    ULONG u;
    LONG i;
    float f;
    UBYTE *fb;

    switch (type)
    {
        case GL_UNSIGNED_BYTE:
            return p[0];
        case GL_BYTE:
            i = (LONG) (BYTE) p[0];
            return (i < 0) ? 0UL : (ULONG) (2 * i + 1);
        case GL_UNSIGNED_SHORT:
            return (Word16(p, swap) * 255UL + 32767UL) / 65535UL;
        case GL_SHORT:
            i = (LONG) (WORD) Word16(p, swap);
            return (i < 0) ? 0UL : ((ULONG) (2 * i + 1) * 255UL + 32767UL) / 65535UL;
        case GL_UNSIGNED_INT:
            return ((Word32(p, swap) >> 16) * 255UL + 32767UL) / 65535UL;
        case GL_INT:
            i = (LONG) Word32(p, swap);
            return (i < 0) ? 0UL : (((ULONG) i >> 15) * 255UL + 32767UL) / 65535UL;
        case GL_FLOAT:
            u = Word32(p, swap);
            fb = (UBYTE *) &f;          /* the float's bytes, big-endian as the 68k keeps them */
            fb[0] = (UBYTE) (u >> 24);
            fb[1] = (UBYTE) (u >> 16);
            fb[2] = (UBYTE) (u >> 8);
            fb[3] = (UBYTE) u;
            if (!(f > 0.0f))
                return 0;
            if (f >= 1.0f)
                return 255;
            return (ULONG) Core_Floor(f * 255.0f + 0.5f);
    }
    return 0;
}

/* The upload as texture.c reads it: NULL when it already is (*format,
 * *type and *pixels unchanged), else a buffer of *size bytes the caller
 * frees, with *format, *type and *pixels describing it. A format or type
 * texture.c refuses anyway goes on unchanged, to be refused there. */
static void *Unpack(struct Core *c, LONG w, LONG h, ULONG *format, ULONG *type,
                    const void **pixels, ULONG *size)
{
    ULONG packed, plain, index;
    ULONG s, n, l, k, a;
    ULONG stride, rowbytes, row4;
    ULONG outfmt, outcpp;
    ULONG i, j, q, ch;
    const UBYTE *in;
    const UBYTE *p;
    UBYTE *out;
    UBYTE *o;

    if (*pixels == NULL || w < 1 || h < 1 || w > CORE_MAX_TEXSIZE || h > CORE_MAX_TEXSIZE)
        return NULL;
    index = (*format == GL_COLOR_INDEX) ? 1UL : 0UL;
    packed = index ? 0UL : Packed16(*format, *type);
    if (index)
    {
        /* through glColorTable's palette, as 29.1: unsigned bytes only, and
         * refused (nothing uploaded) before a palette was given */
        if (*type != GL_UNSIGNED_BYTE || c->pal_size == 0)
        {
            Core_Error(c, (*type != GL_UNSIGNED_BYTE) ? GL_INVALID_ENUM : GL_INVALID_OPERATION);
            *size = 1;                      /* said: the caller uploads nothing */
            return NULL;
        }
        s = 1;
        n = 1;
    }
    else if (packed)
    {
        s = 2;
        n = 1;
    }
    else
    {
        s = TypeBytes(*type);
        n = FormatComps(*format);
        if (s == 0 || n == 0)
            return NULL;
    }
    plain = !index && (packed || (*type == GL_UNSIGNED_BYTE && *format != GL_RED
                                  && *format != GL_GREEN && *format != GL_BLUE));
    /* GL 1.1 3.6.3: a row is k elements of s bytes */
    l = c->unpack_row ? c->unpack_row : (ULONG) w;
    a = c->unpack_align;
    if (s >= a)
        k = n * l;
    else
        k = (a / s) * ((s * n * l + a - 1) / a);
    stride = s * k;
    rowbytes = s * n * (ULONG) w;
    row4 = (rowbytes + 3) & ~3UL;           /* texture.c's rows */
    if (plain && stride == row4 && c->unpack_skip_px == 0 && c->unpack_skip_rows == 0
        && !(packed && c->unpack_swap))
        return NULL;
    in = (const UBYTE *) *pixels + c->unpack_skip_rows * stride + c->unpack_skip_px * s * n;
    if (plain)
    {
        /* the layout only: rows copied as texture.c reads them, 16-bit
         * pixels swapped when asked */
        *size = row4 * (ULONG) h;
        out = (UBYTE *) c->env.alloc(c->env.env, *size);
        if (out == NULL)
        {
            Core_Error(c, GL_OUT_OF_MEMORY);
            return NULL;
        }
        o = out;
        for (j = 0; j < (ULONG) h; j++)
        {
            p = in + j * stride;
            for (i = 0; i < rowbytes; i++)
                o[i] = p[i];
            if (packed && c->unpack_swap)
                for (i = 0; i + 1 < rowbytes; i += 2)
                {
                    q = o[i];
                    o[i] = o[i + 1];
                    o[i + 1] = (UBYTE) q;
                }
            o += row4;
        }
        *pixels = out;
        return out;
    }
    /* every component to an unsigned byte; GL_RED, GL_GREEN and GL_BLUE as
     * RGB with the other two 0 (GL 1.1 3.6.4: then alpha 1); an index as
     * its palette entry, RGBA (past the palette's end opaque black) */
    ch = index ? 4UL : (*format == GL_RED) ? 0UL : (*format == GL_GREEN) ? 1UL
       : (*format == GL_BLUE) ? 2UL : 3UL;
    outfmt = index ? GL_RGBA : (ch < 3) ? GL_RGB : *format;
    outcpp = index ? 4UL : (ch < 3) ? 3UL : n;
    row4 = (outcpp * (ULONG) w + 3) & ~3UL;
    *size = row4 * (ULONG) h;
    out = (UBYTE *) c->env.alloc(c->env.env, *size);
    if (out == NULL)
    {
        Core_Error(c, GL_OUT_OF_MEMORY);
        return NULL;
    }
    for (j = 0; j < (ULONG) h; j++)
    {
        p = in + j * stride;
        o = out + j * row4;
        for (i = 0; i < (ULONG) w; i++)
        {
            if (ch == 4)
            {
                q = p[0];
                if (q < c->pal_size)
                {
                    o[0] = c->pal[q * 4];
                    o[1] = c->pal[q * 4 + 1];
                    o[2] = c->pal[q * 4 + 2];
                    o[3] = c->pal[q * 4 + 3];
                }
                else
                {
                    o[0] = o[1] = o[2] = 0;
                    o[3] = 255;
                }
            }
            else if (ch < 3)
            {
                o[0] = o[1] = o[2] = 0;
                o[ch] = (UBYTE) ToByte(p, *type, c->unpack_swap);
            }
            else
                for (q = 0; q < n; q++)
                    o[q] = (UBYTE) ToByte(p + q * s, *type, c->unpack_swap);
            p += s * n;
            o += outcpp;
        }
    }
    *format = outfmt;
    *type = GL_UNSIGNED_BYTE;
    *pixels = out;
    return out;
}

/* What texture.c reads as it is: unsigned bytes (MiniGL's packed 16-bit
 * formats come with that type too), not GL_RED/GREEN/BLUE, in the default
 * unpack state -- straight on, before anything else (a few instructions:
 * GLQuake's lightmap uploads, every frame) */
#define UPLOAD_PLAIN(c, format, type) ((c)->unpack_plain && (((type) == GL_UNSIGNED_BYTE \
                                       && ((format) < GL_COLOR_INDEX || (format) > GL_BLUE)) \
                                       || ((format) == GL_BGRA && (type) == GL_UNSIGNED_INT_8_8_8_8_REV)))

void Core_TexImage2DX(struct Core *c, ULONG target, LONG level, LONG internal,
                      LONG w, LONG h, LONG border, ULONG format, ULONG type,
                      const void *pixels)
{
    void *tmp;
    ULONG size;

    if (UPLOAD_PLAIN(c, format, type))
    {
        Core_TexImage2D(c, target, level, internal, w, h, border, format, type, pixels);
        return;
    }
    /* EXT_paletted_texture's internal format: stored as what the palette
     * holds, the indices being expanded at upload (29.1 keeps no indices either) */
    if (format == GL_COLOR_INDEX && ((ULONG) internal == GL_COLOR_INDEX8_EXT
                                     || (ULONG) internal == GL_COLOR_INDEX))
        internal = (c->pal_base == GL_RGB || c->pal_base == GL_LUMINANCE) ? (LONG) GL_RGB : (LONG) GL_RGBA;
    size = 0;
    tmp = Unpack(c, w, h, &format, &type, &pixels, &size);
    if (tmp == NULL && size != 0)
        return;                             /* out of memory or refused, said */
    Core_TexImage2D(c, target, level, internal, w, h, border, format, type, pixels);
    if (tmp != NULL)
        c->env.free(c->env.env, tmp, size);
}

void Core_TexSubImage2DX(struct Core *c, ULONG target, LONG level, LONG x, LONG y,
                         LONG w, LONG h, ULONG format, ULONG type, const void *pixels)
{
    void *tmp;
    ULONG size;

    if (UPLOAD_PLAIN(c, format, type))
    {
        Core_TexSubImage2D(c, target, level, x, y, w, h, format, type, pixels);
        return;
    }
    size = 0;
    tmp = Unpack(c, w, h, &format, &type, &pixels, &size);
    if (tmp == NULL && size != 0)
        return;
    Core_TexSubImage2D(c, target, level, x, y, w, h, format, type, pixels);
    if (tmp != NULL)
        c->env.free(c->env.env, tmp, size);
}

/* --- The texture environment colour: GL_BLEND and RGBA GL_DECAL ----------------------- */

static ULONG Byte01(float v)
{
    if (!(v > 0.0f))
        return 0;
    if (v >= 1.0f)
        return 255;
    return (ULONG) Core_Floor(v * 255.0f + 0.5f);
}

void Core_TexEnvfv(struct Core *c, ULONG target, ULONG pname, const float *v)
{
    ULONG u;
    ULONG i;

    if (target != GL_TEXTURE_ENV || v == NULL)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    if (pname == GL_TEXTURE_ENV_MODE)
    {
        Core_TexEnvi(c, target, pname, Core_Floor(v[0] + 0.5f));
        return;
    }
    if (pname != GL_TEXTURE_ENV_COLOR)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    u = c->active_unit;
    for (i = 0; i < 4; i++)
        c->tex_env_color[u][i] = (float) Byte01(v[i]) / 255.0f;
    c->tf_color[u] = (Byte01(v[3]) << 24) | (Byte01(v[0]) << 16) | (Byte01(v[1]) << 8) | Byte01(v[2]);
    c->hw_comb[u][0] = 0xffffffffUL;        /* the stage and its TFACTOR go out again */
    c->dirty |= D_COMBINE;
}

/* hwstate.c's EmitCombiner, for a stage s with GL_DECAL or GL_BLEND: the
 * colour and alpha blends into *cb and *ab (they hold the pass-through on
 * entry), and the stage's TFACTOR when the stage goes out (a changed
 * colour forgets the stage, Core_TexEnvfv). The arguments EmitCombiner
 * uses are worked out again here, so that its call stays small: hwstate.o
 * keeps 0.9's size, and GLQuake's code its places (the 68060 layout note
 * at the top). texc is the texture's colour argument (its alpha is texc + 1
 * as a colour argument, texc as an alpha one), prevc and preva the previous
 * stage's. An emit error sticks in the stream: EmitCombiner's next emit
 * returns it. */
void Core_CombExtra(struct Core *c, ULONG s, ULONG *cb, ULONG *ab)
{
    ULONG texc, prevc, preva, out, base;
    struct CoreTex *t;

    t = Core_Texture(c, s);
    if (t == NULL)
        return;
    base = t->base;
    texc = R_TXC_ARG_R0_COLOR + 2 * s;
    prevc = (s == 0) ? R_TXC_ARG_DIFFUSE_COLOR : R_TXC_ARG_R0_COLOR;
    preva = (s == 0) ? R_TXA_ARG_DIFFUSE_ALPHA : R_TXA_ARG_R0_ALPHA;
    out = R_TXC_CLAMP_0_1 | R_TXC_OUTPUT_REG_R0 | (s << R_TXC_TFACTOR_SEL_SHIFT);
    if (c->tex_env[s] == GL_DECAL)
    {
        /* GL 1.1: RGB C = Ct; RGBA C = Cf (1 - At) + Ct At; A = Af. The
         * other bases are undefined: as RGB. */
        if (base == GL_RGBA)
            *cb = (prevc << R_TXC_ARG_A_SHIFT) | (texc << R_TXC_ARG_B_SHIFT)
                | ((texc + 1) << R_TXC_ARG_C_SHIFT) | R_TXC_OP_LERP;
        else if (base != GL_ALPHA)
            *cb = (texc << R_TXC_ARG_C_SHIFT) | R_TXC_OP_MADD;
        return;
    }
    /* GL_BLEND: C = Cf (1 - Ct) + Cc Ct with the environment colour Cc;
     * A = Af At, but Af (1 - At) + Ac At for an intensity texture */
    if (base != GL_ALPHA)
        *cb = (prevc << R_TXC_ARG_A_SHIFT) | (R_TXC_ARG_TFACTOR_COLOR << R_TXC_ARG_B_SHIFT)
            | (texc << R_TXC_ARG_C_SHIFT) | R_TXC_OP_LERP;
    if (base == GL_INTENSITY)
        *ab = (preva << R_TXC_ARG_A_SHIFT) | (R_TXA_ARG_TFACTOR_ALPHA << R_TXC_ARG_B_SHIFT)
            | (texc << R_TXC_ARG_C_SHIFT) | R_TXC_OP_LERP;
    else if (base == GL_RGBA || base == GL_ALPHA || base == GL_LUMINANCE_ALPHA)
        *ab = (texc << R_TXC_ARG_A_SHIFT) | (preva << R_TXC_ARG_B_SHIFT) | R_TXC_OP_MADD;
    if (c->hw_comb[s][0] != *cb || c->hw_comb[s][1] != out || c->hw_comb[s][2] != *ab
        || c->hw_comb[s][3] != out)
        (void) R2E_Reg(c->cs, R_PP_TFACTOR_0 + 4 * s, c->tf_color[s]);
}

/* --- Vertices: glArrayElement, glMultiDrawArrays, glTexCoord4f ------------------------- */

void Core_ArrayElement(struct Core *c, LONG i)
{
    const UBYTE *p;
    float pos[4];
    ULONG t;

    if (i < 0)
        return;                             /* undefined in GL: nothing */
    if (c->na.enabled)
        FetchNormal(c, (ULONG) i);
    if (c->ea.enabled && c->ea.ptr != NULL)
        Core_EdgeFlag(c, c->ea.ptr[(ULONG) i * (c->ea.stride ? c->ea.stride : 1UL)]);
    if (!c->va.enabled)
    {
        FetchAttribs(c, (ULONG) i);
        return;
    }
    if (c->va.size != 4)
    {
        Fetch(c, (ULONG) i, pos);           /* w is 1: nothing divided */
        Core_Vertex3f(c, pos[0], pos[1], pos[2]);
        return;
    }
    /* w as given: glVertex4f divides (Fetch would divide once already) */
    FetchAttribs(c, (ULONG) i);
    p = Elem(&c->va, (ULONG) i);
    t = c->va.type;
    Core_Vertex4f(c, Comp(p, t, 0), Comp(p, t, 1), Comp(p, t, 2), Comp(p, t, 3));
}

void Core_MultiDrawArrays(struct Core *c, ULONG mode, const LONG *first, const LONG *count,
                          LONG primcount)
{
    LONG i;

    if (primcount < 0)
    {
        Core_Error(c, GL_INVALID_VALUE);
        return;
    }
    if (first == NULL || count == NULL)
        return;
    for (i = 0; i < primcount; i++)
        Core_DrawArraysX(c, mode, first[i], count[i]);
}

/* s/q and t/q, as 29.1 keeps them (exact at the vertices; the R200 is not
 * given q, so a q that varies across a primitive interpolates linearly).
 * A q of about 0 carries no perspective: s and t as they are. */
void Core_TexCoord4f(struct Core *c, float s, float t, float r, float q)
{
    float sq;
    float tq;

    (void) r;
    if (q > -1.0e-8f && q < 1.0e-8f)
    {
        Core_TexCoord2f(c, s, t);
        return;
    }
    sq = s / q;                 /* float locals: SAS/C passes a float expression as a double */
    tq = t / q;
    Core_TexCoord2f(c, sq, tq);
}

/* --- GLU: gluPerspective, gluLookAt ----------------------------------------------------- */

void Core_Perspective(struct Core *c, float fovy, float aspect, float zn, float zf)
{
    float half;
    float s;
    float co;
    float ymax;
    float xmax;

    half = fovy * 0.5f;
    s = Core_Sin(half);
    co = Core_Cos(half);
    /* GLU's own early returns, without an error */
    if (aspect == 0.0f || zf == zn || s == 0.0f || co == 0.0f)
        return;
    ymax = zn * s / co;
    xmax = ymax * aspect;
    Core_Frustum(c, -xmax, xmax, -ymax, ymax, zn, zf);
}

static void Norm3(float *v)
{
    float m;

    m = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    m = Core_Sqrt(m);
    if (m != 0.0f)
    {
        v[0] /= m;
        v[1] /= m;
        v[2] /= m;
    }
}

static void Cross3(float *r, const float *a, const float *b)
{
    r[0] = a[1] * b[2] - a[2] * b[1];
    r[1] = a[2] * b[0] - a[0] * b[2];
    r[2] = a[0] * b[1] - a[1] * b[0];
}

void Core_LookAt(struct Core *c, const float *eye, const float *centre, const float *up)
{
    float u[3], v[3], w[3];
    float m[16];
    ULONG i;

    for (i = 0; i < 3; i++)
    {
        w[i] = eye[i] - centre[i];
        v[i] = up[i];
    }
    Norm3(w);
    Cross3(u, v, w);
    Cross3(v, w, u);
    Norm3(u);
    Norm3(v);
    for (i = 0; i < 16; i++)
        m[i] = 0.0f;
    m[0] = u[0];  m[4] = u[1];  m[8] = u[2];
    m[1] = v[0];  m[5] = v[1];  m[9] = v[2];
    m[2] = w[0];  m[6] = w[1];  m[10] = w[2];
    m[15] = 1.0f;
    Core_MultMatrixf(c, m);
    for (i = 0; i < 3; i++)
        w[i] = -eye[i];             /* float locals, not expressions: SAS/C */
    Core_Translatef(c, w[0], w[1], w[2]);
}

/* --- Draw and read buffers, edge flags, colour index ------------------------------------ */

/* GL_NO_ERROR, or the error mode raises. Front and back are both accepted
 * in every context: a back name is what a program asks for when it asked
 * for double buffering, and 29.1 accepts it then even where it delivered
 * one buffer (H2). There is no stereo and no auxiliary buffer. */
static ULONG BufferMode(ULONG mode, ULONG read)
{
    switch (mode)
    {
        case GL_NONE:
        case GL_FRONT_AND_BACK:
            return read ? GL_INVALID_ENUM : GL_NO_ERROR;
        case GL_FRONT:
        case GL_LEFT:
        case GL_FRONT_LEFT:
        case GL_BACK:
        case GL_BACK_LEFT:
            return GL_NO_ERROR;
        case GL_RIGHT:
        case GL_FRONT_RIGHT:
        case GL_BACK_RIGHT:
            return GL_INVALID_OPERATION;
    }
    return GL_INVALID_ENUM;
}

void Core_DrawBuffer(struct Core *c, ULONG mode)
{
    ULONG e;

    e = IN_BEGIN(c) ? GL_INVALID_OPERATION : BufferMode(mode, 0);
    if (e != GL_NO_ERROR)
        Core_Error(c, e);
    else
        c->draw_buffer = mode;
}

void Core_ReadBuffer(struct Core *c, ULONG mode)
{
    ULONG e;

    e = IN_BEGIN(c) ? GL_INVALID_OPERATION : BufferMode(mode, 1);
    if (e != GL_NO_ERROR)
        Core_Error(c, e);
    else
        c->read_buffer = mode;
}

void Core_EdgeFlag(struct Core *c, ULONG flag)
{
    flag = flag ? 1UL : 0UL;
    if (c->pm_on && flag != c->cur_edge)
    {
        c->cur_edge = flag;
        Pm_EdgeFlag(c);                     /* GL_LINE / GL_POINT polygons (polymode.c) */
        return;
    }
    c->cur_edge = flag;                     /* filled polygons: stored */
}

void Core_EdgeFlagPointer(struct Core *c, LONG stride, const void *p)
{
    if (stride < 0)
    {
        Core_Error(c, GL_INVALID_VALUE);
        return;
    }
    c->ea.size = 1;
    c->ea.stride = (ULONG) stride;
    c->ea.ptr = (const UBYTE *) p;
}

void Core_Index(struct Core *c, float index)
{
    c->cur_index = index;                   /* RGBA mode only: stored */
}

void Core_IndexPointer(struct Core *c, ULONG type, LONG stride, const void *p)
{
    if (type != GL_UNSIGNED_BYTE && type != GL_SHORT && type != GL_INT && type != GL_FLOAT
        && type != GL_DOUBLE)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    if (stride < 0)
    {
        Core_Error(c, GL_INVALID_VALUE);
        return;
    }
    c->ia.size = 1;
    c->ia.type = type;
    c->ia.stride = (ULONG) stride;
    c->ia.ptr = (const UBYTE *) p;
}

/* --- Textures: residency, priorities, memory ------------------------------------------- */

/* Every texture lives in the card's memory while it exists: GL_TRUE for
 * names that are textures (residences then untouched, GL 1.1 3.8.8). */
ULONG Core_AreTexturesResident(struct Core *c, LONG n, const ULONG *names)
{
    LONG i;

    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return 0;
    }
    if (n < 0)
    {
        Core_Error(c, GL_INVALID_VALUE);
        return 0;
    }
    if (n > 0 && names == NULL)
        return 0;
    for (i = 0; i < n; i++)
        if (names[i] == 0 || !Core_IsTexture(c, names[i]))
        {
            Core_Error(c, GL_INVALID_VALUE);
            return 0;
        }
    return 1;
}

void Core_PrioritizeTextures(struct Core *c, LONG n, const ULONG *names)
{
    (void) names;
    if (IN_BEGIN(c))
        Core_Error(c, GL_INVALID_OPERATION);
    else if (n < 0)
        Core_Error(c, GL_INVALID_VALUE);
}

/* Bytes of the card's memory the textures take now, and at most so far:
 * the heap less the stream and the upload cache's block (which grows and
 * shrinks since 2026-11-14, ucgrow.c: the peak during a grow counts the
 * old block and the new one). */
void Core_TexMemStat(struct Core *c, LONG *current, LONG *peak)
{
    ULONG fixed;
    ULONG used;
    ULONG most;

    fixed = c->stream_size + c->ucache_region;
    used = (c->heap.used > fixed) ? c->heap.used - fixed : 0;
    most = (c->heap.peak > fixed) ? c->heap.peak - fixed : 0;
    if (current != NULL)
        *current = (LONG) used;
    if (peak != NULL)
        *peak = (LONG) most;
}

/* --- glInterleavedArrays ------------------------------------------------------------- */

#ifndef GL_N3F_V3F
#define GL_N3F_V3F          0x2a25UL
#endif
#ifndef GL_C4F_N3F_V3F
#define GL_C4F_N3F_V3F      0x2a26UL
#endif
#ifndef GL_T4F_V4F
#define GL_T4F_V4F          0x2a28UL
#endif
#ifndef GL_T2F_N3F_V3F
#define GL_T2F_N3F_V3F      0x2a2bUL
#endif
#ifndef GL_T2F_C4F_N3F_V3F
#define GL_T2F_C4F_N3F_V3F  0x2a2cUL
#endif
#ifndef GL_T4F_C4F_N3F_V4F
#define GL_T4F_C4F_N3F_V4F  0x2a2dUL
#endif

/* GL 1.1 table 2.5, one row a format: texture size, colour size and type,
 * normal, vertex size, then the byte offsets of colour, normal and vertex
 * and the size of one element (texture coordinates start at 0) */
struct Ilv
{
    ULONG format;
    UBYTE st, sc, en, sv;
    UWORD tc;
    UBYTE pc, pn, pv, s;
};

static const struct Ilv ilv[] = {
    { GL_V2F,                0, 0, 0, 2, 0,                 0,  0,  0,  8 },
    { GL_V3F,                0, 0, 0, 3, 0,                 0,  0,  0, 12 },
    { GL_C4UB_V2F,           0, 4, 0, 2, GL_UNSIGNED_BYTE,  0,  0,  4, 12 },
    { GL_C4UB_V3F,           0, 4, 0, 3, GL_UNSIGNED_BYTE,  0,  0,  4, 16 },
    { GL_C3F_V3F,            0, 3, 0, 3, GL_FLOAT,          0,  0, 12, 24 },
    { GL_N3F_V3F,            0, 0, 1, 3, 0,                 0,  0, 12, 24 },
    { GL_C4F_N3F_V3F,        0, 4, 1, 3, GL_FLOAT,          0, 16, 28, 40 },
    { GL_T2F_V3F,            2, 0, 0, 3, 0,                 0,  0,  8, 20 },
    { GL_T4F_V4F,            4, 0, 0, 4, 0,                 0,  0, 16, 32 },
    { GL_T2F_C4UB_V3F,       2, 4, 0, 3, GL_UNSIGNED_BYTE,  8,  0, 12, 24 },
    { GL_T2F_C3F_V3F,        2, 3, 0, 3, GL_FLOAT,          8,  0, 20, 32 },
    { GL_T2F_N3F_V3F,        2, 0, 1, 3, 0,                 0,  8, 20, 32 },
    { GL_T2F_C4F_N3F_V3F,    2, 4, 1, 3, GL_FLOAT,          8, 24, 36, 48 },
    { GL_T4F_C4F_N3F_V4F,    4, 4, 1, 4, GL_FLOAT,         16, 32, 44, 60 }
};

void Core_InterleavedArrays(struct Core *c, ULONG format, LONG stride, const void *p)
{
    const struct Ilv *f;
    const UBYTE *b;
    ULONG i;
    LONG s;

    f = NULL;
    for (i = 0; i < sizeof(ilv) / sizeof(ilv[0]); i++)
        if (ilv[i].format == format)
            f = &ilv[i];
    if (f == NULL)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    if (stride < 0)
    {
        Core_Error(c, GL_INVALID_VALUE);
        return;
    }
    b = (const UBYTE *) p;
    s = stride ? stride : (LONG) f->s;
    Core_EnableClientState(c, GL_EDGE_FLAG_ARRAY, 0);
    Core_EnableClientState(c, GL_INDEX_ARRAY, 0);
    Core_EnableClientState(c, GL_TEXTURE_COORD_ARRAY, f->st != 0);
    if (f->st)
        Core_TexCoordPointer(c, f->st, GL_FLOAT, s, b);
    Core_EnableClientState(c, GL_COLOR_ARRAY, f->sc != 0);
    if (f->sc)
        Core_ColorPointer(c, f->sc, f->tc, s, b + f->pc);
    Core_EnableClientState(c, GL_NORMAL_ARRAY, f->en);
    if (f->en)
        Core_NormalPointer(c, GL_FLOAT, s, b + f->pn);
    Core_EnableClientState(c, GL_VERTEX_ARRAY, 1);
    Core_VertexPointer(c, f->sv, GL_FLOAT, s, b + f->pv);
}

/* --- gluBuild2DMipmaps ------------------------------------------------------------------ */

#define GLU_INVALID_ENUM    100900L
#define GLU_INVALID_VALUE   100901L
#define GLU_OUT_OF_MEMORY   100902L

/* GLU's nearest power of two (SGI's nearestPower: 3 goes to 4) */
static ULONG NearestPow2(ULONG v)
{
    ULONG i;

    i = 1;
    for (;;)
    {
        if (v <= 1)
            return i;
        if (v == 3)
            return i * 4;
        v >>= 1;
        i <<= 1;
    }
}

/* An image of w x h of n bytes a pixel in rows of 4-byte multiples (what
 * texture.c reads) into one of dw x dh: every pixel the mean of the source
 * pixels under it (a box filter; at least one) */
static void Box(const UBYTE *src, ULONG w, ULONG h, UBYTE *dst, ULONG dw, ULONG dh, ULONG n)
{
    ULONG sr, dr;
    ULONG x, y, k;
    ULONG x0, x1, y0, y1, xx, yy;
    ULONG sum, cnt;

    sr = (w * n + 3) & ~3UL;
    dr = (dw * n + 3) & ~3UL;
    for (y = 0; y < dh; y++)
    {
        y0 = y * h / dh;
        y1 = (y + 1) * h / dh;
        if (y1 <= y0)
            y1 = y0 + 1;
        for (x = 0; x < dw; x++)
        {
            x0 = x * w / dw;
            x1 = (x + 1) * w / dw;
            if (x1 <= x0)
                x1 = x0 + 1;
            for (k = 0; k < n; k++)
            {
                sum = 0;
                cnt = 0;
                for (yy = y0; yy < y1; yy++)
                    for (xx = x0; xx < x1; xx++)
                    {
                        sum += src[yy * sr + xx * n + k];
                        cnt++;
                    }
                dst[y * dr + x * n + k] = (UBYTE) ((sum + cnt / 2) / cnt);
            }
        }
    }
}

LONG Core_Build2DMipmaps(struct Core *c, ULONG target, LONG internal, LONG w, LONG h,
                         ULONG format, ULONG type, const void *data)
{
    const void *src;
    void *tmp;
    UBYTE *a;
    UBYTE *b;
    UBYTE *t;
    ULONG size, asize, bsize;
    ULONG n, nw, nh, lw, lh;
    LONG level;

    if (target != GL_TEXTURE_2D)
        return GLU_INVALID_ENUM;
    if (w < 1 || h < 1)
        return GLU_INVALID_VALUE;
    src = data;
    size = 0;
    tmp = Unpack(c, w, h, &format, &type, &src, &size);
    if (tmp == NULL && size != 0)
        return GLU_OUT_OF_MEMORY;
    n = (type == GL_UNSIGNED_BYTE) ? FormatComps(format) : 0;
    if (n == 0 || format == GL_RED || format == GL_GREEN || format == GL_BLUE || src == NULL)
    {
        if (tmp != NULL)
            c->env.free(c->env.env, tmp, size);
        return GLU_INVALID_ENUM;        /* MiniGL's packed layouts are not scaled */
    }
    nw = NearestPow2((ULONG) w);
    nh = NearestPow2((ULONG) h);
    while (nw > CORE_MAX_TEXSIZE)
        nw >>= 1;
    while (nh > CORE_MAX_TEXSIZE)
        nh >>= 1;
    /* two buffers: the level being made, and the one before it */
    asize = ((nw * n + 3) & ~3UL) * nh;
    bsize = asize;
    a = (UBYTE *) c->env.alloc(c->env.env, asize);
    b = (UBYTE *) c->env.alloc(c->env.env, bsize);
    if (a == NULL || b == NULL)
    {
        if (a != NULL)
            c->env.free(c->env.env, a, asize);
        if (b != NULL)
            c->env.free(c->env.env, b, bsize);
        if (tmp != NULL)
            c->env.free(c->env.env, tmp, size);
        return GLU_OUT_OF_MEMORY;
    }
    Box((const UBYTE *) src, (ULONG) w, (ULONG) h, a, nw, nh, n);
    if (tmp != NULL)
        c->env.free(c->env.env, tmp, size);
    lw = nw;
    lh = nh;
    level = 0;
    for (;;)
    {
        Core_TexImage2D(c, GL_TEXTURE_2D, level, internal, (LONG) lw, (LONG) lh, 0, format,
                        GL_UNSIGNED_BYTE, a);
        if (lw == 1 && lh == 1)
            break;
        nw = (lw > 1) ? lw / 2 : 1;
        nh = (lh > 1) ? lh / 2 : 1;
        Box(a, lw, lh, b, nw, nh, n);
        t = a;
        a = b;
        b = t;
        lw = nw;
        lh = nh;
        level++;
    }
    c->env.free(c->env.env, a, asize);
    c->env.free(c->env.env, b, bsize);
    return 0;
}

/* --- GLU quadrics: gluSphere, gluCylinder, gluDisk ----------------------------------------
 * The vertices, their order, normals and texture coordinates as SGI's GLU
 * (quad.c) sends them: a sphere around the origin from the pole at +z, a
 * cylinder along +z from the base, a disk in the plane z = 0; x is the
 * sine, y the cosine of the angle round z. GLU_FILL in quad strips (front
 * faces outwards for GLU_OUTSIDE), GLU_LINE and GLU_SILHOUETTE as the
 * edges along the slices and the rings, GLU_POINT as the vertices. GLU_FLAT
 * normals are given as smooth ones. */

#define Q_SMAX 256                  /* slices at most (more are drawn as this many) */

static void QVert(struct Core *c, const struct CoreQuadric *q, const float *n, float s, float t,
                  const float *v)
{
    if (q->normals != GLU_NONE_Q)
        Core_Normal3f(c, n[0], n[1], n[2]);
    if (q->texture)
        Core_TexCoord2f(c, s, t);
    Core_Vertex3f(c, v[0], v[1], v[2]);
}

/* sine and cosine of 360 i / slices, i = 0 .. slices (the last equals the first) */
static LONG QCache(LONG slices, float *sn, float *cs)
{
    LONG i;
    float a;

    if (slices > Q_SMAX)
        slices = Q_SMAX;
    for (i = 0; i <= slices; i++)
    {
        a = 360.0f * (float) i / (float) slices;
        sn[i] = (i == slices) ? sn[0] : Core_Sin(a);
        cs[i] = (i == slices) ? cs[0] : Core_Cos(a);
    }
    return slices;
}

static ULONG QPrim(const struct CoreQuadric *q)
{
    return (q->style == GLU_POINT_Q) ? GL_POINTS
         : (q->style == GLU_FILL_Q) ? GL_QUAD_STRIP : GL_LINES;   /* the strip's pairs: slice edges */
}

static ULONG QRings(const struct CoreQuadric *q)
{
    return (q->style == GLU_LINE_Q || q->style == GLU_SILHOUETTE_Q) ? 1UL : 0UL;
}

void Core_Sphere(struct Core *c, const struct CoreQuadric *q, float radius, LONG slices, LONG stacks)
{
    float sn[Q_SMAX + 1], cs[Q_SMAX + 1];
    float n[3], v[3];
    float r0, r1, z0, z1, phi, sgn, s, t0, t1;
    LONG i, j, k;

    if (slices < 2 || stacks < 1 || radius < 0.0f)
        return;
    slices = QCache(slices, sn, cs);
    sgn = (q->orientation == GLU_INSIDE_Q) ? -1.0f : 1.0f;
    for (j = 0; j < stacks; j++)
    {
        phi = 180.0f * (float) j / (float) stacks;
        r0 = Core_Sin(phi);
        z0 = Core_Cos(phi);
        phi = 180.0f * (float) (j + 1) / (float) stacks;
        r1 = Core_Sin(phi);
        z1 = Core_Cos(phi);
        t0 = 1.0f - (float) j / (float) stacks;
        t1 = 1.0f - (float) (j + 1) / (float) stacks;
        Core_BeginX(c, QPrim(q));
        for (i = 0; i <= slices; i++)
        {
            s = 1.0f - (float) i / (float) slices;
            /* outside: the lower ring (j + 1) first, as GLU */
            for (k = 0; k < 2; k++)
            {
                if ((k == 0) == (q->orientation != GLU_INSIDE_Q))
                {
                    n[0] = sn[i] * r1; n[1] = cs[i] * r1; n[2] = z1;
                    v[0] = radius * n[0]; v[1] = radius * n[1]; v[2] = radius * z1;
                    n[0] *= sgn; n[1] *= sgn; n[2] *= sgn;
                    QVert(c, q, n, s, t1, v);
                }
                else
                {
                    n[0] = sn[i] * r0; n[1] = cs[i] * r0; n[2] = z0;
                    v[0] = radius * n[0]; v[1] = radius * n[1]; v[2] = radius * z0;
                    n[0] *= sgn; n[1] *= sgn; n[2] *= sgn;
                    QVert(c, q, n, s, t0, v);
                }
            }
        }
        Core_EndX(c);
    }
    for (j = 0; QRings(q) && j <= stacks; j++)
    {
        phi = 180.0f * (float) j / (float) stacks;
        r0 = Core_Sin(phi);
        z0 = Core_Cos(phi);
        t0 = 1.0f - (float) j / (float) stacks;
        Core_BeginX(c, GL_LINE_STRIP);
        for (i = 0; i <= slices; i++)
        {
            s = 1.0f - (float) i / (float) slices;
            n[0] = sn[i] * r0; n[1] = cs[i] * r0; n[2] = z0;
            v[0] = radius * n[0]; v[1] = radius * n[1]; v[2] = radius * z0;
            n[0] *= sgn; n[1] *= sgn; n[2] *= sgn;
            QVert(c, q, n, s, t0, v);
        }
        Core_EndX(c);
    }
}

void Core_Cylinder(struct Core *c, const struct CoreQuadric *q, float base, float top, float height,
                   LONG slices, LONG stacks)
{
    float sn[Q_SMAX + 1], cs[Q_SMAX + 1];
    float n[3], v[3];
    float d, len, nz, nxy, r0, r1, z0, z1, s, t0, t1, sgn;
    LONG i, j, k;

    if (slices < 2 || stacks < 1 || base < 0.0f || top < 0.0f || height < 0.0f)
        return;
    slices = QCache(slices, sn, cs);
    d = base - top;
    len = d * d + height * height;
    len = Core_Sqrt(len);
    if (len == 0.0f)
        return;
    sgn = (q->orientation == GLU_INSIDE_Q) ? -1.0f : 1.0f;
    nz = sgn * d / len;
    nxy = sgn * height / len;
    for (j = 0; j < stacks; j++)
    {
        z0 = height * (float) j / (float) stacks;
        z1 = height * (float) (j + 1) / (float) stacks;
        r0 = base - d * ((float) j / (float) stacks);
        r1 = base - d * ((float) (j + 1) / (float) stacks);
        t0 = (float) j / (float) stacks;
        t1 = (float) (j + 1) / (float) stacks;
        Core_BeginX(c, QPrim(q));
        for (i = 0; i <= slices; i++)
        {
            s = 1.0f - (float) i / (float) slices;
            n[0] = nxy * sn[i];
            n[1] = nxy * cs[i];
            n[2] = nz;
            /* outside: the lower ring first */
            for (k = 0; k < 2; k++)
            {
                if ((k == 0) == (q->orientation != GLU_INSIDE_Q))
                {
                    v[0] = r0 * sn[i]; v[1] = r0 * cs[i]; v[2] = z0;
                    QVert(c, q, n, s, t0, v);
                }
                else
                {
                    v[0] = r1 * sn[i]; v[1] = r1 * cs[i]; v[2] = z1;
                    QVert(c, q, n, s, t1, v);
                }
            }
        }
        Core_EndX(c);
    }
    for (j = 0; QRings(q) && j <= stacks; j++)
    {
        z0 = height * (float) j / (float) stacks;
        r0 = base - d * ((float) j / (float) stacks);
        t0 = (float) j / (float) stacks;
        Core_BeginX(c, GL_LINE_STRIP);
        for (i = 0; i <= slices; i++)
        {
            s = 1.0f - (float) i / (float) slices;
            n[0] = nxy * sn[i]; n[1] = nxy * cs[i]; n[2] = nz;
            v[0] = r0 * sn[i]; v[1] = r0 * cs[i]; v[2] = z0;
            QVert(c, q, n, s, t0, v);
        }
        Core_EndX(c);
    }
}

void Core_Disk(struct Core *c, const struct CoreQuadric *q, float inner, float outer, LONG slices,
               LONG loops)
{
    float sn[Q_SMAX + 1], cs[Q_SMAX + 1];
    float n[3], v[3];
    float d, r0, r1, tx0, tx1, s, t;
    LONG i, j, k;

    if (slices < 2 || loops < 1 || inner < 0.0f || outer <= 0.0f || inner > outer)
        return;
    slices = QCache(slices, sn, cs);
    d = outer - inner;
    n[0] = 0.0f;
    n[1] = 0.0f;
    n[2] = (q->orientation == GLU_INSIDE_Q) ? -1.0f : 1.0f;
    v[2] = 0.0f;
    for (j = 0; j < loops; j++)
    {
        r0 = outer - d * ((float) j / (float) loops);
        r1 = outer - d * ((float) (j + 1) / (float) loops);
        tx0 = r0 / outer * 0.5f;
        tx1 = r1 / outer * 0.5f;
        Core_BeginX(c, QPrim(q));
        for (i = 0; i <= slices; i++)
            for (k = 0; k < 2; k++)
            {
                if ((k == 0) == (q->orientation != GLU_INSIDE_Q))
                {
                    v[0] = r0 * sn[i]; v[1] = r0 * cs[i];
                    s = tx0 * sn[i] + 0.5f; t = tx0 * cs[i] + 0.5f;
                }
                else
                {
                    v[0] = r1 * sn[i]; v[1] = r1 * cs[i];
                    s = tx1 * sn[i] + 0.5f; t = tx1 * cs[i] + 0.5f;
                }
                QVert(c, q, n, s, t, v);
            }
        Core_EndX(c);
    }
    for (j = 0; QRings(q) && j <= loops; j++)
    {
        r0 = outer - d * ((float) j / (float) loops);
        tx0 = r0 / outer * 0.5f;
        Core_BeginX(c, GL_LINE_STRIP);
        for (i = 0; i <= slices; i++)
        {
            v[0] = r0 * sn[i]; v[1] = r0 * cs[i];
            s = tx0 * sn[i] + 0.5f; t = tx0 * cs[i] + 0.5f;
            QVert(c, q, n, s, t, v);
        }
        Core_EndX(c);
    }
}

/* --- glBlendEquation, glBlendFuncSeparate ---------------------------------------------------
 * The R200 blends colour by CBLENDCNTL and alpha by ABLENDCNTL once RB3D_CNTL's
 * SEPARATE_ALPHA_ENABLE is set -- hwstate.c sets it only with blend_x, i.e.
 * once a program set an equation other than GL_FUNC_ADD or alpha factors
 * of their own; then D_BLEND comes here too. Otherwise all three registers
 * hold one value, as in 0.9. As Mesa's r200: GL_MIN and GL_MAX take factors ONE, ONE (GL
 * ignores the factors for them). */

static void BlendX(struct Core *c)
{
    c->blend_x = (c->blend_sep || c->blend_eq_rgb != GL_FUNC_ADD || c->blend_eq_a != GL_FUNC_ADD)
               ? 1UL : 0UL;
    c->dirty |= D_BLEND | D_RB3D;           /* RB3D_CNTL's SEPARATE_ALPHA_ENABLE follows blend_x */
}

static ULONG BlendFcn(ULONG eq)
{
    switch (eq)
    {
        case GL_FUNC_SUBTRACT:          return R_COMB_FCN_SUB_CLAMP;
        case GL_FUNC_REVERSE_SUBTRACT:  return R_COMB_FCN_RSUB_CLAMP;
        case GL_MIN:                    return R_COMB_FCN_MIN;
        case GL_MAX:                    return R_COMB_FCN_MAX;
    }
    return R_COMB_FCN_ADD_CLAMP;
}

/* A source and destination factor pair as GL 1.1 allows it (Core_BlendFunc's rule) */
static ULONG BlendPairOk(ULONG src, ULONG dst)
{
    return (Hw_BlendFactor(src) != 0 && src != GL_SRC_COLOR && src != GL_ONE_MINUS_SRC_COLOR
            && Hw_BlendFactor(dst) != 0 && dst != GL_DST_COLOR && dst != GL_ONE_MINUS_DST_COLOR
            && dst != GL_SRC_ALPHA_SATURATE) ? 1UL : 0UL;
}

void Core_BlendEquation(struct Core *c, ULONG mode)
{
    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    if (mode != GL_FUNC_ADD && mode != GL_FUNC_SUBTRACT && mode != GL_FUNC_REVERSE_SUBTRACT
        && mode != GL_MIN && mode != GL_MAX)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    c->blend_eq_rgb = mode;
    c->blend_eq_a = mode;
    BlendX(c);
}

void Core_BlendFuncSeparate(struct Core *c, ULONG sr, ULONG dr, ULONG sa, ULONG da)
{
    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    if (!BlendPairOk(sr, dr) || !BlendPairOk(sa, da))
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    c->blend_src = sr;
    c->blend_dst = dr;
    c->blend_src_a = sa;
    c->blend_dst_a = da;
    c->blend_sep = (sa != sr || da != dr) ? 1UL : 0UL;
    BlendX(c);
}

void Compat_BlendFuncReset(struct Core *c)
{
    c->blend_sep = 0;
    BlendX(c);
}

void Compat_EmitBlend(struct Core *c)
{
    ULONG sc, dc, sa, da, cb, ab;

    if (!(c->enables & EN_BLEND))
    {
        /* As Mesa's r200: with blending off the registers go back to
         * GL_FUNC_ADD, ONE, ZERO. GL_MIN or GL_FUNC_REVERSE_SUBTRACT left
         * in them spoil drawing without blending (MEASURED 2026-11-08 on
         * the A4000D: the unblended destination came out black). hwstate.c
         * sends every RB3D_CNTL change here while blend_x is set. */
        cb = R_COMB_FCN_ADD_CLAMP | (R_BLEND_GL_ONE << R_SRC_BLEND_SHIFT)
           | (R_BLEND_GL_ZERO << R_DST_BLEND_SHIFT);
        (void) R2E_Reg(c->cs, R_RB3D_BLENDCNTL, cb);
        (void) R2E_Reg(c->cs, R_RB3D_ABLENDCNTL, cb);
        (void) R2E_Reg(c->cs, R_RB3D_CBLENDCNTL, cb);
        return;
    }
    sc = Hw_BlendFactor(c->blend_src);
    dc = Hw_BlendFactor(c->blend_dst);
    sa = c->blend_sep ? Hw_BlendFactor(c->blend_src_a) : sc;
    da = c->blend_sep ? Hw_BlendFactor(c->blend_dst_a) : dc;
    if (c->blend_eq_rgb == GL_MIN || c->blend_eq_rgb == GL_MAX)
        sc = dc = R_BLEND_GL_ONE;
    if (c->blend_eq_a == GL_MIN || c->blend_eq_a == GL_MAX)
        sa = da = R_BLEND_GL_ONE;
    cb = BlendFcn(c->blend_eq_rgb) | (sc << R_SRC_BLEND_SHIFT) | (dc << R_DST_BLEND_SHIFT);
    ab = BlendFcn(c->blend_eq_a) | (sa << R_SRC_BLEND_SHIFT) | (da << R_DST_BLEND_SHIFT);
    (void) R2E_Reg(c->cs, R_RB3D_BLENDCNTL, cb);
    (void) R2E_Reg(c->cs, R_RB3D_ABLENDCNTL, ab);
    (void) R2E_Reg(c->cs, R_RB3D_CBLENDCNTL, cb);
}

/* --- glColorTable: the palette GL_COLOR_INDEX uploads expand through -------------------------
 * As 29.1: one palette a context, for all three targets (GL_COLOR_TABLE,
 * GL_SHARED_TEXTURE_PALETTE_EXT, and GL_TEXTURE_2D, EXT_paletted_texture's);
 * each entry kept as R, G, B, A in the form the texel takes (the source's
 * missing components GL's defaults, then the internal format's base);
 * an index image is expanded at upload and not kept, so a palette changed
 * later does not reach textures already made. A GL_RGBA source is read
 * A, R, G, B: MiniGL's order, which 29.1 keeps on purpose for its callers. */

static ULONG PalBase(ULONG internal)
{
    switch (internal)
    {
        case GL_ALPHA: case GL_ALPHA8:                          return GL_ALPHA;
        case 1: case GL_LUMINANCE: case GL_LUMINANCE8:          return GL_LUMINANCE;
        case 2: case GL_LUMINANCE_ALPHA: case GL_LUMINANCE8_ALPHA8:
                                                                return GL_LUMINANCE_ALPHA;
        case GL_INTENSITY: case GL_INTENSITY8:                  return GL_INTENSITY;
        case 3: case GL_RGB: case GL_RGB5: case GL_RGB8:        return GL_RGB;
        case 4: case GL_RGBA: case GL_RGB5_A1: case GL_RGBA8:   return GL_RGBA;
    }
    return 0;
}

void Core_ColorTable(struct Core *c, ULONG target, ULONG internal, LONG width, ULONG format,
                     ULONG type, const void *data)
{
    ULONG base, n, s, i;
    ULONG r, g, b, a;
    const UBYTE *p;
    UBYTE *o;

    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    if (target != GL_COLOR_TABLE && target != GL_SHARED_TEXTURE_PALETTE_EXT && target != GL_TEXTURE_2D)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    if (width < 0 || width > 256 || (width > 0 && data == NULL))
    {
        Core_Error(c, GL_INVALID_VALUE);
        return;
    }
    base = PalBase(internal);
    n = (format == GL_COLOR_INDEX) ? 0UL : FormatComps(format);
    s = TypeBytes(type);
    if (base == 0 || n == 0 || s == 0)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    if (c->pal == NULL)
    {
        /* 1 KB only for a program that uses a palette (in struct Core it
         * moved what follows the context: GLQuake 0.1 s, 2026-11-08) */
        c->pal = (UBYTE *) c->env.alloc(c->env.env, 256 * 4);
        if (c->pal == NULL)
        {
            Core_Error(c, GL_OUT_OF_MEMORY);
            return;
        }
    }
    p = (const UBYTE *) data;
    o = c->pal;
    for (i = 0; i < (ULONG) width; i++)
    {
        r = g = b = 0;
        a = 255;
        switch (format)
        {
            case GL_RED:    r = ToByte(p, type, 0); break;
            case GL_GREEN:  g = ToByte(p, type, 0); break;
            case GL_BLUE:   b = ToByte(p, type, 0); break;
            case GL_ALPHA:  a = ToByte(p, type, 0); break;
            case GL_LUMINANCE:
                r = g = b = ToByte(p, type, 0);
                break;
            case GL_LUMINANCE_ALPHA:
                r = g = b = ToByte(p, type, 0);
                a = ToByte(p + s, type, 0);
                break;
            case GL_RGB:
                r = ToByte(p, type, 0);
                g = ToByte(p + s, type, 0);
                b = ToByte(p + 2 * s, type, 0);
                break;
            default:        /* GL_RGBA: A, R, G, B (MiniGL's order) */
                a = ToByte(p, type, 0);
                r = ToByte(p + s, type, 0);
                g = ToByte(p + 2 * s, type, 0);
                b = ToByte(p + 3 * s, type, 0);
                break;
        }
        switch (base)
        {
            case GL_ALPHA:              r = g = b = 255; break;
            case GL_LUMINANCE:          g = b = r; a = 255; break;
            case GL_LUMINANCE_ALPHA:    g = b = r; break;
            case GL_INTENSITY:          g = b = a = r; break;
            case GL_RGB:                a = 255; break;
        }
        o[0] = (UBYTE) r;
        o[1] = (UBYTE) g;
        o[2] = (UBYTE) b;
        o[3] = (UBYTE) a;
        o += 4;
        p += n * s;
    }
    c->pal_size = (ULONG) width;
    c->pal_base = base;
}

void Compat_Free(struct Core *c)
{
    if (c->pal != NULL)
    {
        c->env.free(c->env.env, c->pal, 256 * 4);
        c->pal = NULL;
    }
    c->pal_size = 0;
    Pm_Free(c);
}

