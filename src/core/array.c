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

#include <stddef.h>
#include "core.h"
#include "coreint.h"
#include "r200reg.h"
#include "arrayfetch.h"

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

/* glNormalPointer: three components of GL_BYTE, GL_SHORT, GL_INT, GL_FLOAT
 * or GL_DOUBLE (lighting, light.c) */
void Core_NormalPointer(struct Core *c, ULONG type, LONG stride, const void *p)
{
    if (type != GL_BYTE && type != GL_SHORT && type != GL_INT && type != GL_FLOAT && type != GL_DOUBLE)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    SetArray(c, &c->na, 3, type, stride, p, 3);
}

void Core_EnableClientState(struct Core *c, ULONG array, ULONG on)
{
    c->lk_valid = 0;
    switch (array)
    {
        case GL_VERTEX_ARRAY:           c->va.enabled = on ? 1 : 0; return;
        case GL_COLOR_ARRAY:            c->ca.enabled = on ? 1 : 0; return;
        case GL_TEXTURE_COORD_ARRAY:    c->ta[c->client_unit].enabled = on ? 1 : 0; return;
        case GL_NORMAL_ARRAY:           c->na.enabled = on ? 1 : 0; return;
    }
    Compat_ClientState(c, array, on);       /* edge flag and index arrays, else an error */
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

/* The current attributes from the arrays, then the vertex. */
static void ArrayVertex(struct Core *c, ULONG i)
{
    const ULONG *v;
    float pos[4];

    if (c->va.type == GL_FLOAT && c->va.size == 3)
    {
        /* the positions as bits, without the FPU (GLQuake's glows) */
        FetchAttribs(c, i);
        v = (const ULONG *) (const void *) Elem(&c->va, i);
        Core_VertexBits(c, v[0], v[1], v[2]);
        return;
    }
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

/* One packed vertex (as vertex.c writes it into the ring) at p (lit
 * vertices: Core_PackLit, arraylit.c). */
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

/* A colour array element as the packed vertex's colour dword (cur_color's
 * layout: R in the low byte, then G, B, A). */
static ULONG ColorBits(ULONG type, ULONG size, const UBYTE *p)
{
    if (type == MGL_UBYTE_ARGB)         /* A R G B in memory (GLQuake's models) */
        return ((ULONG) p[0] << 24) | ((ULONG) p[3] << 16) | ((ULONG) p[2] << 8) | p[1];
    if (type == MGL_UBYTE_BGRA)         /* B G R A */
        return ((ULONG) p[3] << 24) | ((ULONG) p[0] << 16) | ((ULONG) p[1] << 8) | p[2];
    return ((size == 4 ? (ULONG) p[3] : 255UL) << 24) | ((ULONG) p[2] << 16)
           | ((ULONG) p[1] << 8) | p[0];
}

/* A big-endian longword at p (the 68k's own load; byte by byte elsewhere,
 * so the host tests read what the 68k reads). */
#if defined(__GNUC__) && defined(__mc68000__)
#define LOAD_BE32(p) (*(const ULONG *) (const void *) (p))
#else
#define LOAD_BE32(p) (((ULONG) (p)[0] << 24) | ((ULONG) (p)[1] << 16) | ((ULONG) (p)[2] << 8) \
                      | (ULONG) (p)[3])
#endif

/* The low two bits of an address (size_t: a pointer may be wider than a
 * ULONG on the test host). */
#define LOW2(p) ((ULONG) ((size_t) (const void *) (p) & 3))

/* Two 16-bit indices as one longword, the first in the high half: what a
 * longword load gives on the 68k, built from the halves on the host. */
#if defined(__GNUC__) && defined(__mc68000__)
#define LOAD_PAIR(ix) (*(const ULONG *) (const void *) (ix))
#else
#define LOAD_PAIR(ix) (((ULONG) (ix)[0] << 16) | (ULONG) (ix)[1])
#endif

/* Kept out of line: GCC inlined the hashers and the index packer into
 * Core_DrawElements, a 4.5 KB function with a frame pointer, where their
 * loops lost their values to the stack (MEASURED on the A4000D: about 15
 * cycles a hashed dword, with the loop counter in memory). SAS/C decides
 * by itself. */
#if defined(__GNUC__)
#define UC_NOINLINE __attribute__((noinline))
#else
#define UC_NOINLINE
#endif

/* A constant in a data register: GCC multiplies by an immediate otherwise,
 * an 8-byte instruction in the loop. */
#if defined(__GNUC__) && defined(__mc68000__)
#define UC_IN_REG(x) __asm__("" : "+d" (x))
#else
#define UC_IN_REG(x) ((void) 0)
#endif

/* GLQuake's models, straight through: a byte-swapped stream, x y z
 * floats, A R G B colour bytes, one coordinate set of floats. The colour
 * dword the GPU reads (a << 24 | b << 16 | g << 8 | r), byte-reversed, is
 * the source longword rotated left by 8. Few enough values for registers:
 * MEASURED on the A4000D, the general loop below kept its pointers on the
 * stack and walked the units for every vertex. */
static void PackXyzArgbSt(ULONG *out, const UBYTE *vp, ULONG vs, const UBYTE *cp, ULONG cs,
                          const UBYTE *tp, ULONG ts, ULONG count)
{
    const ULONG *v;
    const ULONG *t;
    ULONG b0, b1, b2, b3;

    while (count-- > 0)
    {
        v = (const ULONG *) (const void *) vp;
        t = (const ULONG *) (const void *) tp;
        b0 = v[0];
        b1 = v[1];
        b2 = v[2];
        R2_SWAPI(b0);
        R2_SWAPI(b1);
        R2_SWAPI(b2);
        out[0] = b0;
        out[1] = b1;
        out[2] = b2;
        b3 = LOAD_BE32(cp);
        out[3] = (b3 << 8) | (b3 >> 24);
        b0 = t[0];
        b1 = t[1];
        R2_SWAPI(b0);
        R2_SWAPI(b1);
        out[4] = b0;
        out[5] = b1;
        out += 6;
        vp += vs;
        cp += cs;
        tp += ts;
    }
}

/* GLQuake's world batches (gl_rsurf.c R_FlushWorldBatches), straight
 * through: x y z floats, no colour array (col: the current colour, already
 * byte-reversed), two coordinate sets of floats (texture, lightmap). */
static void PackXyzCSt2(ULONG *out, const UBYTE *vp, ULONG vs, ULONG col, const UBYTE *t0,
                        ULONG ts0, const UBYTE *t1, ULONG ts1, ULONG count)
{
    const ULONG *v;
    ULONG b0, b1, b2;

    while (count-- > 0)
    {
        v = (const ULONG *) (const void *) vp;
        b0 = v[0];
        b1 = v[1];
        b2 = v[2];
        R2_SWAPI(b0);
        R2_SWAPI(b1);
        R2_SWAPI(b2);
        out[0] = b0;
        out[1] = b1;
        out[2] = b2;
        out[3] = col;
        v = (const ULONG *) (const void *) t0;
        b0 = v[0];
        b1 = v[1];
        R2_SWAPI(b0);
        R2_SWAPI(b1);
        out[4] = b0;
        out[5] = b1;
        v = (const ULONG *) (const void *) t1;
        b0 = v[0];
        b1 = v[1];
        R2_SWAPI(b0);
        R2_SWAPI(b1);
        out[6] = b0;
        out[7] = b1;
        out += 8;
        vp += vs;
        t0 += ts0;
        t1 += ts1;
    }
}

/* The locked range packed the fast way: float positions of 2 or 3
 * components, float texture coordinates of 2 or more, colours as bytes (or
 * none) -- what GLQuake's models use. Every dword moves as bits, through
 * integer registers only. MEASURED on the A4000D: the general way (Fetch:
 * each component through Comp to a float, the colour through
 * Core_Color4ub, then Pack) was 88% of a locked draw. FALSE: other arrays
 * (the caller packs through Fetch). */
static ULONG PackFast(struct Core *c, ULONG *out, ULONG first, ULONG count, ULONG units)
{
    const UBYTE *vp;
    const UBYTE *cp;
    const UBYTE *tp[CORE_UNITS];
    const ULONG *v;
    const ULONG *t;
    ULONG vs;
    ULONG cs;
    ULONG ts[CORE_UNITS];
    ULONG st[CORE_UNITS][2];
    ULONG col;
    ULONG swap;
    ULONG b0, b1, b2, b3;
    ULONG k;
    ULONG u;
    ULONG n;

    if (c->no_fast || c->va.type != GL_FLOAT || c->va.size > 3)
        return 0;
    if (c->ca.enabled && c->ca.type != MGL_UBYTE_ARGB && c->ca.type != MGL_UBYTE_BGRA
        && c->ca.type != GL_UNSIGNED_BYTE)
        return 0;
    for (u = 0; u < CORE_UNITS; u++)
    {
        tp[u] = NULL;
        ts[u] = 0;
        if (!(units & (1UL << u)))
            continue;
        if (c->ta[u].enabled)
        {
            if (c->ta[u].type != GL_FLOAT || c->ta[u].size < 2)
                return 0;
            tp[u] = Elem(&c->ta[u], first);
            ts[u] = c->ta[u].stride ? c->ta[u].stride : c->ta[u].size * 4;
        }
        st[u][0] = R2_FBITS(c->cur_st[u][0]);
        st[u][1] = R2_FBITS(c->cur_st[u][1]);
    }
    vp = Elem(&c->va, first);
    vs = c->va.stride ? c->va.stride : c->va.size * 4;
    cp = c->ca.enabled ? Elem(&c->ca, first) : NULL;
    cs = c->ca.enabled ? (c->ca.stride ? c->ca.stride : c->ca.size * TypeSize(c->ca.type)) : 0;
    col = c->cur_color;
    swap = c->cs->swap;
    if (swap && c->va.size == 3 && cp != NULL && c->ca.type == MGL_UBYTE_ARGB && units == 1
        && c->ta[0].enabled)
    {
        /* unit 0's array, as the loop above found it (not read back from
         * tp/ts: SAS/C cannot see that loop set them, W317) */
        PackXyzArgbSt(out, vp, vs, cp, cs, Elem(&c->ta[0], first),
                      c->ta[0].stride ? c->ta[0].stride : c->ta[0].size * 4, count);
        return 1;
    }
    if (swap && c->va.size == 3 && cp == NULL && units == 3 && c->ta[0].enabled
        && c->ta[1].enabled)
    {
        R2_SWAPI(col);
        PackXyzCSt2(out, vp, vs, col, Elem(&c->ta[0], first),
                    c->ta[0].stride ? c->ta[0].stride : c->ta[0].size * 4,
                    Elem(&c->ta[1], first), c->ta[1].stride ? c->ta[1].stride : c->ta[1].size * 4,
                    count);
        return 1;
    }
    for (k = 0; k < count; k++)
    {
        v = (const ULONG *) vp;
        b0 = v[0];
        b1 = v[1];
        b2 = (c->va.size == 3) ? v[2] : 0;
        b3 = (cp != NULL) ? ColorBits(c->ca.type, c->ca.size, cp) : col;
        if (swap)
        {
            R2_SWAPI(b0);
            R2_SWAPI(b1);
            R2_SWAPI(b2);
            R2_SWAPI(b3);
        }
        out[0] = b0;
        out[1] = b1;
        out[2] = b2;
        out[3] = b3;
        n = 4;
        for (u = 0; u < CORE_UNITS; u++)
        {
            if (!(units & (1UL << u)))
                continue;
            if (tp[u] != NULL)
            {
                t = (const ULONG *) tp[u];
                b0 = t[0];
                b1 = t[1];
                tp[u] += ts[u];
            }
            else
            {
                b0 = st[u][0];
                b1 = st[u][1];
            }
            if (swap)
            {
                R2_SWAPI(b0);
                R2_SWAPI(b1);
            }
            out[n] = b0;
            out[n + 1] = b1;
            n += 2;
        }
        out += n;
        vp += vs;
        if (cp != NULL)
            cp += cs;
    }
    return 1;
}

/* The locked range in the stream still holds what a draw with units would
 * write: the same texture sets and, for each attribute without an array,
 * the same current value. */
static ULONG LockCurrent(struct Core *c, ULONG units)
{
    ULONG u;

    if (!c->lk_valid || c->lk_units != (units | (c->vnorm << 8)))
        return 0;
    if (!c->ca.enabled && c->lk_color != c->cur_color)
        return 0;
    if (c->vnorm && !c->na.enabled)
        return 0;                   /* lit, the current normal for all: packed anew */
    for (u = 0; u < CORE_UNITS; u++)
        if ((units & (1UL << u)) && !c->ta[u].enabled
            && (c->lk_st[u][0] != c->cur_st[u][0] || c->lk_st[u][1] != c->cur_st[u][1]))
            return 0;
    return 1;
}

/* --- The upload cache ---------------------------------------------------------
 *
 * GLQuake rebuilds most of its locked ranges and index lists byte for byte
 * every frame: MEASURED (FS-UAE, demo1), 75% of the locked uploads, 53% of
 * their bytes, repeat one of the previous frame's. With the stream written
 * in place (the null card, RadeonGLDirect=1), the SOURCE of a draw is
 * hashed first: GLQuake's two layouts as the straight-through packers read
 * them, and its 16-bit indices. Video RAM is written only for what the
 * cache does not hold, and a draw it holds entirely needs no packing, no
 * write and no heap lock. (MEASURED on the A4000D: packing everything into
 * fast RAM first and hashing that cost more than the bus it saved.)
 * Entries live in a ring in the heap, evicted oldest first, but never one
 * drawn in the current epoch: the GPU has not read it yet. */

#define UK_MODEL 0x4d4f4431UL   /* key seeds per kind */
#define UK_WORLD 0x574f5231UL
#define UK_IDX   0x49445831UL
#define UK_MODELRUN 0x4d4f4432UL   /* the same, hashed as one block */
#define UK_WORLDRUN 0x574f5232UL
#define UK_STATIC 0x53544131UL     /* the world format keyed by address (static_arrays) */
#define UK_STATIC_IDX 0x53544932UL /* index lists keyed by address (static_indices) */
#define UK_STATIC_MODEL 0x53544d33UL /* the model format keyed by address and tag */

/* Two hashes mixed with x (a: FNV-1a style, b: rotate and add). a rotates
 * after the multiply: without it bit 31 of x reached bit 31 of a only, and
 * two sign flips 64 dwords apart cancelled in every lane (MEASURED: a full
 * key collision in one trial of five; audit 2026-11-03). */

/* n dwords from p into h[0], h[1]. Four lanes -- UMIX's two for the even
 * dwords, two more for the odd ones -- so that two multiplications are in
 * flight on the 68060 instead of one chain; folded into two words at the
 * end. */
static UC_NOINLINE void HashRun(const ULONG *p, ULONG n, ULONG *h)
{
    ULONG a0;
    ULONG a1;
    ULONG b0;
    ULONG b1;
    ULONG x;
    ULONG y;
    ULONG prime;

    prime = 16777619UL;
    UC_IN_REG(prime);
    a0 = h[0];
    b0 = h[1];
    a1 = a0 ^ 0x9e3779b9UL;
    b1 = b0 ^ 0x7f4a7c15UL;
#if defined(__GNUC__) && defined(__mc68000__)
    /* The loop below by hand: in C the rotate (a swap) left GCC one data
     * register short, and it shuffled the lanes through address registers
     * every dword (MEASURED on the A4000D: 1.1 ms a GLQuake frame). Eight
     * data registers, the same operations, the two multiplications apart. */
    if (n >= 2)
    {
        ULONG k;

        k = n >> 1;
        __asm__ volatile (
            "1:\n\t"
            "move.l (%[p])+,%[x]\n\t"
            "move.l (%[p])+,%[y]\n\t"
            "eor.l %[x],%[a0]\n\t"
            "eor.l %[y],%[a1]\n\t"
            "mulu.l %[prime],%[a0]\n\t"
            "rol.l #5,%[b0]\n\t"
            "mulu.l %[prime],%[a1]\n\t"
            "rol.l #5,%[b1]\n\t"
            "swap %[a0]\n\t"
            "add.l %[x],%[b0]\n\t"
            "swap %[a1]\n\t"
            "add.l %[y],%[b1]\n\t"
            "subq.l #1,%[k]\n\t"
            "bne.s 1b"
            : [p] "+a" (p), [a0] "+d" (a0), [a1] "+d" (a1), [b0] "+d" (b0), [b1] "+d" (b1),
              [k] "+d" (k), [x] "=&d" (x), [y] "=&d" (y)
            : [prime] "d" (prime)
            : "cc", "memory");
        n &= 1;
    }
#else
    for (; n >= 2; n -= 2)
    {
        x = p[0];
        y = p[1];
        p += 2;
        a0 = UROL((a0 ^ x) * prime, 16);
        a1 = UROL((a1 ^ y) * prime, 16);
        b0 = UROL(b0, 5) + x;
        b1 = UROL(b1, 5) + y;
    }
#endif
    if (n != 0)
    {
        x = p[0];
        a0 = UROL((a0 ^ x) * prime, 16);
        b0 = UROL(b0, 5) + x;
    }
    h[0] = a0 ^ UROL(a1, 13);
    h[1] = b0 + UROL(b1, 11);
}

/* Three arrays of one interleaved block: the same stride (dwords, under
 * 1 KB), dword-aligned, and every array's bytes (len[i] of them) within
 * one stride of the lowest array -- GLQuake's models (x y z, s t w, A R G B:
 * 28 bytes) and world batches (x y z, s t, s t: 28 bytes). Then all a
 * packer reads of count vertices lies in ONE run of dwords from *base,
 * (count - 1) * stride + *span bytes long; *offs codes where each array
 * starts in it. FALSE: not one block. */
static ULONG OneBlock(const UBYTE *const *ptr, const ULONG *stride, const ULONG *len,
                      const UBYTE **base, ULONG *span, ULONG *offs)
{
    const UBYTE *lo;
    const UBYTE *hi;
    ULONG i;

    lo = ptr[0];
    hi = ptr[0] + len[0];
    for (i = 0; i < 3; i++)
    {
        if (stride[i] != stride[0] || (stride[i] & 3) != 0 || stride[i] >= 1024
            || LOW2(ptr[i]) != 0)
            return 0;
        if (ptr[i] < lo)
            lo = ptr[i];
        if (ptr[i] + len[i] > hi)
            hi = ptr[i] + len[i];
    }
    if ((ULONG) (hi - lo) > stride[0])
        return 0;
    *base = lo;
    *span = (ULONG) (hi - lo);
    *offs = (ULONG) (ptr[0] - lo) | ((ULONG) (ptr[1] - lo) << 10) | ((ULONG) (ptr[2] - lo) << 20);
    return 1;
}

/* The source of the model packer (PackXyzArgbSt), into h[0], h[1], when
 * its arrays are not one block. */
static UC_NOINLINE void HashModels(const UBYTE *vp, ULONG vs, const UBYTE *cp, ULONG cs,
                                   const UBYTE *tp, ULONG ts, ULONG count, ULONG *h)
{
    const ULONG *v;
    ULONG a;
    ULONG b;
    ULONG x;

    a = h[0];
    b = h[1];
    while (count-- > 0)
    {
        v = (const ULONG *) (const void *) vp;
        x = v[0]; UMIX(a, b, x);
        x = v[1]; UMIX(a, b, x);
        x = v[2]; UMIX(a, b, x);
        x = LOAD_BE32(cp); UMIX(a, b, x);
        v = (const ULONG *) (const void *) tp;
        x = v[0]; UMIX(a, b, x);
        x = v[1]; UMIX(a, b, x);
        vp += vs;
        cp += cs;
        tp += ts;
    }
    h[0] = a;
    h[1] = b;
}

/* The source of the world packer (PackXyzCSt2), when not one block. */
static UC_NOINLINE void HashWorld(const UBYTE *vp, ULONG vs, const UBYTE *t0, ULONG ts0,
                                  const UBYTE *t1, ULONG ts1, ULONG count, ULONG *h)
{
    const ULONG *v;
    ULONG a;
    ULONG b;
    ULONG x;

    a = h[0];
    b = h[1];
    while (count-- > 0)
    {
        v = (const ULONG *) (const void *) vp;
        x = v[0]; UMIX(a, b, x);
        x = v[1]; UMIX(a, b, x);
        x = v[2]; UMIX(a, b, x);
        v = (const ULONG *) (const void *) t0;
        x = v[0]; UMIX(a, b, x);
        x = v[1]; UMIX(a, b, x);
        v = (const ULONG *) (const void *) t1;
        x = v[0]; UMIX(a, b, x);
        x = v[1]; UMIX(a, b, x);
        vp += vs;
        t0 += ts0;
        t1 += ts1;
    }
    h[0] = a;
    h[1] = b;
}

/* 16-bit indices: two to a dword through HashRun, from a dword boundary
 * (a first index before it on its own, which the seed records). */
static UC_NOINLINE void HashIdx16(const UWORD *ix, ULONG n, ULONG *h)
{
    ULONG x;

    if (n != 0 && LOW2(ix) != 0)
    {
        h[1] ^= 0x80000000UL;
        x = ix[0];
        UMIX(h[0], h[1], x);
        ix++;
        n--;
    }
    HashRun((const ULONG *) (const void *) ix, n / 2, h);
    if ((n & 1) != 0)
    {
        x = ix[n - 1];
        UMIX(h[0], h[1], x);
    }
}

/* The key of the locked range's packed vertices: 1 in h[] for the layouts
 * the straight-through packers take (the same tests as PackFast), 0 for
 * the others (not cached). Arrays that form one interleaved block are
 * hashed as one run of dwords (OneBlock), with the block's shape in the
 * seed: the same bytes laid out otherwise pack otherwise. */
static ULONG VertexKey(struct Core *c, ULONG first, ULONG lcount, ULONG units, ULONG *h)
{
    const struct CoreArray *t0;
    const struct CoreArray *t1;
    const UBYTE *ptr[3];
    ULONG stride[3];
    ULONG len[3];
    const UBYTE *base;
    ULONG span;
    ULONG offs;
    ULONG col;
    ULONG k;

    /* lit (normals), or no texture unit on: never one of GLQuake's layouts.
     * 2026-11-12, units 0 (GLTron's cycle shadows and outlines: unlit,
     * untextured, 5.8 draws and ~85 KB packed into the stream a frame):
     * the general key below took only GLQuake's two layouts. Four bytes
     * in the object, the code around unchanged (a key for every other
     * layout at the end instead re-allocated Core_DrawElements' registers,
     * -148 bytes). */
    if (c->vnorm || units == 0)
        return c->static_arrays ? Core_StaticKey(c, first, lcount, units, h) : 0;
    if (c->no_fast || !c->cs->swap || c->va.type != GL_FLOAT || c->va.size != 3)
        return 0;
    t0 = &c->ta[0];
    t1 = &c->ta[1];
    if (units == 1 && c->ca.enabled && c->ca.type == MGL_UBYTE_ARGB && t0->enabled
        && t0->type == GL_FLOAT && t0->size >= 2)
    {
        ptr[0] = Elem(&c->va, first);
        stride[0] = c->va.stride ? c->va.stride : 12;
        len[0] = 12;
        ptr[1] = Elem(&c->ca, first);
        stride[1] = c->ca.stride ? c->ca.stride : 4;
        len[1] = 4;
        ptr[2] = Elem(t0, first);
        stride[2] = t0->stride ? t0->stride : t0->size * 4;
        len[2] = 8;
        if (c->static_arrays)
        {
            /* 2026-11-07: the program promised these bytes do not change
             * while their addresses, strides, count and its tag stay
             * (GL_RGL_arrays_tag: GLQuake's model fills, kept and drawn
             * again) -- nothing is read */
            h[0] = 2166136261UL ^ UK_STATIC_MODEL ^ c->static_gen;
            h[1] = UK_STATIC_MODEL ^ lcount;
            UMIX(h[0], h[1], c->static_tag);
            for (k = 0; k < 3; k++)
            {
                UMIX(h[0], h[1], (ULONG) (unsigned long) ptr[k]);
                UMIX(h[0], h[1], (ULONG) (((unsigned long) ptr[k] >> 16) >> 16));
            }
            UMIX(h[0], h[1], (stride[0] << 16) ^ (stride[1] << 8) ^ stride[2]);
            return 1;
        }
        if (OneBlock(ptr, stride, len, &base, &span, &offs))
        {
            h[0] = 2166136261UL ^ UK_MODELRUN ^ stride[0];
            h[1] = UK_MODELRUN ^ lcount ^ (offs << 1);
            HashRun((const ULONG *) (const void *) base, ((lcount - 1) * stride[0] + span) / 4, h);
            return 1;
        }
        h[0] = 2166136261UL ^ UK_MODEL;
        h[1] = UK_MODEL ^ lcount;
        HashModels(ptr[0], stride[0], ptr[1], stride[1], ptr[2], stride[2], lcount, h);
        return 1;
    }
    if (units == 3 && !c->ca.enabled && t0->enabled && t1->enabled && t0->type == GL_FLOAT
        && t1->type == GL_FLOAT && t0->size >= 2 && t1->size >= 2)
    {
        col = c->cur_color;
        ptr[0] = Elem(&c->va, first);
        stride[0] = c->va.stride ? c->va.stride : 12;
        len[0] = 12;
        ptr[1] = Elem(t0, first);
        stride[1] = t0->stride ? t0->stride : t0->size * 4;
        len[1] = 8;
        ptr[2] = Elem(t1, first);
        stride[2] = t1->stride ? t1->stride : t1->size * 4;
        len[2] = 8;
        if (c->static_arrays)
        {
            /* the program promised these bytes do not change: their
             * addresses, strides, count and colour are the key, nothing is
             * read (GL_RGL_static_arrays) */
            h[0] = 2166136261UL ^ UK_STATIC ^ c->static_gen;
            h[1] = UK_STATIC ^ lcount;
            UMIX(h[0], h[1], col);
            for (k = 0; k < 3; k++)
            {
                /* unsigned long holds a pointer on the Amiga and the host;
                 * the high half is 0 on the Amiga (two shifts: never 32) */
                UMIX(h[0], h[1], (ULONG) (unsigned long) ptr[k]);
                UMIX(h[0], h[1], (ULONG) (((unsigned long) ptr[k] >> 16) >> 16));
            }
            UMIX(h[0], h[1], (stride[0] << 16) ^ (stride[1] << 8) ^ stride[2]);
            return 1;
        }
        if (OneBlock(ptr, stride, len, &base, &span, &offs))
        {
            h[0] = 2166136261UL ^ UK_WORLDRUN ^ stride[0];
            h[1] = UK_WORLDRUN ^ lcount ^ (offs << 1);
            UMIX(h[0], h[1], col);
            HashRun((const ULONG *) (const void *) base, ((lcount - 1) * stride[0] + span) / 4, h);
            return 1;
        }
        h[0] = 2166136261UL ^ UK_WORLD;
        h[1] = UK_WORLD ^ lcount;
        UMIX(h[0], h[1], col);
        HashWorld(ptr[0], stride[0], ptr[1], stride[1], ptr[2], stride[2], lcount, h);
        return 1;
    }
    return 0;
}

static ULONG USlot(ULONG h1, ULONG h2)
{
    return (h1 ^ (h1 >> 11) ^ (h2 >> 17)) & (CORE_USLOTS - 1);
}

/* 2026-11-07: a key's entry is in one of UPROBE slots from USlot on. With
 * one slot, two entries drawn in every frame whose keys shared it pushed
 * each other out in turn: both missed in every frame, as long as both
 * were in view (MEASURED, A4000D, GLQuake demo1: whether such a pair
 * exists depends on where malloc put the world's arrays -- 117 static
 * misses in one run, 1099 in the next, same program, same library). */
#define UPROBE 4

static struct CoreUEntry *ULook(struct Core *c, ULONG h1, ULONG h2, ULONG ndw)
{
    struct CoreUEntry *e;
    ULONG s;
    ULONG i;
    ULONG q;

    s = USlot(h1, h2);
    for (i = 0; i < UPROBE; i++)
    {
        q = c->uslot[(s + i) & (CORE_USLOTS - 1)];
        if (q != 0)
        {
            e = &c->uq[q - 1];
            if (e->h1 == h1 && e->h2 == h2 && e->ndw == ndw)
                return e;
        }
    }
    return NULL;
}

/* Drop the oldest entry; FALSE if the current epoch drew it. */
static ULONG UPop(struct Core *c)
{
    struct CoreUEntry *e;

    e = &c->uq[c->uq_head];
    if (e->epoch == c->epoch)
        return 0;
    if (c->uslot[e->slot] == c->uq_head + 1)
        c->uslot[e->slot] = 0;
    c->uq_head = (c->uq_head + 1) % CORE_UQ;
    c->uq_n--;
    return 1;
}

/* The oldest entry, drawn in the current epoch, becomes the newest: its
 * bytes stay where they are and the allocation goes on past them, so the
 * queue still runs in the ring's order from ucache_pos. */
static void URotate(struct Core *c)
{
    struct CoreUEntry *e;
    ULONG q;

    e = &c->uq[c->uq_head];
    q = (c->uq_head + c->uq_n) % CORE_UQ;
    if (q != c->uq_head)
    {
        c->uq[q] = *e;
        if (c->uslot[e->slot] == c->uq_head + 1)
            c->uslot[e->slot] = (UWORD) (q + 1);
    }
    c->uq_head = (c->uq_head + 1) % CORE_UQ;
}

/* [lo, hi) cleared of entries from the oldest on: 0 when it is free, 1
 * when an entry was kept in the way (rotated, *next: the end of its
 * bytes), -1 when every entry has been passed over twice. Kept: one drawn
 * in this epoch, and one hit since the ring last passed it -- once: its
 * mark is cleared (a second chance, CLOCK). The world's blocks are hit
 * whenever they are in view and stay; a model's, new in each frame and
 * never hit, go first. (MEASURED, A4000D: without the second chance the
 * moving ring evicted world blocks just out of view, 1168 static misses
 * a demo1 run against 280.) */
static LONG UClear(struct Core *c, ULONG lo, ULONG hi, ULONG *turns, ULONG *next)
{
    struct CoreUEntry *e;

    while (c->uq_n > 0)
    {
        e = &c->uq[c->uq_head];
        if (!(e->pos < hi && e->pos + e->bytes > lo))
            return 0;
        if (!e->ref && UPop(c))
            continue;
        if (*turns == 0)
            return -1;
        (*turns)--;
        e->ref = 0;
        *next = e->pos + e->bytes;
        URotate(c);
        return 1;
    }
    return 0;
}

/* Room for bytes in the region: its offset there, or ~0 (the stream then).
 * 2026-11-07: entries drawn in this epoch are passed over, no longer the
 * end of the search. The ring is filled in the order of insertion, and
 * the world's blocks, inserted first, are drawn in every frame: once the
 * ring had wrapped they stood at its head, and every new block after the
 * world in a frame -- the models, the weapon -- went to the stream
 * uncached, to miss again in the next frame (MEASURED on the A4000D,
 * GLQuake demo1: 1425 blocks, 7 MB of 14.7 streamed). */
static ULONG UAlloc(struct Core *c, ULONG bytes)
{
    ULONG start;
    ULONG turns;
    ULONG next;
    LONG r;

    if (bytes > c->ucache_size / 4)
        return 0xffffffffUL;
    /* no room found once in this epoch: none until it ends (a frame with
     * more new data than the region holds would pass over every entry
     * twice for each draw after that) */
    if (c->ufull_epoch == c->epoch)
        return 0xffffffffUL;
    if (c->uq_n == CORE_UQ && !UPop(c))
        return 0xffffffffUL;
    start = c->ucache_pos;
    turns = 2 * c->uq_n;        /* a pass to clear the marks, one to evict */
    next = 0;
    for (;;)
    {
        if (start + bytes > c->ucache_size)
        {
            /* the end of the region: whatever is there goes or stays */
            while ((r = UClear(c, start, c->ucache_size, &turns, &next)) == 1)
                ;
            if (r < 0)
            {
                c->ufull_epoch = c->epoch;
                return 0xffffffffUL;
            }
            start = 0;
        }
        r = UClear(c, start, start + bytes, &turns, &next);
        if (r == 0)
            break;
        if (r < 0)
        {
            c->ufull_epoch = c->epoch;
            return 0xffffffffUL;
        }
        start = next;
    }
    c->ucache_pos = start + bytes;
    return start;
}

static void UInsert(struct Core *c, const ULONG *h, ULONG ndw, ULONG pos, ULONG bytes)
{
    struct CoreUEntry *e;
    ULONG q;
    ULONG s;
    ULONG t;
    ULONG i;
    ULONG slot;
    ULONG best;

    if (c->uq_n == CORE_UQ && !UPop(c))
        return;
    q = (c->uq_head + c->uq_n) % CORE_UQ;
    e = &c->uq[q];
    e->h1 = h[0];
    e->h2 = h[1];
    e->ndw = ndw;
    e->pos = pos;
    e->bytes = bytes;
    e->epoch = c->epoch;
    e->ref = 0;
    /* a free slot of the key's UPROBE, else the one whose entry was drawn
     * longest ago (it stays in the ring, out of reach, until evicted) */
    s = USlot(h[0], h[1]);
    slot = s;
    for (i = 0; i < UPROBE; i++)
    {
        t = (s + i) & (CORE_USLOTS - 1);
        if (c->uslot[t] == 0)
        {
            slot = t;
            break;
        }
    }
    if (i == UPROBE)
    {
        best = 0;
        for (i = 0; i < UPROBE; i++)
        {
            t = (s + i) & (CORE_USLOTS - 1);
            if (c->epoch - c->uq[c->uslot[t] - 1].epoch >= best)
            {
                best = c->epoch - c->uq[c->uslot[t] - 1].epoch;
                slot = t;
            }
        }
    }
    e->slot = slot;
    c->uslot[slot] = (UWORD) (q + 1);
    c->uq_n++;
}

void UCache_Clear(struct Core *c)
{
    ULONG i;

    c->uq_head = 0;
    c->uq_n = 0;
    c->ufull_epoch = 0;
    c->ucache_pos = 0;
    c->udirty_n = 0;
    c->udirty_all = 0;
    for (i = 0; i < CORE_USLOTS; i++)
        c->uslot[i] = 0;
}

/* Staged: bytes at pos of the region wait in ushadow for the back end. */
static void UDirty(struct Core *c, ULONG pos, ULONG bytes)
{
    ULONG n;

    n = c->udirty_n;
    if (c->udirty_all)
        return;
    if (n > 0 && c->udirty_off[n - 1] + c->udirty_len[n - 1] == pos)
    {
        c->udirty_len[n - 1] += bytes;      /* the ring allocates in order */
        return;
    }
    if (n == CORE_UDIRTY)
    {
        /* 2026-11-07: the last range stretched over the gap (the mirror and
         * video RAM hold the same bytes there: copied again, unchanged)
         * rather than the whole region over the bus. UAlloc passes over
         * live entries, so a frame's ranges are no longer one run. */
        if (pos >= c->udirty_off[n - 1])
            c->udirty_len[n - 1] = pos + bytes - c->udirty_off[n - 1];
        else
            c->udirty_all = 1;
        return;
    }
    c->udirty_off[n] = pos;
    c->udirty_len[n] = bytes;
    c->udirty_n = n + 1;
}

/* Where to write what goes to heap offset off (pos: its place in the
 * cache region, ~0 for the stream): the mirror or the staging buffer when
 * staged, else the heap (heap: as this moment has it). */
static ULONG *UWhere(struct Core *c, ULONG *heap, ULONG off, ULONG pos)
{
    if (pos != 0xffffffffUL)
        return c->ustaged ? c->ushadow + pos / 4 : heap + off / 4;
    return c->stream_direct ? heap + off / 4 : c->stream_cpu + (off - c->stream_off) / 4;
}

/* n 16-bit indices rebased to the lock, two to a dword in stream order,
 * each checked; FALSE if one is outside the lock (what was written then is
 * referred to by nothing). */
static UC_NOINLINE ULONG PackIdx16(struct Core *c, ULONG *p, const UWORD *ix16, ULONG n,
                                   ULONG first, ULONG lcount)
{
    ULONG i;
    ULONG a;
    ULONG b;
    ULONG g;

    if (c->cs->swap && first == 0 && LOW2(ix16) == 0)
    {
        /* GLQuake's case, glLockArrays(0, n): a pair is one longword, the
         * first index in its high half; the GPU wants the first in the low
         * half, each byte-swapped (R2_SWAPI of (b << 16 | a)) -- which is
         * that longword with the bytes of each half swapped */
        for (i = n / 2; i > 0; i--)
        {
            g = LOAD_PAIR(ix16);
            ix16 += 2;
            if ((g >> 16) >= lcount || (g & 0xffffUL) >= lcount)
                return 0;
            *p++ = ((g & 0x00ff00ffUL) << 8) | ((g >> 8) & 0x00ff00ffUL);
        }
        if ((n & 1) != 0)
        {
            g = ix16[0];
            if (g >= lcount)
                return 0;
            R2_SWAPI(g);
            *p = g;
        }
        return 1;
    }
    for (i = 0; i + 1 < n; i += 2)
    {
        a = (ULONG) ix16[i] - first;
        b = (ULONG) ix16[i + 1] - first;
        if (a >= lcount || b >= lcount)
            return 0;
        g = (b << 16) | (a & 0xffffUL);
        if (c->cs->swap)
            R2_SWAPI(g);
        *p++ = g;
    }
    if (i < n)
    {
        a = (ULONG) ix16[i] - first;
        if (a >= lcount)
            return 0;
        g = a & 0xffffUL;
        if (c->cs->swap)
            R2_SWAPI(g);
        *p = g;
    }
    return 1;
}

/* DrawLocked through the cache (16-bit indices). FALSE: not drawn (the
 * immediate way). */
static ULONG DrawCached(struct Core *c, ULONG mode, ULONG n, const UWORD *ix16, ULONG first,
                        ULONG lcount, ULONG units, ULONG vd)
{
    struct R2Aos aos;
    struct CoreUEntry *e;
    ULONG *heap;
    ULONG *p;
    ULONG vdw, idw, vbytes, ibytes;
    ULONG vh[2], ih[2];
    ULONG vkey;
    ULONG vpos, ipos;           /* cache positions to fill, ~0: none */
    ULONG voff, ioff;           /* heap offsets to write, ~0: nothing */
    ULONG vgpu, igpu;
    ULONG upload;
    ULONG vmiss, imiss, want;
    ULONG k;
    float pos[4];

    vdw = lcount * vd;
    idw = (n + 1) / 2;
    vbytes = (vdw * 4 + 31) & ~31UL;
    ibytes = (idw * 4 + 31) & ~31UL;
    upload = !LockCurrent(c, units);
    /* the worst case in the stream decides a wrap now, before any lock and
     * before the cache marks what this draw uses */
    if (c->stream_pos + ibytes + (upload ? vbytes : 0) > c->stream_size)
    {
        StreamWrap(c);
        upload = 1;
    }

    /* where the vertices are, or go: looked up here, room made below */
    vpos = 0xffffffffUL;
    voff = 0xffffffffUL;
    vgpu = c->lk_gpu;
    vkey = 0;
    vmiss = 0;
    vh[0] = 0;
    vh[1] = 0;
    if (upload)
    {
        /* GL_RGL_DYNAMIC_ARRAYS_HINT: new bytes each draw -- no key */
        vkey = c->dynamic_arrays ? 0 : VertexKey(c, first, lcount, units, vh);
        if (c->dynamic_arrays)
            c->stat_dyn_v++;
        e = vkey ? ULook(c, vh[0], vh[1], vdw) : NULL;
        if (e != NULL)
        {
            e->epoch = c->epoch;
            e->ref = 1;
            vgpu = c->heap.gpu + c->ucache_off + e->pos;
            c->stat_uc_hits++;
            c->stat_uc_saved += vdw * 4;
        }
        else
        {
            vmiss = 1;
            if (vkey)
                c->stat_uc_misses++;
            if (vkey && c->static_arrays)
                c->stat_uc_smiss_v++;
        }
    }

    /* the indices: a hit was checked against this same lock already */
    if (c->static_indices)
    {
        /* GL_RGL_STATIC_INDICES_HINT: the program promised this list does
         * not change: its address, count and lock are the key, nothing is
         * read (a miss still packs and checks it) */
        ih[0] = 2166136261UL ^ UK_STATIC_IDX ^ c->static_gen ^ first;
        ih[1] = UK_STATIC_IDX ^ lcount;
        UMIX(ih[0], ih[1], n);
        UMIX(ih[0], ih[1], (ULONG) (unsigned long) ix16);
        UMIX(ih[0], ih[1], (ULONG) (((unsigned long) ix16 >> 16) >> 16));
    }
    else
    {
        ih[0] = 2166136261UL ^ UK_IDX ^ first;
        ih[1] = UK_IDX ^ lcount;
        HashIdx16(ix16, n, ih);
    }
    ipos = 0xffffffffUL;
    ioff = 0xffffffffUL;
    igpu = 0;
    e = ULook(c, ih[0], ih[1], idw);
    imiss = (e == NULL);
    if (e != NULL)
    {
        e->epoch = c->epoch;
        e->ref = 1;
        igpu = c->heap.gpu + c->ucache_off + e->pos;
        c->stat_uc_hits++;
        c->stat_uc_saved += idw * 4;
    }
    else
    {
        c->stat_uc_misses++;
        if (c->static_indices)
            c->stat_uc_smiss_i++;
    }

    /* 2026-11-07: the room for what missed in one piece, after both
     * lookups. UAlloc passes over kept entries and moves them to the newest
     * end of its queue; with two allocations, one for the vertices and one
     * for the indices, the second could move entries ahead of the first
     * before it was inserted -- in the queue behind them, in the ring
     * before them -- and a later allocation would write over it unseen
     * (found in review; test_ucache_live checks the order after every
     * frame). The lookups first also keep a list that hits from being
     * evicted by its own draw's vertices. */
    want = ((vmiss && vkey) ? vbytes : 0) + (imiss ? ibytes : 0);
    k = (want != 0) ? UAlloc(c, want) : 0xffffffffUL;
    if (k != 0xffffffffUL)
    {
        if (vmiss && vkey)
        {
            vpos = k;
            k += vbytes;
        }
        if (imiss)
            ipos = k;
    }
    if (vmiss)
    {
        voff = (vpos != 0xffffffffUL) ? c->ucache_off + vpos : StreamAlloc(c, vbytes);
        vgpu = c->heap.gpu + voff;
    }
    if (imiss)
    {
        ioff = (ipos != 0xffffffffUL) ? c->ucache_off + ipos : StreamAlloc(c, ibytes);
        igpu = c->heap.gpu + ioff;
    }

    /* what the cache does not hold: into video RAM (one lock), or when
     * staged into the mirror and the staging buffer (no lock) */
    if (voff != 0xffffffffUL || ioff != 0xffffffffUL)
    {
        heap = c->heap.cpu;
        if (c->stream_direct)
        {
            heap = c->vram_begin(c->env.env);
            if (heap == NULL)
            {
                c->stat_vram_lost++;
                return 0;
            }
            c->heap.cpu = heap;
        }
        if (ioff != 0xffffffffUL
            && !PackIdx16(c, UWhere(c, heap, ioff, ipos), ix16, n, first, lcount))
        {
            /* an index outside the lock: nothing refers to what was
             * written, nothing enters the cache, the immediate way draws */
            if (c->stream_direct)
                c->vram_end(c->env.env);
            return 0;
        }
        if (voff != 0xffffffffUL)
        {
            p = UWhere(c, heap, voff, vpos);
            if (c->vnorm)
                Core_PackLit(c, p, first, lcount, vd, units);
            else if (!PackFast(c, p, first, lcount, units))
                for (k = 0; k < lcount; k++)
                {
                    Fetch(c, first + k, pos);
                    Pack(c, p + k * vd, units, pos);
                }
        }
        if (c->stream_direct)
            c->vram_end(c->env.env);    /* before anything that may flush */
        if (vpos != 0xffffffffUL)
        {
            UInsert(c, vh, vdw, vpos, vbytes);
            if (c->ustaged)
                UDirty(c, vpos, vbytes);
            c->stat_uc_written += vdw * 4;
        }
        if (ipos != 0xffffffffUL)
        {
            UInsert(c, ih, idw, ipos, ibytes);
            if (c->ustaged)
                UDirty(c, ipos, ibytes);
            c->stat_uc_written += idw * 4;
        }
        c->stat_stream_bytes += ((voff != 0xffffffffUL) ? vdw * 4 : 0)
                              + ((ioff != 0xffffffffUL) ? idw * 4 : 0);
    }
    if (upload)
    {
        c->lk_gpu = vgpu;
        c->lk_valid = 1;
        c->lk_units = units | (c->vnorm << 8);
        c->lk_color = c->cur_color;
        for (k = 0; k < CORE_UNITS; k++)
        {
            c->lk_st[k][0] = c->cur_st[k][0];
            c->lk_st[k][1] = c->cur_st[k][1];
        }
        c->stat_lock_uploads++;
    }
    Core_Validate(c);
    Hw_VertexFormat(c, units);
    Core_MarkTextures(c, units);
    aos.comps = vd;
    aos.stride = vd;
    aos.gpu = vgpu;
    R2E_LoadVbpntr(c->cs, 1, &aos);
    R2E_DrawIndexed(c->cs, Vtx_HwPrim(mode) | R_VF_COLOR_ORDER_RGBA | R_VF_TCL_OUTPUT_VTX_ENABLE,
                    n, igpu, lcount);
    c->stat_lock_draws++;
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
    ULONG *base;
    ULONG *heap;
    const UWORD *ix16;
    ULONG bad;
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
    if (type == GL_UNSIGNED_SHORT && c->cs->swap)
        ;                           /* checked while they are packed, below */
    else if (type == GL_UNSIGNED_SHORT)
    {
        ix16 = (const UWORD *) idx;
        for (i = 0; i < n; i++)
            if ((ULONG) ix16[i] - first >= lcount)
                return 0;           /* outside the lock: the general way */
    }
    else
        for (i = 0; i < n; i++)
        {
            k = Index(type, idx, (LONG) i);
            if (k < first || k >= first + lcount)
                return 0;
        }
    units = Core_UnitsOn(c);
    vd = 4 + c->x_vdw + ((units & 1) ? 2 : 0) + ((units & 2) ? 2 : 0);    /* x_vdw: a normal */
    vbytes = (lcount * vd * 4 + 31) & ~31UL;
    ibytes = (((n + 1) / 2) * 4 + 31) & ~31UL;
    if (vbytes + ibytes > c->stream_size)
        return 0;                   /* never fits: the general way */
    /* the room for the state and the draw before any address is taken: a
     * flush that drops the frame here rebases everything first */
    CORE_ROOM(c, CORE_DRAW_DW);
    if (c->ucache_on && !c->no_ucache && type == GL_UNSIGNED_SHORT)
        return DrawCached(c, mode, n, (const UWORD *) idx, first, lcount, units, vd);
    upload = !LockCurrent(c, units);
    if (c->stream_pos + ibytes + (upload ? vbytes : 0) > c->stream_size)
    {
        StreamWrap(c);              /* may flush: before any lock */
        upload = 1;
    }
    /* where the stream's dwords go: the staging buffer, or the heap itself
     * for this moment (Core_SetVram) */
    base = c->stream_cpu;
    if (c->stream_direct)
    {
        heap = c->vram_begin(c->env.env);
        if (heap == NULL)
        {
            c->stat_vram_lost++;
            return 0;               /* out of reach: the immediate way */
        }
        c->heap.cpu = heap;
        base = heap + c->stream_off / 4;
    }
    if (upload)
    {
        off = StreamAlloc(c, vbytes);
        p = base + (off - c->stream_off) / 4;
        if (c->vnorm)
            Core_PackLit(c, p, first, lcount, vd, units);
        else if (!PackFast(c, p, first, lcount, units))
            for (k = 0; k < lcount; k++)
            {
                Fetch(c, first + k, pos);
                Pack(c, p + k * vd, units, pos);
            }
        c->lk_gpu = c->heap.gpu + off;
        c->lk_valid = 1;
        c->lk_units = units | (c->vnorm << 8);
        c->lk_color = c->cur_color;
        for (k = 0; k < CORE_UNITS; k++)
        {
            c->lk_st[k][0] = c->cur_st[k][0];
            c->lk_st[k][1] = c->cur_st[k][1];
        }
        c->stat_lock_uploads++;
        c->stat_stream_bytes += lcount * vd * 4;
    }
    /* indices rebased to the lock, two to a dword, the first in the low
     * half as the GPU reads it (Mesa EMIT_TWO_ELTS) */
    off = StreamAlloc(c, ibytes);
    p = base + (off - c->stream_off) / 4;
    if (type == GL_UNSIGNED_SHORT && c->cs->swap)
    {
        /* GLQuake's indices, in one pass: pairs, rebased, byte-reversed (a
         * b as the GPU reads them), each checked against the lock on the
         * way (MEASURED on the A4000D: a separate check loop cost about
         * as much as the packing). An index outside: nothing refers to
         * what was written, and the immediate way draws instead. */
        ix16 = (const UWORD *) idx;
        bad = 0;
        for (i = 0; i + 1 < n; i += 2)
        {
            a = (ULONG) ix16[i] - first;
            b = (ULONG) ix16[i + 1] - first;
            bad |= (a >= lcount) | (b >= lcount);
            g = (b << 16) | (a & 0xffffUL);
            R2_SWAPI(g);
            *p++ = g;
        }
        if (i < n)
        {
            a = (ULONG) ix16[i] - first;
            bad |= (a >= lcount);
            g = a & 0xffffUL;
            R2_SWAPI(g);
            *p = g;
        }
        if (bad)
        {
            if (c->stream_direct)
                c->vram_end(c->env.env);
            return 0;
        }
    }
    else
        for (i = 0; i < n; i += 2)
        {
            a = Index(type, idx, (LONG) i) - first;
            b = (i + 1 < n) ? Index(type, idx, (LONG) i + 1) - first : 0;
            g = (b << 16) | a;
            if (c->cs->swap)
                R2_SWAPI(g);
            p[i / 2] = g;
        }
    if (c->stream_direct)
        c->vram_end(c->env.env);    /* before anything that may flush */
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
    c->stat_stream_bytes += ((n + 1) / 2) * 4;
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
    if (!c->va.enabled || count == 0 || CORE_SCISSOR_EMPTY(c))
        return;
    Core_Begin(c, mode);
    if (c->vnorm && c->na.enabled)
        for (i = 0; i < count; i++)
        {
            FetchNormal(c, (ULONG) (first + i));
            ArrayVertex(c, (ULONG) (first + i));
        }
    else
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
    if (!c->va.enabled || count == 0 || idx == NULL || CORE_SCISSOR_EMPTY(c))
        return;
    if (DrawLocked(c, mode, count, type, idx))
        return;
    Core_Begin(c, mode);
    if (c->vnorm && c->na.enabled)
        for (i = 0; i < count; i++)
        {
            k = Index(type, idx, i);
            FetchNormal(c, k);
            ArrayVertex(c, k);
        }
    else
        for (i = 0; i < count; i++)
        {
            k = Index(type, idx, i);
            ArrayVertex(c, k);
        }
    Core_End(c);
}
