/*
 * r2emit.c -- see r2emit.h.
 */

#include "r2emit.h"
#include "r2pm4.h"
#include "r200reg.h"

#define ONE_F 0x3f800000UL      /* 1.0f */

/* The largest payload a packet carries. */
#define MAX_PAYLOAD (R2_PKT_MAX_COUNT + 1)

static LONG Fail(struct R2Cs *cs, LONG err)
{
    if (cs->error == R2_OK)
        cs->error = err;
    return cs->error;
}

LONG R2E_Reg(struct R2Cs *cs, ULONG reg, ULONG val)
{
    ULONG *p;

    p = R2Cs_Room(cs, 2);
    if (p == NULL)
        return cs->error;
    p[0] = R2_OUT(cs, R2_PKT0(reg, 0));
    p[1] = R2_OUT(cs, val);
    cs->ptr = p + 2;
    return R2_OK;
}

static LONG RegsPkt(struct R2Cs *cs, ULONG header, ULONG n, const ULONG *vals)
{
    ULONG *p;
    ULONG i;

    if (n == 0 || n > MAX_PAYLOAD)
        return Fail(cs, R2_EARG);
    p = R2Cs_Room(cs, n + 1);
    if (p == NULL)
        return cs->error;
    p[0] = R2_OUT(cs, header);
    for (i = 0; i < n; i++)
        p[1 + i] = R2_OUT(cs, vals[i]);
    cs->ptr = p + n + 1;
    return R2_OK;
}

LONG R2E_Regs(struct R2Cs *cs, ULONG reg, ULONG n, const ULONG *vals)
{
    if (n == 0)
        return Fail(cs, R2_EARG);
    return RegsPkt(cs, R2_PKT0(reg, n - 1), n, vals);
}

LONG R2E_RegOne(struct R2Cs *cs, ULONG reg, ULONG n, const ULONG *vals)
{
    if (n == 0)
        return Fail(cs, R2_EARG);
    return RegsPkt(cs, R2_PKT0_ONE(reg, n - 1), n, vals);
}

LONG R2E_Nop(struct R2Cs *cs, ULONG ndw, ULONG skip_ok)
{
    ULONG *p;
    ULONG i;
    ULONG chunk;

    if (ndw == 0)
        return R2_OK;
    p = R2Cs_Room(cs, ndw);
    if (p == NULL)
        return cs->error;
    if (!skip_ok || ndw == 1)
    {
        for (i = 0; i < ndw; i++)
            p[i] = R2_OUT(cs, R2_PKT2);
        cs->ptr = p + ndw;
        return R2_OK;
    }
    /* NOP packets of at most 1 + MAX_PAYLOAD dwords; a remainder of one
     * dword is folded into the packet before it. */
    while (ndw > 0)
    {
        chunk = ndw;
        if (chunk > 1 + MAX_PAYLOAD)
        {
            chunk = 1 + MAX_PAYLOAD;
            if (ndw - chunk == 1)
                chunk--;
        }
        if (chunk == 1)
            p[0] = R2_OUT(cs, R2_PKT2);
        else
            p[0] = R2_OUT(cs, R2_PKT3(R2_OP_NOP, chunk - 2));
        p += chunk;
        ndw -= chunk;
    }
    cs->ptr = p;
    return R2_OK;
}

LONG R2E_Vectors(struct R2Cs *cs, ULONG addr, ULONG n, const ULONG *vals)
{
    ULONG *p;
    ULONG i;

    if (n == 0 || (n & 3) != 0 || n > MAX_PAYLOAD)
        return Fail(cs, R2_EARG);
    p = R2Cs_Room(cs, 5 + n);
    if (p == NULL)
        return cs->error;
    p[0] = R2_OUT(cs, R2_PKT0(R_SE_TCL_STATE_FLUSH, 0));
    p[1] = R2_OUT(cs, 0);
    p[2] = R2_OUT(cs, R2_PKT0(R_SE_TCL_VECTOR_INDX_REG, 0));
    p[3] = R2_OUT(cs, addr | (1UL << R_VEC_INDX_OCTWORD_STRIDE_SHIFT));
    p[4] = R2_OUT(cs, R2_PKT0_ONE(R_SE_TCL_VECTOR_DATA_REG, n - 1));
    for (i = 0; i < n; i++)
        p[5 + i] = R2_OUT(cs, vals[i]);
    cs->ptr = p + 5 + n;
    return R2_OK;
}

LONG R2E_Scalars(struct R2Cs *cs, ULONG addr, ULONG n, const ULONG *vals)
{
    ULONG *p;
    ULONG i;

    if (n == 0 || n > MAX_PAYLOAD)
        return Fail(cs, R2_EARG);
    p = R2Cs_Room(cs, 3 + n);
    if (p == NULL)
        return cs->error;
    p[0] = R2_OUT(cs, R2_PKT0(R_SE_TCL_SCALAR_INDX_REG, 0));
    p[1] = R2_OUT(cs, addr | (1UL << R_SCAL_INDX_DWORD_STRIDE_SHIFT));
    p[2] = R2_OUT(cs, R2_PKT0_ONE(R_SE_TCL_SCALAR_DATA_REG, n - 1));
    for (i = 0; i < n; i++)
        p[3 + i] = R2_OUT(cs, vals[i]);
    cs->ptr = p + 3 + n;
    return R2_OK;
}

LONG R2E_Matrix(struct R2Cs *cs, ULONG vec, const float *m)
{
    ULONG rows[16];
    ULONG i;

    for (i = 0; i < 4; i++)
    {
        rows[i * 4 + 0] = R2Cs_FloatBits(m[i]);
        rows[i * 4 + 1] = R2Cs_FloatBits(m[i + 4]);
        rows[i * 4 + 2] = R2Cs_FloatBits(m[i + 8]);
        rows[i * 4 + 3] = R2Cs_FloatBits(m[i + 12]);
    }
    return R2E_Vectors(cs, vec, 16, rows);
}

LONG R2E_TclDefaults(struct R2Cs *cs)
{
    ULONG v[6];

    R2E_Reg(cs, R_SE_VAP_CNTL_STATUS, cs->swap ? R_VC_NO_SWAP : R_VC_32BIT_SWAP);
    R2E_Reg(cs, R_SE_VAP_CNTL, R_VAP_TCL_ENABLE | (9UL << R_VAP_VF_MAX_VTX_NUM_SHIFT));
    R2E_Reg(cs, R_SE_VTE_CNTL, R_VPORT_X_SCALE_ENA | R_VPORT_X_OFFSET_ENA
                             | R_VPORT_Y_SCALE_ENA | R_VPORT_Y_OFFSET_ENA
                             | R_VPORT_Z_SCALE_ENA | R_VPORT_Z_OFFSET_ENA
                             | R_VTX_W0_FMT);
    R2E_Reg(cs, R_SE_VTX_STATE_CNTL, R_VSC_UPDATE_USER_COLOR_0_ENABLE);

    /* SE_TCL_MATRIX_SEL_0..4 */
    v[0] = R_MTX_MV;
    v[1] = R_MTX_IMV;
    v[2] = R_MTX_MVP;
    v[3] = R_MTX_TEX0 | (R_MTX_TEX1 << 8) | (R_MTX_TEX2 << 16) | (R_MTX_TEX3 << 24);
    v[4] = R_MTX_TEX4 | (R_MTX_TEX5 << 8);
    R2E_Regs(cs, R_SE_TCL_MATRIX_SEL_0, 5, v);

    /* SE_TCL_INPUT_VTX_VECTOR_ADDR_0..3: position 0, colours 2/3,
     * texture coordinates 6..11 */
    v[0] = 0x00;
    v[1] = 0x02 | (0x03UL << 8);
    v[2] = 0x06 | (0x07UL << 8) | (0x08UL << 16) | (0x09UL << 24);
    v[3] = 0x0a | (0x0bUL << 8);
    R2E_Regs(cs, R_SE_TCL_INPUT_VTX_VECTOR_ADDR_0, 4, v);

    /* SE_TCL_LIGHT_MODEL_CTL_0/1 and PER_LIGHT_CTL_0..3: lighting off,
     * materials from MATERIAL_0 (front) and _1 (back) */
    v[0] = (1UL << 5) | (1UL << 6) | (1UL << 8) | (1UL << 13);
    v[1] = 0xffff1111UL;
    v[2] = 0;
    v[3] = 0;
    v[4] = 0;
    v[5] = 0;
    R2E_Regs(cs, R_SE_TCL_LIGHT_MODEL_CTL_0, 6, v);

    /* SE_TCL_TEX_PROC_CTL_2, _3, _0, _1 and TEX_CYL_WRAP_CTL: texgen
     * off, unit n reads texture coordinate n */
    v[0] = 0x00ffffffUL;
    v[1] = 0x00543210UL;
    v[2] = 0;
    v[3] = 0x00543210UL;
    v[4] = 0;
    R2E_Regs(cs, R_SE_TCL_TEX_PROC_CTL_2, 5, v);

    R2E_Reg(cs, R_SE_TCL_UCP_VERT_BLEND_CTL, R_UCP_IN_CLIP_SPACE | R_CULL_FRONT_IS_CCW);

    /* guard band: vertical and horizontal clip and discard = 1.0 */
    v[0] = ONE_F;
    v[1] = ONE_F;
    v[2] = ONE_F;
    v[3] = ONE_F;
    R2E_Scalars(cs, R_SS_VERT_GUARD_CLIP_ADJ_ADDR, 4, v);
    return cs->error;
}

LONG R2E_RasterDefaults(struct R2Cs *cs)
{
    ULONG blend;

    blend = R_COMB_FCN_ADD_CLAMP | (R_BLEND_GL_ONE << R_SRC_BLEND_SHIFT)
          | (R_BLEND_GL_ZERO << R_DST_BLEND_SHIFT);
    R2E_Reg(cs, R_PP_MISC, R_ALPHA_TEST_PASS);
    R2E_Reg(cs, R_RB3D_BLENDCNTL, blend);
    R2E_Reg(cs, R_RB3D_ABLENDCNTL, blend);
    R2E_Reg(cs, R_RB3D_CBLENDCNTL, blend);
    R2E_Reg(cs, R_RB3D_ZSTENCILCNTL, R_DEPTH_FORMAT_16BIT_INT_Z | R_Z_TEST_LESS
                                   | R_STENCIL_TEST_ALWAYS | R_Z_WRITE_ENABLE);
    R2E_Reg(cs, R_SE_CNTL, R_FFACE_CULL_CCW | R_BFACE_SOLID | R_FFACE_SOLID
                         | R_FLAT_SHADE_VTX_LAST | R_DIFFUSE_SHADE_GOURAUD
                         | R_ALPHA_SHADE_GOURAUD | R_SPECULAR_SHADE_GOURAUD
                         | R_FOG_SHADE_GOURAUD | R_DISC_FOG_SHADE_GOURAUD
                         | R_VTX_PIX_CENTER_OGL | R_ROUND_MODE_TRUNC
                         | R_ROUND_PREC_8TH_PIX);
    R2E_Reg(cs, R_RE_CNTL, R_PERSPECTIVE_ENABLE | R_SCISSOR_ENABLE);
    R2E_Reg(cs, R_RB3D_STENCILREFMASK, (0xffUL << R_STENCIL_MASK_SHIFT)
                                     | (0xffUL << R_STENCIL_WRITEMASK_SHIFT));
    R2E_Reg(cs, R_RB3D_ROPCNTL, R_ROP_COPY);
    R2E_Reg(cs, R_RB3D_PLANEMASK, 0xffffffffUL);
    R2E_Reg(cs, R_PP_CNTL_X, 0);
    R2E_Reg(cs, R_RB3D_DEPTHXY_OFFSET, 0);
    R2E_Reg(cs, R_RE_AUX_SCISSOR_CNTL, 0);

    /* combiner stages 0 and 1: out = diffuse (A * B + C with A = B = 0) */
    R2E_Combiner(cs, 0,
                 (R_TXC_ARG_DIFFUSE_COLOR << R_TXC_ARG_C_SHIFT) | R_TXC_OP_MADD,
                 (0UL << R_TXC_TFACTOR_SEL_SHIFT) | R_TXC_CLAMP_0_1 | R_TXC_OUTPUT_REG_R0,
                 (R_TXA_ARG_DIFFUSE_ALPHA << R_TXC_ARG_C_SHIFT) | R_TXC_OP_MADD,
                 (0UL << R_TXC_TFACTOR_SEL_SHIFT) | R_TXC_CLAMP_0_1 | R_TXC_OUTPUT_REG_R0);
    R2E_Combiner(cs, 1,
                 (R_TXC_ARG_DIFFUSE_COLOR << R_TXC_ARG_C_SHIFT) | R_TXC_OP_MADD,
                 (1UL << R_TXC_TFACTOR_SEL_SHIFT) | R_TXC_CLAMP_0_1 | R_TXC_OUTPUT_REG_R0,
                 (R_TXA_ARG_DIFFUSE_ALPHA << R_TXC_ARG_C_SHIFT) | R_TXC_OP_MADD,
                 (1UL << R_TXC_TFACTOR_SEL_SHIFT) | R_TXC_CLAMP_0_1 | R_TXC_OUTPUT_REG_R0);
    return cs->error;
}

LONG R2E_Viewport(struct R2Cs *cs, float x, float y, float w, float h,
                  float zn, float zf, ULONG fb_height)
{
    ULONG v[6];
    float f[6];
    ULONG i;

    /* Mesa r200UpdateWindow for a window-system buffer: y flipped. Float
     * locals: SAS/C evaluates float expressions as double. */
    f[0] = w * 0.5f;
    f[1] = x + w * 0.5f;
    f[2] = -h * 0.5f;
    f[3] = (float) fb_height - (y + h * 0.5f);
    f[4] = (zf - zn) * 0.5f;
    f[5] = (zf + zn) * 0.5f;
    for (i = 0; i < 6; i++)
        v[i] = R2Cs_FloatBits(f[i]);
    return R2E_Regs(cs, R_SE_VPORT_XSCALE, 6, v);
}

LONG R2E_Scissor(struct R2Cs *cs, ULONG x1, ULONG y1, ULONG x2, ULONG y2)
{
    R2E_Reg(cs, R_RE_TOP_LEFT, (x1 & 0xffffUL) | (y1 << 16));
    return R2E_Reg(cs, R_RE_WIDTH_HEIGHT, (x2 & 0xffffUL) | (y2 << 16));
}

LONG R2E_ColorTarget(struct R2Cs *cs, ULONG gpu, ULONG pitch_px, ULONG flags)
{
    R2E_Reg(cs, R_RB3D_COLOROFFSET, gpu);
    return R2E_Reg(cs, R_RB3D_COLORPITCH, (pitch_px & R_COLORPITCH_MASK) | flags);
}

LONG R2E_DepthTarget(struct R2Cs *cs, ULONG gpu, ULONG pitch_px)
{
    ULONG v[2];

    v[0] = gpu;
    v[1] = pitch_px & R_DEPTHPITCH_MASK;
    return R2E_Regs(cs, R_RB3D_DEPTHOFFSET, 2, v);
}

LONG R2E_Texture(struct R2Cs *cs, ULONG unit, const struct R2Tex *t)
{
    ULONG v[5];

    if (unit > 5)
        return Fail(cs, R2_EARG);
    v[0] = t->filter;
    v[1] = t->format;
    v[2] = t->format_x;
    v[3] = t->size;
    v[4] = t->pitch;
    R2E_Regs(cs, R_PP_TXFILTER_0 + unit * R_PP_TXUNIT_STRIDE, 5, v);
    R2E_Reg(cs, R_PP_TXMULTI_CTL_0 + unit * R_PP_TXUNIT_STRIDE, t->multi);
    return R2E_Reg(cs, R_PP_TXOFFSET_0 + unit * R_PP_TXOFFSET_STRIDE, t->offset);
}

LONG R2E_Combiner(struct R2Cs *cs, ULONG stage, ULONG cblend, ULONG cblend2,
                  ULONG ablend, ULONG ablend2)
{
    ULONG v[4];

    if (stage > 7)
        return Fail(cs, R2_EARG);
    v[0] = cblend;
    v[1] = cblend2;
    v[2] = ablend;
    v[3] = ablend2;
    return R2E_Regs(cs, R_PP_TXCBLEND_0 + stage * R_PP_TXBLEND_STRIDE, 4, v);
}

LONG R2E_VertexFormat(struct R2Cs *cs, ULONG fmt0, ULONG fmt1,
                      ULONG out0, ULONG out1, ULONG compsel)
{
    ULONG v[4];

    /* SE_VTX_FMT_0, _1, SE_TCL_OUTPUT_VTX_FMT_0, _1 are consecutive */
    v[0] = fmt0;
    v[1] = fmt1;
    v[2] = out0;
    v[3] = out1;
    R2E_Regs(cs, R_SE_VTX_FMT_0, 4, v);
    return R2E_Reg(cs, R_SE_TCL_OUTPUT_VTX_COMP_SEL, compsel);
}

ULONG R2E_VtxDwords(ULONG fmt0, ULONG fmt1)
{
    ULONG n;
    ULONG i;
    ULONG c;

    n = 2;                                      /* x, y */
    if (fmt0 & R_VTX_Z0)
        n++;
    if (fmt0 & R_VTX_W0)
        n++;
    n += (fmt0 >> R_VTX_WEIGHT_COUNT_SHIFT) & 7;
    if (fmt0 & R_VTX_PV_MATRIX_SEL)
        n++;
    if (fmt0 & R_VTX_N0)
        n += 3;
    if (fmt0 & R_VTX_POINT_SIZE)
        n++;
    if (fmt0 & R_VTX_DISCRETE_FOG)
        n++;
    if (fmt0 & R_VTX_SHININESS_0)
        n++;
    if (fmt0 & R_VTX_SHININESS_1)
        n++;
    for (i = 0; i < 8; i++)
    {
        c = (fmt0 >> (R_VTX_COLOR_0_SHIFT + 2 * i)) & 3;
        if (c == R_VTX_PK_RGBA)
            n += 1;
        else if (c == R_VTX_FP_RGB)
            n += 3;
        else if (c == R_VTX_FP_RGBA)
            n += 4;
    }
    if (fmt0 & R_VTX_XY1)
        n += 2;
    if (fmt0 & R_VTX_Z1)
        n++;
    if (fmt0 & R_VTX_W1)
        n++;
    if (fmt0 & R_VTX_N1)
        n += 3;
    for (i = 0; i < 6; i++)
    {
        c = (fmt1 >> (3 * i)) & 7;
        if (c <= 4)
            n += c;
    }
    return n;
}

ULONG *R2E_ImmdOpen(struct R2Cs *cs, ULONG vtx_dw, ULONG max_verts)
{
    ULONG *p;

    if (cs->immd != NULL)
    {
        Fail(cs, R2_ESTATE);
        return NULL;
    }
    if (vtx_dw == 0 || max_verts == 0 || max_verts > R2E_IMMD_MAX_VERTS(vtx_dw))
    {
        Fail(cs, R2_EARG);
        return NULL;
    }
    p = R2Cs_Room(cs, 2 + vtx_dw * max_verts);
    if (p == NULL)
        return NULL;
    cs->immd = p;
    cs->immd_vtx = vtx_dw;
    return p + 2;
}

LONG R2E_ImmdClose(struct R2Cs *cs, ULONG *vend, ULONG cntl)
{
    ULONG *p;
    ULONG ndw;
    ULONG nverts;

    p = cs->immd;
    if (p == NULL)
        return Fail(cs, R2_ESTATE);
    cs->immd = NULL;
    if (cs->error != R2_OK)
        return cs->error;
    if (vend < p + 2 || vend > cs->end)
        return Fail(cs, R2_EARG);
    ndw = (ULONG) (vend - (p + 2));
    if (ndw % cs->immd_vtx != 0 || ndw > R2_PKT_MAX_COUNT)
        return Fail(cs, R2_EARG);
    if (ndw == 0)
        return R2_OK;                           /* cs->ptr is still p */
    nverts = ndw / cs->immd_vtx;
    /* count = payload - 1 = the vertex dwords (Linux: immd_dwords = count) */
    p[0] = R2_OUT(cs, R2_PKT3(R2_OP_3D_DRAW_IMMD_2, ndw));
    p[1] = R2_OUT(cs, (cntl & ~(R_VF_PRIM_WALK_MASK | 0xffff0000UL))
                      | R_VF_PRIM_WALK_RING | (nverts << R_VF_VERTEX_NUMBER_SHIFT));
    cs->ptr = vend;
    return R2_OK;
}

LONG R2E_Immd(struct R2Cs *cs, ULONG cntl, ULONG vtx_dw, ULONG nverts,
              const ULONG *data)
{
    ULONG *v;
    ULONG i;
    ULONG n;

    v = R2E_ImmdOpen(cs, vtx_dw, nverts);
    if (v == NULL)
        return cs->error;
    n = vtx_dw * nverts;
    for (i = 0; i < n; i++)
        v[i] = R2_OUT(cs, data[i]);
    return R2E_ImmdClose(cs, v + n, cntl);
}

LONG R2E_LoadVbpntr(struct R2Cs *cs, ULONG n, const struct R2Aos *aos)
{
    ULONG *p;
    ULONG sz;
    ULONG i;
    ULONG k;

    if (n == 0 || n > R2E_MAX_AOS)
        return Fail(cs, R2_EARG);
    for (i = 0; i < n; i++)
        if (aos[i].comps > 0xff || aos[i].stride > 0x7f)
            return Fail(cs, R2_EARG);
    /* payload: the count, then per pair an attribute dword and two
     * addresses, an odd last array one attribute dword and one address */
    sz = 1 + (n >> 1) * 3 + (n & 1) * 2;
    p = R2Cs_Room(cs, 1 + sz);
    if (p == NULL)
        return cs->error;
    p[0] = R2_OUT(cs, R2_PKT3(R2_OP_3D_LOAD_VBPNTR, sz - 1));
    p[1] = R2_OUT(cs, n);
    k = 2;
    for (i = 0; i + 1 < n; i += 2)
    {
        p[k++] = R2_OUT(cs, aos[i].comps | (aos[i].stride << 8)
                          | (aos[i + 1].comps << 16) | (aos[i + 1].stride << 24));
        p[k++] = R2_OUT(cs, aos[i].gpu);
        p[k++] = R2_OUT(cs, aos[i + 1].gpu);
    }
    if (n & 1)
    {
        p[k++] = R2_OUT(cs, aos[n - 1].comps | (aos[n - 1].stride << 8));
        p[k++] = R2_OUT(cs, aos[n - 1].gpu);
    }
    cs->ptr = p + k;
    return R2_OK;
}

LONG R2E_DrawVbuf(struct R2Cs *cs, ULONG cntl, ULONG nverts)
{
    ULONG *p;

    if (nverts == 0 || nverts > 0xffffUL)
        return Fail(cs, R2_EARG);
    p = R2Cs_Room(cs, 2);
    if (p == NULL)
        return cs->error;
    p[0] = R2_OUT(cs, R2_PKT3(R2_OP_3D_DRAW_VBUF_2, 0));
    p[1] = R2_OUT(cs, (cntl & ~(R_VF_PRIM_WALK_MASK | 0xffff0000UL))
                      | R_VF_PRIM_WALK_LIST | (nverts << R_VF_VERTEX_NUMBER_SHIFT));
    cs->ptr = p + 2;
    return R2_OK;
}

LONG R2E_DrawIndexed(struct R2Cs *cs, ULONG cntl, ULONG nidx, ULONG idx_gpu,
                     ULONG array_verts)
{
    ULONG *p;

    if (nidx == 0 || nidx > 0xffffUL || (idx_gpu & 3) != 0)
        return Fail(cs, R2_EARG);
    if (cntl & R_VF_INDEX_SZ_4)
        return Fail(cs, R2_EARG);               /* 16-bit indices only */
    p = R2Cs_Room(cs, 8);
    if (p == NULL)
        return cs->error;
    p[0] = R2_OUT(cs, R2_PKT0(R_SE_VF_MAX_VTX_INDX, 0));
    p[1] = R2_OUT(cs, array_verts);
    p[2] = R2_OUT(cs, R2_PKT3(R2_OP_3D_DRAW_INDX_2, 0));
    p[3] = R2_OUT(cs, (cntl & ~(R_VF_PRIM_WALK_MASK | 0xffff0000UL))
                      | R_VF_PRIM_WALK_IND | (nidx << R_VF_VERTEX_NUMBER_SHIFT));
    p[4] = R2_OUT(cs, R2_PKT3(R2_OP_INDX_BUFFER, 2));
    p[5] = R2_OUT(cs, R2_INDX_BUFFER_CNTL);
    p[6] = R2_OUT(cs, idx_gpu);
    p[7] = R2_OUT(cs, (nidx + 1) >> 1);         /* dwords of index data */
    cs->ptr = p + 8;
    return R2_OK;
}

LONG R2E_Fence(struct R2Cs *cs, ULONG scratch_reg, ULONG seq)
{
    ULONG *p;

    p = R2Cs_Room(cs, R2E_FENCE_DWORDS);
    if (p == NULL)
        return cs->error;
    p[0] = R2_OUT(cs, R2_PKT0(R_RB3D_DSTCACHE_CTLSTAT, 0));
    p[1] = R2_OUT(cs, R_RB3D_DC_FLUSH_ALL);
    p[2] = R2_OUT(cs, R2_PKT0(R_RB3D_ZCACHE_CTLSTAT, 0));
    p[3] = R2_OUT(cs, R_RB3D_ZC_FLUSH_ALL);
    p[4] = R2_OUT(cs, R2_PKT0(R_WAIT_UNTIL, 0));
    p[5] = R2_OUT(cs, R_WAIT_2D_IDLECLEAN | R_WAIT_3D_IDLECLEAN);
    p[6] = R2_OUT(cs, R2_PKT0(scratch_reg, 0));
    p[7] = R2_OUT(cs, seq);
    cs->ptr = p + R2E_FENCE_DWORDS;
    return R2_OK;
}
