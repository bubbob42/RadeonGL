/*
 * vtxlit.h -- glVertex and glNormal for a lit glBegin, without a call
 * (lib/rgltable.c's hooks, 2026-11-12).
 *
 * With lighting on (c->vnorm) every vertex carries the current normal and
 * vtxfast.h's CORE_VERTEX_FAST never takes it (vertex.c Open leaves vlimit
 * at the packet's start): Core_VertexBits, Emit's lit branch and EmitLit's
 * staging buffer do it, ~6 us a vertex on a 68060/50. CORE_VERTEX_LIT
 * writes the same record straight into the open packet -- x, y, z, the
 * normal, the colour, the texture coordinates of the units on, each swapped
 * -- and hands everything else to Core_VertexBits: no open lit glBegin, an
 * unswapped stream, the first vertex (a fan's or polygon's is kept for its
 * splits), a full packet (the split). The stream is the same dword for
 * dword (test_core test_vertex_lit).
 *
 * CORE_NORMAL_BITS keeps glNormal's three floats as bit patterns: no FPU.
 */

#ifndef RADEONGL_VTXLIT_H
#define RADEONGL_VTXLIT_H

#include "core.h"
#include "r2cs.h"

#define CORE_NB(c) ((ULONG *) (void *) (c)->cur_normal)

#define CORE_NORMAL_BITS(c, x, y, z) \
    (CORE_NB(c)[0] = (x), CORE_NB(c)[1] = (y), CORE_NB(c)[2] = (z))

/* x, y, z: ULONG variables holding float bits (swapped in place) */
#define CORE_VERTEX_LIT(c, x, y, z) \
    do \
    { \
        ULONG *vl_p; \
        ULONG vl_a; \
        ULONG vl_b; \
        vl_p = (c)->vptr; \
        if ((c)->vnorm && (c)->vfast && (c)->nverts != 0 && vl_p < (c)->vlimit_lit) \
        { \
            R2_SWAPI(x); \
            R2_SWAPI(y); \
            R2_SWAPI(z); \
            vl_p[0] = (x); \
            vl_p[1] = (y); \
            vl_p[2] = (z); \
            vl_a = CORE_NB(c)[0]; \
            vl_b = CORE_NB(c)[1]; \
            R2_SWAPI(vl_a); \
            R2_SWAPI(vl_b); \
            vl_p[3] = vl_a; \
            vl_p[4] = vl_b; \
            vl_a = CORE_NB(c)[2]; \
            vl_b = (c)->cur_color; \
            R2_SWAPI(vl_a); \
            R2_SWAPI(vl_b); \
            vl_p[5] = vl_a; \
            vl_p[6] = vl_b; \
            vl_p += 7; \
            if ((c)->units_on & 1) \
            { \
                vl_a = R2_FBITS((c)->cur_st[0][0]); \
                vl_b = R2_FBITS((c)->cur_st[0][1]); \
                R2_SWAPI(vl_a); \
                R2_SWAPI(vl_b); \
                vl_p[0] = vl_a; \
                vl_p[1] = vl_b; \
                vl_p += 2; \
            } \
            if ((c)->units_on & 2) \
            { \
                vl_a = R2_FBITS((c)->cur_st[1][0]); \
                vl_b = R2_FBITS((c)->cur_st[1][1]); \
                R2_SWAPI(vl_a); \
                R2_SWAPI(vl_b); \
                vl_p[0] = vl_a; \
                vl_p[1] = vl_b; \
                vl_p += 2; \
            } \
            (c)->vptr = vl_p; \
            (c)->nverts++; \
        } \
        else \
            Core_VertexBits((c), (x), (y), (z)); \
    } while (0)

#endif /* RADEONGL_VTXLIT_H */
