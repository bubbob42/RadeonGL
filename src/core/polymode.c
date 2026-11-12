/*
 * polymode.c -- glPolygonMode: polygons drawn as their edges (GL_LINE) or
 * their corners (GL_POINT), culled and given their face's mode on the CPU
 * (2026-11-11, for GLTron's Recognizer outline, which 0.11 drew filled).
 *
 * The R200 has no unfilled polygons as far as the open sources go:
 * SE_CNTL's face fields are used with CULL (0) and SOLID (3) only, and
 * Mesa's r200 driver draws GL_LINE and GL_POINT polygons through its
 * software fallback (tnl's unfilled triangle templates). So does this file:
 *
 *  - While a polygon mode other than GL_FILL is set, glBegin of a polygon
 *    primitive (GL_TRIANGLES .. GL_POLYGON) points the core's stream at a
 *    private buffer (pm_cs). vertex.c packs the vertices as it would into
 *    the ring, splits included, and knows nothing of it.
 *  - When that buffer is full, and at glEnd, its packets are walked: state
 *    packets go to the real stream as they are; each 3D_DRAW_IMMD_2 is cut
 *    into its polygons, and each polygon is culled and given its face's
 *    mode by the sign of its area in clip space (the 2D homogeneous
 *    determinant, Olano and Greer: right for any w, so no clipping is
 *    needed), then drawn as GL_LINES (its boundary edges, edge flags
 *    honoured), GL_POINTS or GL_TRIANGLES. Lines and points are never
 *    culled by the R200, so the culling must happen here.
 *  - glDrawElements and glDrawArrays draw through glArrayElement then; the
 *    locked-array and upload-cache paths stay for filled polygons.
 *
 * GL_FILL costs nothing new on the hot paths: the library's hooks call the
 * ...X wrappers here (a call of the same size), which test one mask and
 * pass the call on; vertex.c and array.c are unchanged, so GLQuake's code
 * keeps its places (Makefile CORE_LATE, compat.c's header).
 *
 * Limits: an edge flag changed right where a long primitive is split (every
 * CORE_IMMD_VERTS vertices) may apply one vertex late or early; lines take
 * the line width and GL_POLYGON_OFFSET_LINE is stored only; with GL_POINT
 * and GL_LINE on different faces the primitive's pieces are drawn in their
 * order, one packet per kind.
 */

#include "core.h"
#include "coreint.h"
#include "r200reg.h"
#include "r2pm4.h"

#define PM_DW       8192UL                  /* the private stream: a glBegin's state and
                                             * a full packet (vertex.c asks for at most
                                             * CORE_STATE_MAX + 2 + 11 x 257 at once) */
#define PM_MAXV     (CORE_IMMD_VERTS + 1)   /* vertices a collected packet holds */
#define PM_OUT      256UL                   /* vertices one drawn packet holds */
#define PM_EV       2048UL                  /* edge flag changes kept per buffer */
#define PM_POLYS    0x3f0UL                 /* GL_TRIANGLES (4) .. GL_POLYGON (9) */
#define PM_VDW      16UL                    /* dwords a vertex has at most (11 now) */
#define PM_SPOLY    1024UL                  /* static outline: polygons whose forms are kept */
#define PM_SIDX     8192UL                  /* ... and the index list drawn for them */

#define K_NONE      0UL
#define K_POINTS    1UL
#define K_LINES     2UL
#define K_TRIS      3UL

struct PmWork
{
    ULONG buf[PM_DW];               /* pm_cs's dwords */
    float clip[PM_MAXV][3];         /* x, y, w of each vertex of a packet */
    UBYTE flag[PM_MAXV + 3];        /* its edge flags */
    UWORD poly[PM_MAXV];            /* a polygon's vertices, in boundary order */
    UWORD out[PM_OUT];              /* the packet being built: vertices of the packet read */
    ULONG *ev_at[PM_EV];            /* edge flag changes: where the next vertex went */
    UBYTE ev_val[PM_EV];
    /* glDrawElements of static locked arrays (Static): which draw the forms
     * belong to, each polygon's area form, the index list drawn */
    const void *s_va, *s_idx;
    ULONG s_stride, s_size, s_count, s_type, s_mode, s_gen, s_tag, s_valid, s_npoly;
    float s_form[PM_SPOLY][4];
    UWORD s_list[PM_SIDX];
};

/* one drain of the private stream */
struct PmRun
{
    const ULONG *v;                 /* the packet's first vertex */
    ULONG vd;                       /* its dwords a vertex */
    ULONG cntl;                     /* its VF_CNTL */
    ULONG kind;                     /* what out[] holds */
    ULONG n;                        /* ... how many */
    ULONG gen;                      /* rebase_gen the real stream's state belongs to */
    ULONG face;                     /* facing is needed: culling or two modes */
    ULONG ev;                       /* the next edge flag change */
    ULONG cur;                      /* the edge flag in effect */
};

static ULONG Rd(const struct Core *c, ULONG v)
{
    if (c->pm_cs.swap)
        R2_SWAPI(v);
    return v;
}

/* --- the packets drawn -------------------------------------------------------------- */

/* out[] as one packet of its kind into the real stream */
static void Flush(struct Core *c, struct PmRun *r)
{
    ULONG *q;
    const ULONG *src;
    ULONG prim;
    ULONG i;
    ULONG k;
    ULONG n;

    n = r->n;
    r->n = 0;
    if (n == 0)
        return;
    prim = (r->kind == K_LINES) ? R_VF_PRIM_LINES
         : (r->kind == K_POINTS) ? R_VF_PRIM_POINTS : R_VF_PRIM_TRIANGLES;
    CORE_ROOM(c, 2 + r->vd * n);
    if (c->rebase_gen != r->gen)
    {
        /* the real stream dropped a frame: its state goes ahead again */
        Core_Validate(c);
        Hw_VertexFormat(c, c->units_on);
        r->gen = c->rebase_gen;
    }
    q = R2E_ImmdOpen(c->cs, r->vd, n);
    if (q == NULL)
        return;
    for (i = 0; i < n; i++)
    {
        src = r->v + (ULONG) c->pm->out[i] * r->vd;
        for (k = 0; k < r->vd; k++)
            *q++ = src[k];
    }
    (void) R2E_ImmdCloseN(c->cs, n, (r->cntl & 0xffffUL & ~R_VF_PRIM_MASK) | prim);
}

/* vertices a, b (c: for K_TRIS) of the packet into out[] */
static void Out(struct Core *c, struct PmRun *r, ULONG kind, ULONG a, ULONG b, ULONG d)
{
    ULONG need;

    need = (kind == K_TRIS) ? 3 : (kind == K_LINES) ? 2 : 1;
    if (r->kind != kind || r->n + need > PM_OUT)
    {
        Flush(c, r);
        r->kind = kind;
    }
    c->pm->out[r->n++] = (UWORD) a;
    if (need > 1)
        c->pm->out[r->n++] = (UWORD) b;
    if (need > 2)
        c->pm->out[r->n++] = (UWORD) d;
}

/* --- polygons ----------------------------------------------------------------------- */

static float Det(const float *a, const float *b, const float *d)
{
    return (float) (a[0] * (b[1] * d[2] - b[2] * d[1])
                    - a[1] * (b[0] * d[2] - b[2] * d[0])
                    + a[2] * (b[0] * d[1] - b[1] * d[0]));
}

/* One polygon: k vertices of the packet (ix, in boundary order).
 *   flags: its edges are boundary edges only where their first vertex's
 *          edge flag says so (GL_TRIANGLES, GL_QUADS, GL_POLYGON)
 *   open:  a split GL_POLYGON's continuation: its first edge (the first
 *          vertex to the last one drawn before) is no edge, and those two
 *          vertices were drawn as points already
 *   shut:  draw the closing edge (all but a split GL_POLYGON's early parts) */
static void Poly(struct Core *c, struct PmRun *r, const UWORD *ix, ULONG k, ULONG flags,
                 ULONG open, ULONG shut)
{
    const UBYTE *fl;
    float area;
    ULONG front;
    ULONG mode;
    ULONG i;
    ULONG a;
    ULONG b;

    fl = c->pm->flag;
    mode = c->pm_front;
    if (r->face)
    {
        area = 0.0f;
        for (i = 1; i + 1 < k; i++)
            area += Det(c->pm->clip[ix[0]], c->pm->clip[ix[i]], c->pm->clip[ix[i + 1]]);
        front = (c->front_face == GL_CW) ? (area < 0.0f) : (area > 0.0f);
        if (c->enables & EN_CULL_FACE)
        {
            if (c->cull_mode == GL_FRONT_AND_BACK)
                return;
            if ((c->cull_mode == GL_FRONT) ? front : !front)
                return;
        }
        if (!front)
            mode = c->pm_back;
    }
    if (mode == GL_LINE)
    {
        for (i = 0; i < k; i++)
        {
            if ((i == 0 && open) || (i == k - 1 && !shut))
                continue;
            a = ix[i];
            b = ix[(i + 1 < k) ? i + 1 : 0];
            if (!flags || fl[a])
                Out(c, r, K_LINES, a, b, 0);
        }
    }
    else if (mode == GL_POINT)
    {
        for (i = open ? 2 : 0; i < k; i++)
            if (!flags || fl[ix[i]])
                Out(c, r, K_POINTS, ix[i], 0, 0);
    }
    else
        for (i = 1; i + 1 < k; i++)
            Out(c, r, K_TRIS, ix[0], ix[i], ix[i + 1]);
}

/* the edge flag of each vertex of the packet at v (n of vd dwords): the
 * changes recorded at glEdgeFlag by where the next vertex was to go */
static void Flags(struct Core *c, struct PmRun *r, const ULONG *v, ULONG n, ULONG vd)
{
    ULONG i;

    for (i = 0; i < n; i++)
    {
        while (r->ev < c->pm_nev && (const ULONG *) c->pm->ev_at[r->ev] <= v + i * vd)
            r->cur = c->pm->ev_val[r->ev++];
        c->pm->flag[i] = (UBYTE) r->cur;
    }
}

/* each vertex's x, y, w in clip space (the TCL's MVP: projection x modelview) */
static void Clip(struct Core *c, const ULONG *v, ULONG n, ULONG vd)
{
    float m[16];
    float x, y, z;
    float *o;
    ULONG *u;
    ULONG i;

    Mat_Mul(m, c->proj[c->proj_top].m, c->mv[c->mv_top].m);
    for (i = 0; i < n; i++, v += vd)
    {
        /* the position's bits into the output's room, read back as floats
         * (no local variable punned: SAS/C keeps those apart) */
        o = c->pm->clip[i];
        u = (ULONG *) (void *) o;
        u[0] = Rd(c, v[0]);
        u[1] = Rd(c, v[1]);
        u[2] = Rd(c, v[2]);
        x = o[0];
        y = o[1];
        z = o[2];
        o[0] = m[0] * x + m[4] * y + m[8] * z + m[12];
        o[1] = m[1] * x + m[5] * y + m[9] * z + m[13];
        o[2] = m[3] * x + m[7] * y + m[11] * z + m[15];
    }
}

static ULONG SameVertex(const ULONG *v, ULONG vd)
{
    ULONG k;

    for (k = 0; k < vd; k++)
        if (v[k] != v[vd + k])
            return 0;
    return 1;
}

/* One collected 3D_DRAW_IMMD_2 at p (len dwords): its polygons drawn as the
 * polygon mode says. last: the primitive's final packet. */
static void Packet(struct Core *c, struct PmRun *r, const ULONG *p, ULONG len, ULONG last)
{
    struct PmWork *w;
    ULONG n;
    ULONG vd;
    ULONG cont;
    ULONG i;
    ULONG k;
    UWORD ix[4];

    w = c->pm;
    r->cntl = Rd(c, p[1]);
    n = r->cntl >> 16;
    if (n == 0 || n > PM_MAXV || len < 2 + n)
        return;
    vd = (len - 2) / n;
    if (vd > PM_VDW)
        return;
    Flush(c, r);                    /* out[] counts in the packet before */
    r->v = p + 2;
    r->vd = vd;
    cont = (c->pm_seq != 0);
    c->pm_seq++;
    Flags(c, r, r->v, n, vd);
    if (r->face)
        Clip(c, r->v, n, vd);
    switch (c->pm_prim)
    {
        case GL_TRIANGLES:
            for (i = 0; i + 2 < n; i += 3)
            {
                ix[0] = (UWORD) i; ix[1] = (UWORD) (i + 1); ix[2] = (UWORD) (i + 2);
                Poly(c, r, ix, 3, 1, 0, 1);
            }
            break;
        case GL_TRIANGLE_STRIP:
            for (i = 0; i + 2 < n; i++)
            {
                /* a split after an odd count repeats a vertex: one
                 * degenerate triangle of vertex.c's, not the program's */
                if (i == 0 && cont && SameVertex(r->v, vd))
                    continue;
                ix[0] = (UWORD) ((i & 1) ? i + 1 : i);
                ix[1] = (UWORD) ((i & 1) ? i : i + 1);
                ix[2] = (UWORD) (i + 2);
                Poly(c, r, ix, 3, 0, 0, 1);
            }
            break;
        case GL_TRIANGLE_FAN:
            for (i = 1; i + 1 < n; i++)
            {
                ix[0] = 0; ix[1] = (UWORD) i; ix[2] = (UWORD) (i + 1);
                Poly(c, r, ix, 3, 0, 0, 1);
            }
            break;
        case GL_QUADS:
            for (i = 0; i + 3 < n; i += 4)
            {
                for (k = 0; k < 4; k++)
                    ix[k] = (UWORD) (i + k);
                Poly(c, r, ix, 4, 1, 0, 1);
            }
            break;
        case GL_QUAD_STRIP:
            for (i = 0; i + 3 < n; i += 2)
            {
                ix[0] = (UWORD) i; ix[1] = (UWORD) (i + 1);
                ix[2] = (UWORD) (i + 3); ix[3] = (UWORD) (i + 2);
                Poly(c, r, ix, 4, 0, 0, 1);
            }
            break;
        default:                    /* GL_POLYGON */
            if (n < 3)
                break;
            for (i = 0; i < n; i++)
                w->poly[i] = (UWORD) i;
            /* a split polygon's closing edge comes with its last part,
             * which always holds a new vertex: vertex.c splits only when
             * one more arrives */
            Poly(c, r, w->poly, n, 1, cont, last);
            break;
    }
}

/* dwords in the packet at p */
static ULONG Len(const struct Core *c, const ULONG *p)
{
    ULONG h;

    h = Rd(c, p[0]);
    switch (R2_PKT_TYPE(h))
    {
        case 0:
        case 3:
            return R2_PKT_COUNT(h) + 2;
        case 2:
            return 1;
    }
    return 3;
}

static ULONG IsDraw(const struct Core *c, const ULONG *p)
{
    ULONG h;

    h = Rd(c, p[0]);
    return (R2_PKT_TYPE(h) == 3 && R2_PKT3_OPCODE(h) == R2_OP_3D_DRAW_IMMD_2) ? 1UL : 0UL;
}

/* Everything collected so far into the real stream; final: glEnd's (the
 * primitive's last packet is in the buffer, or nothing is) */
static void Drain(struct Core *c, ULONG final)
{
    struct PmRun r;
    struct R2Cs *saved;
    const ULONG *p;
    const ULONG *end;
    const ULONG *lastp;
    ULONG *q;
    ULONG len;
    ULONG i;

    saved = c->cs;
    c->cs = c->pm_real;
    end = c->pm_cs.ptr;
    lastp = NULL;
    for (p = c->pm->buf; p < end; p += len)
    {
        len = Len(c, p);
        if (IsDraw(c, p))
            lastp = p;
    }
    r.kind = K_NONE;
    r.n = 0;
    r.v = NULL;
    r.vd = 0;
    r.cntl = R_VF_COLOR_ORDER_RGBA | R_VF_TCL_OUTPUT_VTX_ENABLE;
    r.gen = c->rebase_gen;
    r.face = ((c->enables & EN_CULL_FACE) || c->pm_front != c->pm_back) ? 1UL : 0UL;
    r.ev = 0;
    r.cur = c->pm_flag0;
    for (p = c->pm->buf; p < end; p += len)
    {
        len = Len(c, p);
        if (p + len > end)
            break;                  /* never: vertex.c writes whole packets */
        if (IsDraw(c, p))
        {
            Packet(c, &r, p, len, (ULONG) (final && p == lastp));
            continue;
        }
        /* state: as it is, unless the real stream dropped a frame since
         * (the whole state goes ahead of the next draw then) */
        Flush(c, &r);
        if (c->rebase_gen != r.gen)
            continue;
        q = R2_ROOM(c->cs, len);
        if (q == NULL)
            break;
        if (c->rebase_gen != r.gen)
            continue;
        for (i = 0; i < len; i++)
            q[i] = p[i];
        c->cs->ptr = q + len;
    }
    Flush(c, &r);
    /* the buffer is empty again; its edge flag goes on from the last one */
    c->pm_flag0 = (c->pm_nev != 0) ? c->pm->ev_val[c->pm_nev - 1] : c->pm_flag0;
    c->pm_nev = 0;
    c->pm_cs.ptr = c->pm->buf;
    c->pm_cs.end = c->pm->buf + PM_DW;
    c->cs = saved;
}

/* pm_cs's owner: room again, by drawing what it holds (between packets) */
static LONG Need(struct R2Cs *cs, ULONG ndw)
{
    struct Core *c;

    c = (struct Core *) cs->owner;
    if (ndw > PM_DW)
        return R2_ETOOBIG;
    if (cs->immd != NULL)
        return R2_ESTATE;
    Drain(c, 0);
    return c->pm_real->error;
}

/* --- the calls ----------------------------------------------------------------------- */

void Core_PolygonModeX(struct Core *c, ULONG face, ULONG mode)
{
    ULONG f;
    ULONG b;
    ULONG m;

    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    if ((face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK)
        || (mode != GL_POINT && mode != GL_LINE && mode != GL_FILL))
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    f = (face == GL_BACK) ? c->pm_front : mode;
    b = (face == GL_FRONT) ? c->pm_back : mode;
    if ((f != GL_FILL || b != GL_FILL) && c->pm == NULL)
    {
        c->pm = (struct PmWork *) c->env.alloc(c->env.env, sizeof(struct PmWork));
        if (c->pm == NULL)
        {
            Core_Error(c, GL_OUT_OF_MEMORY);
            return;
        }
    }
    c->pm_front = f;
    c->pm_back = b;
    c->polygon_mode = f;
    m = (f != GL_FILL || b != GL_FILL) ? PM_POLYS : 0UL;
    if (m != c->pm_mask)
    {
        c->pm_mask = m;
        if (core_state_hook != NULL)
            core_state_hook(c);     /* the library's table (lib/rgltable.c) */
    }
}

void Core_BeginX(struct Core *c, ULONG mode)
{
    if (c->pm_mask == 0 || mode > GL_POLYGON || !((c->pm_mask >> mode) & 1) || IN_BEGIN(c))
    {
        Core_Begin(c, mode);
        return;
    }
    R2Cs_InitBuffer(&c->pm_cs, c->pm->buf, PM_DW, c->cs->swap);
    c->pm_cs.need = Need;
    c->pm_cs.owner = (APTR) c;
    c->pm_real = c->cs;
    c->cs = &c->pm_cs;
    c->pm_prim = mode;
    c->pm_seq = 0;
    c->pm_nev = 0;
    c->pm_flag0 = c->cur_edge;
    c->pm_on = 1;
    Core_Begin(c, mode);
}

void Core_EndX(struct Core *c)
{
    Core_End(c);
    if (!c->pm_on)
        return;
    Drain(c, 1);
    c->cs = c->pm_real;
    c->pm_on = 0;
}

/* glEdgeFlag inside such a glBegin: from the next vertex on */
void Pm_EdgeFlag(struct Core *c)
{
    if (c->pm_nev >= PM_EV || c->vptr == NULL)
        return;
    c->pm->ev_at[c->pm_nev] = c->vptr;
    c->pm->ev_val[c->pm_nev] = (UBYTE) c->cur_edge;
    c->pm_nev++;
}

/* The current values glArrayElement overwrites. The filled draws take
 * their attributes from the arrays without touching them, so a lit draw
 * after a polygon-mode one must find them as they were (2026-11-11, GLTron's
 * A/B: the outline's last normal lit the next draws, and every pixel of the
 * frame came out dithered differently). */
struct PmCurrent
{
    ULONG color, colorf_set, edge;
    float colorf[4];
    float normal[3];
    float st[CORE_UNITS][2];
};

static void SaveCurrent(const struct Core *c, struct PmCurrent *s)
{
    ULONG u;
    ULONG k;

    s->color = c->cur_color;
    s->colorf_set = c->colorf_set;
    s->edge = c->cur_edge;
    for (k = 0; k < 4; k++)
        s->colorf[k] = c->cur_colorf[k];
    for (k = 0; k < 3; k++)
        s->normal[k] = c->cur_normal[k];
    for (u = 0; u < CORE_UNITS; u++)
    {
        s->st[u][0] = c->cur_st[u][0];
        s->st[u][1] = c->cur_st[u][1];
    }
}

static void RestoreCurrent(struct Core *c, const struct PmCurrent *s)
{
    ULONG u;
    ULONG k;

    c->cur_color = s->color;
    c->colorf_set = s->colorf_set;
    c->cur_edge = s->edge;
    for (k = 0; k < 4; k++)
        c->cur_colorf[k] = s->colorf[k];
    for (k = 0; k < 3; k++)
        c->cur_normal[k] = s->normal[k];
    for (u = 0; u < CORE_UNITS; u++)
    {
        c->cur_st[u][0] = s->st[u][0];
        c->cur_st[u][1] = s->st[u][1];
    }
}

static ULONG Index(ULONG type, const void *idx, LONG i)
{
    if (type == GL_UNSIGNED_BYTE)
        return ((const UBYTE *) idx)[i];
    if (type == GL_UNSIGNED_SHORT)
        return ((const UWORD *) idx)[i];
    return ((const ULONG *) idx)[i];
}

/* --- static locked arrays: lines drawn by the card ------------------------------------- */

/* 2026-11-11, GLTron's Recognizer: 133 quads a frame through glArrayElement
 * and the CPU's conversion cost its timedemo 2.7% on the A4000D. Under
 * GL_RGL_STATIC_ARRAYS_HINT and GL_RGL_STATIC_INDICES_HINT the program
 * promises that neither the vertices nor the index list change, so:
 *
 *  - A polygon's area in clip space -- the sum over its fan of the 3 x 3
 *    determinant of (x, y, w) -- is, by Cauchy-Binet, K . L: L a 4-vector of
 *    the polygon's object-space corners alone (the 3 x 3 minors of the
 *    corners as columns of (x, y, z, 1)), K a 4-vector of the MVP alone (the
 *    minors of its x, y and w rows). L is worked out once per draw, K once
 *    per frame: culling and the face's mode stay exact (the same sign as
 *    Poly's, up to rounding at edge-on polygons) without transforming a
 *    vertex.
 *  - The visible polygons' edges (or corners) become a 16-bit index list
 *    into the program's own arrays, drawn as GL_LINES (GL_POINTS) through
 *    the locked path: the vertices come from the upload cache as for the
 *    filled draw, the list is keyed by its bytes (it changes with the view).
 *
 * Only independent polygons (GL_TRIANGLES, GL_QUADS, GL_POLYGON) of float
 * positions, one mode for both faces, no edge flag array; everything else
 * goes the glArrayElement way. */

/* the 3 x 3 determinant of rows (a0 a1 a2), (b0 b1 b2), (d0 d1 d2) */
static float Det3(float a0, float a1, float a2, float b0, float b1, float b2,
                  float d0, float d1, float d2)
{
    return (float) (a0 * (b1 * d2 - b2 * d1) - a1 * (b0 * d2 - b2 * d0) + a2 * (b0 * d1 - b1 * d0));
}

/* vertex i's position (float arrays of 2 or 3) */
static void Pos(const struct Core *c, ULONG i, float *v)
{
    const float *f;
    ULONG stride;

    stride = c->va.stride ? c->va.stride : c->va.size * 4;
    f = (const float *) (const void *) (c->va.ptr + i * stride);
    v[0] = f[0];
    v[1] = f[1];
    v[2] = (c->va.size == 3) ? f[2] : 0.0f;
}

/* polygon p's area form L: the fan's minors summed */
static void Form(const struct Core *c, ULONG type, const void *idx, ULONG first, ULONG per,
                 float *l)
{
    float a[3], b[3], d[3];
    ULONG i;

    l[0] = l[1] = l[2] = l[3] = 0.0f;
    Pos(c, Index(type, idx, (LONG) first), a);
    for (i = 1; i + 1 < per; i++)
    {
        Pos(c, Index(type, idx, (LONG) (first + i)), b);
        Pos(c, Index(type, idx, (LONG) (first + i + 1)), d);
        /* the corners as columns of (x, y, z, 1): the minor without row k */
        l[0] += Det3(a[1], b[1], d[1], a[2], b[2], d[2], 1.0f, 1.0f, 1.0f);
        l[1] += Det3(a[0], b[0], d[0], a[2], b[2], d[2], 1.0f, 1.0f, 1.0f);
        l[2] += Det3(a[0], b[0], d[0], a[1], b[1], d[1], 1.0f, 1.0f, 1.0f);
        l[3] += Det3(a[0], b[0], d[0], a[1], b[1], d[1], a[2], b[2], d[2]);
    }
}

/* TRUE when the draw was handled here */
static ULONG Static(struct Core *c, ULONG mode, LONG count, ULONG type, const void *idx)
{
    struct PmWork *w;
    float m[16];
    float k[4];
    float area;
    ULONG per;
    ULONG npoly;
    ULONG front;
    ULONG n;
    ULONG p;
    ULONG j;
    ULONG v;
    ULONG top;
    ULONG si;

    w = c->pm;
    if (!c->static_arrays || !c->static_indices || c->lock_count <= 0 || c->ea.enabled
        || c->pm_front != c->pm_back || c->va.type != GL_FLOAT
        || (c->va.size != 2 && c->va.size != 3)
        || (mode != GL_TRIANGLES && mode != GL_QUADS && mode != GL_POLYGON))
        return 0;
    per = (mode == GL_TRIANGLES) ? 3 : (mode == GL_QUADS) ? 4 : (ULONG) count;
    if (per < 3)
        return 1;                   /* GL draws nothing */
    npoly = (ULONG) count / per;
    if (npoly == 0)
        return 1;
    if (npoly > PM_SPOLY || 2 * npoly * per > PM_SIDX)
        return 0;
    if (!w->s_valid || w->s_va != (const void *) c->va.ptr || w->s_stride != c->va.stride
        || w->s_size != c->va.size || w->s_idx != idx || w->s_count != (ULONG) count
        || w->s_type != type || w->s_mode != mode || w->s_gen != c->static_gen
        || w->s_tag != c->static_tag)
    {
        for (p = 0; p < npoly; p++)
            Form(c, type, idx, p * per, per, w->s_form[p]);
        w->s_va = (const void *) c->va.ptr;
        w->s_stride = c->va.stride;
        w->s_size = c->va.size;
        w->s_idx = idx;
        w->s_count = (ULONG) count;
        w->s_type = type;
        w->s_mode = mode;
        w->s_gen = c->static_gen;
        w->s_tag = c->static_tag;
        w->s_npoly = npoly;
        w->s_valid = 1;
    }
    if (!c->cur_edge)
        return 1;                   /* every edge flag off: no edge, no corner */
    /* this frame's K: the minors of the MVP's x, y and w rows */
    Mat_Mul(m, c->proj[c->proj_top].m, c->mv[c->mv_top].m);
    k[0] = Det3(m[4], m[8], m[12], m[5], m[9], m[13], m[7], m[11], m[15]);
    k[1] = Det3(m[0], m[8], m[12], m[1], m[9], m[13], m[3], m[11], m[15]);
    k[2] = Det3(m[0], m[4], m[12], m[1], m[5], m[13], m[3], m[7], m[15]);
    k[3] = Det3(m[0], m[4], m[8], m[1], m[5], m[9], m[3], m[7], m[11]);
    n = 0;
    top = 0;
    for (p = 0; p < npoly; p++)
    {
        area = k[0] * w->s_form[p][0] + k[1] * w->s_form[p][1]
             + k[2] * w->s_form[p][2] + k[3] * w->s_form[p][3];
        front = (c->front_face == GL_CW) ? (area < 0.0f) : (area > 0.0f);
        if (c->enables & EN_CULL_FACE)
        {
            if (c->cull_mode == GL_FRONT_AND_BACK)
                return 1;
            if ((c->cull_mode == GL_FRONT) ? front : !front)
                continue;
        }
        for (j = 0; j < per; j++)
        {
            v = Index(type, idx, (LONG) (p * per + j));
            top |= v;
            w->s_list[n++] = (UWORD) v;
            if (c->pm_front == GL_LINE)
                w->s_list[n++] = (UWORD) Index(type, idx, (LONG) (p * per + (j + 1 < per ? j + 1 : 0)));
        }
    }
    if (top > 0xffffUL)
        return 0;                   /* not 16-bit: the glArrayElement way */
    if (n == 0)
        return 1;
    /* the list changes with the view: keyed by its bytes, not its address */
    si = c->static_indices;
    c->static_indices = 0;
    Core_DrawElements(c, (c->pm_front == GL_LINE) ? GL_LINES : GL_POINTS, (LONG) n,
                      GL_UNSIGNED_SHORT, w->s_list);
    c->static_indices = si;
    return 1;
}

/* the draw calls: polygons not filled go the glArrayElement way, through
 * the ...X glBegin (the checks are Core_DrawElements' and Core_DrawArrays');
 * static locked arrays through Static */
void Core_DrawElementsX(struct Core *c, ULONG mode, LONG count, ULONG type, const void *idx)
{
    struct PmCurrent cur;
    LONG i;

    if (c->pm_mask == 0 || mode > GL_POLYGON || !((c->pm_mask >> mode) & 1))
    {
        Core_DrawElements(c, mode, count, type, idx);
        return;
    }
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
    if (type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    if (!c->va.enabled || count == 0 || idx == NULL || CORE_SCISSOR_EMPTY(c))
        return;
    if (Static(c, mode, count, type, idx))
        return;
    SaveCurrent(c, &cur);
    Core_BeginX(c, mode);
    for (i = 0; i < count; i++)
        Core_ArrayElement(c, (LONG) Index(type, idx, i));
    Core_EndX(c);
    RestoreCurrent(c, &cur);
}

void Core_DrawArraysX(struct Core *c, ULONG mode, LONG first, LONG count)
{
    struct PmCurrent cur;
    LONG i;

    if (c->pm_mask == 0 || mode > GL_POLYGON || !((c->pm_mask >> mode) & 1))
    {
        Core_DrawArrays(c, mode, first, count);
        return;
    }
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
    if (!c->va.enabled || count == 0 || CORE_SCISSOR_EMPTY(c))
        return;
    SaveCurrent(c, &cur);
    Core_BeginX(c, mode);
    for (i = 0; i < count; i++)
        Core_ArrayElement(c, first + i);
    Core_EndX(c);
    RestoreCurrent(c, &cur);
}

void Pm_Free(struct Core *c)
{
    if (c->pm != NULL)
    {
        c->env.free(c->env.env, c->pm, sizeof(struct PmWork));
        c->pm = NULL;
    }
    c->pm_mask = 0;
}
