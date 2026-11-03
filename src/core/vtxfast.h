/*
 * vtxfast.h -- glVertex straight into the open immediate packet, for the
 * per-vertex hooks (lib/rgl.c): one call fewer than Core_VertexBits for
 * every vertex but a primitive's first.
 *
 * The fast way is open (c->vfast, set by Core_Begin) only inside
 * glBegin/glEnd with a byte-swapped stream and a packet open. A
 * primitive's first vertex (fans and polygons keep it for a split), a full
 * packet (the split) and everything else go through Core_VertexBits, so
 * what lands in the stream is the same dwords either way (test_core.c
 * test_vertex_fast).
 *
 * x, y, z: ULONG VARIABLES holding float bit patterns; swapped in place.
 */

#ifndef RADEONGL_VTXFAST_H
#define RADEONGL_VTXFAST_H

#include "core.h"
#include "r2cs.h"

#define CORE_VERTEX_FAST(c, x, y, z) \
    do \
    { \
        ULONG *vf_p; \
        ULONG vf_a; \
        ULONG vf_b; \
        vf_p = (c)->vptr; \
        if ((c)->vfast && (c)->nverts != 0 && vf_p < (c)->vlimit) \
        { \
            R2_SWAPI(x); \
            R2_SWAPI(y); \
            R2_SWAPI(z); \
            vf_a = (c)->cur_color; \
            R2_SWAPI(vf_a); \
            vf_p[0] = (x); \
            vf_p[1] = (y); \
            vf_p[2] = (z); \
            vf_p[3] = vf_a; \
            vf_p += 4; \
            if ((c)->units_on & 1) \
            { \
                vf_a = R2_FBITS((c)->cur_st[0][0]); \
                vf_b = R2_FBITS((c)->cur_st[0][1]); \
                R2_SWAPI(vf_a); \
                R2_SWAPI(vf_b); \
                vf_p[0] = vf_a; \
                vf_p[1] = vf_b; \
                vf_p += 2; \
            } \
            if ((c)->units_on & 2) \
            { \
                vf_a = R2_FBITS((c)->cur_st[1][0]); \
                vf_b = R2_FBITS((c)->cur_st[1][1]); \
                R2_SWAPI(vf_a); \
                R2_SWAPI(vf_b); \
                vf_p[0] = vf_a; \
                vf_p[1] = vf_b; \
                vf_p += 2; \
            } \
            (c)->vptr = vf_p; \
            (c)->nverts++; \
        } \
        else \
            Core_VertexBits((c), (x), (y), (z)); \
    } while (0)

#endif /* RADEONGL_VTXFAST_H */
