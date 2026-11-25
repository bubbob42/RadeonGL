/*
 * test_tex13.c -- GL 1.3 phase 1 (src/core/tex13.c, 2026-11-12): texture
 * units 2..5, cube maps, the combiner, the GL 1.2 formats and types,
 * sampling, separate specular. Two kinds of check: the packets (registers,
 * vertex layout, the packet checker, texels in video RAM) and pictures
 * through the reference rasteriser (r200sim), build/host/t13-*.ppm.
 *
 * And the other way round: a GLQuake-like two-unit frame never wakes
 * tex13.c (no routing field set, the same registers as before).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "testkit.h"
#include "core.h"
#include "coreint.h"
#include "r200reg.h"
#include "r2pm4.h"
#include "r2check.h"
#include "r200sim.h"

#define W_          320
#define H_          240
#define COLOR_GPU   0x00100000UL
#define DEPTH_GPU   0x00200000UL
#define TEX_GPU     0x00400000UL
#define TEX_SIZE    0x00200000UL
#define VRAM_SIZE   0x01000000UL
#define FENCE_REG   R_SCRATCH_REG2

static ULONG stream[200000];
static ULONG *vram;
static struct Core core;
static struct R2Cs cs;
static struct Sim sim;

static APTR HostAlloc(APTR env, ULONG size) { (void) env; return calloc(1, size); }
static void HostFree(APTR env, APTR mem, ULONG size) { (void) env; (void) size; free(mem); }
static LONG HostIdle(APTR env) { (void) env; return 0; }

static void Fresh(void)
{
    struct CoreTarget t;
    struct CoreEnv e;

    Core_Free(&core);
    memset(&core, 0, sizeof(core));     /* Core_Init's promise (core.h) */
    memset(&e, 0, sizeof(e));           /* alloc_raw and the rest: none */
    memset(vram, 0, VRAM_SIZE);
    R2Cs_InitBuffer(&cs, stream, sizeof(stream) / 4, 1);
    t.width = W_;
    t.height = H_;
    t.color_gpu = COLOR_GPU;
    t.color_pitch = W_;
    t.color_format = R_COLOR_FORMAT_RGB565;
    t.depth_gpu = DEPTH_GPU;
    t.depth_pitch = W_;
    e.alloc = HostAlloc;
    e.free = HostFree;
    e.wait_idle = HostIdle;
    e.env = NULL;
    CHECK_INT(Core_Init(&core, &cs, &e, &t, TEX_GPU, vram + TEX_GPU / 4, TEX_SIZE), GL_NO_ERROR);
    Core_Viewport(&core, 0, 0, W_, H_);
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

/* The last immediate draw: its first vertex dword and vertex count */
static ULONG LastImmd(ULONG *nverts)
{
    ULONG n;
    ULONG i;
    ULONG h;
    ULONG len;
    ULONG at;

    n = (ULONG) (cs.ptr - stream);
    at = 0;
    *nverts = 0;
    for (i = 0; i < n; i += len)
    {
        h = V(i);
        len = (R2_PKT_TYPE(h) == 2) ? 1 : (R2_PKT_TYPE(h) == 1) ? 3 : R2_PKT_COUNT(h) + 2;
        if (R2_PKT_TYPE(h) == 3 && R2_PKT3_OPCODE(h) == R2_OP_3D_DRAW_IMMD_2)
        {
            at = i + 2;
            *nverts = V(i + 1) >> 16;
        }
    }
    return at;
}

static LONG Peek(APTR ctx, ULONG gpu, ULONG *val)
{
    (void) ctx;
    if (gpu + 4 > VRAM_SIZE)
        return -1;
    *val = vram[gpu / 4];
    return 0;
}

static LONG Checked(void)
{
    struct R2ChkCfg cfg;
    struct R2ChkState st;
    struct R2ChkError err;
    LONG e;

    memset(&cfg, 0, sizeof(cfg));
    cfg.range[0].gpu = COLOR_GPU; cfg.range[0].size = W_ * H_ * 2; cfg.range[0].kinds = R2K_COLOR;
    cfg.range[1].gpu = DEPTH_GPU; cfg.range[1].size = W_ * H_ * 2; cfg.range[1].kinds = R2K_DEPTH;
    cfg.range[2].gpu = TEX_GPU; cfg.range[2].size = TEX_SIZE;
    cfg.range[2].kinds = R2K_TEXTURE | R2K_VERTEX | R2K_INDEX;
    cfg.nranges = 3;
    cfg.fence_reg = FENCE_REG;
    cfg.peek = Peek;
    R2Chk_Reset(&st);
    e = R2Chk_Run(&cfg, &st, stream, (ULONG) (cs.ptr - stream), cs.swap, 0, &err);
    if (e != R2CE_OK)
        printf("  checker: %s at %lu what 0x%lx value 0x%lx\n", R2Chk_ErrorName(e),
               (unsigned long) err.at, (unsigned long) err.what, (unsigned long) err.value);
    return e;
}

/* A texel of level l, face f in video RAM, as the GPU reads it */
static ULONG Texel(const struct CoreTex *t, ULONG f, ULONG l, ULONG x, ULONG y)
{
    ULONG byte;
    ULONG g;

    byte = t->gpu + f * t->face_bytes + t->lofs[l] + y * t->pitch[l] + x * t->cpp;
    g = R2Cs_Swap32(vram[byte / 4]);        /* tex_swap: stored reversed */
    g >>= (byte & 3) * 8;
    return t->cpp == 1 ? (g & 0xff) : t->cpp == 4 ? g : (g & 0xffff);
}

static void Ortho(void)
{
    Core_MatrixMode(&core, GL_PROJECTION);
    Core_LoadIdentity(&core);
    Core_Ortho(&core, 0, W_, 0, H_, -10, 10);
    Core_MatrixMode(&core, GL_MODELVIEW);
    Core_LoadIdentity(&core);
}

static void Render(const char *name)
{
    char path[128];

    Sim_Init(&sim, vram, 0, VRAM_SIZE, cs.swap, 1);
    Sim_Run(&sim, stream, (ULONG) (cs.ptr - stream));
    snprintf(path, sizeof(path), "build/host/t13-%s.ppm", name);
    Sim_WritePPM(&sim, path, COLOR_GPU, W_, R_COLOR_FORMAT_RGB565, W_, H_);
    CHECK_INT(sim.unsupported, 0);
}

static ULONG Px(LONG x, LONG y)
{
    return Sim_Pixel(&sim, COLOR_GPU, W_, R_COLOR_FORMAT_RGB565, (ULONG) x, (ULONG) (H_ - 1 - y));
}

/* each channel within tol of r, g, b */
static int Near3(ULONG px, LONG r, LONG g, LONG b, LONG tol)
{
    LONG pr = (LONG) ((px >> 16) & 255), pg = (LONG) ((px >> 8) & 255), pb = (LONG) (px & 255);

    if (pr - r > tol || r - pr > tol || pg - g > tol || g - pg > tol || pb - b > tol || b - pb > tol)
    {
        printf("  pixel 0x%06lx, want about %ld %ld %ld\n", (unsigned long) px, (long) r, (long) g, (long) b);
        return 0;
    }
    return 1;
}
#define CHECK_PX(x, y, r, g, b) CHECK(Near3(Px(x, y), r, g, b, 10))

/* A 1 x 1 texture of one colour (RGBA8) bound and enabled on unit u */
static void Solid(ULONG u, ULONG name, UBYTE r, UBYTE g, UBYTE b, UBYTE a)
{
    UBYTE px[4];

    px[0] = r; px[1] = g; px[2] = b; px[3] = a;
    Core_ActiveTexture(&core, GL_TEXTURE0_ARB + u);
    Core_BindTexture(&core, GL_TEXTURE_2D, name);
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    Core_Enable(&core, GL_TEXTURE_2D, 1);
}

/* An axis-aligned quad, every unit's coordinates (s, t) at the corners */
static void Quad(float x, float y, float d, ULONG units)
{
    static const float cs_[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    ULONG k;
    ULONG u;

    Core_Begin(&core, GL_QUADS);
    for (k = 0; k < 4; k++)
    {
        for (u = 0; u < CORE_UNITS; u++)
            if (units & (1UL << u))
                Core_MultiTexCoord2f(&core, GL_TEXTURE0_ARB + u, cs_[k][0], cs_[k][1]);
        Core_Vertex3f(&core, x + d * cs_[k][0], y + d * cs_[k][1], 0.0f);
    }
    Core_End(&core);
}

/* --- Units 2..5 ------------------------------------------------------------------- */

static void test_units6(void)
{
    ULONG at;
    ULONG nv;
    ULONG u;
    ULONG fmt1;
    LONG n;

    Fresh();
    Ortho();
    for (u = 0; u < 6; u++)
        Solid(u, 10 + u, 255, 255, 255, 255);
    CHECK_INT(core.wide_on, 1);
    CHECK_INT(core.x_route, 1);         /* the active unit is 5 */
    Core_Begin(&core, GL_TRIANGLES);
    for (u = 0; u < 6; u++)
        Core_MultiTexCoord2f(&core, GL_TEXTURE0_ARB + u, 0.125f * (float) u, 1.0f + (float) u);
    Core_Color4ub(&core, 1, 2, 3, 4);
    Core_Vertex3f(&core, 1, 2, 3);
    Core_Vertex3f(&core, 4, 5, 6);
    Core_Vertex3f(&core, 7, 8, 9);
    Core_End(&core);
    CHECK_INT(core.vtx_dw, 4 + 12);
    fmt1 = 0;
    for (u = 0; u < 6; u++)
        fmt1 |= 2UL << (3 * u);
    CHECK_HEX(LastReg(R_SE_VTX_FMT_1), fmt1);
    CHECK_HEX(LastReg(R_SE_TCL_OUTPUT_VTX_FMT_1), fmt1);
    CHECK_HEX(LastReg(R_PP_CNTL) & (0x3fUL * R_TEX_0_ENABLE), 0x3fUL * R_TEX_0_ENABLE);
    CHECK_HEX(LastReg(R_PP_CNTL) & (0x7fUL * R_TEX_BLEND_0_ENABLE), 0x3fUL * R_TEX_BLEND_0_ENABLE);
    at = LastImmd(&nv);
    CHECK_INT(nv, 3);
    if (at != 0)
    {
        CHECK(F(V(at + 1 * 16 + 1)) == 5.0f);         /* the second vertex's y */
        CHECK_HEX(V(at + 3), 0x04030201UL);           /* the colour */
        for (u = 0; u < 6; u++)
        {
            CHECK(F(V(at + 4 + 2 * u)) == 0.125f * (float) u);
            CHECK(F(V(at + 5 + 2 * u)) == 1.0f + (float) u);
        }
    }
    /* every unit's texture registers, stage 5's blend */
    for (u = 0; u < 6; u++)
        CHECK(LastReg(R_PP_TXOFFSET_0 + u * R_PP_TXOFFSET_STRIDE) == core.tex[10 + u]->gpu);
    CHECK(LastReg(R_PP_TXCBLEND_0 + 5 * R_PP_TXBLEND_STRIDE) != 0xdeadbeefUL);
    CHECK_INT(Checked(), R2CE_OK);
    CHECK_INT(Core_GetError(&core), GL_NO_ERROR);
    Core_GetIntegerv(&core, GL_MAX_TEXTURE_UNITS_ARB, &n);
    CHECK_INT(n, 6);
    Core_ActiveTexture(&core, GL_TEXTURE0_ARB + 6);
    CHECK_INT(Core_GetError(&core), GL_INVALID_ENUM);
    /* units 2..5 off again, unit 0 active: the narrow way once more */
    for (u = 2; u < 6; u++)
    {
        Core_ActiveTexture(&core, GL_TEXTURE0_ARB + u);
        Core_Enable(&core, GL_TEXTURE_2D, 0);
    }
    Core_ActiveTexture(&core, GL_TEXTURE0_ARB);
    CHECK_INT(core.wide_on, 0);
    CHECK_INT(core.x_route, 0);
    CHECK_INT(core.vslow, 0);
    /* the 2D bindings tex13.c kept are texture.c's again */
    CHECK_INT(core.bound[0], 10);
    CHECK_INT(core.bound[1], 11);
}

/* Units 2..5 from client arrays (glDrawArrays, glDrawElements) */
static void test_units6_arrays(void)
{
    static float pos[3 * 3] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    static float tc[6][3 * 2];
    static const UWORD idx[3] = { 2, 1, 0 };
    ULONG at;
    ULONG nv;
    ULONG u;
    ULONG i;

    Fresh();
    for (u = 0; u < 6; u++)
    {
        Solid(u, 20 + u, 255, 255, 255, 255);
        for (i = 0; i < 6; i++)
            tc[u][i] = (float) (10 * u + i);
        Core_ClientActiveTexture(&core, GL_TEXTURE0_ARB + u);
        Core_TexCoordPointer(&core, 2, GL_FLOAT, 0, tc[u]);
        Core_EnableClientState(&core, GL_TEXTURE_COORD_ARRAY, 1);
    }
    Core_VertexPointer(&core, 3, GL_FLOAT, 0, pos);
    Core_EnableClientState(&core, GL_VERTEX_ARRAY, 1);
    Core_DrawArrays(&core, GL_TRIANGLES, 0, 3);
    at = LastImmd(&nv);
    CHECK_INT(nv, 3);
    if (at != 0)
    {
        CHECK(F(V(at + 16 * 2 + 0)) == 7.0f);
        CHECK(F(V(at + 16 * 2 + 4 + 2 * 4)) == 44.0f);     /* vertex 2, unit 4's s */
        CHECK(F(V(at + 16 * 1 + 4 + 2 * 5 + 1)) == 53.0f); /* vertex 1, unit 5's t */
    }
    Core_DrawElements(&core, GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, idx);
    at = LastImmd(&nv);
    CHECK_INT(nv, 3);
    if (at != 0)
        CHECK(F(V(at + 4 + 2 * 3)) == 34.0f);              /* vertex 0 is element 2: unit 3's s */
    CHECK_INT(Checked(), R2CE_OK);
    CHECK_INT(Core_GetError(&core), GL_NO_ERROR);
}

/* GLQuake's frame: two units, the classic modes -- tex13.c stays asleep */
static void test_narrow_untouched(void)
{
    ULONG pp;

    Fresh();
    Ortho();
    Solid(0, 1, 255, 255, 255, 255);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    Solid(1, 2, 128, 128, 128, 255);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    Core_ActiveTexture(&core, GL_TEXTURE0_ARB);
    Quad(0, 0, 10, 3);
    CHECK_INT(core.wide_on, 0);
    CHECK_INT(core.x_route, 0);
    CHECK_INT(core.comb_x, 0);
    CHECK_INT(core.tex_x, 0);
    CHECK_INT(core.vslow, 0);
    CHECK_INT(core.vtx_dw, 8);
    pp = LastReg(R_PP_CNTL);
    CHECK_HEX(pp & (0x7fUL * R_TEX_BLEND_0_ENABLE), 3UL * R_TEX_BLEND_0_ENABLE);
    /* 0.11's registers for REPLACE + MODULATE (hwstate.c EmitCombiner) */
    CHECK_HEX(LastReg(R_PP_TXCBLEND_0), (R_TXC_ARG_R0_COLOR << R_TXC_ARG_C_SHIFT) | R_TXC_OP_MADD);
    CHECK_HEX(LastReg(R_PP_TXCBLEND_0 + R_PP_TXBLEND_STRIDE),
              (R_TXC_ARG_R1_COLOR << R_TXC_ARG_A_SHIFT) | (R_TXC_ARG_R0_COLOR << R_TXC_ARG_B_SHIFT)
              | R_TXC_OP_MADD);
    CHECK_HEX(LastReg(R_PP_TXCBLEND2_0 + R_PP_TXBLEND_STRIDE),
              R_TXC_CLAMP_0_1 | R_TXC_OUTPUT_REG_R0 | (1UL << R_TXC_TFACTOR_SEL_SHIFT));
    CHECK_INT(Checked(), R2CE_OK);
}

/* --- The combiner ---------------------------------------------------------------- */

static void test_combine_regs(void)
{
    ULONG cb;
    ULONG cb2;
    ULONG ab2;

    Fresh();
    Solid(0, 1, 200, 100, 50, 255);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_INTERPOLATE);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_ALPHA);
    CHECK_INT(core.comb_x, 1);
    Quad(0, 0, 10, 1);
    /* LERP: A = arg1 (previous: the diffuse colour in stage 0), B = arg0
     * (the texture, R0), C = arg2 (the constant's alpha) */
    cb = LastReg(R_PP_TXCBLEND_0);
    CHECK_HEX(cb, (R_TXC_ARG_DIFFUSE_COLOR << R_TXC_ARG_A_SHIFT) | (R_TXC_ARG_R0_COLOR << R_TXC_ARG_B_SHIFT)
              | ((R_TXC_ARG_TFACTOR_COLOR + 1) << R_TXC_ARG_C_SHIFT) | R_TXC_OP_LERP);
    CHECK(LastReg(R_PP_TFACTOR_0) != 0xdeadbeefUL);
    /* DOT3_RGBA, RGB_SCALE 4: both arguments biased and doubled, alpha the dot */
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_DOT3_RGBA);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_RGB_SCALE, 4);
    Quad(0, 0, 10, 1);
    cb = LastReg(R_PP_TXCBLEND_0);
    CHECK_HEX(cb & R_TXC_OP_MASK, R_TXC_OP_DOT3);
    CHECK_HEX(cb & (R_TXC_BIAS_ARG_A | R_TXC_BIAS_ARG_B | R_TXC_SCALE_ARG_A | R_TXC_SCALE_ARG_B),
              R_TXC_BIAS_ARG_A | R_TXC_BIAS_ARG_B | R_TXC_SCALE_ARG_A | R_TXC_SCALE_ARG_B);
    cb2 = LastReg(R_PP_TXCBLEND2_0);
    ab2 = LastReg(R_PP_TXABLEND2_0);
    CHECK_HEX((cb2 >> R_TXC_SCALE_SHIFT) & 7, 2);
    CHECK(ab2 & R_TXA_DOT_ALPHA);
    CHECK_HEX((ab2 >> R_TXC_SCALE_SHIFT) & 7, 2);
    /* EXT's dot3: no scale */
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_DOT3_RGB_EXT);
    Quad(0, 0, 10, 1);
    CHECK_HEX((LastReg(R_PP_TXCBLEND2_0) >> R_TXC_SCALE_SHIFT) & 7, 0);
    /* errors */
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_DOT3_RGB);
    CHECK_INT(Core_GetError(&core), GL_INVALID_ENUM);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_RGB_SCALE, 3);
    CHECK_INT(Core_GetError(&core), GL_INVALID_VALUE);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE0_ARB + 6);
    CHECK_INT(Core_GetError(&core), GL_INVALID_ENUM);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_COLOR);
    CHECK_INT(Core_GetError(&core), GL_INVALID_ENUM);
    /* back to a classic mode: the narrow combiner again */
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    CHECK_INT(core.comb_x, 0);
    CHECK_INT(Checked(), R2CE_OK);
}

/* The crossbar: a later stage reads unit 0's texel, so stage 0 must not
 * write its result into R0 (Mesa r200UpdateAllTexEnv's registers) */
static void test_crossbar_regs(void)
{
    Fresh();
    Solid(0, 1, 255, 0, 0, 255);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    Solid(1, 2, 0, 0, 255, 255);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_ADD);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE0_ARB);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_TEXTURE1_ARB);
    Quad(0, 0, 10, 3);
    /* stage 1 writes R0, reads R0 and R1; stage 0 writes R2 */
    CHECK_HEX((LastReg(R_PP_TXCBLEND2_0 + R_PP_TXBLEND_STRIDE) >> R_TXC_OUTPUT_REG_SHIFT) & 7, 1);
    CHECK_HEX((LastReg(R_PP_TXCBLEND2_0) >> R_TXC_OUTPUT_REG_SHIFT) & 7, 3);
    CHECK_HEX(LastReg(R_PP_TXCBLEND_0 + R_PP_TXBLEND_STRIDE) & 0x3ffUL,
              R_TXC_ARG_R0_COLOR | (R_TXC_ARG_ZERO << R_TXC_ARG_B_SHIFT));
    CHECK_INT(Checked(), R2CE_OK);
}

/* --- Cube maps --------------------------------------------------------------------- */

static void CubeFaces(ULONG name, ULONG size)
{
    static const UBYTE col[6][4] = { { 255, 0, 0, 255 }, { 0, 255, 0, 255 }, { 0, 0, 255, 255 },
                                     { 255, 255, 0, 255 }, { 0, 255, 255, 255 }, { 255, 0, 255, 255 } };
    UBYTE px[16 * 16 * 4];
    ULONG f;
    ULONG i;

    Core_BindTexture(&core, GL_TEXTURE_CUBE_MAP, name);
    for (f = 0; f < 6; f++)
    {
        for (i = 0; i < size * size; i++)
            memcpy(px + 4 * i, col[f], 4);
        Core_TexImage2D(&core, GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGBA8, (LONG) size, (LONG) size, 0,
                        GL_RGBA, GL_UNSIGNED_BYTE, px);
    }
    Core_TexParameteri(&core, GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    Core_TexParameteri(&core, GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

static void test_cube_regs(void)
{
    struct CoreTex *t;
    ULONG f;
    ULONG at;
    ULONG nv;
    LONG n;

    Fresh();
    Ortho();
    CubeFaces(30, 4);
    t = core.tex[30];
    CHECK(t != NULL && t->cube && t->levels == 1);
    if (t == NULL)
        return;
    CHECK_HEX(Texel(t, 3, 0, 1, 1), 0xffffff00UL);  /* face -Y yellow, ARGB8888 */
    Core_Enable(&core, GL_TEXTURE_CUBE_MAP, 1);
    CHECK_INT(core.wide_on, 1);
    CHECK_INT(core.bound[0], 30);
    Core_Begin(&core, GL_TRIANGLES);
    Core_TexCoord4f(&core, 0.25f, 0.5f, -1.0f, 1.0f);
    Core_Vertex3f(&core, 0, 0, 0);
    Core_Vertex3f(&core, 10, 0, 0);
    Core_Vertex3f(&core, 0, 10, 0);
    Core_End(&core);
    CHECK_INT(core.vtx_dw, 7);
    CHECK_HEX(LastReg(R_SE_VTX_FMT_1), 3);
    at = LastImmd(&nv);
    if (at != 0)
        CHECK(F(V(at + 6)) == -1.0f);               /* r */
    CHECK(LastReg(R_PP_TXFORMAT_0) & R_TXFORMAT_CUBIC_MAP_ENABLE);
    CHECK_HEX((LastReg(R_PP_TXFORMAT_0) >> R_TXFORMAT_F5_WIDTH_SHIFT) & 0xff, 0x22);
    CHECK_HEX(LastReg(R_PP_TXFORMAT_X_0) & (7UL << 16), R_TEXCOORD_CUBIC_ENV);
    CHECK_HEX(LastReg(R_PP_CUBIC_FACES_0), 0x22222222UL);
    for (f = 1; f < 6; f++)
        CHECK_HEX(LastReg(R_PP_CUBIC_OFFSET_F1_0 + 4 * (f - 1)), t->gpu + f * t->face_bytes);
    CHECK_INT(Checked(), R2CE_OK);
    CHECK_INT(Core_GetError(&core), GL_NO_ERROR);
    Core_GetIntegervX(&core, GL_TEXTURE_BINDING_CUBE_MAP, &n);
    CHECK_INT(n, 30);
    /* errors: a face not square, a cube name as 2D, a 2D name as a cube */
    Core_TexImage2D(&core, GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_RGBA, 4, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    CHECK_INT(Core_GetError(&core), GL_INVALID_VALUE);
    Core_BindTexture(&core, GL_TEXTURE_2D, 30);
    CHECK_INT(Core_GetError(&core), GL_INVALID_OPERATION);
    Solid(1, 31, 1, 2, 3, 4);
    Core_BindTexture(&core, GL_TEXTURE_CUBE_MAP, 31);
    CHECK_INT(Core_GetError(&core), GL_INVALID_OPERATION);
    /* the cube off: unit 0's 2D binding (0) back, two coordinates */
    Core_ActiveTexture(&core, GL_TEXTURE0_ARB);
    Core_Enable(&core, GL_TEXTURE_CUBE_MAP, 0);
    CHECK_INT(core.bound[0], 0);
    CHECK_INT(core.wide_on, 0);
}

/* Reflection maps: S, T and R generated from the eye reflection vector */
static void test_texgen_reflect(void)
{
    Fresh();
    CubeFaces(40, 2);
    Core_Enable(&core, GL_TEXTURE_CUBE_MAP, 1);
    Core_TexGeni(&core, GL_S, GL_TEXTURE_GEN_MODE, GL_REFLECTION_MAP);
    Core_TexGeni(&core, GL_T, GL_TEXTURE_GEN_MODE, GL_REFLECTION_MAP);
    Core_TexGeni(&core, GL_R, GL_TEXTURE_GEN_MODE, GL_REFLECTION_MAP);
    Core_Enable(&core, GL_TEXTURE_GEN_S, 1);
    Core_Enable(&core, GL_TEXTURE_GEN_T, 1);
    Core_Enable(&core, GL_TEXTURE_GEN_R, 1);
    CHECK_INT(Core_GetError(&core), GL_NO_ERROR);
    CHECK_INT(core.vnorm, 1);
    CHECK_INT(core.vslow, 1);
    Core_Begin(&core, GL_TRIANGLES);
    Core_Normal3f(&core, 0, 0, 1);
    Core_Vertex3f(&core, 0, 0, 0);
    Core_Vertex3f(&core, 1, 0, 0);
    Core_Vertex3f(&core, 0, 1, 0);
    Core_End(&core);
    CHECK_INT(core.vtx_dw, 4 + 3 + 3);              /* a normal, s t r */
    CHECK_HEX(LastReg(R_SE_TCL_TEX_PROC_CTL_1) & 0xf, 0xb);     /* EYE_REFLECT */
    CHECK_HEX(LastReg(R_SE_TCL_TEX_PROC_CTL_2) & 0xf, 0x8);     /* Q passed, S T R made */
    CHECK(LastReg(R_SE_TCL_OUTPUT_VTX_COMP_SEL) & R_OUTPUT_TEX_0);
    CHECK_INT(Checked(), R2CE_OK);
}

/* --- Formats ---------------------------------------------------------------------- */

static void test_formats(void)
{
    static const UBYTE bgra[2 * 4] = { 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80 };
    static const UBYTE rgba[2 * 4] = { 0xf8, 0x00, 0x00, 0xff, 0x00, 0xf8, 0x00, 0x00 };
    static const UBYTE bgr3[3 * 3 + 3] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0 };
    ULONG argb[2];
    UWORD w16[2];
    struct CoreTex *t;

    Fresh();
    /* GL_BGRA bytes: B G R A */
    Core_BindTexture(&core, GL_TEXTURE_2D, 1);
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, GL_RGBA8, 2, 1, 0, GL_BGRA, GL_UNSIGNED_BYTE, bgra);
    t = core.tex[1];
    CHECK_INT(Core_GetError(&core), GL_NO_ERROR);
    CHECK(t->x13);
    CHECK_HEX(Texel(t, 0, 0, 0, 0), 0x40302010UL);
    CHECK_HEX(Texel(t, 0, 0, 1, 0), 0x80706050UL);
    /* the Amiga's ARGB32: GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, native words */
    argb[0] = 0x11223344UL;
    argb[1] = 0x55667788UL;
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, GL_RGBA8, 2, 1, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, argb);
    CHECK_HEX(Texel(t, 0, 0, 0, 0), 0x11223344UL);
    CHECK_HEX(Texel(t, 0, 0, 1, 0), 0x55667788UL);
    /* into 16-bit storage: ARGB4444 */
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, GL_RGBA, 2, 1, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, argb);
    CHECK_HEX(Texel(t, 0, 0, 0, 0), 0x1234UL);
    /* GL_RGB5_A1: ARGB1555 */
    Core_BindTexture(&core, GL_TEXTURE_2D, 2);
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, GL_RGB5_A1, 2, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    t = core.tex[2];
    CHECK_HEX(t->hwfmt, R_TXFORMAT_ARGB1555 | R_TXFORMAT_ALPHA_IN_MAP);
    CHECK_HEX(Texel(t, 0, 0, 0, 0), 0xfc00UL);      /* opaque red */
    CHECK_HEX(Texel(t, 0, 0, 1, 0), 0x03e0UL);      /* transparent green */
    /* a later plain sub-image into the 1555 texture: tex13.c writes it */
    Core_TexSubImage2D(&core, GL_TEXTURE_2D, 0, 1, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    CHECK_HEX(Texel(t, 0, 0, 1, 0), 0xfc00UL);
    /* GL_UNSIGNED_SHORT_5_6_5 (native) into RGB565: the same word */
    w16[0] = 0xf81fU;
    w16[1] = 0x07e0U;
    Core_BindTexture(&core, GL_TEXTURE_2D, 3);
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, GL_RGB, 2, 1, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, w16);
    t = core.tex[3];
    CHECK_HEX(Texel(t, 0, 0, 0, 0), 0xf81fUL);
    CHECK_HEX(Texel(t, 0, 0, 1, 0), 0x07e0UL);
    /* _REV: red in the low bits */
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, GL_RGB, 2, 1, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5_REV, w16);
    CHECK_HEX(Texel(t, 0, 0, 0, 0), 0xf81fUL);      /* f81f is symmetric: r 31, b 31 */
    w16[0] = 0x001fU;                               /* _REV: r = 31 */
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, GL_RGB, 2, 1, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5_REV, w16);
    CHECK_HEX(Texel(t, 0, 0, 0, 0), 0xf800UL);
    /* 4_4_4_4_REV for RGBA: r in the low nibble */
    w16[0] = 0x4321U;
    Core_BindTexture(&core, GL_TEXTURE_2D, 4);
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4_REV, w16);
    CHECK_HEX(Texel(core.tex[4], 0, 0, 0, 0), 0x4123UL);    /* A R G B */
    /* GL_BGR bytes, GL_UNPACK_ALIGNMENT 1, a 3 x 1 image */
    Core_PixelStore(&core, GL_UNPACK_ALIGNMENT, 1);
    Core_BindTexture(&core, GL_TEXTURE_2D, 5);
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, GL_RGB8, 3, 1, 0, GL_BGR, GL_UNSIGNED_BYTE, bgr3);
    CHECK_HEX(Texel(core.tex[5], 0, 0, 2, 0), 0xff090807UL);
    Core_PixelStore(&core, GL_UNPACK_ALIGNMENT, 4);
    /* a sub-image in GL_BGRA into texture.c's own RGBA texture */
    Core_BindTexture(&core, GL_TEXTURE_2D, 6);
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, GL_RGBA8, 2, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    CHECK(!core.tex[6]->x13);
    Core_TexSubImage2D(&core, GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_BGRA, GL_UNSIGNED_BYTE, bgra);
    CHECK_HEX(Texel(core.tex[6], 0, 0, 0, 0), 0x40302010UL);
    CHECK(!core.tex[6]->x13);
    /* refused: a type with too few components, a float BGRA */
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_SHORT_5_6_5, w16);
    CHECK_INT(Core_GetError(&core), GL_INVALID_ENUM);
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_BGRA, GL_FLOAT, w16);
    CHECK_INT(Core_GetError(&core), GL_INVALID_ENUM);
}

/* --- Sampling ------------------------------------------------------------------------ */

static void test_sampling_regs(void)
{
    UBYTE px[8 * 8 * 4];
    ULONG f;
    ULONG fx;
    ULONG b;
    float bias;
    LONG lvl;

    Fresh();
    memset(px, 0x80, sizeof(px));
    Core_BindTexture(&core, GL_TEXTURE_2D, 1);
    for (lvl = 0; lvl < 4; lvl++)
        Core_TexImage2D(&core, GL_TEXTURE_2D, lvl, GL_RGBA, 8 >> lvl, 8 >> lvl, 0, GL_RGBA,
                        GL_UNSIGNED_BYTE, px);
    Core_Enable(&core, GL_TEXTURE_2D, 1);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 8);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 1);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 2);
    bias = 1.5f;
    Core_TexEnvfv(&core, GL_TEXTURE_FILTER_CONTROL_EXT, GL_TEXTURE_LOD_BIAS_EXT, &bias);
    CHECK_INT(Core_GetError(&core), GL_NO_ERROR);
    CHECK_INT(core.tex_x, 1);
    Quad(0, 0, 10, 1);
    f = LastReg(R_PP_TXFILTER_0);
    fx = LastReg(R_PP_TXFORMAT_X_0);
    CHECK_HEX((f >> R_CLAMP_S_SHIFT) & 7, R_CLAMP_MIRROR);
    CHECK_HEX((f >> R_CLAMP_T_SHIFT) & 7, R_CLAMP_GL);     /* border clamp: CLAMP_GL, D3D border mode */
    CHECK_HEX(f & R_BORDER_MODE_D3D, R_BORDER_MODE_D3D);
    CHECK_HEX((f & R_MAX_ANISO_MASK) >> R_MAX_ANISO_SHIFT, 3);
    CHECK_HEX(f & R_MIN_FILTER_MASK, R_MIN_FILTER_ANISO_NEAREST_MIP_LINEAR);
    CHECK_HEX((f & R_MAX_MIP_LEVEL_MASK) >> R_MAX_MIP_LEVEL_SHIFT, 2);
    CHECK_HEX((fx & R_MIN_MIP_LEVEL_MASK) >> R_MIN_MIP_LEVEL_SHIFT, 1);
    b = (ULONG) ((LONG) (1.5f * (float) R_LOD_BIAS_FIXED_ONE) + (LONG) R_LOD_BIAS_CORRECTION) & R_LOD_BIAS_MASK;
    CHECK_HEX(fx & R_LOD_BIAS_MASK, b);
    CHECK_HEX(LastReg(R_PP_BORDER_COLOR_0), 0);     /* GL's (0, 0, 0, 0) */
    CHECK_INT(Checked(), R2CE_OK);
    /* a plain value through texture.c's fast way drops the mirror again */
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    Quad(0, 0, 10, 1);
    CHECK_HEX((LastReg(R_PP_TXFILTER_0) >> R_CLAMP_S_SHIFT) & 7, R_CLAMP_WRAP);
    /* errors */
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 0);
    CHECK_INT(Core_GetError(&core), GL_INVALID_VALUE);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, -1);
    CHECK_INT(Core_GetError(&core), GL_INVALID_VALUE);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_LINEAR);
    CHECK_INT(Core_GetError(&core), GL_INVALID_ENUM);
}

/* 2026-11-25: anisotropy alone stays in the fast route (no tex_x: GLQuake
 * paid 3.3 % for ValidateOnce_W at 2x and 8x alike) -- the min field's
 * anisotropic bit and the ratio ORed into the plain filter; 1 turns both
 * off again */
static void test_aniso_fast(void)
{
    static UBYTE px[8 * 8 * 4];
    ULONG f;
    LONG lvl;

    Fresh();
    memset(px, 0x80, sizeof(px));
    Core_BindTexture(&core, GL_TEXTURE_2D, 1);
    for (lvl = 0; lvl < 4; lvl++)
        Core_TexImage2D(&core, GL_TEXTURE_2D, lvl, GL_RGBA, 8 >> lvl, 8 >> lvl, 0, GL_RGBA,
                        GL_UNSIGNED_BYTE, px);
    Core_Enable(&core, GL_TEXTURE_2D, 1);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 8);
    CHECK_INT(Core_GetError(&core), GL_NO_ERROR);
    CHECK_INT(core.tex_x, 0);                       /* the narrow route */
    CHECK_INT(core.tex[1]->xparm, 0);
    Quad(0, 0, 10, 1);
    f = LastReg(R_PP_TXFILTER_0);
    CHECK_HEX((f & R_MAX_ANISO_MASK) >> R_MAX_ANISO_SHIFT, 3);
    CHECK_HEX(f & R_MIN_FILTER_MASK, R_MIN_FILTER_LINEAR_MIP_NEAREST | R_MIN_FILTER_ANISO_NEAREST);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 16);
    Quad(0, 0, 10, 1);
    CHECK_HEX((LastReg(R_PP_TXFILTER_0) & R_MAX_ANISO_MASK) >> R_MAX_ANISO_SHIFT, 4);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1);
    Quad(0, 0, 10, 1);
    f = LastReg(R_PP_TXFILTER_0);
    CHECK_HEX(f & R_MAX_ANISO_MASK, 0);
    CHECK_HEX(f & R_MIN_FILTER_MASK, R_MIN_FILTER_LINEAR_MIP_NEAREST);
    CHECK_INT(core.tex_x, 0);
    CHECK_INT(Checked(), R2CE_OK);
}

/* --- Separate specular, rescale, queries ---------------------------------------------- */

/* 2026-11-21: every minification filter's R200 mode, plain (no
 * anisotropy): trilinear (GL_LINEAR_MIPMAP_LINEAR) is LINEAR_MIP_LINEAR,
 * a mip filter reaches the top of the uploaded chain, a filter without
 * mipmaps and a texture of one level stay at level 0. (With anisotropy,
 * test_sampling_regs: trilinear becomes ANISO_NEAREST_MIP_LINEAR.) */
static void test_min_filters(void)
{
    static const ULONG gl[6] = { GL_NEAREST, GL_LINEAR, GL_NEAREST_MIPMAP_NEAREST,
                                 GL_NEAREST_MIPMAP_LINEAR, GL_LINEAR_MIPMAP_NEAREST,
                                 GL_LINEAR_MIPMAP_LINEAR };
    static const ULONG hw[6] = { 0, R_MIN_FILTER_LINEAR, R_MIN_FILTER_NEAREST_MIP_NEAREST,
                                 R_MIN_FILTER_NEAREST_MIP_LINEAR, R_MIN_FILTER_LINEAR_MIP_NEAREST,
                                 R_MIN_FILTER_LINEAR_MIP_LINEAR };
    static const ULONG top[6] = { 0, 0, 3, 3, 3, 3 };
    UBYTE px[8 * 8 * 4];
    ULONG i;
    ULONG lvl;
    ULONG f;

    /* the field's bits as the card does them (GLFilt on the A4000D,
     * 2026-11-21): 1 linear within a level, 2 mipmapped, 4 linear between
     * levels, 8 anisotropic -- the values pinned, not only the names */
    CHECK_HEX(R_MIN_FILTER_LINEAR, 1UL << 1);
    CHECK_HEX(R_MIN_FILTER_NEAREST_MIP_NEAREST, 2UL << 1);
    CHECK_HEX(R_MIN_FILTER_LINEAR_MIP_NEAREST, 3UL << 1);
    CHECK_HEX(R_MIN_FILTER_NEAREST_MIP_LINEAR, 6UL << 1);
    CHECK_HEX(R_MIN_FILTER_LINEAR_MIP_LINEAR, 7UL << 1);
    CHECK_HEX(R_MIN_FILTER_ANISO_NEAREST_MIP_NEAREST, 10UL << 1);
    CHECK_HEX(R_MIN_FILTER_ANISO_NEAREST_MIP_LINEAR, 14UL << 1);
    memset(px, 0x80, sizeof(px));
    for (i = 0; i < 6; i++)
    {
        Fresh();
        Core_BindTexture(&core, GL_TEXTURE_2D, 1);
        for (lvl = 0; lvl < 4; lvl++)
            Core_TexImage2D(&core, GL_TEXTURE_2D, lvl, GL_RGBA, 8 >> lvl, 8 >> lvl, 0, GL_RGBA,
                            GL_UNSIGNED_BYTE, px);
        Core_Enable(&core, GL_TEXTURE_2D, 1);
        Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (LONG) gl[i]);
        Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        CHECK_INT(Core_GetError(&core), GL_NO_ERROR);
        Quad(0, 0, 10, 1);
        f = LastReg(R_PP_TXFILTER_0);
        CHECK_HEX(f & R_MIN_FILTER_MASK, hw[i]);
        CHECK_HEX(f & R_MAX_ANISO_MASK, 0);
        CHECK_HEX(f & R_MAG_FILTER_LINEAR, R_MAG_FILTER_LINEAR);
        CHECK_INT((f & R_MAX_MIP_LEVEL_MASK) >> R_MAX_MIP_LEVEL_SHIFT, top[i]);
    }
    /* trilinear on a texture of one level: the mode stays, level 0 only */
    Fresh();
    Core_BindTexture(&core, GL_TEXTURE_2D, 1);
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    Core_Enable(&core, GL_TEXTURE_2D, 1);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    Quad(0, 0, 10, 1);
    f = LastReg(R_PP_TXFILTER_0);
    CHECK_HEX(f & R_MIN_FILTER_MASK, R_MIN_FILTER_LINEAR_MIP_LINEAR);
    CHECK_INT((f & R_MAX_MIP_LEVEL_MASK) >> R_MAX_MIP_LEVEL_SHIFT, 0);
}

/* 2026-11-21: GLCompat's trilinear block replayed (glsmoke/glcompat.c
 * FilterCases): a texture of seven RGB levels made first, the filters set
 * after it, GL_REPLACE -- which minification mode reaches the register for
 * each draw. On the card the first draw (LINEAR_MIPMAP_NEAREST) came out
 * blended between levels like a MIP_LINEAR mode. */
static void test_filter_after_upload(void)
{
    static UBYTE lv[64 * 64 * 3];
    ULONG l, sz, f;

    Fresh();
    memset(lv, 0x40, sizeof(lv));
    Core_BindTexture(&core, GL_TEXTURE_2D, 1);
    for (l = 0, sz = 64; sz >= 1; l++, sz >>= 1)
        Core_TexImage2D(&core, GL_TEXTURE_2D, (LONG) l, GL_RGB, (LONG) sz, (LONG) sz, 0, GL_RGB,
                        GL_UNSIGNED_BYTE, lv);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    Core_Enable(&core, GL_TEXTURE_2D, 1);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
    Quad(0, 0, 10, 1);
    f = LastReg(R_PP_TXFILTER_0);
    printf("  replay: after LINEAR_MIPMAP_NEAREST: TXFILTER 0x%08lx (min 0x%lx, top %lu)\n",
           (unsigned long) f, (unsigned long) (f & R_MIN_FILTER_MASK),
           (unsigned long) ((f & R_MAX_MIP_LEVEL_MASK) >> R_MAX_MIP_LEVEL_SHIFT));
    CHECK_HEX(f & R_MIN_FILTER_MASK, R_MIN_FILTER_LINEAR_MIP_NEAREST);
    CHECK_INT((f & R_MAX_MIP_LEVEL_MASK) >> R_MAX_MIP_LEVEL_SHIFT, 6);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    Quad(0, 0, 10, 1);
    f = LastReg(R_PP_TXFILTER_0);
    CHECK_HEX(f & R_MIN_FILTER_MASK, R_MIN_FILTER_LINEAR_MIP_LINEAR);
}

static void test_light_and_queries(void)
{
    float v[4];
    float one;

    Fresh();
    one = (float) GL_SEPARATE_SPECULAR_COLOR;
    Core_LightModelfv(&core, GL_LIGHT_MODEL_COLOR_CONTROL, &one, 1);
    Core_Enable(&core, GL_LIGHTING, 1);
    Core_Enable(&core, GL_RESCALE_NORMAL, 1);
    CHECK_INT(Core_GetError(&core), GL_NO_ERROR);
    Core_Begin(&core, GL_TRIANGLES);
    Core_Vertex3f(&core, 0, 0, 0);
    Core_Vertex3f(&core, 1, 0, 0);
    Core_Vertex3f(&core, 0, 1, 0);
    Core_End(&core);
    CHECK(!(LastReg(R_SE_TCL_LIGHT_MODEL_CTL_0) & (1UL << 6)));    /* not combined */
    CHECK(LastReg(R_SE_TCL_LIGHT_MODEL_CTL_0) & (1UL << 3));       /* normalized */
    CHECK(LastReg(R_SE_TCL_OUTPUT_VTX_COMP_SEL) & R_OUTPUT_COLOR_1);
    CHECK(LastReg(R_PP_CNTL) & 0x00200000UL);                      /* SPECULAR_ENABLE */
    CHECK_INT(Checked(), R2CE_OK);
    Core_GetFloatvX(&core, GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, v);
    CHECK(v[0] == 16.0f);
    Core_GetFloatvX(&core, GL_LIGHT_MODEL_COLOR_CONTROL, v);
    CHECK(v[0] == (float) GL_SEPARATE_SPECULAR_COLOR);
    CHECK_INT(Core_IsEnabled(&core, GL_RESCALE_NORMAL), 1);
    one = 7.0f;
    Core_LightModelfv(&core, GL_LIGHT_MODEL_COLOR_CONTROL, &one, 1);
    CHECK_INT(Core_GetError(&core), GL_INVALID_ENUM);
}

/* --- Pictures ------------------------------------------------------------------------ */

/* Six units, each multiplying by a colour of its own */
static void pic_six_units(void)
{
    Fresh();
    Core_Clear(&core, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    Ortho();
    Solid(0, 1, 255, 255, 255, 255);
    Solid(1, 2, 128, 255, 255, 255);
    Solid(2, 3, 255, 128, 255, 255);
    Solid(3, 4, 255, 255, 128, 255);
    Solid(4, 5, 255, 255, 255, 255);
    Solid(5, 6, 255, 255, 255, 255);
    Quad(10, 10, 40, 0x3f);
    /* unit 5 REPLACE with red: only it shows */
    Core_ActiveTexture(&core, GL_TEXTURE0_ARB + 5);
    Solid(5, 7, 255, 0, 0, 255);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    Quad(60, 10, 40, 0x3f);
    /* unit 4 off: a hole in the middle (stages compacted) */
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    Solid(5, 8, 255, 255, 255, 255);
    Core_ActiveTexture(&core, GL_TEXTURE0_ARB + 1);
    Core_Enable(&core, GL_TEXTURE_2D, 0);
    Quad(110, 10, 40, 0x3d);
    Render("units6");
    CHECK_PX(30, 30, 128, 128, 128);
    CHECK_PX(80, 30, 255, 0, 0);
    CHECK_PX(130, 30, 255, 128, 128);
    CHECK_INT(Checked(), R2CE_OK);
}

/* The combine modes on one unit: texture (0.75, 0.25, 0.5), colour 0.5 */
static void CombQuad(float x, ULONG mode, ULONG s0, ULONG s1, ULONG s2)
{
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_COMBINE_RGB, mode);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_SOURCE0_RGB, s0);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_SOURCE1_RGB, s1);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_SOURCE2_RGB, s2);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_COLOR);    /* GL's default: SRC_ALPHA */
    Quad(x, 10, 30, 1);
}

static void pic_combine(void)
{
    static const float cc[4] = { 0.25f, 0.25f, 0.25f, 1.0f };

    Fresh();
    Core_Clear(&core, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    Ortho();
    Solid(0, 1, 191, 64, 128, 255);
    Core_TexEnvfv(&core, GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, cc);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
    Core_Color4ub(&core, 128, 128, 128, 255);
    CombQuad(0, GL_ADD, GL_TEXTURE, GL_PREVIOUS, GL_CONSTANT);
    CombQuad(40, GL_ADD_SIGNED, GL_TEXTURE, GL_PREVIOUS, GL_CONSTANT);
    CombQuad(80, GL_SUBTRACT, GL_TEXTURE, GL_PREVIOUS, GL_CONSTANT);
    CombQuad(120, GL_INTERPOLATE, GL_TEXTURE, GL_PREVIOUS, GL_CONSTANT);
    CombQuad(160, GL_MODULATE_ADD_ATI, GL_TEXTURE, GL_PREVIOUS, GL_CONSTANT);  /* t c + p */
    CombQuad(200, GL_MODULATE_SUBTRACT_ATI, GL_TEXTURE, GL_CONSTANT, GL_PREVIOUS);   /* t p - c */
    /* DOT3: texture and colour (1, 0.5, 0.5): 4 (0.25 * 0.5) = 0.5 */
    Core_Color4ub(&core, 255, 128, 128, 255);
    CombQuad(240, GL_DOT3_RGB, GL_TEXTURE, GL_PREVIOUS, GL_CONSTANT);
    /* RGB_SCALE 2 on MODULATE: 2 t p */
    Core_Color4ub(&core, 128, 128, 128, 255);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_RGB_SCALE, 2);
    CombQuad(280, GL_MODULATE, GL_TEXTURE, GL_PREVIOUS, GL_CONSTANT);
    Render("combine");
    CHECK_PX(15, 25, 255, 192, 255);
    CHECK_PX(55, 25, 191, 64, 128);
    CHECK_PX(95, 25, 63, 0, 0);
    CHECK_PX(135, 25, 144, 112, 128);
    CHECK_PX(175, 25, 176, 144, 160);
    CHECK_PX(215, 25, 32, 0, 0);
    CHECK_PX(255, 25, 128, 128, 128);
    CHECK_PX(295, 25, 191, 64, 128);
    CHECK_INT(Checked(), R2CE_OK);
}

/* The crossbar: unit 1 adds unit 0's raw texel (red) to its own (blue),
 * while unit 0's own stage made a darker red of it */
static void pic_crossbar(void)
{
    Fresh();
    Core_Clear(&core, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    Ortho();
    Core_Color4ub(&core, 128, 128, 128, 255);
    Solid(0, 1, 255, 0, 0, 255);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    Solid(1, 2, 0, 0, 255, 255);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_ADD);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE0_ARB);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_TEXTURE1_ARB);
    Quad(10, 10, 40, 3);
    /* and PREVIOUS: the dark red unit 0 made, plus blue */
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PREVIOUS);
    Quad(60, 10, 40, 3);
    Render("crossbar");
    CHECK_PX(30, 30, 255, 0, 255);
    CHECK_PX(80, 30, 128, 0, 255);
}

/* A cube map's six faces, looked up along each axis */
static void pic_cube(void)
{
    static const float dir[6][3] = { { 1, 0.2f, 0.1f }, { -1, 0.1f, 0.2f }, { 0.2f, 1, 0.1f },
                                     { 0.1f, -1, 0.2f }, { 0.2f, 0.1f, 1 }, { 0.1f, 0.2f, -1 } };
    static const LONG want[6][3] = { { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 },
                                     { 255, 255, 0 }, { 0, 255, 255 }, { 255, 0, 255 } };
    ULONG f;
    ULONG k;

    Fresh();
    Core_Clear(&core, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    Ortho();
    CubeFaces(1, 4);
    Core_Enable(&core, GL_TEXTURE_CUBE_MAP, 1);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    for (f = 0; f < 6; f++)
    {
        Core_Begin(&core, GL_QUADS);
        for (k = 0; k < 4; k++)
        {
            Core_TexCoord4f(&core, dir[f][0], dir[f][1], dir[f][2], 1.0f);
            Core_Vertex3f(&core, 10.0f + 50.0f * (float) f + ((k == 1 || k == 2) ? 40.0f : 0.0f),
                          10.0f + ((k >= 2) ? 40.0f : 0.0f), 0.0f);
        }
        Core_End(&core);
    }
    Render("cube");
    for (f = 0; f < 6; f++)
        CHECK_PX(30 + 50 * (LONG) f, 30, want[f][0], want[f][1], want[f][2]);
    CHECK_INT(Checked(), R2CE_OK);
}

/* Mirrored repeat and border clamp on a black | white texture */
static void pic_wrap(void)
{
    static const UBYTE bw[2 * 4] = { 0, 0, 0, 255, 255, 255, 255, 255 };

    Fresh();
    Core_ClearColor(&core, 0.0f, 0.0f, 1.0f, 1.0f);
    Core_Clear(&core, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    Ortho();
    Core_BindTexture(&core, GL_TEXTURE_2D, 1);
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, GL_RGBA8, 2, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, bw);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    Core_Enable(&core, GL_TEXTURE_2D, 1);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    /* s 0..2 over 80 pixels: black, white, white, black */
    Core_Begin(&core, GL_QUADS);
    Core_TexCoord2f(&core, 0, 0); Core_Vertex3f(&core, 0, 0, 0);
    Core_TexCoord2f(&core, 2, 0); Core_Vertex3f(&core, 80, 0, 0);
    Core_TexCoord2f(&core, 2, 1); Core_Vertex3f(&core, 80, 20, 0);
    Core_TexCoord2f(&core, 0, 1); Core_Vertex3f(&core, 0, 20, 0);
    Core_End(&core);
    /* border clamp: s -1..2 over 120 pixels, outside [0, 1] the border (black, alpha 0) */
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    Core_Begin(&core, GL_QUADS);
    Core_TexCoord2f(&core, -1, 0); Core_Vertex3f(&core, 0, 40, 0);
    Core_TexCoord2f(&core, 2, 0); Core_Vertex3f(&core, 120, 40, 0);
    Core_TexCoord2f(&core, 2, 1); Core_Vertex3f(&core, 120, 60, 0);
    Core_TexCoord2f(&core, -1, 1); Core_Vertex3f(&core, 0, 60, 0);
    Core_End(&core);
    Render("wrap");
    CHECK_PX(10, 10, 0, 0, 0);
    CHECK_PX(30, 10, 255, 255, 255);
    CHECK_PX(50, 10, 255, 255, 255);
    CHECK_PX(70, 10, 0, 0, 0);
    CHECK_PX(20, 50, 0, 0, 0);          /* border */
    CHECK_PX(70, 50, 255, 255, 255);    /* the white texel */
    CHECK_PX(100, 50, 0, 0, 0);         /* border */
    CHECK_INT(Checked(), R2CE_OK);
}

/* ARGB32 and ARGB1555 sampled; 1555's one-bit alpha through the alpha test */
static void pic_formats(void)
{
    static const UBYTE a1[2 * 4] = { 0, 255, 0, 255, 255, 0, 0, 0 };
    ULONG argb[2];

    Fresh();
    Core_ClearColor(&core, 0.0f, 0.0f, 1.0f, 1.0f);
    Core_Clear(&core, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    Ortho();
    argb[0] = 0xffff0000UL;             /* red, then green */
    argb[1] = 0xff00ff00UL;
    Core_BindTexture(&core, GL_TEXTURE_2D, 1);
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, GL_RGBA8, 2, 1, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, argb);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    Core_Enable(&core, GL_TEXTURE_2D, 1);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    Quad(0, 0, 40, 1);
    Core_BindTexture(&core, GL_TEXTURE_2D, 2);
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, GL_RGB5_A1, 2, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, a1);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    Core_Enable(&core, GL_ALPHA_TEST, 1);
    Core_AlphaFunc(&core, GL_GREATER, 0.5f);
    Quad(60, 0, 40, 1);
    Render("formats");
    CHECK_PX(10, 20, 255, 0, 0);
    CHECK_PX(30, 20, 0, 255, 0);
    CHECK_PX(70, 20, 0, 255, 0);        /* opaque green */
    CHECK_PX(90, 20, 0, 0, 255);        /* alpha 0: not drawn, the clear colour */
    CHECK_INT(Checked(), R2CE_OK);
}

/* --- ARGB1555 uploads without the per-texel way (2026-11-25) ------------------------- */

/* every texel of textures a and b (w x h, level 0) alike */
static int SameTexels(const struct CoreTex *a, const struct CoreTex *b, ULONG w, ULONG h)
{
    ULONG x, y;

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            if (Texel(a, 0, 0, x, y) != Texel(b, 0, 0, x, y))
                return 0;
    return 1;
}

static void test_argb1555_paths(void)
{
    static UBYTE zero[16 * 4 * 4];
    static UBYTE rgba[7 * 3 * 4];       /* 7 x 3, rows of 28 bytes */
    static UBYTE odd[1 + 7 * 3 * 4];    /* the same, one byte off its alignment */
    static ULONG packed[7 * 3];         /* GL_UNSIGNED_INT_8_8_8_8: R in the top byte */
    static UWORD words[8 * 3];          /* 1_5_5_5_REV, rows of 8 (alignment 4) */
    static UWORD swapped[8 * 3];
    ULONG i, x, y, r, g, b, a, seed, v;
    struct CoreTex *ta, *tb, *tc, *td, *te;

    seed = 12345;
    for (i = 0; i < sizeof(rgba); i++)
    {
        seed = seed * 1103515245UL + 12345UL;
        rgba[i] = (UBYTE) (seed >> 16);
    }
    rgba[3] = 127;                      /* both sides of the alpha cut */
    rgba[7] = 128;
    for (i = 0; i < sizeof(rgba); i++)
        odd[1 + i] = rgba[i];
    for (y = 0; y < 3; y++)
        for (x = 0; x < 7; x++)
        {
            i = (y * 7 + x) * 4;
            r = rgba[i]; g = rgba[i + 1]; b = rgba[i + 2]; a = rgba[i + 3];
            packed[y * 7 + x] = (r << 24) | (g << 16) | (b << 8) | a;
            v = ((a >= 128) ? 0x8000UL : 0) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
            words[y * 8 + x] = (UWORD) v;
            swapped[y * 8 + x] = (UWORD) (((v & 0xff) << 8) | (v >> 8));
        }

    Fresh();
    for (i = 1; i <= 5; i++)
    {
        Core_BindTexture(&core, GL_TEXTURE_2D, i);
        Core_TexImage2D(&core, GL_TEXTURE_2D, 0, GL_RGB5_A1, 16, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, zero);
    }
    ta = core.tex[1]; tb = core.tex[2]; tc = core.tex[3]; td = core.tex[4]; te = core.tex[5];
    CHECK_HEX(ta->hwfmt, R_TXFORMAT_ARGB1555 | R_TXFORMAT_ALPHA_IN_MAP);
    /* 1: RGBA bytes, the direct way, at x 1 (a partial first dword), 7 wide
     * (a partial last one) */
    Core_BindTexture(&core, GL_TEXTURE_2D, 1);
    Core_TexSubImage2D(&core, GL_TEXTURE_2D, 0, 1, 1, 7, 3, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    /* 2: the same colours through the generic way (a packed 32-bit type) */
    Core_BindTexture(&core, GL_TEXTURE_2D, 2);
    Core_TexSubImage2D(&core, GL_TEXTURE_2D, 0, 1, 1, 7, 3, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, packed);
    CHECK_INT(Core_GetError(&core), GL_NO_ERROR);
    CHECK(SameTexels(ta, tb, 16, 4));
    CHECK_HEX(Texel(ta, 0, 0, 1, 1), words[0]);
    CHECK_HEX(Texel(ta, 0, 0, 2, 1), words[1] | 0x8000UL);      /* alpha 128: set */
    CHECK_HEX(Texel(ta, 0, 0, 0, 1), 0);                        /* beside it: kept */
    CHECK_HEX(Texel(ta, 0, 0, 8, 1), 0);
    /* 3: native 1555 words, copied as they are */
    Core_BindTexture(&core, GL_TEXTURE_2D, 3);
    Core_TexSubImage2D(&core, GL_TEXTURE_2D, 0, 1, 1, 7, 3, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV, words);
    CHECK_INT(Core_GetError(&core), GL_NO_ERROR);
    CHECK(SameTexels(ta, tc, 16, 4));
    /* 4: the RGBA bytes off their alignment (GL_UNPACK_ALIGNMENT 1) */
    Core_PixelStore(&core, GL_UNPACK_ALIGNMENT, 1);
    Core_BindTexture(&core, GL_TEXTURE_2D, 4);
    Core_TexSubImage2D(&core, GL_TEXTURE_2D, 0, 1, 1, 7, 3, GL_RGBA, GL_UNSIGNED_BYTE, odd + 1);
    Core_PixelStore(&core, GL_UNPACK_ALIGNMENT, 4);
    CHECK(SameTexels(ta, td, 16, 4));
    /* 5: byte-swapped words with GL_UNPACK_SWAP_BYTES: the generic way */
    Core_PixelStore(&core, GL_UNPACK_SWAP_BYTES, 1);
    Core_BindTexture(&core, GL_TEXTURE_2D, 5);
    Core_TexSubImage2D(&core, GL_TEXTURE_2D, 0, 1, 1, 7, 3, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV, swapped);
    Core_PixelStore(&core, GL_UNPACK_SWAP_BYTES, 0);
    CHECK_INT(Core_GetError(&core), GL_NO_ERROR);
    CHECK(SameTexels(ta, te, 16, 4));
    /* an even start, an even width: whole dwords only */
    Core_BindTexture(&core, GL_TEXTURE_2D, 1);
    Core_TexSubImage2D(&core, GL_TEXTURE_2D, 0, 2, 0, 6, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    Core_BindTexture(&core, GL_TEXTURE_2D, 2);
    Core_TexSubImage2D(&core, GL_TEXTURE_2D, 0, 2, 0, 6, 1, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, packed);
    CHECK(SameTexels(ta, tb, 16, 4));
}

int main(void)
{
    vram = (ULONG *) calloc(1, VRAM_SIZE);
    memset(&core, 0, sizeof(core));
    test_units6();
    test_units6_arrays();
    test_narrow_untouched();
    test_combine_regs();
    test_crossbar_regs();
    test_cube_regs();
    test_texgen_reflect();
    test_formats();
    test_sampling_regs();
    test_min_filters();
    test_filter_after_upload();
    test_light_and_queries();
    test_argb1555_paths();
    test_aniso_fast();
    pic_six_units();
    pic_combine();
    pic_crossbar();
    pic_cube();
    pic_wrap();
    pic_formats();
    Core_Free(&core);
    free(vram);
    return tk_done("test_tex13");
}
