/*
 * hwstate.c -- GL state to R200 registers, emitted when dirty.
 *
 * Register semantics follow Mesa 21.3's r200 driver (r200_state.c,
 * r200_state_init.c, r200_texstate.c): culling in SE_CNTL and in the TCL
 * unit's UCP_VERT_BLEND_CTL together (r200CullFace), the colour mask as
 * RB3D_PLANEMASK plus PLANE_MASK_ENABLE (r200ColorMask), polygon offset as
 * SE_ZBIAS with the constant in depth units (r200PolygonOffset), point
 * size as RE_POINTSIZE * 16 (r200PointSize), the viewport y-flipped for a
 * window-system buffer (r200UpdateWindow).
 *
 * Texture environment: stage n combines unit n's texel (register Rn) with
 * the previous result (the diffuse colour for stage 0, R0 after it) as
 * A * B + C, and writes R0 (Mesa r200_blit, the concept's section 7.3).
 */

#include "core.h"
#include "coreint.h"
#include "r200reg.h"

ULONG Hw_DepthFunc(ULONG f)
{
    /* GL_NEVER .. GL_ALWAYS are 0x200 .. 0x207 in GL's order (never, less,
     * equal, lequal, greater, notequal, gequal, always); R200's order is
     * never, less, lequal, equal, gequal, greater, notequal, always */
    static const UBYTE map[8] = { 0, 1, 3, 2, 5, 6, 4, 7 };

    if (f < GL_NEVER || f > GL_ALWAYS)
        return 0xffffffffUL;
    return map[f - GL_NEVER];
}

ULONG Hw_BlendFactor(ULONG f)
{
    switch (f)
    {
        case GL_ZERO:                   return R_BLEND_GL_ZERO;
        case GL_ONE:                    return R_BLEND_GL_ONE;
        case GL_SRC_COLOR:              return R_BLEND_GL_SRC_COLOR;
        case GL_ONE_MINUS_SRC_COLOR:    return R_BLEND_GL_ONE_MINUS_SRC_COLOR;
        case GL_DST_COLOR:              return R_BLEND_GL_DST_COLOR;
        case GL_ONE_MINUS_DST_COLOR:    return R_BLEND_GL_ONE_MINUS_DST_COLOR;
        case GL_SRC_ALPHA:              return R_BLEND_GL_SRC_ALPHA;
        case GL_ONE_MINUS_SRC_ALPHA:    return R_BLEND_GL_ONE_MINUS_SRC_ALPHA;
        case GL_DST_ALPHA:              return R_BLEND_GL_DST_ALPHA;
        case GL_ONE_MINUS_DST_ALPHA:    return R_BLEND_GL_ONE_MINUS_DST_ALPHA;
        case GL_SRC_ALPHA_SATURATE:     return 42UL;
    }
    return 0;
}

ULONG Core_UnitsOn(struct Core *c)
{
    ULONG u;
    ULONG on;

    on = 0;
    for (u = 0; u < CORE_UNITS; u++)
        if (c->tex_enabled[u] && Core_Texture(c, u) != NULL)
            on |= 1UL << u;
    return on;
}

static ULONG Rb3dCntl(struct Core *c)
{
    ULONG v;

    v = c->target.color_format;
    if (c->enables & EN_DEPTH_TEST)
        v |= R_Z_ENABLE;
    if (c->enables & EN_BLEND)
        v |= R_ALPHA_BLEND_ENABLE;
    if (c->color_mask != 0xf)
        v |= R_PLANE_MASK_ENABLE;
    if ((c->enables & EN_DITHER) && c->target.color_format == R_COLOR_FORMAT_RGB565)
        v |= R_DITHER_ENABLE;
    else
        v |= R_ROUND_ENABLE;
    return v;
}

static ULONG PlaneMask(struct Core *c)
{
    ULONG m;

    m = c->color_mask;
    if (c->target.color_format == R_COLOR_FORMAT_RGB565)
        return ((m & 1) ? 0xf800UL : 0) | ((m & 2) ? 0x07e0UL : 0) | ((m & 4) ? 0x001fUL : 0);
    return ((m & 8) ? 0xff000000UL : 0) | ((m & 1) ? 0x00ff0000UL : 0)
         | ((m & 2) ? 0x0000ff00UL : 0) | ((m & 4) ? 0x000000ffUL : 0);
}

static ULONG SeCntl(struct Core *c)
{
    ULONG v;

    v = R_FLAT_SHADE_VTX_LAST | R_VTX_PIX_CENTER_OGL | R_ROUND_MODE_TRUNC | R_ROUND_PREC_8TH_PIX
      | R_BFACE_SOLID | R_FFACE_SOLID;
    if (c->shade_model == GL_FLAT)
        v |= R_DIFFUSE_SHADE_FLAT | R_ALPHA_SHADE_FLAT | R_SPECULAR_SHADE_FLAT
           | R_FOG_SHADE_FLAT | R_DISC_FOG_SHADE_FLAT;
    else
        v |= R_DIFFUSE_SHADE_GOURAUD | R_ALPHA_SHADE_GOURAUD | R_SPECULAR_SHADE_GOURAUD
           | R_FOG_SHADE_GOURAUD | R_DISC_FOG_SHADE_GOURAUD;
    if (c->front_face == GL_CCW)
        v |= R_FFACE_CULL_CCW;
    if (c->enables & EN_CULL_FACE)
    {
        if (c->cull_mode != GL_BACK)
            v &= ~R_FFACE_SOLID;
        if (c->cull_mode != GL_FRONT)
            v &= ~R_BFACE_SOLID;
    }
    if (c->enables & (EN_POLY_OFFSET | EN_ZOFFSET))
        v |= R_ZBIAS_ENABLE_TRI;
    return v;
}

static ULONG UcpCtl(struct Core *c)
{
    ULONG v;

    v = R_UCP_IN_CLIP_SPACE;
    if (c->front_face == GL_CCW)
        v |= R_CULL_FRONT_IS_CCW;
    if (c->enables & EN_CULL_FACE)
    {
        if (c->cull_mode != GL_BACK)
            v |= R_CULL_FRONT;
        if (c->cull_mode != GL_FRONT)
            v |= R_CULL_BACK;
    }
    return v;
}

static ULONG MinFilter(ULONG f)
{
    switch (f)
    {
        case GL_LINEAR:                 return R_MIN_FILTER_LINEAR;
        case GL_NEAREST_MIPMAP_NEAREST: return R_MIN_FILTER_NEAREST_MIP_NEAREST;
        case GL_NEAREST_MIPMAP_LINEAR:  return R_MIN_FILTER_NEAREST_MIP_LINEAR;
        case GL_LINEAR_MIPMAP_NEAREST:  return R_MIN_FILTER_LINEAR_MIP_NEAREST;
        case GL_LINEAR_MIPMAP_LINEAR:   return R_MIN_FILTER_LINEAR_MIP_LINEAR;
    }
    return 0;                           /* GL_NEAREST */
}

static ULONG IsMip(ULONG f)
{
    return (f != GL_NEAREST && f != GL_LINEAR) ? 1UL : 0UL;
}

/* The highest level a mipmapped draw may use: consecutive levels from 0. */
static ULONG MaxLevel(const struct CoreTex *t)
{
    ULONG l;

    if (!IsMip(t->min_filter) || !t->pot)
        return 0;
    l = 0;
    while (l + 1 < CORE_MAX_LEVELS && (t->levels & (1UL << (l + 1))))
        l++;
    return l;
}

static LONG EmitTexture(struct Core *c, ULONG u)
{
    struct CoreTex *t;
    struct R2Tex r;

    t = Core_Texture(c, u);
    if (t == NULL)
        return R2_OK;
    r.filter = MinFilter(t->min_filter) | (t->mag_filter == GL_LINEAR ? R_MAG_FILTER_LINEAR : 0)
             | (MaxLevel(t) << R_MAX_MIP_LEVEL_SHIFT);
    if (t->wrap_s == GL_CLAMP)
        r.filter |= R_CLAMP_S_CLAMP_LAST;
    if (t->wrap_t == GL_CLAMP)
        r.filter |= R_CLAMP_T_CLAMP_LAST;
    r.format = t->hwfmt | (u << R_TXFORMAT_ST_ROUTE_SHIFT);
    if (t->pot)
    {
        r.format |= (t->lw << R_TXFORMAT_WIDTH_SHIFT) | (t->lh << R_TXFORMAT_HEIGHT_SHIFT);
        r.size = 0;
        r.pitch = 0;
    }
    else
    {
        /* NPOT: one level, rows TXPITCH + 32 bytes apart; R200 clamps (Mesa) */
        r.format |= R_TXFORMAT_NON_POWER2;
        r.size = (t->w - 1) | ((t->h - 1) << 16);
        r.pitch = t->pitch[0] - 32;
        r.filter |= R_CLAMP_S_CLAMP_LAST | R_CLAMP_T_CLAMP_LAST;
    }
    r.format_x = R_LOD_BIAS_CORRECTION;
    r.multi = 0;
    r.offset = t->gpu;
    t->used_since_idle = 1;
    return R2E_Texture(c->cs, u, &r);
}

/* What the texture contributes: colour unless an alpha-only base, alpha
 * unless a base without one. */
static ULONG HasRgb(ULONG base)
{
    return (base != GL_ALPHA) ? 1UL : 0UL;
}

static ULONG HasAlpha(ULONG base)
{
    return (base == GL_RGBA || base == GL_ALPHA || base == GL_LUMINANCE_ALPHA
            || base == GL_INTENSITY) ? 1UL : 0UL;
}

static LONG EmitCombiner(struct Core *c, ULONG units)
{
    ULONG s;
    ULONG prevc;
    ULONG preva;
    ULONG texc;
    ULONG cb;
    ULONG ab;
    ULONG out;
    struct CoreTex *t;
    LONG e;

    e = R2_OK;
    for (s = 0; s < CORE_UNITS; s++)
    {
        prevc = (s == 0) ? R_TXC_ARG_DIFFUSE_COLOR : R_TXC_ARG_R0_COLOR;
        preva = (s == 0) ? R_TXA_ARG_DIFFUSE_ALPHA : R_TXA_ARG_R0_ALPHA;
        out = R_TXC_CLAMP_0_1 | R_TXC_OUTPUT_REG_R0 | (s << R_TXC_TFACTOR_SEL_SHIFT);
        /* pass the previous result through */
        cb = (prevc << R_TXC_ARG_C_SHIFT) | R_TXC_OP_MADD;
        ab = (preva << R_TXC_ARG_C_SHIFT) | R_TXC_OP_MADD;
        if (units & (1UL << s))
        {
            t = Core_Texture(c, s);
            texc = R_TXC_ARG_R0_COLOR + 2 * s;      /* Rs; alpha args use the same numbers */
            switch (c->tex_env[s])
            {
                case GL_REPLACE:
                case GL_DECAL:      /* DECAL is REPLACE for RGB; RGBA decal not yet */
                    if (HasRgb(t->base))
                        cb = (texc << R_TXC_ARG_C_SHIFT) | R_TXC_OP_MADD;
                    if (HasAlpha(t->base) && c->tex_env[s] == GL_REPLACE)
                        ab = (texc << R_TXC_ARG_C_SHIFT) | R_TXC_OP_MADD;
                    break;
                case GL_ADD:
                    if (HasRgb(t->base))
                        cb = (texc << R_TXC_ARG_A_SHIFT) | (R_TXC_ARG_ZERO << R_TXC_ARG_B_SHIFT)
                           | R_TXC_COMP_ARG_B | (prevc << R_TXC_ARG_C_SHIFT) | R_TXC_OP_MADD;
                    if (HasAlpha(t->base))
                        ab = (texc << R_TXC_ARG_A_SHIFT) | (preva << R_TXC_ARG_B_SHIFT) | R_TXC_OP_MADD;
                    break;
                default:            /* GL_MODULATE; GL_BLEND not yet, drawn as MODULATE */
                    if (HasRgb(t->base))
                        cb = (texc << R_TXC_ARG_A_SHIFT) | (prevc << R_TXC_ARG_B_SHIFT) | R_TXC_OP_MADD;
                    if (HasAlpha(t->base))
                        ab = (texc << R_TXC_ARG_A_SHIFT) | (preva << R_TXC_ARG_B_SHIFT) | R_TXC_OP_MADD;
                    break;
            }
        }
        else if (s != 0)
            continue;               /* stage disabled in PP_CNTL */
        e = R2E_Combiner(c->cs, s, cb, out, ab, out);
    }
    return e;
}

static ULONG PpCntl(struct Core *c, ULONG units)
{
    ULONG v;
    ULONG u;

    v = R_TEX_BLEND_0_ENABLE;
    for (u = 0; u < CORE_UNITS; u++)
        if (units & (1UL << u))
            v |= (R_TEX_0_ENABLE << u) | (R_TEX_BLEND_0_ENABLE << u);
    if (c->enables & EN_ALPHA_TEST)
        v |= R_ALPHA_TEST_ENABLE;
    return v;
}

void Hw_VertexFormat(struct Core *c, ULONG units)
{
    ULONG fmt1;
    ULONG compsel;

    if (units == c->hw_units && !(c->dirty & D_VTXFMT))
        return;
    fmt1 = ((units & 1) ? 2UL : 0) | ((units & 2) ? 2UL << 3 : 0);
    compsel = R_OUTPUT_XYZW | R_OUTPUT_COLOR_0
            | ((units & 1) ? R_OUTPUT_TEX_0 : 0) | ((units & 2) ? R_OUTPUT_TEX_0 << 1 : 0);
    R2E_VertexFormat(c->cs, R_VTX_Z0 | (R_VTX_PK_RGBA << R_VTX_COLOR_0_SHIFT), fmt1,
                     R_VTX_Z0 | R_VTX_W0 | (R_VTX_FP_RGBA << R_VTX_COLOR_0_SHIFT), fmt1,
                     compsel);
    c->hw_units = units;
    c->dirty &= ~D_VTXFMT;
}

static LONG EmitScissor(struct Core *c)
{
    LONG x1, y1, x2, y2;
    LONG H;

    H = (LONG) c->target.height;
    if (c->enables & EN_SCISSOR)
    {
        /* GL's box has a lower-left origin; the R200's corners are top-left
         * and inclusive. Clamped to the target (MiniGLV3D wrapped a u16). */
        x1 = c->sc_x;
        x2 = c->sc_x + c->sc_w - 1;
        y1 = H - (c->sc_y + c->sc_h);
        y2 = H - c->sc_y - 1;
    }
    else
    {
        x1 = 0;
        y1 = 0;
        x2 = (LONG) c->target.width - 1;
        y2 = H - 1;
    }
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > (LONG) c->target.width - 1) x2 = (LONG) c->target.width - 1;
    if (y2 > H - 1) y2 = H - 1;
    if (x2 < x1 || y2 < y1)
    {
        /* an empty box: a one-pixel box drawn with no colour, no depth
         * would still touch a pixel; draws are skipped instead (vertex.c) */
        x2 = x1;
        y2 = y1;
    }
    return R2E_Scissor(c->cs, (ULONG) x1, (ULONG) y1, (ULONG) x2, (ULONG) y2);
}

LONG Core_Validate(struct Core *c)
{
    struct R2Cs *cs;
    ULONG d;
    ULONG units;
    ULONG u;
    ULONG v[2];
    float mvp[16];
    float k;

    d = c->dirty;
    if ((d & ~D_VTXFMT) == 0)
        return R2_OK;
    cs = c->cs;
    c->stat_state_emits++;
    if (d & D_INIT)
    {
        R2E_TclDefaults(cs);
        R2E_RasterDefaults(cs);
        d = D_ALL;                  /* the defaults overwrote every block */
    }
    units = Core_UnitsOn(c);
    if (d & D_VAP)
    {
        R2E_Reg(cs, R_SE_VAP_CNTL, R_VAP_TCL_ENABLE | (9UL << R_VAP_VF_MAX_VTX_NUM_SHIFT));
        R2E_Reg(cs, R_SE_VTE_CNTL, R_VPORT_X_SCALE_ENA | R_VPORT_X_OFFSET_ENA
                                 | R_VPORT_Y_SCALE_ENA | R_VPORT_Y_OFFSET_ENA
                                 | R_VPORT_Z_SCALE_ENA | R_VPORT_Z_OFFSET_ENA
                                 | R_VTX_W0_FMT);
        d |= D_VTXFMT;
    }
    if (d & D_TARGET)
    {
        R2E_ColorTarget(cs, c->target.color_gpu, c->target.color_pitch, 0);
        R2E_DepthTarget(cs, c->target.depth_gpu, c->target.depth_pitch);
    }
    if (d & D_VIEWPORT)
        R2E_Viewport(cs, (float) c->vp_x, (float) c->vp_y, (float) c->vp_w, (float) c->vp_h,
                     c->depth_near, c->depth_far, c->target.height);
    if (d & D_SCISSOR)
        EmitScissor(c);
    if (d & D_RB3D)
        R2E_Reg(cs, R_RB3D_CNTL, Rb3dCntl(c));
    if (d & D_ZSTENCIL)
        R2E_Reg(cs, R_RB3D_ZSTENCILCNTL, R_DEPTH_FORMAT_16BIT_INT_Z
                | (Hw_DepthFunc(c->depth_func) << R_Z_TEST_SHIFT) | R_STENCIL_TEST_ALWAYS
                | (c->depth_mask ? R_Z_WRITE_ENABLE : 0));
    if (d & D_BLEND)
    {
        v[0] = R_COMB_FCN_ADD_CLAMP | (Hw_BlendFactor(c->blend_src) << R_SRC_BLEND_SHIFT)
             | (Hw_BlendFactor(c->blend_dst) << R_DST_BLEND_SHIFT);
        R2E_Reg(cs, R_RB3D_BLENDCNTL, v[0]);
        R2E_Reg(cs, R_RB3D_ABLENDCNTL, v[0]);
        R2E_Reg(cs, R_RB3D_CBLENDCNTL, v[0]);
    }
    if (d & D_ALPHA)
        R2E_Reg(cs, R_PP_MISC, Core_Unit255(c->alpha_ref)
                | (Hw_DepthFunc(c->alpha_func) << R_ALPHA_TEST_SHIFT));
    if (d & D_PLANEMASK)
        R2E_Reg(cs, R_RB3D_PLANEMASK, PlaneMask(c));
    if (d & D_SECNTL)
    {
        R2E_Reg(cs, R_SE_CNTL, SeCntl(c));
        R2E_Reg(cs, R_SE_TCL_UCP_VERT_BLEND_CTL, UcpCtl(c));
    }
    if (d & D_ZBIAS)
    {
        /* constant in depth units: glPolygonOffset's units are steps of the
         * 16-bit buffer (Mesa: units / DepthMax); mglSetZOffset is direct */
        k = 0.0f;
        v[0] = 0;
        if (c->enables & EN_POLY_OFFSET)
        {
            k = c->poly_units / 65535.0f;
            v[0] = R2Cs_FloatBits(c->poly_factor);
        }
        if (c->enables & EN_ZOFFSET)
            k += c->zoffset;
        v[1] = R2Cs_FloatBits(k);
        R2E_Regs(cs, R_SE_ZBIAS_FACTOR, 2, v);
    }
    if (d & D_POINTSIZE)
        R2E_Reg(cs, R_RE_POINTSIZE, ((ULONG) Core_Floor(c->point_size * 16.0f) & 0xffffUL)
                                  | ((64UL * 16UL) << R_MAXPOINTSIZE_SHIFT));
    for (u = 0; u < CORE_UNITS; u++)
        if ((d & (D_TEX0 << u)) && (units & (1UL << u)))
            EmitTexture(c, u);
    if (d & D_COMBINE)
        EmitCombiner(c, units);
    if (d & D_PPCNTL)
        R2E_Reg(cs, R_PP_CNTL, PpCntl(c, units));
    if (d & D_MATRIX)
    {
        Mat_Mul(mvp, c->proj[c->proj_top].m, c->mv[c->mv_top].m);
        R2E_Matrix(cs, R_VS_MATRIX_2_MVP, mvp);
    }
    c->dirty = d & D_VTXFMT;
    return cs->error;
}
