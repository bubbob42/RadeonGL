/*
 * arrayfetch.h -- reading one vertex out of the client's arrays: the
 * helpers array.c and arraylit.c share (2026-11-08).
 *
 * Static functions in a header, on purpose: array.c inlines them into its
 * hot loops exactly as before, and arraylit.c (linked last) gets copies of
 * its own. Exported from array.c instead, they cost array.c out-of-line
 * copies, which moved every hot object linked after it -- and that layout
 * change alone cost GLQuake ~0.8% on the 68060 (MEASURED 2026-11-08).
 * Include it once per file, after core.h.
 */

#ifndef CORE_ARRAYFETCH_H
#define CORE_ARRAYFETCH_H

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

/* A normal array's component k, the signed integer types mapped to
 * [-1, 1] as GL does */
static float NormComp(const UBYTE *p, ULONG type, ULONG k)
{
    float f;

    /* through a float local: SAS/C computes the product as a double */
    switch (type)
    {
        case GL_BYTE:   f = (float) ((const BYTE *) p)[k] * (1.0f / 127.0f); return f;
        case GL_SHORT:  f = (float) ((const WORD *) p)[k] * (1.0f / 32767.0f); return f;
        case GL_INT:    f = (float) ((const LONG *) p)[k] * (1.0f / 2147483647.0f); return f;
    }
    return Comp(p, type, k);
}

static const UBYTE *Elem(const struct CoreArray *a, ULONG i)
{
    ULONG stride;

    stride = a->stride ? a->stride : a->size * TypeSize(a->type);
    return a->ptr + i * stride;
}

/* The current attributes from the arrays; the position into pos[4]. */
/* Vertex i's colour and texture coordinates into the current ones. */
static void FetchAttribs(struct Core *c, ULONG i)
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
        if (c->ta[u].type == GL_FLOAT && c->ta[u].size >= 2)
            CORE_TEXCOORD_BITS(c, u, ((const ULONG *) (const void *) p)[0],
                               ((const ULONG *) (const void *) p)[1]);
        else
        {
            c->cur_st[u][0] = Comp(p, c->ta[u].type, 0);
            c->cur_st[u][1] = c->ta[u].size >= 2 ? Comp(p, c->ta[u].type, 1) : 0.0f;
        }
    }
}

static void Fetch(struct Core *c, ULONG i, float *pos)
{
    const UBYTE *p;

    FetchAttribs(c, i);
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

/* Vertex i's normal into the current one (lit draws only: DrawArrays and
 * DrawElements call it in a loop of their own, so the unlit loop stays as
 * it was; arraylit.c's Core_PackLit too) */
static void FetchNormal(struct Core *c, ULONG i)
{
    const UBYTE *p;

    p = Elem(&c->na, i);
    if (c->na.type == GL_FLOAT)
    {
        c->cur_normal[0] = ((const float *) (const void *) p)[0];
        c->cur_normal[1] = ((const float *) (const void *) p)[1];
        c->cur_normal[2] = ((const float *) (const void *) p)[2];
    }
    else
    {
        c->cur_normal[0] = NormComp(p, c->na.type, 0);
        c->cur_normal[1] = NormComp(p, c->na.type, 1);
        c->cur_normal[2] = NormComp(p, c->na.type, 2);
    }
}

#endif
