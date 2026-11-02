/*
 * vertex.c -- immediate mode: glBegin, the per-vertex calls, glEnd.
 *
 * glBegin emits the dirty state and opens one 3D_DRAW_IMMD_2 packet with
 * room for CORE_IMMD_VERTS vertices (plus one spare, for closing a line
 * loop). Each glVertex writes its packed vertex straight into that room:
 *
 *   x, y, z            floats (TCL transforms them: object space)
 *   colour             one dword, as the GPU reads it: a << 24 | b << 16 | g << 8 | r
 *   s0, t0             if unit 0 textures this draw
 *   s1, t1             if unit 1 does
 *
 * A primitive longer than the room is split: the packet is closed at a
 * boundary the primitive allows and a new one starts with the vertices the
 * rest needs (read back from the packet, which costs a few VRAM reads per
 * split). glEnd trims an incomplete primitive the way GL ignores it and
 * closes the packet.
 */

#include "core.h"
#include "coreint.h"
#include "r200reg.h"

#define CNTL_BITS (R_VF_COLOR_ORDER_RGBA | R_VF_TCL_OUTPUT_VTX_ENABLE)

static ULONG HwPrim(ULONG mode)
{
    switch (mode)
    {
        case GL_POINTS:         return R_VF_PRIM_POINTS;
        case GL_LINES:          return R_VF_PRIM_LINES;
        case GL_LINE_LOOP:      return R_VF_PRIM_LINE_STRIP;    /* closed at glEnd */
        case GL_LINE_STRIP:     return R_VF_PRIM_LINE_STRIP;
        case GL_TRIANGLES:      return R_VF_PRIM_TRIANGLES;
        case GL_TRIANGLE_STRIP: return R_VF_PRIM_TRIANGLE_STRIP;
        case GL_TRIANGLE_FAN:   return R_VF_PRIM_TRIANGLE_FAN;
        case GL_QUADS:          return R_VF_PRIM_QUADS;
        case GL_QUAD_STRIP:     return R_VF_PRIM_QUAD_STRIP;
    }
    return R_VF_PRIM_POLYGON;
}

/* The longest prefix of n vertices that is a whole primitive of mode. */
static ULONG Whole(ULONG mode, ULONG n)
{
    switch (mode)
    {
        case GL_POINTS:         return n;
        case GL_LINES:          return n & ~1UL;
        case GL_LINE_LOOP:
        case GL_LINE_STRIP:     return n >= 2 ? n : 0;
        case GL_TRIANGLES:      return n - n % 3;
        case GL_QUADS:          return n & ~3UL;
        case GL_QUAD_STRIP:     return n >= 4 ? (n & ~1UL) : 0;
    }
    return n >= 3 ? n : 0;      /* strip, fan, polygon */
}

static ULONG NeedsFirst(ULONG mode)
{
    return (mode == GL_TRIANGLE_FAN || mode == GL_POLYGON || mode == GL_LINE_LOOP) ? 1UL : 0UL;
}

static void Open(struct Core *c)
{
    ULONG *p;

    p = R2E_ImmdOpen(c->cs, c->vtx_dw, CORE_IMMD_VERTS + 1);
    c->vbase = p;
    c->vptr = p;
    c->vlimit = (p != NULL) ? p + c->vtx_dw * CORE_IMMD_VERTS : NULL;
    c->nverts = 0;
}

static void Close(struct Core *c, ULONG n)
{
    if (c->vbase == NULL)
        return;
    R2E_ImmdClose(c->cs, c->vbase + n * c->vtx_dw, c->hwprim | CNTL_BITS);
    if (n != 0)
    {
        c->stat_prims++;
        c->stat_verts += n;
    }
}

/* The room is full in the middle of a primitive: close what can be drawn
 * now, reopen with what the rest needs. */
static void Split(struct Core *c)
{
    ULONG carry[4];     /* vertex indices to carry, or ~0 for the first vertex */
    ULONG save[4 * 8];
    ULONG keep;
    ULONG ncarry;
    ULONG n;
    ULONG vd;
    ULONG i;
    ULONG k;
    ULONG *src;

    n = c->nverts;
    vd = c->vtx_dw;
    ncarry = 0;
    keep = n;
    switch (c->prim)
    {
        case GL_POINTS:
            break;
        case GL_LINES:
        case GL_TRIANGLES:
        case GL_QUADS:
            keep = Whole(c->prim, n);
            for (i = keep; i < n; i++)
                carry[ncarry++] = i;
            break;
        case GL_LINE_STRIP:
        case GL_LINE_LOOP:
            carry[ncarry++] = n - 1;
            break;
        case GL_TRIANGLE_STRIP:
            /* the next triangle uses n-2, n-1 and keeps its winding only if
             * it starts the new strip at the same parity: an odd split
             * repeats n-2 (one degenerate triangle) */
            carry[ncarry++] = n - 2;
            if (n & 1)
                carry[ncarry++] = n - 2;
            carry[ncarry++] = n - 1;
            break;
        case GL_QUAD_STRIP:
            keep = n & ~1UL;
            carry[ncarry++] = keep - 2;
            carry[ncarry++] = keep - 1;
            if (n & 1)
                carry[ncarry++] = n - 1;
            break;
        default:                    /* fan, polygon: first and last */
            carry[ncarry++] = 0xffffffffUL;
            carry[ncarry++] = n - 1;
            break;
    }
    /* the carried vertices are stream dwords already: copy them raw */
    for (i = 0; i < ncarry; i++)
    {
        src = (carry[i] == 0xffffffffUL) ? c->first : c->vbase + carry[i] * vd;
        for (k = 0; k < vd; k++)
            save[i * vd + k] = src[k];
    }
    Close(c, keep);
    Open(c);
    if (c->vptr == NULL)
        return;
    for (i = 0; i < ncarry * vd; i++)
        c->vptr[i] = save[i];
    c->vptr += ncarry * vd;
    c->nverts = ncarry;
    c->stat_splits++;
}

void Core_Begin(struct Core *c, ULONG mode)
{
    ULONG units;

    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    if (mode > GL_POLYGON)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    units = Core_UnitsOn(c);
    Core_Validate(c);
    Hw_VertexFormat(c, units);
    c->units_on = units;
    c->vtx_dw = 4 + ((units & 1) ? 2 : 0) + ((units & 2) ? 2 : 0);
    c->prim = mode;
    c->hwprim = HwPrim(mode);
    Open(c);
}

void Core_End(struct Core *c)
{
    ULONG n;
    ULONG k;

    if (!IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    n = Whole(c->prim, c->nverts);
    if (c->prim == GL_LINE_LOOP && n >= 2 && c->vbase != NULL)
    {
        /* close the loop: the first vertex once more (the spare room) */
        for (k = 0; k < c->vtx_dw; k++)
            c->vbase[n * c->vtx_dw + k] = c->first[k];
        n++;
    }
    Close(c, n);
    c->prim = CORE_NO_PRIM;
    c->vbase = NULL;
    c->vptr = NULL;
}

static void Emit(struct Core *c, float x, float y, float z)
{
    ULONG *p;
    ULONG b0, b1, b2, b3;
    ULONG k;

    p = c->vptr;
    if (p == NULL)
        return;
    if (p >= c->vlimit)
    {
        Split(c);
        p = c->vptr;
        if (p == NULL)
            return;
    }
    /* the hot path: no calls (r2cs.h R2_FBITS, R2_SWAPI) */
    b0 = R2_FBITS(x);
    b1 = R2_FBITS(y);
    b2 = R2_FBITS(z);
    b3 = c->cur_color;
    if (c->cs->swap)
    {
        R2_SWAPI(b0);
        R2_SWAPI(b1);
        R2_SWAPI(b2);
        R2_SWAPI(b3);
    }
    p[0] = b0;
    p[1] = b1;
    p[2] = b2;
    p[3] = b3;
    k = 4;
    if (c->units_on & 1)
    {
        b0 = R2_FBITS(c->cur_st[0][0]);
        b1 = R2_FBITS(c->cur_st[0][1]);
        if (c->cs->swap)
        {
            R2_SWAPI(b0);
            R2_SWAPI(b1);
        }
        p[4] = b0;
        p[5] = b1;
        k = 6;
    }
    if (c->units_on & 2)
    {
        b0 = R2_FBITS(c->cur_st[1][0]);
        b1 = R2_FBITS(c->cur_st[1][1]);
        if (c->cs->swap)
        {
            R2_SWAPI(b0);
            R2_SWAPI(b1);
        }
        p[k] = b0;
        p[k + 1] = b1;
    }
    if (c->nverts == 0 && NeedsFirst(c->prim))
        for (k = 0; k < c->vtx_dw; k++)
            c->first[k] = p[k];
    c->vptr = p + c->vtx_dw;
    c->nverts++;
}

void Core_Vertex3f(struct Core *c, float x, float y, float z)
{
    if (!IN_BEGIN(c))
        return;         /* GL: undefined outside glBegin/glEnd; ignored */
    Emit(c, x, y, z);
}

void Core_Vertex4f(struct Core *c, float x, float y, float z, float w)
{
    if (!IN_BEGIN(c))
        return;
    if (w != 1.0f && w != 0.0f)
    {
        /* the same point; perspective-correct interpolation differs only
         * for w != 1, which no MiniGL client sends (GLQuake: never) */
        x /= w;
        y /= w;
        z /= w;
    }
    Emit(c, x, y, z);
}

void Core_TexCoord2f(struct Core *c, float s, float t)
{
    c->cur_st[0][0] = s;
    c->cur_st[0][1] = t;
}

void Core_MultiTexCoord2f(struct Core *c, ULONG unit, float s, float t)
{
    unit -= GL_TEXTURE0_ARB;
    if (unit >= CORE_UNITS)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    c->cur_st[unit][0] = s;
    c->cur_st[unit][1] = t;
}

void Core_Color4ub(struct Core *c, ULONG r, ULONG g, ULONG b, ULONG a)
{
    c->cur_color = ((a & 0xff) << 24) | ((b & 0xff) << 16) | ((g & 0xff) << 8) | (r & 0xff);
    c->cur_colorf[0] = -1.0f;   /* the floats follow from cur_color when asked */
}

void Core_Color4f(struct Core *c, float r, float g, float b, float a)
{
    c->cur_color = (Core_Unit255(a) << 24) | (Core_Unit255(b) << 16)
                 | (Core_Unit255(g) << 8) | Core_Unit255(r);
    c->cur_colorf[0] = r;
    c->cur_colorf[1] = g;
    c->cur_colorf[2] = b;
    c->cur_colorf[3] = a;
}

void Core_Normal3f(struct Core *c, float x, float y, float z)
{
    c->cur_normal[0] = x;
    c->cur_normal[1] = y;
    c->cur_normal[2] = z;
}
