/*
 * array.c -- client vertex arrays: glVertexPointer and friends,
 * glDrawArrays, glDrawElements, glLockArraysEXT.
 *
 * Two ways out:
 *
 *  - Locked arrays (glLockArraysEXT, GLQuake's alias models): the locked
 *    range is packed ONCE into the stream block in VRAM, and each
 *    glDrawElements sends only its 16-bit indices and an indexed draw
 *    (3D_LOAD_VBPNTR, 3D_DRAW_INDX_2 + INDX_BUFFER). The census counted
 *    the models as the biggest bus item; this is how its cost model
 *    assumed they would travel. The stream starts from 0 again when the
 *    GPU is idle (the end of a frame, Core_GpuIdle).
 *  - Everything else (unlocked arrays, indices outside the lock, line
 *    loops): each array vertex is fetched, packed and written into an
 *    immediate packet like a glVertex.
 */

#include "core.h"
#include "coreint.h"
#include "r200reg.h"

static ULONG TypeSize(ULONG type)
{
    switch (type)
    {
        case GL_UNSIGNED_BYTE:
        case GL_BYTE:
        case MGL_UBYTE_ARGB:    /* MiniGL's packed colour layouts, a byte each */
        case MGL_UBYTE_BGRA:    return 1;
        case GL_SHORT:
        case GL_UNSIGNED_SHORT: return 2;
        case GL_INT:
        case GL_UNSIGNED_INT:
        case GL_FLOAT:          return 4;
        case GL_DOUBLE:         return 8;
    }
    return 0;
}

static float Comp(const UBYTE *p, ULONG type, ULONG k)
{
    switch (type)
    {
        case GL_FLOAT:          return ((const float *) p)[k];
        case GL_DOUBLE:         return (float) ((const double *) p)[k];
        case GL_SHORT:          return (float) ((const WORD *) p)[k];
        case GL_INT:            return (float) ((const LONG *) p)[k];
        case GL_UNSIGNED_BYTE:  return (float) p[k];
        case GL_BYTE:           return (float) ((const BYTE *) p)[k];
        case GL_UNSIGNED_SHORT: return (float) ((const UWORD *) p)[k];
        case GL_UNSIGNED_INT:   return (float) ((const ULONG *) p)[k];
    }
    return 0.0f;
}

static void SetArray(struct Core *c, struct CoreArray *a, LONG size, ULONG type,
                     LONG stride, const void *p, LONG minsize)
{
    if ((type == MGL_UBYTE_ARGB || type == MGL_UBYTE_BGRA) && (a != &c->ca || size != 4))
    {
        Core_Error(c, GL_INVALID_ENUM);         /* colour arrays of four only */
        return;
    }
    if (size < minsize || size > 4 || stride < 0)
    {
        Core_Error(c, GL_INVALID_VALUE);
        return;
    }
    if (TypeSize(type) == 0)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    c->lk_valid = 0;            /* a locked range in the stream no longer matches */
    a->size = (ULONG) size;
    a->type = type;
    a->stride = (ULONG) stride;
    a->ptr = (const UBYTE *) p;
}

void Core_VertexPointer(struct Core *c, LONG size, ULONG type, LONG stride, const void *p)
{
    SetArray(c, &c->va, size, type, stride, p, 2);
}

void Core_ColorPointer(struct Core *c, LONG size, ULONG type, LONG stride, const void *p)
{
    SetArray(c, &c->ca, size, type, stride, p, 3);
}

void Core_TexCoordPointer(struct Core *c, LONG size, ULONG type, LONG stride, const void *p)
{
    SetArray(c, &c->ta[c->client_unit], size, type, stride, p, 1);
}

void Core_EnableClientState(struct Core *c, ULONG array, ULONG on)
{
    c->lk_valid = 0;
    switch (array)
    {
        case GL_VERTEX_ARRAY:           c->va.enabled = on ? 1 : 0; return;
        case GL_COLOR_ARRAY:            c->ca.enabled = on ? 1 : 0; return;
        case GL_TEXTURE_COORD_ARRAY:    c->ta[c->client_unit].enabled = on ? 1 : 0; return;
        case GL_NORMAL_ARRAY:           return;     /* stored with lighting, later */
    }
    Core_Error(c, GL_INVALID_ENUM);
}

void Core_LockArrays(struct Core *c, LONG first, LONG count)
{
    c->lk_valid = 0;            /* a new lock: its vertices are written at the first draw */
    if (first < 0 || count <= 0)
    {
        Core_Error(c, GL_INVALID_VALUE);
        return;
    }
    if (c->lock_count != 0)
    {
        Core_Error(c, GL_INVALID_OPERATION);    /* EXT_compiled_vertex_array: no nesting */
        return;
    }
    c->lock_first = first;
    c->lock_count = count;
}

void Core_UnlockArrays(struct Core *c)
{
    if (c->lock_count == 0)
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    c->lock_count = 0;
}

static ULONG Index(ULONG type, const void *idx, LONG i)
{
    if (type == GL_UNSIGNED_BYTE)
        return ((const UBYTE *) idx)[i];
    if (type == GL_UNSIGNED_SHORT)
        return ((const UWORD *) idx)[i];
    return ((const ULONG *) idx)[i];
}

static const UBYTE *Elem(const struct CoreArray *a, ULONG i)
{
    ULONG stride;

    stride = a->stride ? a->stride : a->size * TypeSize(a->type);
    return a->ptr + i * stride;
}

/* The current attributes from the arrays; the position into pos[4]. */
static void Fetch(struct Core *c, ULONG i, float *pos)
{
    const UBYTE *p;
    ULONG u;

    if (c->ca.enabled)
    {
        p = Elem(&c->ca, i);
        /* GLQuake's alias models use MGL_UBYTE_ARGB: A, R, G, B in memory */
        if (c->ca.type == MGL_UBYTE_ARGB)
            Core_Color4ub(c, p[1], p[2], p[3], p[0]);
        else if (c->ca.type == MGL_UBYTE_BGRA)
            Core_Color4ub(c, p[2], p[1], p[0], p[3]);
        else if (c->ca.type == GL_UNSIGNED_BYTE)
            Core_Color4ub(c, p[0], p[1], p[2], c->ca.size == 4 ? p[3] : 255);
        else
            Core_Color4f(c, Comp(p, c->ca.type, 0), Comp(p, c->ca.type, 1),
                         Comp(p, c->ca.type, 2), c->ca.size == 4 ? Comp(p, c->ca.type, 3) : 1.0f);
    }
    for (u = 0; u < CORE_UNITS; u++)
    {
        if (!c->ta[u].enabled)
            continue;
        p = Elem(&c->ta[u], i);
        c->cur_st[u][0] = Comp(p, c->ta[u].type, 0);
        c->cur_st[u][1] = c->ta[u].size >= 2 ? Comp(p, c->ta[u].type, 1) : 0.0f;
    }
    p = Elem(&c->va, i);
    pos[0] = Comp(p, c->va.type, 0);
    pos[1] = Comp(p, c->va.type, 1);
    pos[2] = c->va.size >= 3 ? Comp(p, c->va.type, 2) : 0.0f;
    pos[3] = c->va.size == 4 ? Comp(p, c->va.type, 3) : 1.0f;
    if (pos[3] != 1.0f && pos[3] != 0.0f)
    {
        pos[0] /= pos[3];       /* as glVertex4f does (vertex.c) */
        pos[1] /= pos[3];
        pos[2] /= pos[3];
    }
}

/* The current attributes from the arrays, then the vertex. */
static void ArrayVertex(struct Core *c, ULONG i)
{
    float pos[4];

    Fetch(c, i, pos);
    Core_Vertex3f(c, pos[0], pos[1], pos[2]);
}

/* The stream is full: wait for the GPU, then start from 0 (which forgets
 * the locked range: it is written again). */
static void StreamWrap(struct Core *c)
{
    if (c->env.wait_idle != NULL)
        c->env.wait_idle(c->env.env);
    c->stat_idle_waits++;
    c->stat_stream_wraps++;
    Core_GpuIdle(c);
}

static ULONG StreamAlloc(struct Core *c, ULONG bytes)
{
    ULONG off;

    off = c->stream_off + c->stream_pos;
    c->stream_pos += bytes;     /* the callers round to 32 */
    return off;
}

/* One packed vertex (as vertex.c writes it into the ring) at p. */
static void Pack(struct Core *c, ULONG *p, ULONG units, const float *pos)
{
    ULONG b[8];
    ULONG n;
    ULONG k;

    b[0] = R2_FBITS(pos[0]);
    b[1] = R2_FBITS(pos[1]);
    b[2] = R2_FBITS(pos[2]);
    b[3] = c->cur_color;
    n = 4;
    if (units & 1)
    {
        b[n++] = R2_FBITS(c->cur_st[0][0]);
        b[n++] = R2_FBITS(c->cur_st[0][1]);
    }
    if (units & 2)
    {
        b[n++] = R2_FBITS(c->cur_st[1][0]);
        b[n++] = R2_FBITS(c->cur_st[1][1]);
    }
    for (k = 0; k < n; k++)
    {
        if (c->cs->swap)
            R2_SWAPI(b[k]);
        p[k] = b[k];
    }
}

/* The locked range in the stream still holds what a draw with units would
 * write: the same texture sets and, for each attribute without an array,
 * the same current value. */
static ULONG LockCurrent(struct Core *c, ULONG units)
{
    ULONG u;

    if (!c->lk_valid || c->lk_units != units)
        return 0;
    if (!c->ca.enabled && c->lk_color != c->cur_color)
        return 0;
    for (u = 0; u < CORE_UNITS; u++)
        if ((units & (1UL << u)) && !c->ta[u].enabled
            && (c->lk_st[u][0] != c->cur_st[u][0] || c->lk_st[u][1] != c->cur_st[u][1]))
            return 0;
    return 1;
}

/* glDrawElements on locked arrays from VRAM. FALSE: not this way (the
 * caller draws through the immediate path). */
static ULONG DrawLocked(struct Core *c, ULONG mode, LONG count, ULONG type, const void *idx)
{
    struct R2Aos aos;
    ULONG units;
    ULONG vd;
    ULONG n;
    ULONG first;
    ULONG lcount;
    ULONG vbytes;
    ULONG ibytes;
    ULONG upload;
    ULONG off;
    ULONG *p;
    ULONG i;
    ULONG k;
    ULONG a;
    ULONG b;
    ULONG g;
    float pos[4];

    if (c->lock_count <= 0 || c->stream_size == 0 || mode == GL_LINE_LOOP)
        return 0;
    first = (ULONG) c->lock_first;
    lcount = (ULONG) c->lock_count;
    n = Vtx_Whole(mode, (ULONG) count);
    if (n == 0)
        return 1;                   /* GL draws nothing */
    if (n > 0xffffUL || lcount > 0xffffUL)
        return 0;
    for (i = 0; i < n; i++)
    {
        k = Index(type, idx, (LONG) i);
        if (k < first || k >= first + lcount)
            return 0;               /* outside the lock: the general way */
    }
    units = Core_UnitsOn(c);
    vd = 4 + ((units & 1) ? 2 : 0) + ((units & 2) ? 2 : 0);
    vbytes = (lcount * vd * 4 + 31) & ~31UL;
    ibytes = (((n + 1) / 2) * 4 + 31) & ~31UL;
    if (vbytes + ibytes > c->stream_size)
        return 0;                   /* never fits: the general way */
    upload = !LockCurrent(c, units);
    if (c->stream_pos + ibytes + (upload ? vbytes : 0) > c->stream_size)
    {
        StreamWrap(c);
        upload = 1;
    }
    if (upload)
    {
        off = StreamAlloc(c, vbytes);
        p = c->heap.cpu + off / 4;
        for (k = 0; k < lcount; k++)
        {
            Fetch(c, first + k, pos);
            Pack(c, p + k * vd, units, pos);
        }
        c->lk_gpu = c->heap.gpu + off;
        c->lk_valid = 1;
        c->lk_units = units;
        c->lk_color = c->cur_color;
        for (k = 0; k < CORE_UNITS; k++)
        {
            c->lk_st[k][0] = c->cur_st[k][0];
            c->lk_st[k][1] = c->cur_st[k][1];
        }
        c->stat_lock_uploads++;
    }
    /* indices rebased to the lock, two to a dword, the first in the low
     * half as the GPU reads it (Mesa EMIT_TWO_ELTS) */
    off = StreamAlloc(c, ibytes);
    p = c->heap.cpu + off / 4;
    for (i = 0; i < n; i += 2)
    {
        a = Index(type, idx, (LONG) i) - first;
        b = (i + 1 < n) ? Index(type, idx, (LONG) i + 1) - first : 0;
        g = (b << 16) | a;
        if (c->cs->swap)
            R2_SWAPI(g);
        p[i / 2] = g;
    }
    Core_Validate(c);
    Hw_VertexFormat(c, units);
    Core_MarkTextures(c, units);
    aos.comps = vd;
    aos.stride = vd;
    aos.gpu = c->lk_gpu;
    R2E_LoadVbpntr(c->cs, 1, &aos);
    R2E_DrawIndexed(c->cs, Vtx_HwPrim(mode) | R_VF_COLOR_ORDER_RGBA | R_VF_TCL_OUTPUT_VTX_ENABLE,
                    n, c->heap.gpu + off, lcount);
    c->stat_lock_draws++;
    return 1;
}

void Core_DrawArrays(struct Core *c, ULONG mode, LONG first, LONG count)
{
    LONG i;

    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    if (count < 0 || first < 0)
    {
        Core_Error(c, GL_INVALID_VALUE);
        return;
    }
    if (mode > GL_POLYGON)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    if (!c->va.enabled || count == 0)
        return;
    Core_Begin(c, mode);
    for (i = 0; i < count; i++)
        ArrayVertex(c, (ULONG) (first + i));
    Core_End(c);
}

void Core_DrawElements(struct Core *c, ULONG mode, LONG count, ULONG type, const void *idx)
{
    LONG i;
    ULONG k;

    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    if (count < 0)
    {
        Core_Error(c, GL_INVALID_VALUE);
        return;
    }
    if (mode > GL_POLYGON
        || (type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT))
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    if (!c->va.enabled || count == 0 || idx == NULL)
        return;
    if (DrawLocked(c, mode, count, type, idx))
        return;
    Core_Begin(c, mode);
    for (i = 0; i < count; i++)
    {
        k = Index(type, idx, i);
        ArrayVertex(c, k);
    }
    Core_End(c);
}
