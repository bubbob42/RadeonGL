/*
 * array.c -- client vertex arrays: glVertexPointer and friends,
 * glDrawArrays, glDrawElements, glLockArraysEXT.
 *
 * Milestone 1 draws arrays through the immediate path: each array vertex
 * is fetched, packed and written into an immediate packet like a glVertex.
 * That is correct and checker-clean, and costs the bus what the census
 * counted for unlocked arrays. Keeping locked arrays in VRAM (concept 8:
 * GLQuake re-locks the same alias-model arrays every frame) is the first
 * fast path and replaces this for locked draws.
 */

#include "core.h"
#include "coreint.h"

static ULONG TypeSize(ULONG type)
{
    switch (type)
    {
        case GL_UNSIGNED_BYTE:
        case GL_BYTE:           return 1;
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

static const UBYTE *Elem(const struct CoreArray *a, ULONG i)
{
    ULONG stride;

    stride = a->stride ? a->stride : a->size * TypeSize(a->type);
    return a->ptr + i * stride;
}

/* The current attributes from the arrays, then the vertex. */
static void ArrayVertex(struct Core *c, ULONG i)
{
    const UBYTE *p;
    ULONG u;
    float x, y, z;

    if (c->ca.enabled)
    {
        p = Elem(&c->ca, i);
        if (c->ca.type == GL_UNSIGNED_BYTE)
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
    x = Comp(p, c->va.type, 0);
    y = Comp(p, c->va.type, 1);
    z = c->va.size >= 3 ? Comp(p, c->va.type, 2) : 0.0f;
    if (c->va.size == 4)
        Core_Vertex4f(c, x, y, z, Comp(p, c->va.type, 3));
    else
        Core_Vertex3f(c, x, y, z);
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
    Core_Begin(c, mode);
    for (i = 0; i < count; i++)
    {
        if (type == GL_UNSIGNED_BYTE)
            k = ((const UBYTE *) idx)[i];
        else if (type == GL_UNSIGNED_SHORT)
            k = ((const UWORD *) idx)[i];
        else
            k = ((const ULONG *) idx)[i];
        ArrayVertex(c, k);
    }
    Core_End(c);
}
