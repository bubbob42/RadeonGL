/*
 * arraylit.c -- the rare layouts of locked arrays (2026-11-08, for
 * Reflector's lit meshes): lit or sphere-mapped vertices, which carry a
 * normal after the position, into the stream; and the upload cache's key
 * for them under GL_RGL_STATIC_ARRAYS_HINT.
 *
 * Not in array.c: code added there moved everything linked after it and
 * cost GLQuake ~0.8% on the 68060, the work dword for dword the same
 * (MEASURED 2026-11-08; the same layout effect as lib/rgllight.c's the
 * night before). The Makefile links this module last. A static key for
 * unlit layouts GLQuake does not use as well (VertexKey's fall-through)
 * grew the inlined VertexKey and was 0.7% slow again (measured with vsync
 * on, 2026-11-08): lit only. Since 2026-11-12 also unlit draws with no
 * texture unit on (GLTron's shadows), through VertexKey's first test.
 */
#include "core.h"
#include "coreint.h"
#include "r200reg.h"
#include "arrayfetch.h"

#define UK_STATIC_ANY 0x53544134UL   /* a lit layout keyed by address */

/* Vertices first .. first + n - 1 of the arrays into the stream at p, vd
 * dwords apart, as vertex.c's EmitLit sends them: x y z, nx ny nz, colour,
 * s t per unit on. Without a normal array every vertex takes the current
 * normal. */
void Core_PackLit(struct Core *c, ULONG *p, ULONG first, ULONG n, ULONG vd, ULONG units)
{
    float pos[4];
    ULONG b[11];
    ULONG i;
    ULONG k;
    ULONG m;

    for (i = 0; i < n; i++, p += vd)
    {
        Fetch(c, first + i, pos);
        if (c->na.enabled)
            FetchNormal(c, first + i);
        b[0] = R2_FBITS(pos[0]);
        b[1] = R2_FBITS(pos[1]);
        b[2] = R2_FBITS(pos[2]);
        b[3] = R2_FBITS(c->cur_normal[0]);
        b[4] = R2_FBITS(c->cur_normal[1]);
        b[5] = R2_FBITS(c->cur_normal[2]);
        b[6] = c->cur_color;
        m = 7;
        if (units & 1)
        {
            b[m++] = R2_FBITS(c->cur_st[0][0]);
            b[m++] = R2_FBITS(c->cur_st[0][1]);
        }
        if (units & 2)
        {
            b[m++] = R2_FBITS(c->cur_st[1][0]);
            b[m++] = R2_FBITS(c->cur_st[1][1]);
        }
        for (k = 0; k < m; k++)
        {
            if (c->cs->swap)
                R2_SWAPI(b[k]);
            p[k] = b[k];
        }
    }
}

/* One array of the draw into a static key: where it is, the first vertex,
 * its stride, type and size */
static void KeyArray(const struct CoreArray *a, ULONG first, ULONG *h)
{
    UMIX(h[0], h[1], (ULONG) (unsigned long) a->ptr);
    UMIX(h[0], h[1], (ULONG) ((((unsigned long) a->ptr) >> 16) >> 16));
    UMIX(h[0], h[1], first);
    UMIX(h[0], h[1], (a->stride << 16) ^ (a->type << 4) ^ a->size);
}

/* Under GL_RGL_STATIC_ARRAYS_HINT a lit draw is keyed by its arrays'
 * addresses and by the current values that stand in for disabled ones;
 * nothing is read. The program promised the bytes stay. Returns 1 (a key
 * in h[0], h[1]). */
ULONG Core_StaticKey(struct Core *c, ULONG first, ULONG lcount, ULONG units, ULONG *h)
{
    ULONG u;

    h[0] = 2166136261UL ^ UK_STATIC_ANY ^ c->static_gen;
    h[1] = UK_STATIC_ANY ^ lcount;
    UMIX(h[0], h[1], c->static_tag);
    UMIX(h[0], h[1], units | (c->vnorm << 8) | (c->ca.enabled << 9) | (c->na.enabled << 10));
    KeyArray(&c->va, first, h);
    if (c->ca.enabled)
        KeyArray(&c->ca, first, h);
    else
        UMIX(h[0], h[1], c->cur_color);
    for (u = 0; u < CORE_UNITS; u++)
    {
        if (!(units & (1UL << u)))
            continue;
        if (c->ta[u].enabled)
            KeyArray(&c->ta[u], first, h);
        else
        {
            UMIX(h[0], h[1], R2_FBITS(c->cur_st[u][0]));
            UMIX(h[0], h[1], R2_FBITS(c->cur_st[u][1]));
        }
    }
    if (c->vnorm)
    {
        if (c->na.enabled)
            KeyArray(&c->na, first, h);
        else
        {
            UMIX(h[0], h[1], R2_FBITS(c->cur_normal[0]));
            UMIX(h[0], h[1], R2_FBITS(c->cur_normal[1]));
            UMIX(h[0], h[1], R2_FBITS(c->cur_normal[2]));
        }
    }
    return 1;
}
