/*
 * test_core.c -- host tests for the GL core (src/core): GL call sequences
 * in, R200 packets out, every stream through the packet checker.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "testkit.h"
#include "core.h"
#include "coreint.h"
#include "r2check.h"
#include "r2pm4.h"
#include "r200reg.h"
#include "simcp.h"

#define W_          640
#define H_          480
#define COLOR_GPU   0x00100000UL
#define DEPTH_GPU   0x00300000UL
#define TEX_GPU     0x00400000UL
#define TEX_SIZE    0x00400000UL
#define FENCE_REG   R_SCRATCH_REG2
#define SCREEN_GPU  0x01000000UL

static ULONG stream[400000];
static ULONG vram[TEX_SIZE / 4];
static struct Core core;
static struct R2Cs cs;
static ULONG idle_waits;
static ULONG vram_held;         /* a VRAM hook's lock is held now */
static ULONG idle_in_lock;      /* idle waits (flushes) while one was */

static APTR HostAlloc(APTR env, ULONG size)
{
    (void) env;
    return calloc(1, size);
}

static void HostFree(APTR env, APTR mem, ULONG size)
{
    (void) env;
    (void) size;
    free(mem);
}

static LONG HostIdle(APTR env)
{
    (void) env;
    idle_waits++;
    if (vram_held)
        idle_in_lock++;
    return 0;
}

static void Target(struct CoreTarget *t)
{
    t->width = W_;
    t->height = H_;
    t->color_gpu = COLOR_GPU;
    t->color_pitch = W_;
    t->color_format = R_COLOR_FORMAT_RGB565;
    t->depth_gpu = DEPTH_GPU;
    t->depth_pitch = W_;
}

static void Env(struct CoreEnv *e)
{
    e->alloc = HostAlloc;
    e->free = HostFree;
    e->wait_idle = HostIdle;
    e->env = NULL;
}

static void Fresh(ULONG swap)
{
    struct CoreTarget t;
    struct CoreEnv e;

    Core_Free(&core);
    R2Cs_InitBuffer(&cs, stream, sizeof(stream) / 4, swap);
    Target(&t);
    Env(&e);
    CHECK_INT(Core_Init(&core, &cs, &e, &t, TEX_GPU, vram, TEX_SIZE), GL_NO_ERROR);
    idle_waits = 0;
}

/* the checker reads every index of an indexed draw from the heap */
static LONG Peek(APTR ctx, ULONG gpu, ULONG *val)
{
    (void) ctx;
    if (gpu < TEX_GPU || gpu - TEX_GPU + 4 > TEX_SIZE)
        return -1;
    *val = vram[(gpu - TEX_GPU) / 4];
    return 0;
}

static void Cfg(struct R2ChkCfg *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->range[0].gpu = COLOR_GPU; cfg->range[0].size = W_ * H_ * 2;
    cfg->range[0].kinds = R2K_COLOR | R2K_TEXTURE;     /* the present reads it */
    cfg->range[1].gpu = DEPTH_GPU; cfg->range[1].size = W_ * H_ * 2; cfg->range[1].kinds = R2K_DEPTH;
    cfg->range[2].gpu = TEX_GPU;   cfg->range[2].size = TEX_SIZE;
    cfg->range[2].kinds = R2K_TEXTURE | R2K_VERTEX | R2K_INDEX;    /* textures, the stream */
    cfg->range[3].gpu = SCREEN_GPU; cfg->range[3].size = 1024 * 768 * 4; cfg->range[3].kinds = R2K_COLOR;
    cfg->nranges = 4;
    cfg->fence_reg = FENCE_REG;
    cfg->peek = Peek;
    cfg->peek_ctx = NULL;
}

static LONG Checked(struct R2ChkState *st)
{
    struct R2ChkCfg cfg;
    struct R2ChkError err;
    LONG e;

    Cfg(&cfg);
    R2Chk_Reset(st);
    e = R2Chk_Run(&cfg, st, stream, (ULONG) (cs.ptr - stream), cs.swap, 0, &err);
    if (e != R2CE_OK)
        printf("  checker: %s at %lu what 0x%lx value 0x%lx\n", R2Chk_ErrorName(e),
               (unsigned long) err.at, (unsigned long) err.what, (unsigned long) err.value);
    return e;
}

static ULONG V(ULONG i)
{
    return cs.swap ? R2Cs_Swap32(stream[i]) : stream[i];
}

static float F(ULONG bits)
{
    float f;

    memcpy(&f, &bits, 4);
    return f;
}

/* --- Rebuilding primitives from the packets ------------------------------------ */

#define MAXP 8192
static LONG got[MAXP][3];
static LONG want[MAXP][3];
static int ngot, nwant;
static ULONG packets_seen;

/* A triangle with its winding, rotated so the smallest id leads; a line
 * as an ordered pair (lines have no winding). */
static void Add(LONG (*l)[3], int *n, LONG a, LONG b, LONG c, int line)
{
    LONG t;

    if (line)
    {
        if (a > b) { t = a; a = b; b = t; }
        c = -1;
    }
    else
    {
        if (a == b || b == c || a == c)
            return;                 /* degenerate: draws nothing */
        while (a > b || a > c)
        {
            t = a; a = b; b = c; c = t;
        }
    }
    l[*n][0] = a;
    l[*n][1] = b;
    l[*n][2] = c;
    (*n)++;
}

static void Expand(LONG (*l)[3], int *n, ULONG prim, const LONG *v, ULONG cnt)
{
    ULONG i;

    switch (prim)
    {
        case R_VF_PRIM_LINES:
            for (i = 0; i + 1 < cnt; i += 2) Add(l, n, v[i], v[i + 1], 0, 1);
            break;
        case R_VF_PRIM_LINE_STRIP:
            for (i = 0; i + 1 < cnt; i++) Add(l, n, v[i], v[i + 1], 0, 1);
            break;
        case R_VF_PRIM_TRIANGLES:
            for (i = 0; i + 2 < cnt; i += 3) Add(l, n, v[i], v[i + 1], v[i + 2], 0);
            break;
        case R_VF_PRIM_TRIANGLE_STRIP:
            for (i = 0; i + 2 < cnt; i++)
            {
                if (i & 1) Add(l, n, v[i + 1], v[i], v[i + 2], 0);
                else       Add(l, n, v[i], v[i + 1], v[i + 2], 0);
            }
            break;
        case R_VF_PRIM_TRIANGLE_FAN:
        case R_VF_PRIM_POLYGON:
            for (i = 1; i + 1 < cnt; i++) Add(l, n, v[0], v[i], v[i + 1], 0);
            break;
        case R_VF_PRIM_QUADS:
            for (i = 0; i + 3 < cnt; i += 4)
            {
                Add(l, n, v[i], v[i + 1], v[i + 2], 0);
                Add(l, n, v[i], v[i + 2], v[i + 3], 0);
            }
            break;
        case R_VF_PRIM_QUAD_STRIP:
            for (i = 0; i + 3 < cnt; i += 2)
            {
                Add(l, n, v[i], v[i + 1], v[i + 3], 0);
                Add(l, n, v[i], v[i + 3], v[i + 2], 0);
            }
            break;
        case R_VF_PRIM_POINTS:
            for (i = 0; i < cnt; i++) Add(l, n, v[i], v[i], 0, 1);
            break;
    }
}

/* A heap dword as the core stored it, in the stream's byte order. */
static ULONG HeapV(ULONG gpu)
{
    ULONG v;

    v = vram[(gpu - TEX_GPU) / 4];
    return cs.swap ? R2Cs_Swap32(v) : v;
}

/* Every draw in the stream (immediate packets, and indexed draws from the
 * arrays in the heap), its vertices' x as ids. */
static void Collect(void)
{
    ULONG i;
    ULONG n;
    ULONG h;
    ULONG cntl;
    ULONG nv;
    ULONG vd;
    ULONG k;
    ULONG aos_gpu;
    ULONG aos_stride;
    ULONG pend_cntl;
    ULONG ix;
    ULONG a;
    LONG ids[4096];

    aos_gpu = 0;
    aos_stride = 0;
    pend_cntl = 0;

    ngot = 0;
    packets_seen = 0;
    n = (ULONG) (cs.ptr - stream);
    i = 0;
    while (i < n)
    {
        h = V(i);
        if (R2_PKT_TYPE(h) == 2)
        {
            i++;
            continue;
        }
        if (R2_PKT_TYPE(h) == 3 && R2_PKT3_OPCODE(h) == R2_OP_3D_DRAW_IMMD_2)
        {
            cntl = V(i + 1);
            nv = cntl >> 16;
            vd = R2_PKT_COUNT(h) / nv;
            for (k = 0; k < nv && k < 4096; k++)
                ids[k] = (LONG) F(V(i + 2 + k * vd));
            if ((cntl & R_VF_PRIM_MASK) != R_VF_PRIM_RECT_LIST)
            {
                Expand(got, &ngot, cntl & R_VF_PRIM_MASK, ids, nv);
                packets_seen++;
            }
        }
        if (R2_PKT_TYPE(h) == 3 && R2_PKT3_OPCODE(h) == R2_OP_3D_LOAD_VBPNTR)
        {
            aos_stride = (V(i + 2) >> 8) & 0x7f;    /* one array: the whole vertex */
            aos_gpu = V(i + 3);
        }
        if (R2_PKT_TYPE(h) == 3 && R2_PKT3_OPCODE(h) == R2_OP_3D_DRAW_INDX_2)
            pend_cntl = V(i + 1);
        if (R2_PKT_TYPE(h) == 3 && R2_PKT3_OPCODE(h) == R2_OP_INDX_BUFFER)
        {
            nv = pend_cntl >> 16;
            for (k = 0; k < nv && k < 4096; k++)
            {
                a = V(i + 2) + 4 * (k / 2);
                ix = (k & 1) ? HeapV(a) >> 16 : HeapV(a) & 0xffffUL;
                ids[k] = (LONG) F(HeapV(aos_gpu + 4 * aos_stride * ix));
            }
            Expand(got, &ngot, pend_cntl & R_VF_PRIM_MASK, ids, nv);
            packets_seen++;
        }
        i += 2 + R2_PKT_COUNT(h);
    }
}

static int Cmp(const void *a, const void *b)
{
    const LONG *x = (const LONG *) a;
    const LONG *y = (const LONG *) b;
    int k;

    for (k = 0; k < 3; k++)
        if (x[k] != y[k])
            return x[k] < y[k] ? -1 : 1;
    return 0;
}

static ULONG GlToHw(ULONG mode)
{
    static const ULONG m[10] = { R_VF_PRIM_POINTS, R_VF_PRIM_LINES, R_VF_PRIM_LINE_STRIP,
        R_VF_PRIM_LINE_STRIP, R_VF_PRIM_TRIANGLES, R_VF_PRIM_TRIANGLE_STRIP,
        R_VF_PRIM_TRIANGLE_FAN, R_VF_PRIM_QUADS, R_VF_PRIM_QUAD_STRIP, R_VF_PRIM_POLYGON };
    return m[mode];
}

/* Draw mode with n vertices (x = vertex number), rebuild what was drawn
 * from the packets, compare with what GL draws for it. */
static int SamePrims(ULONG mode, ULONG n)
{
    LONG ids[4096];
    ULONG i;
    struct R2ChkState st;

    Fresh(1);
    Core_Begin(&core, mode);
    for (i = 0; i < n; i++)
        Core_Vertex3f(&core, (float) i, 0.0f, 0.0f);
    Core_End(&core);
    if (Checked(&st) != R2CE_OK)
        return 0;
    Collect();
    nwant = 0;
    for (i = 0; i < n; i++)
        ids[i] = (LONG) i;
    /* GL's ignored incomplete tail: what Whole() keeps */
    if (mode == GL_LINE_LOOP)
    {
        if (n >= 2)
        {
            Expand(want, &nwant, R_VF_PRIM_LINE_STRIP, ids, n);
            Add(want, &nwant, ids[n - 1], ids[0], 0, 1);
        }
    }
    else
    {
        ULONG k = n;
        if (mode == GL_LINES) k &= ~1UL;
        if (mode == GL_TRIANGLES) k -= k % 3;
        if (mode == GL_QUADS) k &= ~3UL;
        if (mode == GL_QUAD_STRIP) k = k >= 4 ? (k & ~1UL) : 0;
        Expand(want, &nwant, GlToHw(mode), ids, k);
    }
    if (ngot != nwant)
    {
        printf("  mode %lu n %lu: %d drawn, %d wanted\n", (unsigned long) mode,
               (unsigned long) n, ngot, nwant);
        return 0;
    }
    qsort(got, (size_t) ngot, sizeof(got[0]), Cmp);
    qsort(want, (size_t) nwant, sizeof(want[0]), Cmp);
    if (memcmp(got, want, (size_t) ngot * sizeof(got[0])) != 0)
    {
        printf("  mode %lu n %lu: different primitives\n", (unsigned long) mode, (unsigned long) n);
        return 0;
    }
    return 1;
}

static void test_splits(void)
{
    static const ULONG lens[] = { 0, 1, 2, 3, 4, 5, 255, 256, 257, 258, 259, 511, 512, 513, 600, 1001 };
    ULONG mode;
    ULONG i;
    int ok;

    for (mode = GL_POINTS; mode <= GL_POLYGON; mode++)
    {
        ok = 1;
        for (i = 0; i < sizeof(lens) / sizeof(lens[0]); i++)
            if (!SamePrims(mode, lens[i]))
                ok = 0;
        CHECK(ok);
    }
    /* a long strip really was split, with its winding kept */
    SamePrims(GL_TRIANGLE_STRIP, 600);
    CHECK(core.stat_splits >= 2);
    CHECK(packets_seen >= 3);
}

/* --- A first triangle, byte for byte -------------------------------------------- */

static void test_triangle(void)
{
    struct R2ChkState st;
    ULONG n;
    ULONG i;
    ULONG h;
    ULONG found;

    Fresh(1);
    Core_Viewport(&core, 0, 0, W_, H_);
    Core_MatrixMode(&core, GL_PROJECTION);
    Core_LoadIdentity(&core);
    Core_Frustum(&core, -1, 1, -1, 1, 1, 100);
    Core_MatrixMode(&core, GL_MODELVIEW);
    Core_LoadIdentity(&core);
    Core_Clear(&core, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    Core_Enable(&core, GL_DEPTH_TEST, 1);
    Core_Begin(&core, GL_TRIANGLES);
    Core_Color4ub(&core, 0x11, 0x22, 0x33, 0x44);
    Core_Vertex3f(&core, -1.0f, -1.0f, -5.0f);
    Core_Vertex3f(&core, 1.0f, -1.0f, -5.0f);
    Core_Vertex3f(&core, 0.0f, 1.0f, -5.0f);
    Core_End(&core);
    CHECK_INT(Core_GetError(&core), GL_NO_ERROR);
    CHECK_INT(Checked(&st), R2CE_OK);
    CHECK_INT(st.draws, 2);             /* the clear and the triangle */
    /* the triangle's packet: last IMMD in the stream */
    n = (ULONG) (cs.ptr - stream);
    found = 0;
    for (i = 0; i < n; i += (R2_PKT_TYPE(h) == 2) ? 1 : 2 + R2_PKT_COUNT(h))
    {
        h = V(i);
        if (R2_PKT_TYPE(h) == 3 && R2_PKT3_OPCODE(h) == R2_OP_3D_DRAW_IMMD_2)
            found = i;
    }
    CHECK_HEX(V(found), R2_PKT3(R2_OP_3D_DRAW_IMMD_2, 12));
    CHECK_HEX(V(found + 1), R_VF_PRIM_TRIANGLES | R_VF_COLOR_ORDER_RGBA | R_VF_TCL_OUTPUT_VTX_ENABLE
                            | R_VF_PRIM_WALK_RING | (3UL << 16));
    CHECK_HEX(V(found + 2), R2Cs_FloatBits(-1.0f));
    CHECK_HEX(V(found + 4), R2Cs_FloatBits(-5.0f));
    /* colour as the GPU reads it: a << 24 | b << 16 | g << 8 | r */
    CHECK_HEX(V(found + 5), 0x44332211UL);
    CHECK_INT(core.stat_prims, 1);
}

/* --- Textures ------------------------------------------------------------------- */

/* texel k of a level, as the GPU reads it */
static ULONG Texel(const struct CoreTex *t, ULONG l, ULONG x, ULONG y)
{
    ULONG byte;
    ULONG g;

    byte = t->lofs[l] + y * t->pitch[l] + x * t->cpp;
    g = R2Cs_Swap32(vram[(t->gpu - TEX_GPU + byte) / 4]);   /* tex_swap: stored reversed */
    g >>= (byte & 3) * 8;
    return t->cpp == 1 ? (g & 0xff) : (g & 0xffff);
}

static void test_textures(void)
{
    UBYTE rgba[8 * 8 * 4];
    UBYTE lm[16 * 16 * 2];
    UBYTE l8[4 * 4];
    struct CoreTex *t;
    struct R2ChkState st;
    ULONG i;
    LONG lvl;
    ULONG wd;

    Fresh(1);
    for (i = 0; i < 8 * 8; i++)
    {
        rgba[i * 4 + 0] = (UBYTE) (i * 3);
        rgba[i * 4 + 1] = (UBYTE) (i * 5);
        rgba[i * 4 + 2] = (UBYTE) (i * 7);
        rgba[i * 4 + 3] = (UBYTE) (i & 1 ? 255 : 0);
    }
    Core_BindTexture(&core, GL_TEXTURE_2D, 5);
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, 4, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    t = core.tex[5];
    CHECK(t != NULL && t->gpu != 0);
    CHECK_INT(t->cpp, 2);
    CHECK_INT(t->pitch[0], 32);         /* 16 bytes of texels padded to 32 */
    /* ARGB4444 */
    for (i = 0; i < 8 * 8; i++)
        if (Texel(t, 0, i % 8, i / 8) != ((((ULONG) rgba[i * 4 + 3] >> 4) << 12)
                                          | (((ULONG) rgba[i * 4 + 0] >> 4) << 8)
                                          | (((ULONG) rgba[i * 4 + 1] >> 4) << 4)
                                          | ((ULONG) rgba[i * 4 + 2] >> 4)))
            break;
    CHECK_INT(i, 64);
    /* the stored dword holds texel 0 in its LOW half, byte-reversed */
    CHECK_HEX(vram[(t->gpu - TEX_GPU) / 4],
              R2Cs_Swap32((Texel(t, 0, 1, 0) << 16) | Texel(t, 0, 0, 0)));
    /* mip levels 1..3 */
    for (lvl = 1; lvl <= 3; lvl++)
    {
        wd = 8 >> lvl;
        Core_TexImage2D(&core, GL_TEXTURE_2D, lvl, 4, (LONG) wd, (LONG) wd, 0, GL_RGBA,
                        GL_UNSIGNED_BYTE, rgba);
    }
    CHECK_INT(core.tex[5]->levels, 0xf);
    CHECK_INT(Core_GetError(&core), GL_NO_ERROR);
    Core_TexImage2D(&core, GL_TEXTURE_2D, 1, 4, 3, 3, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    CHECK_INT(Core_GetError(&core), GL_INVALID_VALUE);      /* not the chain's size */

    /* a lightmap in MiniGL's 4_4_4_4 source format, as GLQuake uploads it */
    for (i = 0; i < 16 * 16; i++)
    {
        lm[i * 2 + 0] = (UBYTE) (0xf0 | (i & 15));      /* A = 15, R = i & 15 */
        lm[i * 2 + 1] = (UBYTE) i;
    }
    Core_BindTexture(&core, GL_TEXTURE_2D, 6);
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, MGL_UNSIGNED_SHORT_4_4_4_4, 16, 16, 0,
                    MGL_UNSIGNED_SHORT_4_4_4_4, GL_UNSIGNED_BYTE, lm);
    t = core.tex[6];
    CHECK_HEX(Texel(t, 0, 3, 2), ((ULONG) lm[(2 * 16 + 3) * 2] << 8) | lm[(2 * 16 + 3) * 2 + 1]);
    /* a sub-rectangle starting at an odd column: the edge dwords merge */
    memset(lm, 0, sizeof(lm));
    Core_TexSubImage2D(&core, GL_TEXTURE_2D, 0, 3, 4, 5, 2, MGL_UNSIGNED_SHORT_4_4_4_4,
                       GL_UNSIGNED_BYTE, lm);
    CHECK_HEX(Texel(t, 0, 3, 4), 0);
    CHECK_HEX(Texel(t, 0, 7, 5), 0);
    CHECK_HEX(Texel(t, 0, 2, 4), 0xf000UL | (((4 * 16 + 2) & 15) << 8) | ((4 * 16 + 2) & 0xff));
    CHECK_HEX(Texel(t, 0, 8, 5), 0xf000UL | (((5 * 16 + 8) & 15) << 8) | ((5 * 16 + 8) & 0xff));

    /* hazards: an upload waits only for a texture drawn in this epoch, and
     * one wait makes every texture safe (Regression 2026-11-02: each
     * texture kept its own flag, so a frame of lightmap updates waited
     * once per lightmap) */
    {
        ULONG w0 = idle_waits;
        UBYTE row[16 * 2];
        memset(row, 0x55, sizeof(row));
        core.tex[5]->use_epoch = core.epoch;        /* both drawn this epoch */
        core.tex[6]->use_epoch = core.epoch;
        Core_BindTexture(&core, GL_TEXTURE_2D, 6);
        Core_TexSubImage2D(&core, GL_TEXTURE_2D, 0, 0, 0, 16, 1, MGL_UNSIGNED_SHORT_4_4_4_4,
                           GL_UNSIGNED_BYTE, row);
        CHECK_INT(idle_waits - w0, 1);
        Core_BindTexture(&core, GL_TEXTURE_2D, 5);
        Core_TexSubImage2D(&core, GL_TEXTURE_2D, 0, 0, 0, 8, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        CHECK_INT(idle_waits - w0, 1);              /* the GPU was idle already */
        core.tex[5]->use_epoch = core.epoch;
        Core_GpuIdle(&core);                        /* a frame's fence passed */
        Core_TexSubImage2D(&core, GL_TEXTURE_2D, 0, 0, 0, 8, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        CHECK_INT(idle_waits - w0, 1);
        Core_BindTexture(&core, GL_TEXTURE_2D, 6);
    }

    /* Regression (2026-11-02, found reviewing the epochs): a texture drawn
     * in a later frame without its registers being emitted again is still
     * in use -- the use is stamped at the draw, not at the emit */
    {
        ULONG w0;
        UBYTE row[16 * 2];
        memset(row, 0x11, sizeof(row));
        Core_ActiveTexture(&core, GL_TEXTURE0_ARB);
        Core_BindTexture(&core, GL_TEXTURE_2D, 6);
        Core_Enable(&core, GL_TEXTURE_2D, 1);
        Core_Begin(&core, GL_TRIANGLES);
        Core_Vertex3f(&core, 0, 0, 0); Core_Vertex3f(&core, 1, 0, 0); Core_Vertex3f(&core, 0, 1, 0);
        Core_End(&core);
        Core_GpuIdle(&core);                    /* frame over */
        Core_Begin(&core, GL_TRIANGLES);        /* the same texture, nothing re-emitted */
        Core_Vertex3f(&core, 0, 0, 0); Core_Vertex3f(&core, 1, 0, 0); Core_Vertex3f(&core, 0, 1, 0);
        Core_End(&core);
        w0 = idle_waits;
        Core_TexSubImage2D(&core, GL_TEXTURE_2D, 0, 0, 0, 16, 1, MGL_UNSIGNED_SHORT_4_4_4_4,
                           GL_UNSIGNED_BYTE, row);
        CHECK_INT(idle_waits - w0, 1);
        Core_Enable(&core, GL_TEXTURE_2D, 0);
    }

    /* luminance: four texels a dword */
    for (i = 0; i < 16; i++)
        l8[i] = (UBYTE) (0x10 * i);
    Core_BindTexture(&core, GL_TEXTURE_2D, 7);
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, GL_LUMINANCE, 4, 4, 0, GL_LUMINANCE,
                    GL_UNSIGNED_BYTE, l8);
    CHECK_INT(core.tex[7]->cpp, 1);
    CHECK_HEX(Texel(core.tex[7], 0, 2, 3), 0xe0);

    /* draw with unit 0 = texture 5 (MODULATE), unit 1 = lightmap 6 */
    Core_ActiveTexture(&core, GL_TEXTURE0_ARB);
    Core_BindTexture(&core, GL_TEXTURE_2D, 5);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
    Core_Enable(&core, GL_TEXTURE_2D, 1);
    Core_ActiveTexture(&core, GL_TEXTURE1_ARB);
    Core_BindTexture(&core, GL_TEXTURE_2D, 6);
    Core_Enable(&core, GL_TEXTURE_2D, 1);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    CHECK_INT(Core_UnitsOn(&core), 3);
    Core_Begin(&core, GL_POLYGON);
    for (i = 0; i < 5; i++)
    {
        Core_MultiTexCoord2f(&core, GL_TEXTURE0_ARB, (float) i, 0.5f);
        Core_MultiTexCoord2f(&core, GL_TEXTURE1_ARB, 0.25f, (float) i);
        Core_Vertex3f(&core, (float) i, 1.0f, 2.0f);
    }
    Core_End(&core);
    CHECK_INT(core.vtx_dw, 8);
    CHECK_INT(Core_GetError(&core), GL_NO_ERROR);
    CHECK_INT(Checked(&st), R2CE_OK);
    CHECK_INT(st.draws, 3);         /* the two hazard-test triangles and the polygon */
}

/* --- State encoding -------------------------------------------------------------- */

/* The last value written to reg in the stream (0xdeadbeef if none). */
static ULONG LastReg(ULONG reg)
{
    ULONG i;
    ULONG n;
    ULONG h;
    ULONG k;
    ULONG val;

    val = 0xdeadbeefUL;
    n = (ULONG) (cs.ptr - stream);
    for (i = 0; i < n; i += (R2_PKT_TYPE(h) == 2) ? 1 : 2 + R2_PKT_COUNT(h))
    {
        h = V(i);
        if (R2_PKT_TYPE(h) != 0)
            continue;
        for (k = 0; k <= R2_PKT_COUNT(h); k++)
            if ((R2_PKT0_ONE_REG_WR(h) ? R2_PKT0_REG(h) : R2_PKT0_REG(h) + 4 * k) == reg)
                val = V(i + 1 + k);
    }
    return val;
}

static void Draw1(void)
{
    Core_Begin(&core, GL_TRIANGLES);
    Core_Vertex3f(&core, 0, 0, 0);
    Core_Vertex3f(&core, 1, 0, 0);
    Core_Vertex3f(&core, 0, 1, 0);
    Core_End(&core);
}

static void test_state(void)
{
    struct R2ChkState st;
    ULONG v;

    Fresh(0);
    Core_DepthFunc(&core, GL_LEQUAL);
    Core_Enable(&core, GL_DEPTH_TEST, 1);
    Core_DepthMask(&core, 0);
    Core_BlendFunc(&core, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    Core_Enable(&core, GL_BLEND, 1);
    Core_AlphaFunc(&core, GL_GREATER, 0.666f);
    Core_Enable(&core, GL_ALPHA_TEST, 1);
    Core_CullFace(&core, GL_FRONT);
    Core_Enable(&core, GL_CULL_FACE, 1);
    Core_ShadeModel(&core, GL_FLAT);
    Core_Scissor(&core, 10, 20, 100, 50);
    Core_Enable(&core, GL_SCISSOR_TEST, 1);
    Core_SetZOffset(&core, -0.0005f);
    Core_Enable(&core, MGL_Z_OFFSET, 1);
    Core_ColorMask(&core, 1, 1, 0, 1);
    Draw1();
    CHECK_INT(Checked(&st), R2CE_OK);
    v = LastReg(R_RB3D_ZSTENCILCNTL);
    CHECK_HEX(v & R_Z_TEST_LESS * 7, 2UL << 4);     /* LEQUAL = R200 2 */
    CHECK(!(v & R_Z_WRITE_ENABLE));
    v = LastReg(R_RB3D_CNTL);
    CHECK(v & R_Z_ENABLE);
    CHECK(v & R_ALPHA_BLEND_ENABLE);
    CHECK(v & R_PLANE_MASK_ENABLE);
    CHECK_HEX(LastReg(R_RB3D_PLANEMASK), 0xffe0UL);  /* RGB565 without blue */
    CHECK_HEX(LastReg(R_RB3D_BLENDCNTL), (R_BLEND_GL_SRC_ALPHA << 16) | (R_BLEND_GL_ONE_MINUS_SRC_ALPHA << 24));
    CHECK_HEX(LastReg(R_PP_MISC), 170UL | (5UL << 8));   /* round(0.666 * 255), GREATER */
    CHECK(LastReg(R_PP_CNTL) & R_ALPHA_TEST_ENABLE);
    v = LastReg(R_SE_CNTL);
    CHECK(!(v & R_FFACE_SOLID));
    CHECK(v & R_BFACE_SOLID);
    CHECK(v & R_DIFFUSE_SHADE_FLAT);
    CHECK(v & R_ZBIAS_ENABLE_TRI);
    CHECK(LastReg(R_SE_TCL_UCP_VERT_BLEND_CTL) & R_CULL_FRONT);
    /* GL box (10, 20) 100 x 50, lower-left origin -> top-left inclusive */
    CHECK_HEX(LastReg(R_RE_TOP_LEFT), 10UL | ((ULONG) (H_ - 70) << 16));
    CHECK_HEX(LastReg(R_RE_WIDTH_HEIGHT), 109UL | ((ULONG) (H_ - 21) << 16));
    CHECK_HEX(LastReg(R_SE_ZBIAS_CONSTANT), R2Cs_FloatBits(-0.0005f));
    /* a scissor box past the edge is clamped, not wrapped */
    Core_Scissor(&core, -50, -50, 2000, 2000);
    Draw1();
    CHECK_HEX(LastReg(R_RE_TOP_LEFT), 0);
    CHECK_HEX(LastReg(R_RE_WIDTH_HEIGHT), (W_ - 1) | ((ULONG) (H_ - 1) << 16));
    CHECK_INT(Checked(&st), R2CE_OK);
    /* nothing changed: nothing re-emitted */
    v = (ULONG) (cs.ptr - stream);
    Draw1();
    CHECK_INT((ULONG) (cs.ptr - stream) - v, 2 + 12);
    /* errors */
    Core_BlendFunc(&core, GL_SRC_COLOR, GL_ZERO);
    CHECK_INT(Core_GetError(&core), GL_INVALID_ENUM);
    Core_Begin(&core, GL_TRIANGLES);
    Core_Begin(&core, GL_TRIANGLES);
    CHECK_INT(Core_GetError(&core), GL_INVALID_OPERATION);
    Core_Enable(&core, GL_BLEND, 0);
    CHECK_INT(Core_GetError(&core), GL_INVALID_OPERATION);
    Core_End(&core);
    Core_End(&core);
    CHECK_INT(Core_GetError(&core), GL_INVALID_OPERATION);
    Core_MatrixMode(&core, GL_PROJECTION);
    Core_PopMatrix(&core);
    CHECK_INT(Core_GetError(&core), GL_STACK_UNDERFLOW);
    /* Regression (GLQuake in FS-UAE): valid caps without an R200 effect
     * are accepted and remembered, not refused */
    Core_Enable(&core, GL_STENCIL_TEST, 1);
    Core_Enable(&core, MGL_PERSPECTIVE_MAPPING, 0);
    Core_Enable(&core, GL_LIGHT0 + 3, 1);
    CHECK_INT(Core_GetError(&core), GL_NO_ERROR);
    CHECK_INT(Core_IsEnabled(&core, GL_STENCIL_TEST), 1);
    CHECK_INT(Core_IsEnabled(&core, MGL_PERSPECTIVE_MAPPING), 0);
    CHECK_INT(Core_IsEnabled(&core, GL_LIGHT0 + 3), 1);
    CHECK_INT(Core_IsEnabled(&core, GL_LIGHT0 + 2), 0);
    Core_Enable(&core, 0x1234, 1);
    CHECK_INT(Core_GetError(&core), GL_INVALID_ENUM);
}

/* The last value written to reg from stream index start on (0xdeadbeef if
 * none). */
static ULONG RegSince(ULONG start, ULONG reg)
{
    ULONG i;
    ULONG n;
    ULONG h;
    ULONG k;
    ULONG val;

    val = 0xdeadbeefUL;
    n = (ULONG) (cs.ptr - stream);
    for (i = start; i < n; i += (R2_PKT_TYPE(h) == 2) ? 1 : 2 + R2_PKT_COUNT(h))
    {
        h = V(i);
        if (R2_PKT_TYPE(h) != 0)
            continue;
        for (k = 0; k <= R2_PKT_COUNT(h); k++)
            if ((R2_PKT0_ONE_REG_WR(h) ? R2_PKT0_REG(h) : R2_PKT0_REG(h) + 4 * k) == reg)
                val = V(i + 1 + k);
    }
    return val;
}

/* The shadow registers: a bind sends what differs, the rest stays; a lost
 * GPU state (clear, present) and a texture's new storage send it again. */
static void test_bind_shadow(void)
{
    static UBYTE pix[32 * 16 * 4];
    struct R2ChkState st;
    ULONG at;
    ULONG off1;
    ULONG off2;
    ULONG fmt3;

    Fresh(1);
    Core_BindTexture(&core, GL_TEXTURE_2D, 1);
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, GL_RGB, 16, 16, 0, GL_RGB, GL_UNSIGNED_BYTE, pix);
    Core_BindTexture(&core, GL_TEXTURE_2D, 2);
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, GL_RGB, 16, 16, 0, GL_RGB, GL_UNSIGNED_BYTE, pix);
    Core_BindTexture(&core, GL_TEXTURE_2D, 3);
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, GL_RGBA, 32, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, pix);
    off1 = core.tex[1]->gpu;
    off2 = core.tex[2]->gpu;
    Core_Enable(&core, GL_TEXTURE_2D, 1);
    Core_BindTexture(&core, GL_TEXTURE_2D, 1);
    Draw1();
    CHECK_HEX(LastReg(R_PP_TXOFFSET_0), off1);

    /* the same format and size: TXOFFSET alone, no vertex format */
    at = (ULONG) (cs.ptr - stream);
    Core_BindTexture(&core, GL_TEXTURE_2D, 2);
    Draw1();
    CHECK_HEX(RegSince(at, R_PP_TXOFFSET_0), off2);
    CHECK_HEX(RegSince(at, R_PP_TXFILTER_0), 0xdeadbeefUL);
    CHECK_HEX(RegSince(at, R_PP_TXCBLEND_0), 0xdeadbeefUL);
    CHECK_HEX(RegSince(at, R_PP_CNTL), 0xdeadbeefUL);
    CHECK_HEX(RegSince(at, R_SE_VTX_FMT_0), 0xdeadbeefUL);
    CHECK_INT((ULONG) (cs.ptr - stream) - at, 2 + 2 + 3 * 6);

    /* another size and an alpha base: the format block and the combiner */
    at = (ULONG) (cs.ptr - stream);
    Core_BindTexture(&core, GL_TEXTURE_2D, 3);
    Draw1();
    fmt3 = RegSince(at, R_PP_TXFORMAT_0);
    CHECK(fmt3 != 0xdeadbeefUL);
    CHECK_HEX(RegSince(at, R_PP_TXOFFSET_0), core.tex[3]->gpu);
    CHECK(RegSince(at, R_PP_TXABLEND_0) != 0xdeadbeefUL);
    CHECK_HEX(RegSince(at, R_SE_VTX_FMT_0), 0xdeadbeefUL);

    /* back to 1: every texture register it differs in */
    at = (ULONG) (cs.ptr - stream);
    Core_BindTexture(&core, GL_TEXTURE_2D, 1);
    Draw1();
    CHECK_HEX(RegSince(at, R_PP_TXOFFSET_0), off1);
    CHECK(RegSince(at, R_PP_TXFORMAT_0) != 0xdeadbeefUL && RegSince(at, R_PP_TXFORMAT_0) != fmt3);

    /* new storage of the bound texture at the same address: TXOFFSET again */
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, GL_RGB, 16, 16, 0, GL_RGB, GL_UNSIGNED_BYTE, pix);
    CHECK_HEX(core.tex[1]->gpu, off1);
    at = (ULONG) (cs.ptr - stream);
    Draw1();
    CHECK_HEX(RegSince(at, R_PP_TXOFFSET_0), off1);
    /* a sub-image: nothing (as before the shadows) */
    Core_TexSubImage2D(&core, GL_TEXTURE_2D, 0, 0, 0, 4, 4, GL_RGB, GL_UNSIGNED_BYTE, pix);
    at = (ULONG) (cs.ptr - stream);
    Draw1();
    CHECK_INT((ULONG) (cs.ptr - stream) - at, 2 + 3 * 6);

    /* off and on: PP_CNTL and the vertex format both ways, the texture stays */
    at = (ULONG) (cs.ptr - stream);
    Core_Enable(&core, GL_TEXTURE_2D, 0);
    Draw1();
    CHECK(!(RegSince(at, R_PP_CNTL) & R_TEX_0_ENABLE));
    CHECK(RegSince(at, R_SE_VTX_FMT_1) == 0);
    at = (ULONG) (cs.ptr - stream);
    Core_Enable(&core, GL_TEXTURE_2D, 1);
    Draw1();
    CHECK(RegSince(at, R_PP_CNTL) & R_TEX_0_ENABLE);
    CHECK(RegSince(at, R_SE_VTX_FMT_1) != 0xdeadbeefUL);
    CHECK_HEX(RegSince(at, R_PP_TXOFFSET_0), 0xdeadbeefUL);

    /* a clear overwrites PP_CNTL and the combiner, not the texture */
    Core_Clear(&core, GL_COLOR_BUFFER_BIT);
    at = (ULONG) (cs.ptr - stream);
    Draw1();
    CHECK(RegSince(at, R_PP_CNTL) & R_TEX_0_ENABLE);
    CHECK(RegSince(at, R_PP_TXCBLEND_0) != 0xdeadbeefUL);
    CHECK_HEX(RegSince(at, R_SE_VAP_CNTL), R_VAP_TCL_ENABLE | (9UL << R_VAP_VF_MAX_VTX_NUM_SHIFT));
    CHECK(RegSince(at, R_SE_VTX_FMT_1) != 0xdeadbeefUL);
    CHECK_HEX(RegSince(at, R_PP_TXOFFSET_0), 0xdeadbeefUL);
    /* nor did it break the next bind */
    at = (ULONG) (cs.ptr - stream);
    Core_BindTexture(&core, GL_TEXTURE_2D, 2);
    Draw1();
    CHECK_HEX(RegSince(at, R_PP_TXOFFSET_0), off2);
    CHECK_INT(Checked(&st), R2CE_OK);
    CHECK_INT(Core_GetError(&core), GL_NO_ERROR);
}

/* The register state at every draw, as hashes: a register file (type-0
 * writes, data ports as their last value) hashed with the draw packet.
 * Returns the draws seen. */
static ULONG regfile[0x4000 / 4];

static ULONG DrawStates(ULONG *hash, ULONG max)
{
    ULONG i;
    ULONG n;
    ULONG h;
    ULONG k;
    ULONG reg;
    ULONG len;
    ULONG f;
    ULONG nd;

    memset(regfile, 0, sizeof(regfile));
    nd = 0;
    n = (ULONG) (cs.ptr - stream);
    for (i = 0; i < n; i += len)
    {
        h = V(i);
        len = (R2_PKT_TYPE(h) == 2) ? 1 : 2 + R2_PKT_COUNT(h);
        if (R2_PKT_TYPE(h) == 0)
        {
            for (k = 0; k <= R2_PKT_COUNT(h); k++)
            {
                reg = R2_PKT0_ONE_REG_WR(h) ? R2_PKT0_REG(h) : R2_PKT0_REG(h) + 4 * k;
                if (reg < sizeof(regfile) / sizeof(regfile[0]) * 4)
                    regfile[reg / 4] = V(i + 1 + k);
            }
        }
        else if (R2_PKT_TYPE(h) == 3 && R2_PKT3_OPCODE(h) != R2_OP_NOP && nd < max)
        {
            f = 2166136261UL;
            for (k = 0; k < sizeof(regfile) / sizeof(regfile[0]); k++)
                f = (f ^ regfile[k]) * 16777619UL;
            for (k = 0; k < len; k++)
                f = (f ^ V(i + k)) * 16777619UL;
            hash[nd++] = f;
        }
    }
    return nd;
}

static ULONG ShRand(ULONG *seed)
{
    *seed = *seed * 1103515245UL + 12345UL;
    return (*seed >> 16) & 0x7fff;
}

/* A random GL sequence of binds, texture units, enables, environments,
 * filters, uploads, clears and draws. */
static void ShadowRun(ULONG no_shadow, ULONG seed)
{
    static UBYTE pix[32 * 32 * 4];
    static const ULONG fmt[6] = { GL_RGB, GL_RGB, GL_RGBA, GL_LUMINANCE, GL_ALPHA,
                                  MGL_UNSIGNED_SHORT_4_4_4_4 };
    static const ULONG env[4] = { GL_MODULATE, GL_REPLACE, GL_DECAL, GL_ADD };
    ULONG step;
    ULONG r;
    ULONG name;
    ULONG sz;

    Fresh(1);
    core.no_shadow = no_shadow;
    for (name = 1; name <= 8; name++)
    {
        Core_BindTexture(&core, GL_TEXTURE_2D, name);
        sz = (name & 1) ? 16 : 32;
        if (fmt[name % 6] == MGL_UNSIGNED_SHORT_4_4_4_4)
            Core_TexImage2D(&core, GL_TEXTURE_2D, 0, GL_RGBA, (LONG) sz, 16, 0, GL_RGBA,
                            MGL_UNSIGNED_SHORT_4_4_4_4, pix);
        else
            Core_TexImage2D(&core, GL_TEXTURE_2D, 0, (LONG) fmt[name % 6], (LONG) sz, 16, 0,
                            fmt[name % 6], GL_UNSIGNED_BYTE, pix);
    }
    for (step = 0; step < 600; step++)
    {
        r = ShRand(&seed);
        switch (r % 13)
        {
            case 0: case 1: case 2:
                Core_BindTexture(&core, GL_TEXTURE_2D, 1 + ShRand(&seed) % 8);
                break;
            case 3:
                Core_ActiveTexture(&core, GL_TEXTURE0_ARB + ShRand(&seed) % 2);
                break;
            case 4:
                Core_Enable(&core, GL_TEXTURE_2D, ShRand(&seed) % 3 != 0);
                break;
            case 5:
                Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, (LONG) env[ShRand(&seed) % 4]);
                break;
            case 6:
                Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                   (ShRand(&seed) & 1) ? GL_LINEAR : GL_NEAREST);
                break;
            case 7:
                if (core.bound[core.active_unit] != 0 && core.tex[core.bound[core.active_unit]]->base == GL_RGBA)
                    Core_TexSubImage2D(&core, GL_TEXTURE_2D, 0, 0, 0, 4, 4, GL_RGBA,
                                       MGL_UNSIGNED_SHORT_4_4_4_4, pix);
                break;
            case 8:
                if (ShRand(&seed) % 4 == 0)
                    Core_Clear(&core, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                else
                    Core_Enable(&core, GL_BLEND, ShRand(&seed) & 1);
                break;
            case 9:
                if (ShRand(&seed) % 8 == 0)
                    Core_Rebase(&core, TEX_GPU, COLOR_GPU, DEPTH_GPU);
                else
                    Core_Enable(&core, GL_ALPHA_TEST, ShRand(&seed) & 1);
                break;
            default:
                Core_Begin(&core, GL_TRIANGLE_FAN);
                Core_MultiTexCoord2f(&core, GL_TEXTURE1_ARB, 0.5f, 0.5f);
                Core_TexCoord2f(&core, 0.0f, 0.0f);
                Core_Vertex3f(&core, 0, 0, 0);
                Core_TexCoord2f(&core, 1.0f, 0.0f);
                Core_Vertex3f(&core, 1, 0, 0);
                Core_TexCoord2f(&core, 1.0f, 1.0f);
                Core_Vertex3f(&core, 1, 1, 0);
                Core_End(&core);
                break;
        }
    }
}

static void test_shadow_equivalence(void)
{
    static ULONG with[4000];
    static ULONG without[4000];
    struct R2ChkState st;
    ULONG nw;
    ULONG nwo;
    ULONG dw_with;
    ULONG dw_without;
    ULONG seed;
    ULONG i;
    int same;
    int smaller;

    same = 1;
    smaller = 1;
    for (seed = 1; seed <= 12; seed++)
    {
        ShadowRun(1, seed);
        CHECK_INT(Checked(&st), R2CE_OK);
        nwo = DrawStates(without, 4000);
        dw_without = (ULONG) (cs.ptr - stream);
        ShadowRun(0, seed);
        CHECK_INT(Checked(&st), R2CE_OK);
        nw = DrawStates(with, 4000);
        dw_with = (ULONG) (cs.ptr - stream);
        if (nw != nwo || nw < 50)
            same = 0;
        for (i = 0; i < nw && i < nwo; i++)
            if (with[i] != without[i])
            {
                if (same)
                    printf("  seed %lu: draw %lu differs\n", (unsigned long) seed, (unsigned long) i);
                same = 0;
            }
        if (dw_with >= dw_without)
            smaller = 0;
    }
    CHECK(same);
    CHECK(smaller);
    core.no_shadow = 0;
    CHECK_INT(Core_GetError(&core), GL_NO_ERROR);
}

/* The bit-pattern vertex path (the hooks' hot path) writes the stream the
 * float path writes. */
static void BitsDraw(ULONG bits)
{
    static const float xyz[5][3] = { { -0.5f, -0.5f, 0.0f }, { 0.5f, -0.5f, 0.25f },
                                     { 0.6f, 0.4f, -0.75f }, { 0.0f, 0.6f, 1.0f },
                                     { -0.6f, 0.4f, 0.5f } };
    ULONG i;

    Core_Begin(&core, GL_TRIANGLE_FAN);
    for (i = 0; i < 5; i++)
    {
        if (bits)
        {
            CORE_TEXCOORD_BITS(&core, 0, R2_FBITS(xyz[i][1]), R2_FBITS(xyz[i][0]));
            CORE_TEXCOORD_BITS(&core, 1, R2_FBITS(xyz[i][2]), R2_FBITS(xyz[i][1]));
            Core_VertexBits(&core, R2_FBITS(xyz[i][0]), R2_FBITS(xyz[i][1]), R2_FBITS(xyz[i][2]));
        }
        else
        {
            Core_TexCoord2f(&core, xyz[i][1], xyz[i][0]);
            Core_MultiTexCoord2f(&core, GL_TEXTURE1_ARB, xyz[i][2], xyz[i][1]);
            Core_Vertex3f(&core, xyz[i][0], xyz[i][1], xyz[i][2]);
        }
    }
    Core_End(&core);
}

static void test_vertex_bits(void)
{
    static ULONG a[400];
    static UBYTE pix[16 * 16 * 3];
    ULONG na;
    ULONG nb;
    ULONG i;
    ULONG start;
    int same;

    Fresh(1);
    Core_ActiveTexture(&core, GL_TEXTURE1_ARB);
    Core_BindTexture(&core, GL_TEXTURE_2D, 2);
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, GL_RGB, 16, 16, 0, GL_RGB, GL_UNSIGNED_BYTE, pix);
    Core_Enable(&core, GL_TEXTURE_2D, 1);
    Core_ActiveTexture(&core, GL_TEXTURE0_ARB);
    Core_BindTexture(&core, GL_TEXTURE_2D, 1);
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, GL_RGB, 16, 16, 0, GL_RGB, GL_UNSIGNED_BYTE, pix);
    Core_Enable(&core, GL_TEXTURE_2D, 1);
    BitsDraw(0);                        /* the state goes out with the first */
    start = (ULONG) (cs.ptr - stream);
    BitsDraw(0);
    na = (ULONG) (cs.ptr - stream) - start;
    for (i = 0; i < na && i < 400; i++)
        a[i] = stream[start + i];
    start = (ULONG) (cs.ptr - stream);
    BitsDraw(1);
    nb = (ULONG) (cs.ptr - stream) - start;
    CHECK_INT(nb, na);
    CHECK_INT(na, 2 + 5 * 8);           /* xyz, colour, two coordinate sets */
    same = 1;
    for (i = 0; i < na && i < nb; i++)
        if (stream[start + i] != a[i])
            same = 0;
    CHECK(same);
    /* outside glBegin: ignored, as Core_Vertex3f */
    start = (ULONG) (cs.ptr - stream);
    Core_VertexBits(&core, 0, 0, 0);
    CHECK_INT((ULONG) (cs.ptr - stream), start);
    CHECK_INT(Core_GetError(&core), GL_NO_ERROR);
}

/* --- Matrices -------------------------------------------------------------------- */

static int Near(float a, float b)
{
    return a - b < 1e-5f && b - a < 1e-5f;
}

static void test_matrices(void)
{
    float m[16];
    float r[16];
    ULONG i;
    int ok;
    float d;

    Fresh(1);
    ok = 1;
    for (d = -720.0f; d <= 720.0f; d += 0.5f)
    {
        /* against the identities, no libm needed */
        if (!Near(Core_Sin(d) * Core_Sin(d) + Core_Cos(d) * Core_Cos(d), 1.0f))
            ok = 0;
    }
    CHECK(ok);
    CHECK(Near(Core_Sin(30.0f), 0.5f));
    CHECK(Near(Core_Sin(-210.0f), 0.5f));
    CHECK(Near(Core_Cos(60.0f), 0.5f));
    CHECK(Near(Core_Sin(90.0f), 1.0f));
    CHECK(Near(Core_Cos(180.0f), -1.0f));

    Core_MatrixMode(&core, GL_MODELVIEW);
    Core_LoadIdentity(&core);
    Core_Rotatef(&core, 90.0f, 0.0f, 0.0f, 1.0f);
    Core_GetFloatv(&core, GL_MODELVIEW_MATRIX, m);
    /* x -> y: column 0 = (0, 1, 0, 0) */
    CHECK(Near(m[0], 0.0f) && Near(m[1], 1.0f) && Near(m[4], -1.0f) && Near(m[5], 0.0f));
    Core_LoadIdentity(&core);
    Core_RotateAxisDeg(&core, 90.0f, (LONG) GLROT_001);
    Core_GetFloatv(&core, GL_MODELVIEW_MATRIX, r);
    ok = 1;
    for (i = 0; i < 16; i++)
        if (!Near(m[i], r[i]))
            ok = 0;
    CHECK(ok);
    Core_RotateAxisDeg(&core, 10.0f, (LONG) GLROT_011);
    CHECK_INT(Core_GetError(&core), GL_INVALID_ENUM);
    /* translate then scale, and push/pop */
    Core_LoadIdentity(&core);
    Core_PushMatrix(&core);
    Core_Translatef(&core, 1.0f, 2.0f, 3.0f);
    Core_Scalef(&core, 2.0f, 2.0f, 2.0f);
    Core_GetFloatv(&core, GL_MODELVIEW_MATRIX, m);
    CHECK(Near(m[0], 2.0f) && Near(m[12], 1.0f) && Near(m[13], 2.0f) && Near(m[14], 3.0f));
    Core_PopMatrix(&core);
    Core_GetFloatv(&core, GL_MODELVIEW_MATRIX, m);
    CHECK(Near(m[12], 0.0f) && Near(m[0], 1.0f));
    CHECK_INT(Core_GetError(&core), GL_NO_ERROR);
}

/* The fast matrix paths (a unit-axis rotation, a translation, a scale
 * touch only their columns) against the full multiply with the explicit
 * matrix: the same products in the same order, so equal. */
static void Explicit(float *t, ULONG op, float a, float b, float cc, float s, float co)
{
    ULONG i;

    for (i = 0; i < 16; i++)
        t[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    switch (op)
    {
        case 0:                         /* rotation about x */
            t[5] = co; t[6] = s; t[9] = -s; t[10] = co;
            break;
        case 1:                         /* about y */
            t[0] = co; t[2] = -s; t[8] = s; t[10] = co;
            break;
        case 2:                         /* about z */
            t[0] = co; t[1] = s; t[4] = -s; t[5] = co;
            break;
        case 3:                         /* translation */
            t[12] = a; t[13] = b; t[14] = cc;
            break;
        default:                        /* scale */
            t[0] = a; t[5] = b; t[10] = cc;
            break;
    }
}

static void test_matrix_fast(void)
{
    float start[16];
    float fast[16];
    float full[16];
    float t[16];
    ULONG op;
    ULONG i;
    ULONG round;
    int same;
    int near;
    float ang;

    Fresh(1);
    Core_MatrixMode(&core, GL_MODELVIEW);
    same = 1;
    for (round = 0; round < 40; round++)
    {
        for (i = 0; i < 16; i++)
            start[i] = (float) ((LONG) ((round * 37 + i * 11) % 23) - 11) * 0.37f;
        ang = (float) round * 23.5f - 400.0f;
        for (op = 0; op < 5; op++)
        {
            Core_LoadMatrixf(&core, start);
            if (op < 3)
                Core_Rotatef(&core, ang, op == 0 ? 1.0f : 0.0f, op == 1 ? 1.0f : 0.0f,
                             op == 2 ? 1.0f : 0.0f);
            else if (op == 3)
                Core_Translatef(&core, ang * 0.01f, -1.5f, 2.25f);
            else
                Core_Scalef(&core, ang * 0.01f, -1.5f, 2.25f);
            Core_GetFloatv(&core, GL_MODELVIEW_MATRIX, fast);
            Explicit(t, op, ang * 0.01f, -1.5f, 2.25f, Core_Sin(ang), Core_Cos(ang));
            Core_LoadMatrixf(&core, start);
            Core_MultMatrixf(&core, t);
            Core_GetFloatv(&core, GL_MODELVIEW_MATRIX, full);
            for (i = 0; i < 16; i++)
                if (fast[i] != full[i])
                    same = 0;
            if (op < 3)
            {
                /* RotateAxis takes the same path */
                Core_LoadMatrixf(&core, start);
                Core_RotateAxis(&core, Core_Sin(ang), Core_Cos(ang),
                                (LONG) (op == 0 ? GLROT_100 : op == 1 ? GLROT_010 : GLROT_001));
                Core_GetFloatv(&core, GL_MODELVIEW_MATRIX, fast);
                for (i = 0; i < 16; i++)
                    if (fast[i] != full[i])
                        same = 0;
            }
        }
    }
    CHECK(same);
    /* a non-unit axis is normalised, a negative unit axis takes the general
     * path: both still the rotation */
    Core_LoadIdentity(&core);
    Core_Rotatef(&core, 30.0f, 0.0f, 0.0f, 2.0f);
    Core_GetFloatv(&core, GL_MODELVIEW_MATRIX, fast);
    Core_LoadIdentity(&core);
    Core_Rotatef(&core, -30.0f, 0.0f, 0.0f, -1.0f);
    Core_GetFloatv(&core, GL_MODELVIEW_MATRIX, full);
    near = Near(fast[0], 0.8660254f) && Near(fast[1], 0.5f) && Near(fast[4], -0.5f);
    for (i = 0; i < 16; i++)
        if (!Near(fast[i], full[i]))
            near = 0;
    CHECK(near);
    /* inside glBegin: an error, the matrix untouched */
    Core_LoadIdentity(&core);
    Core_Begin(&core, GL_TRIANGLES);
    Core_Rotatef(&core, 30.0f, 0.0f, 0.0f, 1.0f);
    Core_Translatef(&core, 1.0f, 0.0f, 0.0f);
    Core_End(&core);
    CHECK_INT(Core_GetError(&core), GL_INVALID_OPERATION);
    Core_GetFloatv(&core, GL_MODELVIEW_MATRIX, fast);
    CHECK(fast[0] == 1.0f && fast[1] == 0.0f && fast[12] == 0.0f);
}

/* --- Clear, arrays, a GLQuake-shaped frame through the ring ---------------------- */

static void test_arrays(void)
{
    float pos[4 * 3];
    float st0[4 * 2];
    UBYTE col[4 * 4];
    UWORD idx[6] = { 0, 1, 2, 0, 2, 3 };
    struct R2ChkState st;
    ULONG i;

    Fresh(1);
    for (i = 0; i < 4; i++)
    {
        pos[i * 3 + 0] = (float) i;
        pos[i * 3 + 1] = (float) (i & 1);
        pos[i * 3 + 2] = 0.0f;
        st0[i * 2 + 0] = 0.0f;
        st0[i * 2 + 1] = 1.0f;
        col[i * 4 + 0] = col[i * 4 + 1] = col[i * 4 + 2] = col[i * 4 + 3] = 0x80;
    }
    Core_VertexPointer(&core, 3, GL_FLOAT, 0, pos);
    Core_ColorPointer(&core, 4, GL_UNSIGNED_BYTE, 0, col);
    Core_TexCoordPointer(&core, 2, GL_FLOAT, 0, st0);
    Core_EnableClientState(&core, GL_VERTEX_ARRAY, 1);
    Core_EnableClientState(&core, GL_COLOR_ARRAY, 1);
    Core_LockArrays(&core, 0, 4);
    Core_DrawElements(&core, GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
    Core_UnlockArrays(&core);
    Core_DrawArrays(&core, GL_TRIANGLE_FAN, 0, 4);
    CHECK_INT(Core_GetError(&core), GL_NO_ERROR);
    CHECK_INT(Checked(&st), R2CE_OK);
    CHECK_INT(st.draws, 2);
    CHECK_INT(st.vertices, 10);
    Core_UnlockArrays(&core);
    CHECK_INT(Core_GetError(&core), GL_INVALID_OPERATION);

    /* Regression (2026-11-02, GLQuake in FS-UAE): alias models declare their
     * colours as MiniGL's MGL_UBYTE_ARGB; the core refused the type, kept
     * the old pointer and drew every model black. */
    Fresh(1);
    for (i = 0; i < 4; i++)
    {
        col[i * 4 + 0] = 0x80;          /* A */
        col[i * 4 + 1] = 0x11;          /* R */
        col[i * 4 + 2] = 0x22;          /* G */
        col[i * 4 + 3] = 0x33;          /* B */
    }
    Core_VertexPointer(&core, 3, GL_FLOAT, 0, pos);
    Core_ColorPointer(&core, 4, MGL_UBYTE_ARGB, 0, col);
    Core_EnableClientState(&core, GL_VERTEX_ARRAY, 1);
    Core_EnableClientState(&core, GL_COLOR_ARRAY, 1);
    Core_DrawArrays(&core, GL_TRIANGLES, 0, 3);
    CHECK_INT(Core_GetError(&core), GL_NO_ERROR);
    CHECK_HEX(core.cur_color, 0x80332211UL);    /* a << 24 | b << 16 | g << 8 | r */
    Core_ColorPointer(&core, 4, MGL_UBYTE_BGRA, 0, col);
    Core_DrawArrays(&core, GL_TRIANGLES, 0, 3);
    CHECK_HEX(core.cur_color, 0x33801122UL);    /* B = 0x80, G = 0x11, R = 0x22, A = 0x33 */
    Core_ColorPointer(&core, 3, MGL_UBYTE_ARGB, 0, col);
    CHECK_INT(Core_GetError(&core), GL_INVALID_ENUM);
    CHECK_INT(Checked(&st), R2CE_OK);
}

/* --- Locked arrays from the stream (array.c DrawLocked) --------------------------- */

#define LK_MAX 12000
static float lk_pos[LK_MAX * 3];
static ULONG lk_rand;

static ULONG LkRand(void)
{
    lk_rand = lk_rand * 1103515245UL + 12345UL;
    return (lk_rand >> 16) & 0x7fff;
}

/* Arrays whose vertex k has x = k (the id Collect reads back). */
static void LkArrays(ULONG swap)
{
    ULONG k;

    Fresh(swap);
    for (k = 0; k < LK_MAX; k++)
    {
        lk_pos[k * 3 + 0] = (float) k;
        lk_pos[k * 3 + 1] = (float) (k & 7);
        lk_pos[k * 3 + 2] = 0.0f;
    }
    Core_VertexPointer(&core, 3, GL_FLOAT, 0, lk_pos);
    Core_EnableClientState(&core, GL_VERTEX_ARRAY, 1);
}

/* What was drawn equals what GL draws for mode over the indices, times
 * times. */
static int SameAsGlTimes(ULONG mode, const UWORD *idx, ULONG n, ULONG times)
{
    LONG ids[4096];
    ULONG i;
    ULONG k;
    struct R2ChkState st;

    if (Checked(&st) != R2CE_OK)
        return 0;
    if (st.index_unchecked != 0)
        return 0;
    Collect();
    nwant = 0;
    for (i = 0; i < n; i++)
        ids[i] = (LONG) idx[i];
    if (mode == GL_LINE_LOOP)
    {
        if (n >= 2)
        {
            Expand(want, &nwant, R_VF_PRIM_LINE_STRIP, ids, n);
            Add(want, &nwant, ids[n - 1], ids[0], 0, 1);
        }
    }
    else
    {
        k = Vtx_Whole(mode, n);
        for (i = 0; i < times; i++)
            Expand(want, &nwant, GlToHw(mode), ids, k);
    }
    if (ngot != nwant)
    {
        printf("  mode %lu n %lu: %d drawn, %d wanted\n", (unsigned long) mode,
               (unsigned long) n, ngot, nwant);
        return 0;
    }
    qsort(got, (size_t) ngot, sizeof(got[0]), Cmp);
    qsort(want, (size_t) nwant, sizeof(want[0]), Cmp);
    if (memcmp(got, want, (size_t) ngot * sizeof(got[0])) != 0)
    {
        printf("  mode %lu n %lu: different primitives\n", (unsigned long) mode, (unsigned long) n);
        return 0;
    }
    return 1;
}

static int SameAsGl(ULONG mode, const UWORD *idx, ULONG n)
{
    return SameAsGlTimes(mode, idx, n, 1);
}

static void test_locked(void)
{
    static const ULONG lens[] = { 1, 2, 3, 4, 5, 6, 7, 8, 12, 255, 256, 257, 600, 1001 };
    static UWORD idx[2048];
    UBYTE idx8[6];
    ULONG idx32[6];
    UBYTE col[4];
    UBYTE texel[2 * 2 * 4];
    ULONG swap;
    ULONG mode;
    ULONG i;
    ULONG l;
    ULONG bad;
    ULONG lock_draws;
    struct R2ChkState st;

    /* every mode but the line loop goes the locked way, in both byte
     * orders, and draws what GL draws over the same indices */
    bad = 0;
    lk_rand = 1;
    for (swap = 0; swap < 2; swap++)
        for (mode = GL_POINTS; mode <= GL_POLYGON; mode++)
            for (l = 0; l < sizeof(lens) / sizeof(lens[0]); l++)
            {
                LkArrays(swap);
                for (i = 0; i < lens[l]; i++)
                    idx[i] = (UWORD) (5 + LkRand() % 40);
                Core_LockArrays(&core, 5, 40);
                Core_DrawElements(&core, mode, (LONG) lens[l], GL_UNSIGNED_SHORT, idx);
                Core_UnlockArrays(&core);
                lock_draws = (mode != GL_LINE_LOOP && Vtx_Whole(mode, lens[l]) != 0) ? 1 : 0;
                if (!SameAsGl(mode, idx, lens[l]) || core.stat_lock_draws != lock_draws
                    || Core_GetError(&core) != GL_NO_ERROR)
                {
                    printf("  locked: swap %lu mode %lu n %lu (locked draws %lu)\n",
                           (unsigned long) swap, (unsigned long) mode, (unsigned long) lens[l],
                           (unsigned long) core.stat_lock_draws);
                    bad++;
                }
            }
    CHECK_INT(bad, 0);

    /* the range is written once per lock: two draws, one upload */
    LkArrays(1);
    for (i = 0; i < 6; i++)
        idx[i] = (UWORD) (i < 3 ? i : 5 - i + 3);
    Core_LockArrays(&core, 0, 8);
    Core_DrawElements(&core, GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
    Core_DrawElements(&core, GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
    CHECK_INT(core.stat_lock_uploads, 1);
    CHECK_INT(core.stat_lock_draws, 2);
    CHECK_INT(core.stat_stream_bytes, 8 * 4 * 4 + 2 * 3 * 4);  /* 8 vertices, 2 x 6 indices */
    /* a current colour without a colour array is in the packed vertices:
     * a new one writes them again, the same one does not */
    Core_Color4ub(&core, 1, 2, 3, 4);
    Core_DrawElements(&core, GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
    CHECK_INT(core.stat_lock_uploads, 2);
    Core_Color4ub(&core, 1, 2, 3, 4);
    Core_DrawElements(&core, GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
    CHECK_INT(core.stat_lock_uploads, 2);
    /* with a colour array the current colour does not matter */
    col[0] = col[1] = col[2] = col[3] = 0x40;
    Core_ColorPointer(&core, 4, GL_UNSIGNED_BYTE, 0, col);
    Core_EnableClientState(&core, GL_COLOR_ARRAY, 1);
    Core_UnlockArrays(&core);
    Core_LockArrays(&core, 0, 1);
    idx[0] = 0; idx[1] = 0; idx[2] = 0;
    Core_DrawElements(&core, GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, idx);
    CHECK_INT(core.stat_lock_uploads, 3);           /* a new lock */
    Core_Color4ub(&core, 9, 9, 9, 9);
    Core_DrawElements(&core, GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, idx);
    CHECK_INT(core.stat_lock_uploads, 3);
    /* a texture set more: a longer vertex, written again */
    memset(texel, 0xff, sizeof(texel));
    Core_BindTexture(&core, GL_TEXTURE_2D, 3);
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, 4, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, texel);
    Core_DrawElements(&core, GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, idx);
    CHECK_INT(core.stat_lock_uploads, 3);           /* (not enabled yet) */
    Core_Enable(&core, GL_TEXTURE_2D, 1);
    Core_DrawElements(&core, GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, idx);
    CHECK_INT(core.stat_lock_uploads, 4);
    Core_Enable(&core, GL_TEXTURE_2D, 0);
    /* an array pointer set while locked */
    Core_ColorPointer(&core, 4, GL_UNSIGNED_BYTE, 0, col);
    Core_DrawElements(&core, GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, idx);
    CHECK_INT(core.stat_lock_uploads, 5);
    Core_UnlockArrays(&core);
    CHECK_INT(Core_GetError(&core), GL_NO_ERROR);
    CHECK_INT(Checked(&st), R2CE_OK);

    /* the other index types go the locked way too */
    LkArrays(1);
    for (i = 0; i < 6; i++)
    {
        idx[i] = (UWORD) (10 + (i * 7) % 6);
        idx8[i] = (UBYTE) idx[i];
        idx32[i] = idx[i];
    }
    Core_LockArrays(&core, 10, 6);
    Core_DrawElements(&core, GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, idx8);
    Core_DrawElements(&core, GL_TRIANGLES, 6, GL_UNSIGNED_INT, idx32);
    Core_UnlockArrays(&core);
    CHECK_INT(core.stat_lock_draws, 2);
    CHECK_INT(core.stat_lock_uploads, 1);

    /* fallbacks to the immediate way, drawing the same: an index outside
     * the lock, no lock, no stream */
    LkArrays(1);
    for (i = 0; i < 6; i++)
        idx[i] = (UWORD) (i == 4 ? 30 : i);
    Core_LockArrays(&core, 0, 8);
    Core_DrawElements(&core, GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
    Core_UnlockArrays(&core);
    CHECK_INT(core.stat_lock_draws, 0);
    CHECK(SameAsGl(GL_TRIANGLES, idx, 6));
    LkArrays(1);
    Core_DrawElements(&core, GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
    CHECK_INT(core.stat_lock_draws, 0);
    CHECK(SameAsGl(GL_TRIANGLES, idx, 6));
    LkArrays(1);
    core.stream_size = 0;
    Core_LockArrays(&core, 0, 40);
    Core_DrawElements(&core, GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
    Core_UnlockArrays(&core);
    CHECK_INT(core.stat_lock_draws, 0);
    CHECK(SameAsGl(GL_TRIANGLES, idx, 6));
    /* a lock larger than the stream */
    LkArrays(1);
    Core_LockArrays(&core, 0, (LONG) (CORE_STREAM_BYTES / 16 + 1));
    Core_DrawElements(&core, GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
    Core_UnlockArrays(&core);
    CHECK_INT(core.stat_lock_draws, 0);
    CHECK_INT(core.stat_stream_wraps, 0);
    CHECK(SameAsGl(GL_TRIANGLES, idx, 6));

    /* the stream runs full: one wait for the GPU, from 0 again. Four locks
     * of 10000 vertices (160000 bytes each) need 640000 of 524288. */
    LkArrays(1);
    for (i = 0; i < 6; i++)
        idx[i] = (UWORD) (9990 + i);
    for (l = 0; l < 4; l++)
    {
        Core_LockArrays(&core, 0, 10000);
        Core_DrawElements(&core, GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
        Core_UnlockArrays(&core);
    }
    CHECK_INT(core.stat_lock_draws, 4);
    CHECK_INT(core.stat_lock_uploads, 4);
    CHECK_INT(core.stat_stream_wraps, 1);
    CHECK_INT(idle_waits, 1);
    CHECK_INT(Core_GetError(&core), GL_NO_ERROR);
    CHECK(SameAsGlTimes(GL_TRIANGLES, idx, 6, 4));  /* (the rewritten stream holds the same) */
    /* the wrap forgot the lock: the next draw of the same lock writes it */
    Core_LockArrays(&core, 0, 10000);
    Core_DrawElements(&core, GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
    Core_DrawElements(&core, GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
    Core_UnlockArrays(&core);
    CHECK_INT(core.stat_lock_uploads, 5);
    /* and so does the end of a frame (the GPU idle) */
    Core_LockArrays(&core, 0, 10000);
    Core_DrawElements(&core, GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
    CHECK_INT(core.stat_lock_uploads, 6);
    Core_GpuIdle(&core);
    Core_DrawElements(&core, GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
    CHECK_INT(core.stat_lock_uploads, 7);
    CHECK_INT(core.stream_pos, 160000 + 32);    /* from 0 again: one range, one index block */
    Core_UnlockArrays(&core);
    CHECK_INT(core.stat_lock_draws, 8);
    CHECK_INT(Core_GetError(&core), GL_NO_ERROR);
}

#define RING_DW 16384
static ULONG ring_mem[RING_DW];
static ULONG ring_shadow[RING_DW];

/* PackFast against the general way (Fetch + Pack): the same packed
 * vertices, dword for dword, for every array layout it takes. */
static ULONG pf_out[2][64 * 8];
static ULONG pf_n;

static void PfDraw(ULONG fast, ULONG which)
{
    static const UWORD idx[6] = { 0, 1, 2, 3, 4, 5 };
    ULONG vd;

    core.no_fast = fast ? 0 : 1;
    Core_LockArrays(&core, 0, 40);
    Core_DrawElements(&core, GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
    Core_UnlockArrays(&core);
    vd = 4 + ((Core_UnitsOn(&core) & 1) ? 2 : 0) + ((Core_UnitsOn(&core) & 2) ? 2 : 0);
    pf_n = 40 * vd;
    memcpy(pf_out[which], &vram[(core.lk_gpu - TEX_GPU) / 4], pf_n * 4);
}

static void test_packfast(void)
{
    static float pos3[40 * 4];
    static float st[40 * 3];
    static UBYTE col[40 * 5];
    static const ULONG ctypes[5] = { 0, MGL_UBYTE_ARGB, MGL_UBYTE_BGRA, GL_UNSIGNED_BYTE, GL_UNSIGNED_BYTE };
    static const ULONG csizes[5] = { 0, 4, 4, 4, 3 };
    UBYTE texel[2 * 2 * 4];
    ULONG swap;
    ULONG ci;
    ULONG psize;
    ULONG tmode;
    ULONG bad;
    ULONG cases;
    ULONG i;

    for (i = 0; i < 40 * 4; i++)
        pos3[i] = (float) i * 0.25f - 7.0f;
    for (i = 0; i < 40 * 3; i++)
        st[i] = (float) i / 64.0f;
    for (i = 0; i < 40 * 5; i++)
        col[i] = (UBYTE) (i * 37 + 11);
    for (i = 0; i < sizeof(texel); i++)
        texel[i] = (UBYTE) (i * 29);
    bad = 0;
    cases = 0;
    for (swap = 0; swap < 2; swap++)
        for (ci = 0; ci < 5; ci++)
            for (psize = 2; psize <= 3; psize++)
                for (tmode = 0; tmode < 4; tmode++)
                {
                    /* tmode: 0 no texture; 1 unit 0 from an array; 2 unit 0
                     * current st; 3 both units, unit 0 an array of 3 with a
                     * padded stride, unit 1 current */
                    Fresh(swap);
                    Core_VertexPointer(&core, (LONG) psize, GL_FLOAT,
                                       psize == 2 ? 16 : 0, pos3);
                    Core_EnableClientState(&core, GL_VERTEX_ARRAY, 1);
                    if (ctypes[ci] != 0)
                    {
                        Core_ColorPointer(&core, (LONG) csizes[ci], ctypes[ci],
                                          csizes[ci] == 3 ? 5 : 0, col);
                        Core_EnableClientState(&core, GL_COLOR_ARRAY, 1);
                    }
                    else
                        Core_Color4ub(&core, 10, 20, 30, 40);
                    if (tmode != 0)
                    {
                        Core_BindTexture(&core, GL_TEXTURE_2D, 1);
                        Core_TexImage2D(&core, GL_TEXTURE_2D, 0, 4, 2, 2, 0, GL_RGBA,
                                        GL_UNSIGNED_BYTE, texel);
                        Core_Enable(&core, GL_TEXTURE_2D, 1);
                        Core_TexCoord2f(&core, 0.375f, 0.625f);
                    }
                    if (tmode == 1)
                    {
                        Core_TexCoordPointer(&core, 2, GL_FLOAT, 0, st);
                        Core_EnableClientState(&core, GL_TEXTURE_COORD_ARRAY, 1);
                    }
                    if (tmode == 3)
                    {
                        Core_TexCoordPointer(&core, 3, GL_FLOAT, 12, st);
                        Core_EnableClientState(&core, GL_TEXTURE_COORD_ARRAY, 1);
                        Core_ActiveTexture(&core, GL_TEXTURE1_ARB);
                        Core_BindTexture(&core, GL_TEXTURE_2D, 1);
                        Core_Enable(&core, GL_TEXTURE_2D, 1);
                        Core_MultiTexCoord2f(&core, GL_TEXTURE1_ARB, 0.125f, 0.875f);
                        Core_ActiveTexture(&core, GL_TEXTURE0_ARB);
                    }
                    PfDraw(1, 0);
                    PfDraw(0, 1);
                    cases++;
                    if (memcmp(pf_out[0], pf_out[1], pf_n * 4) != 0
                        || Core_GetError(&core) != GL_NO_ERROR)
                    {
                        printf("  packfast: swap %lu colour %lu pos %lu tex %lu differs\n",
                               (unsigned long) swap, (unsigned long) ci, (unsigned long) psize,
                               (unsigned long) tmode);
                        bad++;
                    }
                }
    CHECK_INT(cases, 80);
    CHECK_INT(bad, 0);

    /* GLQuake's qglarray_t: x y z s t w floats and A R G B bytes in one
     * 28-byte record (the straight-through path's case, interleaved) */
    for (swap = 0; swap < 2; swap++)
    {
        static UBYTE il[40 * 28];
        float f;

        for (i = 0; i < 40 * 7; i++)
        {
            f = (float) i * 0.37f - 11.0f;
            if (i % 7 == 6)
            {
                il[i * 4] = (UBYTE) (i * 13 + 1);
                il[i * 4 + 1] = (UBYTE) (i * 7 + 2);
                il[i * 4 + 2] = (UBYTE) (i * 5 + 3);
                il[i * 4 + 3] = (UBYTE) (i * 3 + 4);
            }
            else
                memcpy(&il[i * 4], &f, 4);
        }
        Fresh(swap);
        Core_VertexPointer(&core, 3, GL_FLOAT, 28, il);
        Core_TexCoordPointer(&core, 3, GL_FLOAT, 28, il + 12);
        Core_ColorPointer(&core, 4, MGL_UBYTE_ARGB, 28, il + 24);
        Core_EnableClientState(&core, GL_VERTEX_ARRAY, 1);
        Core_EnableClientState(&core, GL_TEXTURE_COORD_ARRAY, 1);
        Core_EnableClientState(&core, GL_COLOR_ARRAY, 1);
        Core_BindTexture(&core, GL_TEXTURE_2D, 1);
        Core_TexImage2D(&core, GL_TEXTURE_2D, 0, 4, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, texel);
        Core_Enable(&core, GL_TEXTURE_2D, 1);
        PfDraw(1, 0);
        PfDraw(0, 1);
        CHECK_INT(pf_n, 40 * 6);
        CHECK(memcmp(pf_out[0], pf_out[1], pf_n * 4) == 0);
        CHECK_INT(Core_GetError(&core), GL_NO_ERROR);
    }
    core.no_fast = 0;
}

/* A back end whose video RAM moves (P96 bitmaps): texels only between the
 * vram hooks, at the address they return; the stream into staging; a
 * rebase moves textures and target. */
static ULONG vram2[TEX_SIZE / 4];
static ULONG staging[CORE_STREAM_BYTES / 4];
static ULONG *vram_now;
static ULONG vram_begins;
static ULONG vram_ends;

static ULONG *HostVramBegin(APTR env)
{
    (void) env;
    vram_begins++;
    vram_held = (vram_now != NULL);
    return vram_now;
}

static void HostVramEnd(APTR env)
{
    (void) env;
    vram_ends++;
    vram_held = 0;
}

/* The stream written into the heap itself (no staging): one lock a locked
 * draw, released before anything that may flush, the draw still what GL
 * draws; out of reach, the immediate way. */
static void test_stream_direct(void)
{
    static UWORD idx[3000];
    ULONG i;
    ULONG r;
    ULONG draws;

    LkArrays(1);
    vram_now = vram;                    /* the heap Core_Init was given */
    vram_begins = 0;
    vram_ends = 0;
    vram_held = 0;
    idle_in_lock = 0;
    Core_SetVram(&core, HostVramBegin, HostVramEnd, NULL);
    CHECK_INT(core.stream_direct, 1);
    for (i = 0; i < 3000; i++)
        idx[i] = (UWORD) ((i * 7) % 300);
    Core_LockArrays(&core, 0, 300);
    Core_DrawElements(&core, GL_TRIANGLES, 300, GL_UNSIGNED_SHORT, idx);
    CHECK(SameAsGl(GL_TRIANGLES, idx, 300));
    CHECK_INT(core.stat_lock_draws, 1);
    CHECK_INT(vram_begins, 1);
    CHECK_INT(vram_ends, 1);
    /* a second draw of the same lock: the indices only, one lock again */
    Core_DrawElements(&core, GL_TRIANGLES, 30, GL_UNSIGNED_SHORT, idx);
    CHECK_INT(core.stat_lock_uploads, 1);
    CHECK_INT(vram_begins, 2);
    Core_UnlockArrays(&core);
    /* enough to wrap the stream: the wraps flush outside every lock */
    for (r = 0; r < 60; r++)
    {
        Core_LockArrays(&core, 0, 3000);
        Core_DrawElements(&core, GL_TRIANGLES, 3000, GL_UNSIGNED_SHORT, idx);
        Core_UnlockArrays(&core);
    }
    CHECK(core.stat_stream_wraps > 0);
    CHECK_INT(idle_in_lock, 0);
    CHECK_INT(vram_begins, vram_ends);
    CHECK_INT(vram_begins, 62);
    /* the heap out of reach: counted, drawn the immediate way, no end */
    LkArrays(1);
    Core_SetVram(&core, HostVramBegin, HostVramEnd, NULL);
    vram_begins = 0;
    vram_ends = 0;
    draws = core.stat_lock_draws;
    vram_now = NULL;
    Core_LockArrays(&core, 0, 300);
    Core_DrawElements(&core, GL_TRIANGLES, 300, GL_UNSIGNED_SHORT, idx);
    Core_UnlockArrays(&core);
    CHECK_INT(core.stat_vram_lost, 1);
    CHECK_INT(core.stat_lock_draws, draws);
    CHECK_INT(vram_ends, vram_begins - 1);
    CHECK(SameAsGl(GL_TRIANGLES, idx, 300));
    CHECK_INT(Core_GetError(&core), GL_NO_ERROR);
    vram_now = NULL;
}

static void test_vram_hooks(void)
{
    UBYTE rgba[4 * 4 * 4];
    UWORD idx[3];
    struct CoreTex *t;
    struct R2ChkState st;
    ULONG off;
    ULONG gpu0;
    ULONG i;
    ULONG n;

    LkArrays(1);
    memset(vram, 0, sizeof(vram));
    memset(vram2, 0, sizeof(vram2));
    memset(staging, 0, sizeof(staging));
    vram_now = vram2;
    vram_begins = 0;
    vram_ends = 0;
    Core_SetVram(&core, HostVramBegin, HostVramEnd, staging);
    for (i = 0; i < sizeof(rgba); i++)
        rgba[i] = (UBYTE) (i * 7 + 1);
    Core_BindTexture(&core, GL_TEXTURE_2D, 3);
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, 4, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    t = core.tex[3];
    CHECK(t != NULL && t->gpu != 0);
    off = (t->gpu - TEX_GPU) / 4;
    CHECK_INT(t->pitch[0], 32);             /* 8 bytes of texels a row, padded */
    for (i = 0, n = 0; i < 8; i++)
        n += (vram2[off + (i / 2) * 8 + (i & 1)] != 0) ? 1 : 0;
    CHECK_INT(n, 8);                        /* written where vram_begin said */
    for (i = 0, n = 0; i < TEX_SIZE / 4; i++)
        n += (vram[i] != 0) ? 1 : 0;
    CHECK_INT(n, 0);                        /* never through the base Core_Init had */
    CHECK_INT(vram_begins, 1);
    CHECK_INT(vram_ends, 1);
    /* out of reach: the upload is lost and counted, no end without a begin */
    vram_now = NULL;
    rgba[0] ^= 0xff;
    memcpy(vram, vram2, sizeof(vram));
    Core_TexSubImage2D(&core, GL_TEXTURE_2D, 0, 0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    CHECK_INT(core.stat_vram_lost, 1);
    CHECK_INT(vram_begins, 2);
    CHECK_INT(vram_ends, 1);
    CHECK(memcmp(vram, vram2, sizeof(vram)) == 0);
    CHECK_INT(Core_GetError(&core), GL_NO_ERROR);

    /* the locked-array stream goes into staging, not the heap */
    vram_now = vram2;
    memcpy(vram, vram2, sizeof(vram));
    idx[0] = 0; idx[1] = 1; idx[2] = 2;
    Core_LockArrays(&core, 0, 3);
    Core_DrawElements(&core, GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, idx);
    Core_UnlockArrays(&core);
    CHECK_INT(core.stat_lock_draws, 1);
    for (i = 0, n = 0; i < 3 * 4; i++)
        n += (staging[i] != 0) ? 1 : 0;
    CHECK(n >= 3);                          /* the colours at least (vertex 0 is 0,0,0) */
    CHECK(memcmp(vram, vram2, sizeof(vram)) == 0);  /* the heap untouched */
    CHECK_INT(core.stat_stream_bytes, 3 * 4 * 4 + 2 * 4);  /* 3 vertices, 3 indices */

    /* a rebase: textures follow the heap, the target its new place, and
     * everything is emitted again from a restarted stream */
    gpu0 = t->gpu;
    n = core.stream_gen;
    Core_Rebase(&core, TEX_GPU + 0x00100000UL, COLOR_GPU + 0x1000, DEPTH_GPU + 0x1000);
    CHECK_HEX(core.tex[3]->gpu, gpu0 + 0x00100000UL);
    CHECK_INT(core.stream_gen, n + 1);
    CHECK_INT(core.stream_pos, 0);
    cs.ptr = stream;
    Core_Enable(&core, GL_TEXTURE_2D, 1);
    Core_Begin(&core, GL_TRIANGLES);
    Core_TexCoord2f(&core, 0.0f, 0.0f);
    Core_Vertex3f(&core, 0.0f, 0.0f, 0.0f);
    Core_Vertex3f(&core, 1.0f, 0.0f, 0.0f);
    Core_Vertex3f(&core, 0.0f, 1.0f, 0.0f);
    Core_End(&core);
    CHECK_HEX(LastReg(R_RB3D_COLOROFFSET), COLOR_GPU + 0x1000);
    CHECK_HEX(LastReg(R_RB3D_DEPTHOFFSET), DEPTH_GPU + 0x1000);
    CHECK_HEX(LastReg(R_PP_TXOFFSET_0) & ~0x1fUL, gpu0 + 0x00100000UL);
    CHECK_INT(Core_GetError(&core), GL_NO_ERROR);
    (void) st;
}

static void test_frames_through_ring(void)
{
    struct SimCp sim;
    struct R2ChkCfg cfg;
    struct R2Ring ring;
    struct CoreTarget t;
    struct CoreEnv e;
    UBYTE tex[64 * 64 * 4];
    UBYTE lm[128 * 128 * 2];
    ULONG frame;
    ULONG p;
    ULONG i;
    ULONG seq;

    Core_Free(&core);
    Cfg(&cfg);
    SimCp_Init(&sim, ring_mem, ring_shadow, RING_DW, 1, FENCE_REG, 0, 300, &cfg);
    CHECK_INT(R2Ring_Init(&ring, ring_mem, 0x01000000UL, RING_DW, &sim.hw, 1, FENCE_REG, 100000),
              R2_OK);
    Target(&t);
    Env(&e);
    CHECK_INT(Core_Init(&core, &ring.cs, &e, &t, TEX_GPU, vram, TEX_SIZE), GL_NO_ERROR);
    for (i = 0; i < sizeof(tex); i++)
        tex[i] = (UBYTE) (i * 13);
    for (i = 0; i < sizeof(lm); i++)
        lm[i] = (UBYTE) (i * 7);
    /* a "level load": 20 textures with mips, 8 lightmaps */
    for (i = 1; i <= 20; i++)
    {
        Core_BindTexture(&core, GL_TEXTURE_2D, i);
        Core_TexImage2D(&core, GL_TEXTURE_2D, 0, 3, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex);
        Core_TexImage2D(&core, GL_TEXTURE_2D, 1, 3, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex);
    }
    for (i = 100; i < 108; i++)
    {
        Core_BindTexture(&core, GL_TEXTURE_2D, i);
        Core_TexImage2D(&core, GL_TEXTURE_2D, 0, MGL_UNSIGNED_SHORT_4_4_4_4, 128, 128, 0,
                        MGL_UNSIGNED_SHORT_4_4_4_4, GL_UNSIGNED_BYTE, lm);
    }
    CHECK_INT(Core_GetError(&core), GL_NO_ERROR);
    for (frame = 0; frame < 20; frame++)
    {
        Core_Clear(&core, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        Core_MatrixMode(&core, GL_PROJECTION);
        Core_LoadIdentity(&core);
        Core_Frustum(&core, -1, 1, -0.75, 0.75, 4, 4096);
        Core_MatrixMode(&core, GL_MODELVIEW);
        Core_LoadIdentity(&core);
        Core_RotateAxisDeg(&core, (float) frame * 3.0f, (LONG) GLROT_001);
        Core_Enable(&core, GL_DEPTH_TEST, 1);
        /* the world: multitextured polygons, lightmap updates between */
        Core_ActiveTexture(&core, GL_TEXTURE0_ARB);
        Core_Enable(&core, GL_TEXTURE_2D, 1);
        Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        Core_ActiveTexture(&core, GL_TEXTURE1_ARB);
        Core_Enable(&core, GL_TEXTURE_2D, 1);
        Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        for (p = 0; p < 150; p++)
        {
            Core_ActiveTexture(&core, GL_TEXTURE0_ARB);
            Core_BindTexture(&core, GL_TEXTURE_2D, 1 + p % 20);
            Core_ActiveTexture(&core, GL_TEXTURE1_ARB);
            Core_BindTexture(&core, GL_TEXTURE_2D, 100 + p % 8);
            if (p % 40 == 0)
                Core_TexSubImage2D(&core, GL_TEXTURE_2D, 0, 0, 16, 128, 8,
                                   MGL_UNSIGNED_SHORT_4_4_4_4, GL_UNSIGNED_BYTE, lm);
            Core_Begin(&core, GL_POLYGON);
            for (i = 0; i < 4 + p % 4; i++)
            {
                Core_MultiTexCoord2f(&core, GL_TEXTURE0_ARB, (float) i, 0.0f);
                Core_MultiTexCoord2f(&core, GL_TEXTURE1_ARB, 0.0f, (float) i);
                Core_Vertex3f(&core, (float) i, (float) p, -10.0f);
            }
            Core_End(&core);
        }
        Core_Enable(&core, GL_TEXTURE_2D, 0);
        Core_ActiveTexture(&core, GL_TEXTURE0_ARB);
        /* models: blended, alpha tested, through arrays would be here;
         * particles: one long point list */
        Core_Enable(&core, GL_BLEND, 1);
        Core_BlendFunc(&core, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        Core_PointSize(&core, 2.5f);
        Core_Begin(&core, GL_POINTS);
        for (i = 0; i < 700; i++)
        {
            Core_Color4ub(&core, 255, (ULONG) i & 255, 0, 128);
            Core_Vertex3f(&core, (float) i, 0.0f, -20.0f);
        }
        Core_End(&core);
        Core_Enable(&core, GL_BLEND, 0);
        R2Ring_Fence(&ring, &seq);
        R2Ring_Commit(&ring);
    }
    CHECK_INT(Core_GetError(&core), GL_NO_ERROR);
    CHECK_INT(R2Ring_WaitFence(&ring, seq), R2_OK);
    SimCp_Drain(&sim);
    CHECK(SimCp_Clean(&sim));
    CHECK_INT(sim.st.draws, 20 * (1 + 150 + 3));     /* 700 points: 256 + 256 + 188 */
    CHECK(ring.stats.wraps > 0);
    printf("  20 frames: %lu dwords, %lu state blocks, %lu packets split, %lu idle waits\n",
           (unsigned long) sim.consumed, (unsigned long) core.stat_state_emits,
           (unsigned long) core.stat_splits, (unsigned long) idle_waits);
    Core_Free(&core);
}

static void test_present(void)
{
    struct CoreScreen scr;
    struct R2ChkState st;
    ULONG before;

    Fresh(1);
    Core_Clear(&core, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    Core_Enable(&core, GL_DEPTH_TEST, 1);
    Draw1();
    scr.gpu = SCREEN_GPU;
    scr.pitch = 1024;
    scr.format = R_COLOR_FORMAT_ARGB8888;
    scr.width = 1024;
    scr.height = 768;
    Core_Present(&core, &scr, 192, 144);        /* centred */
    CHECK_INT(Checked(&st), R2CE_OK);
    CHECK_INT(st.draws, 3);                     /* clear, triangle, the copy */
    CHECK_HEX(LastReg(R_RB3D_COLOROFFSET), SCREEN_GPU);
    CHECK_HEX(LastReg(R_PP_TXOFFSET_0), COLOR_GPU);
    CHECK_HEX(LastReg(R_PP_TXPITCH_0), W_ * 2 - 32);
    CHECK_HEX(LastReg(R_RE_TOP_LEFT), 192UL | (144UL << 16));
    CHECK_HEX(LastReg(R_RE_WIDTH_HEIGHT), (192UL + W_ - 1) | ((144UL + H_ - 1) << 16));
    CHECK_HEX(LastReg(R_RB3D_DSTCACHE_CTLSTAT), R_RB3D_DC_FLUSH_ALL);
    /* the next draw is back on the back buffer, TCL on */
    before = (ULONG) (cs.ptr - stream);
    Draw1();
    CHECK(LastReg(R_SE_VAP_CNTL) & R_VAP_TCL_ENABLE);
    CHECK_HEX(LastReg(R_RB3D_COLOROFFSET), COLOR_GPU);
    CHECK_INT(Checked(&st), R2CE_OK);
    CHECK((ULONG) (cs.ptr - stream) - before < 400);   /* one full state re-emit, no more */
    /* a screen smaller than the frame: only the visible part is copied */
    scr.width = 320;
    scr.height = 200;
    Core_Present(&core, &scr, 0, 0);
    CHECK_HEX(LastReg(R_RE_WIDTH_HEIGHT), 319UL | (199UL << 16));
    CHECK_INT(Checked(&st), R2CE_OK);
}

int main(void)
{
    struct CoreTarget t;
    struct CoreEnv e;

    memset(&core, 0, sizeof(core));
    Target(&t);
    Env(&e);
    test_splits();
    test_triangle();
    test_textures();
    test_state();
    test_bind_shadow();
    test_shadow_equivalence();
    test_vertex_bits();
    test_matrices();
    test_matrix_fast();
    test_arrays();
    test_locked();
    test_present();
    test_vram_hooks();
    test_stream_direct();
    test_packfast();
    test_frames_through_ring();
    (void) t;
    (void) e;
    return tk_done("test_core");
}
