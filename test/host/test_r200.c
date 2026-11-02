/*
 * test_r200.c -- host tests for the R200 packet layer: encodings against
 * the drivers' own values, the emitter against the checker, the checker
 * against streams it must refuse, and the ring against a simulated CP.
 */

#include <stdio.h>
#include <string.h>
#include "testkit.h"
#include "r2cs.h"
#include "r2emit.h"
#include "r2ring.h"
#include "r2cp.h"
#include "r2check.h"
#include "r2pm4.h"
#include "r200reg.h"
#include "simcp.h"

/* --- A fake VRAM map ----------------------------------------------------------- */

#define COLOR_GPU  0x00100000UL
#define COLOR_SIZE 0x00140000UL     /* 640 x 480 x 4 = 0x12c000 */
#define DEPTH_GPU  0x00280000UL
#define DEPTH_SIZE 0x000a0000UL     /* 640 x 480 x 2 = 0x96000 */
#define TEX_GPU    0x00400000UL
#define TEX_SIZE   0x00400000UL
#define VTX_GPU    0x00800000UL
#define VTX_SIZE   0x00100000UL
#define IDX_GPU    0x00900000UL
#define IDX_SIZE   0x00010000UL
#define RING_GPU   0x01000000UL
#define FENCE_REG  R_SCRATCH_REG2

/* host copy of the vertex and index ranges, for the checker's peek */
static ULONG vram[(IDX_GPU + IDX_SIZE - VTX_GPU) / 4];

static LONG Peek(APTR ctx, ULONG gpu, ULONG *val)
{
    (void) ctx;
    if (gpu < VTX_GPU || gpu >= IDX_GPU + IDX_SIZE || (gpu & 3) != 0)
        return -1;
    *val = vram[(gpu - VTX_GPU) / 4];
    return 0;
}

static void Cfg(struct R2ChkCfg *cfg, int with_peek)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->range[0].gpu = COLOR_GPU; cfg->range[0].size = COLOR_SIZE; cfg->range[0].kinds = R2K_COLOR;
    cfg->range[1].gpu = DEPTH_GPU; cfg->range[1].size = DEPTH_SIZE; cfg->range[1].kinds = R2K_DEPTH;
    cfg->range[2].gpu = TEX_GPU;   cfg->range[2].size = TEX_SIZE;   cfg->range[2].kinds = R2K_TEXTURE;
    cfg->range[3].gpu = VTX_GPU;   cfg->range[3].size = VTX_SIZE;   cfg->range[3].kinds = R2K_VERTEX;
    cfg->range[4].gpu = IDX_GPU;   cfg->range[4].size = IDX_SIZE;   cfg->range[4].kinds = R2K_INDEX;
    cfg->range[5].gpu = RING_GPU;  cfg->range[5].size = 0x10000;    cfg->range[5].kinds = R2K_RING;
    cfg->nranges = 6;
    cfg->fence_reg = FENCE_REG;
    if (with_peek)
    {
        cfg->peek = Peek;
        cfg->peek_ctx = NULL;
    }
}

static ULONG lcg = 12345;

static ULONG Rand(void)
{
    lcg = lcg * 1103515245UL + 12345UL;
    return (lcg >> 8) & 0xffffffUL;
}

/* xyz + packed colour in, TCL on: 4 dwords a vertex */
#define FMT0_XYZC   (R_VTX_Z0 | (R_VTX_PK_RGBA << R_VTX_COLOR_0_SHIFT))
#define TRI_CNTL    (R_VF_PRIM_TRIANGLES | R_VF_COLOR_ORDER_RGBA | R_VF_TCL_OUTPUT_VTX_ENABLE)

static const float identity[16] =
{
    1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1
};

/* Everything a TCL draw into a 640x480 ARGB8888 + Z16 target needs. */
static LONG SetupFrame(struct R2Cs *cs)
{
    R2E_TclDefaults(cs);
    R2E_RasterDefaults(cs);
    R2E_ColorTarget(cs, COLOR_GPU, 640, 0);
    R2E_DepthTarget(cs, DEPTH_GPU, 640);
    R2E_Scissor(cs, 0, 0, 639, 479);
    R2E_Viewport(cs, 0, 0, 640, 480, 0, 1, 480);
    R2E_Reg(cs, R_RB3D_CNTL, R_COLOR_FORMAT_ARGB8888 | R_Z_ENABLE);
    R2E_Reg(cs, R_PP_CNTL, R_TEX_BLEND_0_ENABLE);
    R2E_VertexFormat(cs, FMT0_XYZC, 0,
                     R_VTX_Z0 | R_VTX_W0 | (R_VTX_FP_RGBA << R_VTX_COLOR_0_SHIFT), 0,
                     R_OUTPUT_XYZW | R_OUTPUT_COLOR_0);
    return R2E_Matrix(cs, R_VS_MATRIX_2_MVP, identity);
}

static LONG Triangle(struct R2Cs *cs, float z)
{
    ULONG v[12];
    ULONG i;

    for (i = 0; i < 3; i++)
    {
        v[i * 4 + 0] = R2Cs_FloatBits(i == 1 ? 1.0f : -1.0f);
        v[i * 4 + 1] = R2Cs_FloatBits(i == 2 ? 1.0f : -1.0f);
        v[i * 4 + 2] = R2Cs_FloatBits(z);
        v[i * 4 + 3] = 0xff0000ffUL;
    }
    return R2E_Immd(cs, TRI_CNTL, 4, 3, v);
}

static LONG CheckBuf(const ULONG *buf, ULONG n, ULONG swap, int with_peek,
                     struct R2ChkState *st, struct R2ChkError *err)
{
    struct R2ChkCfg cfg;

    Cfg(&cfg, with_peek);
    return R2Chk_Run(&cfg, st, buf, n, swap, 0, err);
}

/* --- Encodings ----------------------------------------------------------------- */

static void test_headers(void)
{
    /* Linux r100_ring_test: PACKET0(scratch, 0) */
    CHECK_HEX(R2_PKT0(R_SCRATCH_REG0, 0), 0x00000578UL);
    CHECK_HEX(R2_PKT0(R_SE_VAP_CNTL, 0), 0x00000820UL);
    /* Mesa OUT_VEC of a matrix: CP_PACKET0_ONE(VECTOR_DATA, 16 - 1) */
    CHECK_HEX(R2_PKT0_ONE(R_SE_TCL_VECTOR_DATA_REG, 15), 0x000f8881UL);
    /* Mesa r200_blit: R200_CP_CMD_3D_DRAW_IMMD_2 | (12 << 16) */
    CHECK_HEX(R2_PKT3(R2_OP_3D_DRAW_IMMD_2, 12), 0xc00c3500UL);
    /* Mesa R200_CP_CMD_* carry the type bits */
    CHECK_HEX(R2_PKT3(R2_OP_NOP, 0), 0xc0001000UL);
    CHECK_HEX(R2_PKT3(R2_OP_3D_LOAD_VBPNTR, 0), 0xc0002f00UL);
    CHECK_HEX(R2_PKT3(R2_OP_INDX_BUFFER, 0), 0xc0003300UL);
    CHECK_HEX(R2_PKT3(R2_OP_3D_DRAW_VBUF_2, 0), 0xc0003400UL);
    CHECK_HEX(R2_PKT3(R2_OP_3D_DRAW_INDX_2, 0), 0xc0003600UL);
    /* and the decoders read them back */
    CHECK_INT(R2_PKT_TYPE(R2_PKT3(R2_OP_NOP, 7)), 3);
    CHECK_INT(R2_PKT_COUNT(R2_PKT3(R2_OP_NOP, 0x3fff)), 0x3fff);
    CHECK_HEX(R2_PKT0_REG(R2_PKT0(R_PP_TXOFFSET_0 + 5 * R_PP_TXOFFSET_STRIDE, 0)), 0x2d78UL);
    CHECK_INT(R2_PKT0_ONE_REG_WR(R2_PKT0_ONE(R_SE_TCL_SCALAR_DATA_REG, 3)), 1);
    CHECK_INT(R2_PKT0_ONE_REG_WR(R2_PKT0(R_SE_TCL_SCALAR_DATA_REG, 3)), 0);
    CHECK_INT(R2_PKT3_OPCODE(R2_PKT3(R2_OP_3D_DRAW_INDX_2, 5)), R2_OP_3D_DRAW_INDX_2);
    /* the highest register a type-0 header reaches */
    CHECK_HEX(R2_PKT0_REG(R2_PKT0(0x7ffcUL, 0)), 0x7ffcUL);
}

static void test_swap(void)
{
    ULONG buf[4];
    struct R2Cs cs;

    CHECK_HEX(R2Cs_Swap32(0x11223344UL), 0x44332211UL);
    CHECK_HEX(R2Cs_FloatBits(1.0f), 0x3f800000UL);
    CHECK_HEX(R2Cs_FloatBits(-2.0f), 0xc0000000UL);
    R2Cs_InitBuffer(&cs, buf, 4, 1);
    CHECK_INT(R2E_Reg(&cs, R_PP_CNTL, 0x00001010UL), R2_OK);
    CHECK_HEX(buf[0], R2Cs_Swap32(R2_PKT0(R_PP_CNTL, 0)));
    CHECK_HEX(buf[1], 0x10100000UL);
    R2Cs_InitBuffer(&cs, buf, 4, 0);
    R2E_Reg(&cs, R_PP_CNTL, 0x00001010UL);
    CHECK_HEX(buf[1], 0x00001010UL);
}

static void test_vtx_sizes(void)
{
    ULONG i;
    ULONG f0;
    ULONG f1;
    int same;

    CHECK_INT(R2E_VtxDwords(0, 0), 2);
    CHECK_INT(R2E_VtxDwords(R_VTX_Z0, 0), 3);
    CHECK_INT(R2E_VtxDwords(FMT0_XYZC, 0), 4);
    /* Mesa blit: XY + one 2-component texture coordinate */
    CHECK_INT(R2E_VtxDwords(0, 2UL << R_VTX_TEX0_COMP_CNT_SHIFT), 4);
    /* GLQuake multitexture world vertex: xyz, colour, st0, st1 */
    CHECK_INT(R2E_VtxDwords(FMT0_XYZC, 2 | (2UL << 3)), 8);
    /* with a normal and float RGBA */
    CHECK_INT(R2E_VtxDwords(R_VTX_Z0 | R_VTX_N0 | (R_VTX_FP_RGBA << R_VTX_COLOR_0_SHIFT), 0), 10);
    /* texture component counts 5..7 count nothing (Linux) */
    CHECK_INT(R2E_VtxDwords(0, 7), 2);
    same = 1;
    for (i = 0; i < 4000; i++)
    {
        f0 = (Rand() << 8) ^ Rand();
        f1 = Rand();
        if (R2E_VtxDwords(f0, f1) != R2Chk_VtxDwords(f0, f1))
            same = 0;
    }
    CHECK(same);
}

/* --- Emitter output, checked ---------------------------------------------------- */

static void test_frame(ULONG swap)
{
    ULONG buf[1024];
    struct R2Cs cs;
    struct R2ChkState st;
    struct R2ChkError err;
    ULONG *tri;
    ULONG n;
    LONG e;

    R2Cs_InitBuffer(&cs, buf, 1024, swap);
    CHECK_INT(SetupFrame(&cs), R2_OK);
    tri = cs.ptr;
    CHECK_INT(Triangle(&cs, 0.5f), R2_OK);
    CHECK_INT(R2E_Fence(&cs, FENCE_REG, 1), R2_OK);
    n = (ULONG) (cs.ptr - buf);
    CHECK_HEX(swap ? R2Cs_Swap32(tri[0]) : tri[0], R2_PKT3(R2_OP_3D_DRAW_IMMD_2, 12));
    CHECK_HEX(swap ? R2Cs_Swap32(tri[1]) : tri[1],
              TRI_CNTL | R_VF_PRIM_WALK_RING | (3UL << R_VF_VERTEX_NUMBER_SHIFT));
    R2Chk_Reset(&st);
    e = CheckBuf(buf, n, swap, 0, &st, &err);
    CHECK_INT(e, R2CE_OK);
    if (e != R2CE_OK)
        printf("  frame: %s at %lu what 0x%lx value 0x%lx\n", R2Chk_ErrorName(e),
               (unsigned long) err.at, (unsigned long) err.what, (unsigned long) err.value);
    CHECK_INT(st.draws, 1);
    CHECK_INT(st.vertices, 3);
    CHECK_INT(st.dw_reg + st.dw_draw + st.dw_nop, n);
    /* the same stream read in the wrong byte order is refused */
    R2Chk_Reset(&st);
    CHECK(CheckBuf(buf, n, !swap, 0, &st, &err) != R2CE_OK);
}

/* Mesa 21.3 r200_blit(): the 3D-engine copy RadeonGL presents with. Its
 * state and draw, rebuilt with our emitter, must give Mesa's dwords and
 * pass the checker. */
static void test_mesa_blit(void)
{
    ULONG buf[256];
    struct R2Cs cs;
    struct R2Tex t;
    struct R2ChkState st;
    struct R2ChkError err;
    ULONG verts[12];
    ULONG *draw;
    ULONG i;
    LONG e;

    R2Cs_InitBuffer(&cs, buf, 256, 0);
    /* emit_vtx_state (a TCL chip) */
    R2E_Reg(&cs, R_SE_VAP_CNTL_STATUS, 0);
    R2E_Reg(&cs, R_SE_VAP_CNTL, R_VAP_FORCE_W_TO_ONE | (9UL << R_VAP_VF_MAX_VTX_NUM_SHIFT));
    CHECK_HEX(buf[3], 0x00250000UL);
    R2E_Reg(&cs, R_SE_VTX_STATE_CNTL, 0);
    R2E_Reg(&cs, R_SE_VTE_CNTL, 0);
    R2E_Reg(&cs, R_SE_VTX_FMT_0, 0);
    R2E_Reg(&cs, R_SE_VTX_FMT_1, 2UL << R_VTX_TEX0_COMP_CNT_SHIFT);
    R2E_Reg(&cs, R_SE_CNTL, R_DIFFUSE_SHADE_GOURAUD | R_BFACE_SOLID | R_FFACE_SOLID
                          | R_VTX_PIX_CENTER_OGL | R_ROUND_MODE_ROUND | R_ROUND_PREC_4TH_PIX);
    /* emit_tx_setup: REPLACE from texture 0, 256 x 256 RGB565 NPOT */
    R2E_Reg(&cs, R_PP_CNTL, R_TEX_0_ENABLE | R_TEX_BLEND_0_ENABLE);
    R2E_Combiner(&cs, 0,
                 (R_TXC_ARG_R0_COLOR << R_TXC_ARG_C_SHIFT) | R_TXC_OP_MADD,
                 R_TXC_CLAMP_0_1 | R_TXC_OUTPUT_REG_R0,
                 (R_TXA_ARG_R0_ALPHA << R_TXC_ARG_C_SHIFT) | R_TXC_OP_MADD,
                 R_TXC_CLAMP_0_1 | R_TXC_OUTPUT_REG_R0);
    R2E_Reg(&cs, R_PP_CNTL_X, 0);
    t.filter = R_CLAMP_S_CLAMP_LAST | R_CLAMP_T_CLAMP_LAST;
    t.format = R_TXFORMAT_NON_POWER2 | R_TXFORMAT_RGB565;
    t.format_x = 0;
    t.size = 255 | (255UL << 16);
    t.pitch = 256 * 2 - 32;
    t.multi = 0;
    t.offset = TEX_GPU;
    R2E_Texture(&cs, 0, &t);
    /* emit_cb_setup: 640 x 480 RGB565 */
    R2E_Reg(&cs, R_RE_AUX_SCISSOR_CNTL, 0);
    R2E_Reg(&cs, R_RE_CNTL, 0);
    R2E_Scissor(&cs, 0, 0, 639, 479);
    R2E_Reg(&cs, R_RB3D_PLANEMASK, 0xffffffffUL);
    R2E_Reg(&cs, R_RB3D_BLENDCNTL, (R_BLEND_GL_ONE << R_SRC_BLEND_SHIFT)
                                 | (R_BLEND_GL_ZERO << R_DST_BLEND_SHIFT));
    R2E_Reg(&cs, R_RB3D_CNTL, R_COLOR_FORMAT_RGB565);
    R2E_ColorTarget(&cs, COLOR_GPU, 640, 0);
    /* emit_draw_packet: a RECT_LIST of three window-space vertices */
    for (i = 0; i < 12; i++)
        verts[i] = R2Cs_FloatBits((float) (i * 16));
    draw = cs.ptr;
    CHECK_INT(R2E_Immd(&cs, R_VF_PRIM_RECT_LIST, 4, 3, verts), R2_OK);
    CHECK_HEX(draw[0], 0xc00c3500UL);
    CHECK_HEX(draw[1], 0x00030038UL);   /* RADEON_CP_VC_CNTL_PRIM_WALK_RING | RECT_LIST | 3 << 16 */
    R2Chk_Reset(&st);
    e = CheckBuf(buf, (ULONG) (cs.ptr - buf), 0, 0, &st, &err);
    CHECK_INT(e, R2CE_OK);
    if (e != R2CE_OK)
        printf("  blit: %s at %lu what 0x%lx value 0x%lx\n", R2Chk_ErrorName(e),
               (unsigned long) err.at, (unsigned long) err.what, (unsigned long) err.value);
}

static void test_nop(void)
{
    ULONG buf[0x4010];
    struct R2Cs cs;
    struct R2ChkState st;
    struct R2ChkError err;

    R2Cs_InitBuffer(&cs, buf, 0x4010, 0);
    CHECK_INT(R2E_Nop(&cs, 1, 1), R2_OK);
    CHECK_HEX(buf[0], R2_PKT2);
    R2Cs_InitBuffer(&cs, buf, 0x4010, 0);
    buf[1] = 0x12345678UL;
    R2E_Nop(&cs, 5, 1);
    CHECK_HEX(buf[0], R2_PKT3(R2_OP_NOP, 3));
    CHECK_HEX(buf[1], 0x12345678UL);    /* the payload is never written */
    CHECK_INT(cs.ptr - buf, 5);
    R2Cs_InitBuffer(&cs, buf, 0x4010, 0);
    R2E_Nop(&cs, 5, 0);
    CHECK_HEX(buf[4], R2_PKT2);
    /* longer than one NOP can carry, with a remainder of one dword folded in */
    R2Cs_InitBuffer(&cs, buf, 0x4010, 0);
    CHECK_INT(R2E_Nop(&cs, 0x4002, 1), R2_OK);
    CHECK_HEX(buf[0], R2_PKT3(R2_OP_NOP, 0x3ffe));
    CHECK_HEX(buf[0x4000], R2_PKT3(R2_OP_NOP, 0));
    R2Chk_Reset(&st);
    CHECK_INT(CheckBuf(buf, 0x4002, 0, 0, &st, &err), R2CE_OK);
    CHECK_INT(st.dw_nop, 0x4002);
}

static void test_immd(void)
{
    ULONG buf[64];
    struct R2Cs cs;
    ULONG *v;

    R2Cs_InitBuffer(&cs, buf, 64, 0);
    v = R2E_ImmdOpen(&cs, 4, 3);
    CHECK(v == buf + 2);
    CHECK_INT(R2E_ImmdClose(&cs, v, TRI_CNTL), R2_OK);
    CHECK(cs.ptr == buf);                       /* no vertex, no packet */
    v = R2E_ImmdOpen(&cs, 4, 3);
    CHECK(R2E_ImmdOpen(&cs, 4, 3) == NULL);
    CHECK_INT(cs.error, R2_ESTATE);
    R2Cs_InitBuffer(&cs, buf, 64, 0);
    CHECK(R2E_ImmdOpen(&cs, 4, 100) == NULL);   /* 402 dwords in a 64-dword buffer */
    CHECK_INT(cs.error, R2_EFULL);
    R2Cs_InitBuffer(&cs, buf, 64, 0);
    CHECK(R2E_ImmdOpen(&cs, 4, R2E_IMMD_MAX_VERTS(4) + 1) == NULL);
    CHECK_INT(cs.error, R2_EARG);
    R2Cs_InitBuffer(&cs, buf, 64, 0);
    v = R2E_ImmdOpen(&cs, 4, 3);
    CHECK_INT(R2E_ImmdClose(&cs, v + 5, TRI_CNTL), R2_EARG);    /* not whole vertices */
}

static void test_arrays(void)
{
    ULONG buf[256];
    struct R2Cs cs;
    struct R2Aos aos[3];
    struct R2ChkState st;
    struct R2ChkError err;
    ULONG start;
    ULONG i;
    LONG e;

    R2Cs_InitBuffer(&cs, buf, 256, 0);
    aos[0].comps = 3; aos[0].stride = 3; aos[0].gpu = VTX_GPU;
    aos[1].comps = 1; aos[1].stride = 1; aos[1].gpu = VTX_GPU + 0x10000;
    aos[2].comps = 2; aos[2].stride = 2; aos[2].gpu = VTX_GPU + 0x20000;
    /* Mesa r200EmitAOS: sz = 1 + (nr >> 1) * 3 + (nr & 1) * 2 */
    R2E_LoadVbpntr(&cs, 1, aos);
    CHECK_HEX(buf[0], R2_PKT3(R2_OP_3D_LOAD_VBPNTR, 2));
    CHECK_HEX(buf[1], 1);
    CHECK_HEX(buf[2], 3 | (3 << 8));
    CHECK_HEX(buf[3], VTX_GPU);
    start = (ULONG) (cs.ptr - buf);
    R2E_LoadVbpntr(&cs, 3, aos);
    CHECK_HEX(buf[start], R2_PKT3(R2_OP_3D_LOAD_VBPNTR, 5));
    CHECK_HEX(buf[start + 2], 3 | (3 << 8) | (1UL << 16) | (1UL << 24));
    CHECK_HEX(buf[start + 5], 2 | (2 << 8));
    CHECK_HEX(buf[start + 6], VTX_GPU + 0x20000);
    CHECK_INT(cs.ptr - buf, start + 7);

    /* a whole indexed draw: setup, arrays, indices, checked with peek */
    R2Cs_InitBuffer(&cs, buf, 256, 0);
    SetupFrame(&cs);
    R2E_VertexFormat(&cs, FMT0_XYZC, 2,
                     R_VTX_Z0 | R_VTX_W0 | (R_VTX_FP_RGBA << R_VTX_COLOR_0_SHIFT), 2,
                     R_OUTPUT_XYZW | R_OUTPUT_COLOR_0 | R_OUTPUT_TEX_0);
    R2E_LoadVbpntr(&cs, 3, aos);
    for (i = 0; i < 3; i++)
        vram[(IDX_GPU - VTX_GPU) / 4 + i] = (2 * i) | ((2 * i + 1) << 16);
    start = (ULONG) (cs.ptr - buf);
    CHECK_INT(R2E_DrawIndexed(&cs, TRI_CNTL, 6, IDX_GPU, 6), R2_OK);
    CHECK_INT(cs.ptr - buf, start + 8);
    CHECK_HEX(buf[start + 0], R2_PKT0(R_SE_VF_MAX_VTX_INDX, 0));
    CHECK_HEX(buf[start + 4], R2_PKT3(R2_OP_INDX_BUFFER, 2));
    CHECK_HEX(buf[start + 5], 0x80000810UL);
    CHECK_HEX(buf[start + 7], 3);
    R2Chk_Reset(&st);
    e = CheckBuf(buf, (ULONG) (cs.ptr - buf), 0, 1, &st, &err);
    CHECK_INT(e, R2CE_OK);
    if (e != R2CE_OK)
        printf("  indexed: %s at %lu what 0x%lx value 0x%lx\n", R2Chk_ErrorName(e),
               (unsigned long) err.at, (unsigned long) err.what, (unsigned long) err.value);
    CHECK_INT(st.index_unchecked, 0);
    /* index 6 into 6 vertices */
    vram[(IDX_GPU - VTX_GPU) / 4 + 2] = 4 | (6UL << 16);
    R2Chk_Reset(&st);
    CHECK_INT(CheckBuf(buf, (ULONG) (cs.ptr - buf), 0, 1, &st, &err), R2CE_INDEX);
    CHECK_INT(err.what, 5);
    CHECK_INT(err.value, 6);
    /* without peek the draw passes on its declared bounds, and says so */
    R2Chk_Reset(&st);
    CHECK_INT(CheckBuf(buf, (ULONG) (cs.ptr - buf), 0, 0, &st, &err), R2CE_OK);
    CHECK_INT(st.index_unchecked, 1);
    /* a VBUF draw of the same arrays */
    R2Cs_InitBuffer(&cs, buf, 256, 0);
    SetupFrame(&cs);
    R2E_VertexFormat(&cs, FMT0_XYZC, 2,
                     R_VTX_Z0 | R_VTX_W0 | (R_VTX_FP_RGBA << R_VTX_COLOR_0_SHIFT), 2,
                     R_OUTPUT_XYZW | R_OUTPUT_COLOR_0 | R_OUTPUT_TEX_0);
    R2E_LoadVbpntr(&cs, 3, aos);
    R2E_DrawVbuf(&cs, TRI_CNTL, 30);
    R2Chk_Reset(&st);
    CHECK_INT(CheckBuf(buf, (ULONG) (cs.ptr - buf), 0, 0, &st, &err), R2CE_OK);
}

/* --- The checker refuses -------------------------------------------------------- */

static ULONG good[1024];
static ULONG good_n;

/* Run the good set-up, then words[0..n), and give the checker's verdict. */
static LONG Verdict(const ULONG *words, ULONG n, struct R2ChkError *err)
{
    struct R2ChkCfg cfg;
    struct R2ChkState st;
    LONG e;

    Cfg(&cfg, 1);
    R2Chk_Reset(&st);
    e = R2Chk_Run(&cfg, &st, good, good_n, 0, 0, err);
    if (e != R2CE_OK)
        return 100 + e;
    return R2Chk_Run(&cfg, &st, words, n, 0, good_n, err);
}

#define W(...) do { ULONG w_[] = { __VA_ARGS__ }; \
    e = Verdict(w_, sizeof(w_) / sizeof(w_[0]), &err); } while (0)

static void test_rejects(void)
{
    struct R2Cs cs;
    struct R2ChkError err;
    struct R2Aos aos;
    ULONG one = 1;
    LONG e;
    ULONG fl1;
    ULONG fl0;

    R2Cs_InitBuffer(&cs, good, 1024, 0);
    SetupFrame(&cs);
    good_n = (ULONG) (cs.ptr - good);
    fl1 = R2Cs_FloatBits(1.0f);
    fl0 = R2Cs_FloatBits(0.0f);
    (void) one;

    /* registers that are not ours: display, 2D, CP, memory controller,
     * surfaces, host path, an indirect buffer */
    W(R2_PKT0(R_CRTC_OFFSET, 0), 0);                        CHECK_INT(e, R2CE_REG);
    W(R2_PKT0(R_DP_GUI_MASTER_CNTL, 0), 0);                 CHECK_INT(e, R2CE_REG);
    W(R2_PKT0(R_CP_RB_WPTR, 0), 0);                         CHECK_INT(e, R2CE_REG);
    W(R2_PKT0(R_CP_IB_BASE, 1), RING_GPU, 16);              CHECK_INT(e, R2CE_REG);
    W(R2_PKT0(R_MC_FB_LOCATION, 0), 0);                     CHECK_INT(e, R2CE_REG);
    W(R2_PKT0(R_SURFACE_CNTL, 0), 0);                       CHECK_INT(e, R2CE_REG);
    W(R2_PKT0(R_HOST_PATH_CNTL, 0), 0);                     CHECK_INT(e, R2CE_REG);
    W(R2_PKT0(R_RB2D_DSTCACHE_CTLSTAT, 0), 0xf);            CHECK_INT(e, R2CE_REG);
    /* a run that walks off the list: CBLENDCNTL then 0x3224 */
    W(R2_PKT0(R_RB3D_CBLENDCNTL, 1), 0, 0);                 CHECK_INT(e, R2CE_REG);
    CHECK_HEX(err.what, 0x3224UL);
    CHECK_INT(err.at, good_n);
    /* ONE_REG_WR into a control register */
    W(R2_PKT0_ONE(R_PP_CNTL, 1), 0, 0);                     CHECK_INT(e, R2CE_ONEREG);
    /* opcodes we never send: 2D blits, microcode, the old draw packets */
    W(R2_PKT3(0x9b, 0), 0);                                 CHECK_INT(e, R2CE_OPCODE);
    W(R2_PKT3(0x24, 0), 0);                                 CHECK_INT(e, R2CE_OPCODE);
    W(R2_PKT3(0x29, 0), 0);                                 CHECK_INT(e, R2CE_OPCODE);
    /* truncated, type 1 */
    W(R2_PKT0(R_PP_CNTL, 3), 0, 0);                         CHECK_INT(e, R2CE_TRUNC);
    W(0x40000000UL);                                        CHECK_INT(e, R2CE_TYPE);
    /* immediate draws: size, walk, primitive, TCL flag */
    W(R2_PKT3(R2_OP_3D_DRAW_IMMD_2, 4), TRI_CNTL | R_VF_PRIM_WALK_RING | (3UL << 16),
      fl0, fl0, fl0, 0);                                    CHECK_INT(e, R2CE_SIZE);
    W(R2_PKT3(R2_OP_3D_DRAW_IMMD_2, 12), TRI_CNTL | R_VF_PRIM_WALK_LIST | (3UL << 16),
      0,0,0,0, 0,0,0,0, 0,0,0,0);                           CHECK_INT(e, R2CE_WALK);
    W(R2_PKT3(R2_OP_3D_DRAW_IMMD_2, 16), TRI_CNTL | R_VF_PRIM_WALK_RING | (4UL << 16),
      0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0);                  CHECK_INT(e, R2CE_PRIM);
    W(R2_PKT3(R2_OP_3D_DRAW_IMMD_2, 12), (TRI_CNTL & ~R_VF_PRIM_MASK) | 7UL | R_VF_PRIM_WALK_RING | (3UL << 16),
      0,0,0,0, 0,0,0,0, 0,0,0,0);                           CHECK_INT(e, R2CE_PRIM);
    W(R2_PKT3(R2_OP_3D_DRAW_IMMD_2, 12), R_VF_PRIM_TRIANGLES | R_VF_PRIM_WALK_RING | (3UL << 16),
      0,0,0,0, 0,0,0,0, 0,0,0,0);                           CHECK_INT(e, R2CE_VALUE);
    /* arrays: none loaded, too short, not the vertex size */
    W(R2_PKT3(R2_OP_3D_DRAW_VBUF_2, 0), TRI_CNTL | R_VF_PRIM_WALK_LIST | (3UL << 16));
    CHECK_INT(e, R2CE_ARRAYS);
    W(R2_PKT3(R2_OP_3D_LOAD_VBPNTR, 2), 1, 4 | (4 << 8), VTX_GPU + VTX_SIZE - 64,
      R2_PKT3(R2_OP_3D_DRAW_VBUF_2, 0), TRI_CNTL | R_VF_PRIM_WALK_LIST | (6UL << 16));
    CHECK_INT(e, R2CE_ARRAYS);
    W(R2_PKT3(R2_OP_3D_LOAD_VBPNTR, 2), 1, 3 | (3 << 8), VTX_GPU,
      R2_PKT3(R2_OP_3D_DRAW_VBUF_2, 0), TRI_CNTL | R_VF_PRIM_WALK_LIST | (3UL << 16));
    CHECK_INT(e, R2CE_ARRAYS);
    W(R2_PKT3(R2_OP_3D_LOAD_VBPNTR, 2), 1, 4 | (4 << 8), COLOR_GPU);
    CHECK_INT(e, R2CE_ADDR);
    W(R2_PKT3(R2_OP_3D_LOAD_VBPNTR, 3), 1, 4 | (4 << 8), VTX_GPU, 0);
    CHECK_INT(e, R2CE_SIZE);
    /* index buffers: alone, separated from their draw, wrong control word */
    W(R2_PKT3(R2_OP_INDX_BUFFER, 2), R2_INDX_BUFFER_CNTL, IDX_GPU, 2);
    CHECK_INT(e, R2CE_INDX);
    W(R2_PKT3(R2_OP_3D_DRAW_INDX_2, 0), TRI_CNTL | R_VF_PRIM_WALK_IND | (3UL << 16),
      R2_PKT0(R_PP_CNTL, 0), 0);
    CHECK_INT(e, R2CE_INDX);
    W(R2_PKT3(R2_OP_3D_DRAW_INDX_2, 0), TRI_CNTL | R_VF_PRIM_WALK_IND | (3UL << 16), R2_PKT2);
    CHECK_INT(e, R2CE_INDX);
    W(R2_PKT3(R2_OP_3D_DRAW_INDX_2, 0), TRI_CNTL | R_VF_PRIM_WALK_IND | (3UL << 16),
      R2_PKT3(R2_OP_INDX_BUFFER, 2), 0x80000800UL, IDX_GPU, 2);
    CHECK_INT(e, R2CE_INDX);
    W(R2_PKT3(R2_OP_3D_DRAW_INDX_2, 0), TRI_CNTL | R_VF_PRIM_WALK_IND | (3UL << 16),
      R2_PKT3(R2_OP_INDX_BUFFER, 2), R2_INDX_BUFFER_CNTL, IDX_GPU, 1);
    CHECK_INT(e, R2CE_SIZE);
    W(R2_PKT3(R2_OP_3D_DRAW_INDX_2, 0), TRI_CNTL | R_VF_PRIM_WALK_IND | (3UL << 16),
      R2_PKT3(R2_OP_INDX_BUFFER, 2), R2_INDX_BUFFER_CNTL, TEX_GPU, 2);
    CHECK_INT(e, R2CE_ADDR);
    /* targets: outside, misaligned, too small for the scissor */
    W(R2_PKT0(R_RB3D_COLOROFFSET, 0), 0x00f00000UL);        CHECK_INT(e, R2CE_ADDR);
    W(R2_PKT0(R_RB3D_COLOROFFSET, 0), COLOR_GPU + 8);       CHECK_INT(e, R2CE_ALIGN);
    W(R2_PKT0(R_RB3D_DEPTHOFFSET, 0), COLOR_GPU);           CHECK_INT(e, R2CE_ADDR);
    W(R2_PKT0(R_RB3D_COLOROFFSET, 0), COLOR_GPU + 0x20000,
      R2_PKT3(R2_OP_3D_DRAW_IMMD_2, 12), TRI_CNTL | R_VF_PRIM_WALK_RING | (3UL << 16),
      0,0,0,0, 0,0,0,0, 0,0,0,0);                           CHECK_INT(e, R2CE_TARGET);
    W(R2_PKT0(R_RB3D_COLORPITCH, 0), 640 | R_COLOR_TILE_ENABLE,
      R2_PKT0(R_RB3D_COLOROFFSET, 0), COLOR_GPU + 0x10,
      R2_PKT3(R2_OP_3D_DRAW_IMMD_2, 12), TRI_CNTL | R_VF_PRIM_WALK_RING | (3UL << 16),
      0,0,0,0, 0,0,0,0, 0,0,0,0);                           CHECK_INT(e, R2CE_ALIGN);
    W(R2_PKT0(R_RB3D_CNTL, 0), R_COLOR_FORMAT_ARGB8888 | R_DEPTHXY_OFFSET_ENABLE);
    CHECK_INT(e, R2CE_VALUE);
    W(R2_PKT0(R_RB3D_CNTL, 0), 8UL << R_COLOR_FORMAT_SHIFT);  CHECK_INT(e, R2CE_FORMAT);
    /* textures: enabled without an address, too big for texture memory,
     * an address in colour memory */
    W(R2_PKT0(R_PP_CNTL, 0), R_TEX_0_ENABLE | R_TEX_BLEND_0_ENABLE,
      R2_PKT3(R2_OP_3D_DRAW_IMMD_2, 12), TRI_CNTL | R_VF_PRIM_WALK_RING | (3UL << 16),
      0,0,0,0, 0,0,0,0, 0,0,0,0);                           CHECK_INT(e, R2CE_TEXTURE);
    W(R2_PKT0(R_PP_TXFORMAT_0, 0), R_TXFORMAT_ARGB8888 | (8UL << 8) | (8UL << 12),
      R2_PKT0(R_PP_TXOFFSET_0, 0), TEX_GPU + TEX_SIZE - 0x40000,
      R2_PKT0(R_PP_CNTL, 0), R_TEX_0_ENABLE | R_TEX_BLEND_0_ENABLE,
      R2_PKT3(R2_OP_3D_DRAW_IMMD_2, 12), TRI_CNTL | R_VF_PRIM_WALK_RING | (3UL << 16),
      0,0,0,0, 0,0,0,0, 0,0,0,0);                           CHECK_INT(e, R2CE_OK);
    W(R2_PKT0(R_PP_TXFILTER_0, 0), 1UL << R_MAX_MIP_LEVEL_SHIFT,
      R2_PKT0(R_PP_TXFORMAT_0, 0), R_TXFORMAT_ARGB8888 | (8UL << 8) | (8UL << 12),
      R2_PKT0(R_PP_TXOFFSET_0, 0), TEX_GPU + TEX_SIZE - 0x40000,
      R2_PKT0(R_PP_CNTL, 0), R_TEX_0_ENABLE | R_TEX_BLEND_0_ENABLE,
      R2_PKT3(R2_OP_3D_DRAW_IMMD_2, 12), TRI_CNTL | R_VF_PRIM_WALK_RING | (3UL << 16),
      0,0,0,0, 0,0,0,0, 0,0,0,0);                           CHECK_INT(e, R2CE_TEXTURE);
    W(R2_PKT0(R_PP_TXOFFSET_0 + R_PP_TXOFFSET_STRIDE, 0), COLOR_GPU);
    CHECK_INT(e, R2CE_ADDR);
    /* TCL memory: past the end, an odd count, a stride we do not use */
    W(R2_PKT0(R_SE_TCL_VECTOR_INDX_REG, 0), 0x1bfUL | (1UL << 16),
      R2_PKT0_ONE(R_SE_TCL_VECTOR_DATA_REG, 7), fl1,fl1,fl1,fl1, fl1,fl1,fl1,fl1);
    CHECK_INT(e, R2CE_TCLMEM);
    W(R2_PKT0(R_SE_TCL_VECTOR_INDX_REG, 0), 0x88UL | (1UL << 16),
      R2_PKT0_ONE(R_SE_TCL_VECTOR_DATA_REG, 2), fl1, fl1, fl1);
    CHECK_INT(e, R2CE_TCLMEM);
    W(R2_PKT0(R_SE_TCL_VECTOR_INDX_REG, 0), 0x88UL | (2UL << 16));
    CHECK_INT(e, R2CE_VALUE);
    /* scratch registers: only the fence's */
    W(R2_PKT0(R_SCRATCH_REG0, 0), 1);                       CHECK_INT(e, R2CE_FENCE);
    W(R2_PKT0(FENCE_REG, 0), 1);                            CHECK_INT(e, R2CE_OK);
    /* a draw before any vertex format: a fresh state */
    {
        struct R2ChkCfg cfg;
        struct R2ChkState st;
        static const ULONG w[] = { R2_PKT3(R2_OP_3D_DRAW_IMMD_2, 12),
                                   TRI_CNTL | R_VF_PRIM_WALK_RING | (3UL << 16),
                                   0,0,0,0, 0,0,0,0, 0,0,0,0 };
        Cfg(&cfg, 0);
        R2Chk_Reset(&st);
        CHECK_INT(R2Chk_Run(&cfg, &st, w, 14, 0, 0, &err), R2CE_FORMAT);
    }
    (void) aos;
}

/* --- The ring against a simulated CP -------------------------------------------- */

#define RING_DW 4096

static ULONG ring_mem[RING_DW];
static ULONG ring_shadow[RING_DW];

struct RingRun
{
    struct SimCp sim;
    struct R2Ring ring;
    ULONG emitted;          /* dwords handed to the emitter (excluding filler) */
};

static void RingSetup(struct RingRun *rr, ULONG swap, ULONG rate, ULONG flags, ULONG kick)
{
    struct R2ChkCfg cfg;

    Cfg(&cfg, 1);
    SimCp_Init(&rr->sim, ring_mem, ring_shadow, RING_DW, swap, FENCE_REG, 0x7ffffff0UL, rate, &cfg);
    CHECK_INT(R2Ring_Init(&rr->ring, ring_mem, RING_GPU, RING_DW, &rr->sim.hw, swap,
                          FENCE_REG, 100000), R2_OK);
    rr->ring.flags = flags;
    rr->ring.kick = kick;
    rr->emitted = 0;
}

/* A random immediate primitive of 1..300 triangles' worth of vertices. */
static LONG RandomPrim(struct R2Cs *cs)
{
    ULONG n;
    ULONG *v;
    ULONG i;

    n = 3 * (1 + Rand() % 100);
    v = R2E_ImmdOpen(cs, 4, n);
    if (v == NULL)
        return cs->error;
    for (i = 0; i < n; i++)
    {
        v[i * 4 + 0] = R2_OUT(cs, R2Cs_FloatBits((float) (i % 7) * 0.1f));
        v[i * 4 + 1] = R2_OUT(cs, R2Cs_FloatBits((float) (i % 5) * 0.1f));
        v[i * 4 + 2] = R2_OUT(cs, R2Cs_FloatBits(0.5f));
        v[i * 4 + 3] = R2_OUT(cs, 0xffffffffUL);
    }
    return R2E_ImmdClose(cs, v + 4 * n, TRI_CNTL);
}

static void test_ring_basic(void)
{
    struct RingRun rr;
    ULONG seq;

    RingSetup(&rr, 1, 1000000, 0, 0);
    CHECK_INT(SetupFrame(&rr.ring.cs), R2_OK);
    CHECK_INT(Triangle(&rr.ring.cs, 0.25f), R2_OK);
    CHECK_INT(R2Ring_Fence(&rr.ring, &seq), R2_OK);
    CHECK(!R2Ring_FenceDone(&rr.ring, seq));
    CHECK_INT(R2Ring_Commit(&rr.ring), R2_OK);
    CHECK_INT(rr.ring.committed & 15, 0);
    CHECK_INT(R2Ring_WaitFence(&rr.ring, seq), R2_OK);
    CHECK(R2Ring_FenceDone(&rr.ring, seq));
    CHECK_INT(rr.sim.fence_val, seq);
    CHECK_INT(rr.sim.rptr, rr.ring.committed);
    CHECK(SimCp_Clean(&rr.sim));
    CHECK_INT(rr.sim.st.draws, 1);
    /* the fence baseline was the register's old value, wrapping past 2^31 */
    CHECK_HEX(seq, 0x7ffffff1UL);
    /* a commit with nothing new writes nothing */
    CHECK_INT(R2Ring_Commit(&rr.ring), R2_OK);
    CHECK_INT(rr.sim.wptr_writes, 1);
}

static void RingStress(ULONG swap, ULONG rate, ULONG flags, ULONG kick, const char *name)
{
    struct RingRun rr;
    ULONG frame;
    ULONG p;
    ULONG seq;
    ULONG last;
    LONG e;
    int clean;

    RingSetup(&rr, swap, rate, flags, kick);
    e = R2_OK;
    last = 0;
    for (frame = 0; frame < 120 && e == R2_OK; frame++)
    {
        e = SetupFrame(&rr.ring.cs);
        for (p = 0; p < 6 && e == R2_OK; p++)
            e = RandomPrim(&rr.ring.cs);
        if (e == R2_OK)
            e = R2Ring_Fence(&rr.ring, &seq);
        if (e == R2_OK && frame % 4 == 3)
            e = R2Ring_WaitFence(&rr.ring, seq);       /* a frame-paced client */
        last = seq;
    }
    CHECK_INT(e, R2_OK);
    if (e != R2_OK)
        printf("  %s: error %ld in frame %lu: w %lu committed %lu rptr %lu, sim rptr %lu wptr %lu,"
               " fence %lx seq %lx\n", name, (long) e, (unsigned long) frame,
               (unsigned long) (rr.ring.cs.ptr - rr.ring.base), (unsigned long) rr.ring.committed,
               (unsigned long) rr.ring.rptr, (unsigned long) rr.sim.rptr, (unsigned long) rr.sim.wptr,
               (unsigned long) rr.sim.fence_val, (unsigned long) seq);
    CHECK_INT(R2Ring_WaitFence(&rr.ring, last), R2_OK);
    SimCp_Drain(&rr.sim);
    clean = SimCp_Clean(&rr.sim);
    CHECK(clean);
    CHECK_INT(rr.sim.rptr, rr.ring.committed);
    CHECK_INT(rr.sim.fence_val, last);
    CHECK_INT(rr.sim.st.draws, 120 * 6);
    CHECK(rr.ring.stats.wraps > 0);
    if (flags & R2RING_TYPE2_FILL)
        CHECK_INT(rr.sim.st.dw_nop, rr.ring.stats.fill_dw + rr.ring.stats.pad_dw);
    if (kick != 0)
        CHECK(rr.ring.stats.commits > rr.sim.consumed / (kick + RING_DW / 2));
    printf("  %s: %lu dwords through a %u-dword ring, %lu commits, %lu wraps (%lu filler), "
           "%lu pad, %lu RPTR reads, %lu stalls\n", name,
           (unsigned long) rr.sim.consumed, RING_DW, (unsigned long) rr.ring.stats.commits,
           (unsigned long) rr.ring.stats.wraps, (unsigned long) rr.ring.stats.fill_dw,
           (unsigned long) rr.ring.stats.pad_dw, (unsigned long) rr.ring.stats.rptr_reads,
           (unsigned long) rr.ring.stats.stalls);
}

/* Regression (2026-11-02): 2900 dwords pending, a request for 1190. It
 * fits before the ring end (1196) only until the commit that makes room
 * pads the writer to 2912 -- then it must wrap. The first writer kept
 * waiting for room the end could not give and timed out. */
static void test_ring_pad_moves_writer(void)
{
    struct RingRun rr;
    ULONG *p;

    RingSetup(&rr, 1, 1000000, 0, 0);
    CHECK_INT(R2E_Nop(&rr.ring.cs, 1450, 1), R2_OK);
    CHECK_INT(R2E_Nop(&rr.ring.cs, 1450, 1), R2_OK);
    p = R2Cs_Room(&rr.ring.cs, 1190);
    CHECK(p == rr.ring.base);               /* wrapped, after the padding */
    CHECK_INT(rr.ring.stats.pad_dw, 12);
    CHECK_INT(rr.ring.stats.wraps, 1);
    CHECK_INT(rr.ring.stats.fill_dw, 4096 - 2912);
    CHECK_INT(R2E_Nop(&rr.ring.cs, 1190, 1), R2_OK);
    CHECK_INT(R2Ring_Idle(&rr.ring), R2_OK);
    CHECK(SimCp_Clean(&rr.sim));
}

static void test_ring_hang(void)
{
    struct RingRun rr;
    ULONG i;
    LONG e;
    ULONG seq;

    /* a GPU that never reads: the writer must give up, not spin */
    RingSetup(&rr, 0, 0, 0, 0);
    e = R2_OK;
    for (i = 0; i < 100 && e == R2_OK; i++)
        e = RandomPrim(&rr.ring.cs);
    CHECK_INT(e, R2_ETIMEOUT);
    CHECK_INT(rr.ring.cs.error, R2_ETIMEOUT);
    CHECK(rr.sim.clock < 400000);           /* gave up after about the timeout */
    CHECK(SimCp_Clean(&rr.sim));

    RingSetup(&rr, 0, 0, 0, 0);
    R2Ring_Fence(&rr.ring, &seq);
    CHECK_INT(R2Ring_WaitFence(&rr.ring, seq), R2_ETIMEOUT);
}

static void test_ring_errors(void)
{
    struct RingRun rr;
    ULONG *v;

    RingSetup(&rr, 0, 1000, 0, 0);
    v = R2E_ImmdOpen(&rr.ring.cs, 4, 3);
    CHECK(v != NULL);
    CHECK_INT(R2Ring_Commit(&rr.ring), R2_ESTATE);
    CHECK_INT(R2E_ImmdClose(&rr.ring.cs, v, TRI_CNTL), R2_OK);
    CHECK_INT(R2Ring_Commit(&rr.ring), R2_OK);
    RingSetup(&rr, 0, 1000, 0, 0);
    CHECK(R2Cs_Room(&rr.ring.cs, RING_DW / 2 + 1) == NULL);
    CHECK_INT(rr.ring.cs.error, R2_ETOOBIG);
    CHECK_INT(R2Ring_Init(&rr.ring, ring_mem, RING_GPU + 0x800, RING_DW, &rr.sim.hw, 0, FENCE_REG, 1),
              R2_EARG);
    CHECK_INT(R2Ring_Init(&rr.ring, ring_mem, RING_GPU, 3000, &rr.sim.hw, 0, FENCE_REG, 1),
              R2_EARG);
}

/* --- What packets cost on the bus (feeds tools/census.py) ----------------------- */

static ULONG Size(LONG (*fn)(struct R2Cs *))
{
    ULONG buf[256];
    struct R2Cs cs;

    R2Cs_InitBuffer(&cs, buf, 256, 0);
    fn(&cs);
    return (ULONG) (cs.ptr - buf);
}

static LONG OneState(struct R2Cs *cs) { return R2E_Reg(cs, R_PP_CNTL, 0); }
static LONG OneMatrix(struct R2Cs *cs) { return R2E_Matrix(cs, R_VS_MATRIX_2_MVP, identity); }
static LONG OneFence(struct R2Cs *cs) { return R2E_Fence(cs, FENCE_REG, 1); }
static LONG OneTri(struct R2Cs *cs) { return Triangle(cs, 0); }
static LONG OneBind(struct R2Cs *cs)
{
    struct R2Tex t;

    memset(&t, 0, sizeof(t));
    t.offset = TEX_GPU;
    return R2E_Texture(cs, 0, &t);
}
static LONG OneIndexed(struct R2Cs *cs)
{
    struct R2Aos aos[3];

    memset(aos, 0, sizeof(aos));
    R2E_LoadVbpntr(cs, 3, aos);
    return R2E_DrawIndexed(cs, TRI_CNTL, 6, IDX_GPU, 6);
}

static void test_sizes(void)
{
    CHECK_INT(Size(OneState), 2);
    CHECK_INT(Size(OneMatrix), 21);
    CHECK_INT(Size(OneFence), R2E_FENCE_DWORDS);
    CHECK_INT(Size(OneTri), 2 + 3 * 4);
    CHECK_INT(Size(OneBind), 10);
    CHECK_INT(Size(OneIndexed), 7 + 8);
    printf("  bytes on the bus: state call %lu, matrix %lu, texture bind %lu, immediate header %lu,"
           " 3-array indexed draw %lu (+ 2 an index), fence %lu\n",
           (unsigned long) Size(OneState) * 4, (unsigned long) Size(OneMatrix) * 4,
           (unsigned long) Size(OneBind) * 4, 8UL,
           (unsigned long) Size(OneIndexed) * 4, (unsigned long) Size(OneFence) * 4);
}

/* --- CP start and stop (r2cp.c) ----------------------------------------------- */

/* A register file with the microcode RAM behind CP_ME_RAM_*, an engine that
 * is idle or stuck busy, a microsecond clock that advances per call, and a
 * log of every write. */
#define CPLOG 2048
static struct
{
    ULONG reg[0x10000 / 4];
    ULONG me[R2CP_UCODE_DWORDS];
    ULONG me_addr;
    ULONG me_raddr;
    ULONG busy;
    ULONG clock;
    ULONG log_reg[CPLOG], log_val[CPLOG];
    ULONG nlog;
} cpf;

static ULONG CpfRead(APTR hw, ULONG reg)
{
    (void) hw;
    if (reg == R_RBBM_STATUS)
        return cpf.busy ? (R_RBBM_ACTIVE | 12) : 64;
    if (reg == R_CP_ME_RAM_DATAH)
        return cpf.me[2 * (cpf.me_raddr & 0xff)];
    if (reg == R_CP_ME_RAM_DATAL)
        return cpf.me[2 * (cpf.me_raddr & 0xff) + 1];
    return cpf.reg[reg / 4];
}

static void CpfWrite(APTR hw, ULONG reg, ULONG val)
{
    (void) hw;
    if (cpf.nlog < CPLOG)
    {
        cpf.log_reg[cpf.nlog] = reg;
        cpf.log_val[cpf.nlog] = val;
        cpf.nlog++;
    }
    if (reg == R_CP_ME_RAM_ADDR)
        cpf.me_addr = val;
    else if (reg == R_CP_ME_RAM_RADDR)
        cpf.me_raddr = val;
    else if (reg == R_CP_ME_RAM_DATAH)
        cpf.me[2 * (cpf.me_addr & 0xff)] = val;
    else if (reg == R_CP_ME_RAM_DATAL)
        cpf.me[2 * (cpf.me_addr++ & 0xff) + 1] = val;     /* DATAL steps the address */
    else
        cpf.reg[reg / 4] = val;
}

static ULONG CpfClock(APTR hw)
{
    (void) hw;
    return cpf.clock++;
}

static ULONG CpfReadBack(APTR hw, volatile ULONG *p)
{
    (void) hw;
    return *p;
}

static const struct R2Hw cpf_hw = { CpfRead, CpfWrite, CpfReadBack, CpfClock, NULL };

static void CpfReset(void)
{
    memset(&cpf, 0, sizeof(cpf));
}

static void test_cp(void)
{
    static const ULONG want_start[][2] =
    {
        { R_CP_RB_CNTL,       0x0004090BUL | R_RB_NO_UPDATE },
        { R_CP_RB_BASE,       0x00123000UL },
        { R_CP_RB_CNTL,       0x0004090BUL | R_RB_RPTR_WR_ENA | R_RB_NO_UPDATE },
        { R_CP_RB_RPTR_WR,    0 },
        { R_CP_RB_WPTR,       0 },
        { R_CP_RB_RPTR_ADDR,  0 },
        { R_SCRATCH_UMSK,     0 },
        { R_CP_RB_CNTL,       0x0004090BUL | R_RB_NO_UPDATE },
        { R_CP_RB_WPTR_DELAY, 0 },
        { R_CP_CSQ_MODE,      0x00004D4DUL },
        { R_CP_CSQ_CNTL,      R_CSQ_PRIBM_INDBM }
    };
    struct R2CpSaved sv;
    ULONG reset;
    ULONG i;
    ULONG same;

    /* CP_RB_CNTL: 4096 dwords = 2^(11 + 1), 512-byte blocks, MAX_FETCH 1 */
    CHECK_HEX(R2Cp_RbCntl(4096, 0), 0x0004090BUL);
    CHECK_HEX(R2Cp_RbCntl(4096, R2CP_GPU_SWAP), 0x0004090BUL | R_BUF_SWAP_32BIT);
    CHECK_HEX(R2Cp_RbCntl(1024, 0) & R_RB_BUFSZ_MASK, 9);
    CHECK_HEX(R2Cp_RbCntl(1000, 0), 0);
    CHECK_HEX(R2Cp_RbCntl(512, 0), 0);
    CHECK_HEX(R2Cp_RbCntl(3072, 0), 0);

    /* the microcode lands word for word, high half first */
    CpfReset();
    R2Cp_LoadMicrocode(&cpf_hw, R2Cp_Microcode);
    CHECK_INT(cpf.nlog, 1 + R2CP_UCODE_DWORDS);
    CHECK_HEX(cpf.log_reg[0], R_CP_ME_RAM_ADDR);
    CHECK_HEX(cpf.log_reg[1], R_CP_ME_RAM_DATAH);
    CHECK_HEX(cpf.log_reg[2], R_CP_ME_RAM_DATAL);
    CHECK_HEX(cpf.log_val[1], 0x00000000UL);      /* word 0: (0, 0x21007000) */
    CHECK_HEX(cpf.log_val[2], 0x21007000UL);
    CHECK(memcmp(cpf.me, R2Cp_Microcode, sizeof(cpf.me)) == 0);
    CHECK_INT(R2Cp_CheckMicrocode(&cpf_hw, R2Cp_Microcode), 0);
    cpf.me[2 * 17 + 1] ^= 1;
    cpf.me[2 * 200] ^= 0x100;
    CHECK_INT(R2Cp_CheckMicrocode(&cpf_hw, R2Cp_Microcode), 2);

    /* start: Linux r100_cp_init's writes, in its order */
    CpfReset();
    CHECK_INT(R2Cp_Start(&cpf_hw, 0x00123000UL, 4096, 0), R2_OK);
    CHECK_INT(cpf.nlog, sizeof(want_start) / sizeof(want_start[0]));
    same = 1;
    for (i = 0; i < cpf.nlog && i < sizeof(want_start) / sizeof(want_start[0]); i++)
        if (cpf.log_reg[i] != want_start[i][0] || cpf.log_val[i] != want_start[i][1])
        {
            printf("  start write %lu: reg 0x%04lx = 0x%08lx, want 0x%04lx = 0x%08lx\n",
                   (unsigned long) i, (unsigned long) cpf.log_reg[i],
                   (unsigned long) cpf.log_val[i], (unsigned long) want_start[i][0],
                   (unsigned long) want_start[i][1]);
            same = 0;
        }
    CHECK(same);
    CHECK(cpf.clock >= 10);                      /* the 10 us before the CSQ */
    CpfReset();
    CHECK_INT(R2Cp_Start(&cpf_hw, 0x00123800UL, 4096, 0), R2_EARG);    /* not 4 KB aligned */
    CHECK_INT(R2Cp_Start(&cpf_hw, 0x00123000UL, 3000, 0), R2_EARG);
    CHECK_INT(cpf.nlog, 0);                      /* refused before any write */

    /* save, in use, stop with an idle engine: everything as found */
    CpfReset();
    cpf.reg[R_CP_CSQ_CNTL / 4] = 0x02010080UL;   /* A4000D: mode 0, queue status bits */
    cpf.reg[R_CP_CSQ_MODE / 4] = 0x00004D4DUL;
    cpf.reg[R_CP_RB_BASE / 4] = 0xCDCDCDCCUL;
    cpf.reg[R_CP_RB_CNTL / 4] = 0;
    R2Cp_Save(&cpf_hw, &sv);
    CHECK(!R2Cp_InUse(&sv));
    CHECK_INT(R2Cp_Start(&cpf_hw, 0x00123000UL, 4096, R2CP_GPU_SWAP), R2_OK);
    CHECK_HEX(cpf.reg[R_CP_RB_CNTL / 4], 0x0004090BUL | R_BUF_SWAP_32BIT | R_RB_NO_UPDATE);
    CHECK_INT(R2Cp_Stop(&cpf_hw, &sv, 1000, &reset), R2_OK);
    CHECK_INT(reset, 0);
    CHECK_HEX(cpf.reg[R_CP_CSQ_CNTL / 4], 0);                /* mode 0, no status bits written */
    CHECK_HEX(cpf.reg[R_CP_CSQ_MODE / 4], 0x00004D4DUL);
    CHECK_HEX(cpf.reg[R_CP_RB_BASE / 4], 0xCDCDCDCCUL);
    CHECK_HEX(cpf.reg[R_CP_RB_CNTL / 4], 0);
    CHECK_HEX(cpf.reg[R_RBBM_SOFT_RESET / 4], 0);
    for (i = 0, same = 0; i < cpf.nlog; i++)
        if (cpf.log_reg[i] == R_RBBM_SOFT_RESET)
            same++;
    CHECK_INT(same, 0);                                       /* no reset when idle */
    sv.csq_cntl = R_CSQ_PRIBM_INDBM;
    CHECK(R2Cp_InUse(&sv));

    /* a CP left running by a crashed run: stopped, and stays stopped */
    CpfReset();
    cpf.reg[R_CP_CSQ_CNTL / 4] = R_CSQ_PRIBM_INDBM | 0x00010080UL;
    cpf.reg[R_CP_CSQ_MODE / 4] = 0x00004D4DUL;
    cpf.reg[R_CP_RB_BASE / 4] = 0x00456000UL;
    R2Cp_Save(&cpf_hw, &sv);
    CHECK(R2Cp_InUse(&sv));
    CHECK_INT(R2Cp_Reclaim(&cpf_hw, &sv, 1000, &reset), R2_OK);
    CHECK_INT(reset, 0);
    CHECK(!R2Cp_InUse(&sv));
    CHECK_HEX(cpf.reg[R_CP_CSQ_CNTL / 4], 0);
    CHECK_INT(R2Cp_Start(&cpf_hw, 0x00123000UL, 4096, 0), R2_OK);
    CHECK_INT(R2Cp_Stop(&cpf_hw, &sv, 1000, &reset), R2_OK);
    CHECK_HEX(cpf.reg[R_CP_CSQ_CNTL / 4], 0);                /* stopped at the end too */
    CHECK_HEX(cpf.reg[R_CP_RB_BASE / 4], 0x00456000UL);
    CpfReset();
    cpf.reg[R_CP_CSQ_CNTL / 4] = R_CSQ_PRIBM_INDBM;
    R2Cp_Save(&cpf_hw, &sv);
    cpf.busy = 1;
    CHECK_INT(R2Cp_Reclaim(&cpf_hw, &sv, 500, &reset), R2_ETIMEOUT);
    CHECK_INT(reset, 1);
    CHECK_HEX(cpf.reg[R_CP_CSQ_CNTL / 4], 0);
    cpf.busy = 0;

    /* stop with the engine stuck busy: bounded wait, then the CP alone is reset */
    CpfReset();
    R2Cp_Save(&cpf_hw, &sv);
    cpf.busy = 1;
    CHECK_INT(R2Cp_Stop(&cpf_hw, &sv, 500, &reset), R2_ETIMEOUT);
    CHECK_INT(reset, 1);
    CHECK(cpf.clock > 500 && cpf.clock < 1000);
    for (i = 0, same = 0; i < cpf.nlog; i++)
        if (cpf.log_reg[i] == R_RBBM_SOFT_RESET)
        {
            CHECK(cpf.log_val[i] == R_SOFT_RESET_CP || cpf.log_val[i] == 0);
            same++;
        }
    CHECK_INT(same, 2);                                       /* set, then cleared */
    CHECK_HEX(cpf.reg[R_RBBM_SOFT_RESET / 4], 0);
    CHECK_INT(R2Cp_WaitIdle(&cpf_hw, 300), R2_ETIMEOUT);
    cpf.busy = 0;
    CHECK_INT(R2Cp_WaitIdle(&cpf_hw, 300), R2_OK);
}

int main(void)
{
    test_headers();
    test_swap();
    test_vtx_sizes();
    test_frame(0);
    test_frame(1);
    test_mesa_blit();
    test_nop();
    test_immd();
    test_arrays();
    test_rejects();
    test_ring_basic();
    RingStress(1, 97, 0, 0, "swap, NOP filler, slow GPU");
    RingStress(0, 97, R2RING_TYPE2_FILL, 0, "no swap, PACKET2 filler, slow GPU");
    RingStress(1, 300, 0, 512, "swap, kick 512");
    RingStress(1, 5000, 0, 0, "swap, fast GPU");
    test_ring_pad_moves_writer();
    test_ring_hang();
    test_ring_errors();
    test_sizes();
    test_cp();
    return tk_done("test_r200");
}
