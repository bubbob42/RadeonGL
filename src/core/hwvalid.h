/*
 * hwvalid.h -- the validation on every draw: EmitTexture, EmitCombiner,
 * PpCntl and ValidateOnce, written once and compiled twice by hwstate.c:
 *
 *   HWV_WIDE 0  ..._N: units 0 and 1, none of GL 1.3's escapes into
 *               tex13.c -- the code 0.16 ran, for a context that uses no
 *               GL 1.3 feature (GLQuake);
 *   HWV_WIDE 1  ..._W: all six units and the escapes.
 *
 * Core_Validate picks one per call by tex_x | comb_x | wide_on. With all
 * three zero every escape test below is false and Core_UnitsOn gives no
 * unit above 1, so both copies send the same registers and keep the same
 * shadows: a context may move between them at any draw.
 *
 * MEASURED 2026-11-18 (A4000D, GLQuake demo1, PC profile over 3 pads):
 * with the escapes in the one ValidateOnce its samples rose 819 -> 986
 * (+20%) though GLQuake never takes them -- the calls and Tex13_TexRegs'
 * &r reshape GCC's registers for the whole function.
 *
 * Not a header: hwstate.c alone includes it, after its shared helpers.
 */

#if HWV_WIDE
#define HWV(name)       name##_W
#define HWV_UNITS       CORE_UNITS
#else
#define HWV(name)       name##_N
#define HWV_UNITS       CORE_NARROW
#endif

/* Single-call helpers live here too, one per copy, in line in each as GCC
 * put them into 0.16's one ValidateOnce. Two copies alone were not enough:
 * ipa-icf folded the identical pair into one function, called from both
 * (Rb3dCntl, SeCntl, EmitScissor; MEASURED in the object 2026-11-18).
 * Other compilers decide by themselves. */
#ifndef HWV_INLINE
#if defined(__GNUC__)
#define HWV_INLINE __inline__ __attribute__((always_inline))
#else
#define HWV_INLINE
#endif
#endif

static HWV_INLINE ULONG HWV(Rb3dCntl)(struct Core *c)
{
    ULONG v;

    v = c->target.color_format;
    if (c->enables & EN_DEPTH_TEST)
        v |= R_Z_ENABLE;
    /* ABLENDCNTL (alpha) and CBLENDCNTL (colour) apart only for a program
     * that set equations or separate alpha factors (compat.c): always on,
     * as Mesa's r200 has it, cost GLQuake 0.05 s (MEASURED 2026-11-08) */
    if (c->enables & EN_BLEND)
        v |= R_ALPHA_BLEND_ENABLE | c->blend_x * R_SEPARATE_ALPHA_ENABLE;    /* blend_x: 0 or 1 */
    if (c->color_mask != 0xf)
        v |= R_PLANE_MASK_ENABLE;
    if ((c->enables & EN_DITHER) && (c->target.color_format == R_COLOR_FORMAT_RGB565
                                     || c->target.color_format == R_COLOR_FORMAT_ARGB1555))
        v |= R_DITHER_ENABLE;
    else
        v |= R_ROUND_ENABLE;
    return v;
}

static HWV_INLINE ULONG HWV(SeCntl)(struct Core *c)
{
    ULONG v;

    v = R_FLAT_SHADE_VTX_LAST | R_VTX_PIX_CENTER_OGL | R_ROUND_MODE_TRUNC | R_ROUND_PREC_8TH_PIX
      | R_BFACE_SOLID | R_FFACE_SOLID;
    /* GL_FLAT flattens the colour only: fog -- here the factor the TCL puts
     * in the specular alpha -- is interpolated still. Flat fog gave every
     * triangle one corner's fog, hard-edged wedges on GLQuake's world (it
     * draws GL_FLAT) with gl_fog 1 (MEASURED 2026-11-08) */
    if (c->shade_model == GL_FLAT)
        v |= R_DIFFUSE_SHADE_FLAT | R_ALPHA_SHADE_FLAT | R_SPECULAR_SHADE_FLAT
           | R_FOG_SHADE_GOURAUD | R_DISC_FOG_SHADE_GOURAUD;
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
    v |= c->x_se;                   /* WIDELINE_ENABLE (light.c Core_Extras) */
    return v;
}

/* The highest level a mipmapped draw may use: consecutive levels from 0. */
static HWV_INLINE ULONG HWV(MaxLevel)(const struct CoreTex *t)
{
    ULONG l;

    if (!IsMip(t->min_filter) || !t->pot)
        return 0;
    l = 0;
    while (l + 1 < CORE_MAX_LEVELS && (t->levels & (1UL << (l + 1))))
        l++;
    return l;
}

static HWV_INLINE LONG HWV(EmitScissor)(struct Core *c)
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
        /* an empty box (the R200's cannot be): a valid one-pixel box;
         * draws and clears are skipped instead (CORE_SCISSOR_EMPTY) */
        x2 = x1;
        y2 = y1;
    }
    return R2E_Scissor(c->cs, (ULONG) x1, (ULONG) y1, (ULONG) x2, (ULONG) y2);
}

static LONG HWV(EmitTexture)(struct Core *c, ULONG u)
{
    struct CoreTex *t;
    struct R2Tex r;
    ULONG *sh;
    ULONG v[5];

    t = Core_Texture(c, u);
    if (t == NULL)
        return R2_OK;
    r.filter = MinFilter(t->min_filter) | (t->mag_filter == GL_LINEAR ? R_MAG_FILTER_LINEAR : 0)
             | (HWV(MaxLevel)(t) << R_MAX_MIP_LEVEL_SHIFT);
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
#if HWV_WIDE
    if (c->tex_x)
        Tex13_TexRegs(c, u, t, &r);     /* phase-1 parameters, cube maps, LOD bias (tex13.c) */
#endif
    /* R2E_Texture's three packets, each only when it differs */
    sh = c->hw_tex[u];
    if (sh[0] != r.filter || sh[1] != r.format || sh[2] != r.format_x || sh[3] != r.size
        || sh[4] != r.pitch)
    {
        v[0] = sh[0] = r.filter;
        v[1] = sh[1] = r.format;
        v[2] = sh[2] = r.format_x;
        v[3] = sh[3] = r.size;
        v[4] = sh[4] = r.pitch;
        R2E_Regs(c->cs, R_PP_TXFILTER_0 + u * R_PP_TXUNIT_STRIDE, 5, v);
    }
    if (sh[5] != r.multi)
    {
        sh[5] = r.multi;
        R2E_Reg(c->cs, R_PP_TXMULTI_CTL_0 + u * R_PP_TXUNIT_STRIDE, r.multi);
    }
    if (sh[6] != r.offset)
    {
        sh[6] = r.offset;
        R2E_Reg(c->cs, R_PP_TXOFFSET_0 + u * R_PP_TXOFFSET_STRIDE, r.offset);
    }
    return c->cs->error;
}

static LONG HWV(EmitCombiner)(struct Core *c, ULONG units)
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

#if HWV_WIDE
    if ((units & ~3UL) || (c->comb_x & units))
        return Tex13_Combiner(c, units);    /* units above 1, GL_COMBINE (tex13.c) */
#endif
    e = R2_OK;
    for (s = 0; s < CORE_NARROW; s++)
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
                    if (HasRgb(t->base))
                        cb = (texc << R_TXC_ARG_C_SHIFT) | R_TXC_OP_MADD;
                    if (HasAlpha(t->base))
                        ab = (texc << R_TXC_ARG_C_SHIFT) | R_TXC_OP_MADD;
                    break;
                case GL_DECAL:
                case GL_BLEND:      /* compat.c, linked last: LERP, the TFACTOR */
                    Core_CombExtra(c, s, &cb, &ab);
                    break;
                case GL_ADD:
                    if (HasRgb(t->base))
                        cb = (texc << R_TXC_ARG_A_SHIFT) | (R_TXC_ARG_ZERO << R_TXC_ARG_B_SHIFT)
                           | R_TXC_COMP_ARG_B | (prevc << R_TXC_ARG_C_SHIFT) | R_TXC_OP_MADD;
                    if (HasAlpha(t->base))
                        ab = (texc << R_TXC_ARG_A_SHIFT) | (preva << R_TXC_ARG_B_SHIFT) | R_TXC_OP_MADD;
                    break;
                default:            /* GL_MODULATE */
                    if (HasRgb(t->base))
                        cb = (texc << R_TXC_ARG_A_SHIFT) | (prevc << R_TXC_ARG_B_SHIFT) | R_TXC_OP_MADD;
                    if (HasAlpha(t->base))
                        ab = (texc << R_TXC_ARG_A_SHIFT) | (preva << R_TXC_ARG_B_SHIFT) | R_TXC_OP_MADD;
                    break;
            }
        }
        else if (s != 0)
            continue;               /* stage disabled in PP_CNTL */
        if (c->hw_comb[s][0] == cb && c->hw_comb[s][1] == out && c->hw_comb[s][2] == ab
            && c->hw_comb[s][3] == out)
            continue;
        c->hw_comb[s][0] = cb;
        c->hw_comb[s][1] = out;
        c->hw_comb[s][2] = ab;
        c->hw_comb[s][3] = out;
        e = R2E_Combiner(c->cs, s, cb, out, ab, out);
    }
    return e;
}

static ULONG HWV(PpCntl)(struct Core *c, ULONG units)
{
    ULONG v;
    ULONG u;

    v = R_TEX_BLEND_0_ENABLE;
    for (u = 0; u < HWV_UNITS; u++)
        if (units & (1UL << u))
            v |= (R_TEX_0_ENABLE << u) | (R_TEX_BLEND_0_ENABLE << u);
#if HWV_WIDE
    if ((units & ~3UL) || (c->comb_x & units))
        v = Tex13_PpCntl(c, v, units);      /* its stages, compacted (tex13.c) */
#endif
    if (c->enables & EN_ALPHA_TEST)
        v |= R_ALPHA_TEST_ENABLE;
    return v | c->x_pp;             /* FOG_ENABLE (Core_Extras) */
}

static LONG HWV(ValidateOnce)(struct Core *c)
{
    struct R2Cs *cs;
    ULONG d;
    ULONG gen;
    ULONG units;
    ULONG u;
    ULONG v[2];
    float mvp[16];
    float k;

    d = c->dirty;
    if ((d & ~D_VTXFMT) == 0)
        return R2_OK;
    gen = c->rebase_gen;
    cs = c->cs;
    c->stat_state_emits++;
    if (c->no_shadow)
        Hw_Forget(c);
    if (d & D_INIT)
    {
        R2E_TclDefaults(cs);
        R2E_RasterDefaults(cs);
        c->hw_stq_d3d = 0;          /* RE_CNTL without them */
        d = D_ALL;                  /* the defaults overwrote every block */
        Hw_Forget(c);
        Light_Reset(c);             /* the lighting controls as the defaults left them */
        Texgen_Forget(c);           /* and the texgen registers */
        Fog_Forget(c);
    }
    units = Core_UnitsOn(c);
    if (d & D_VAP)
    {
        R2E_VapCntl(cs, R_VAP_TCL_ENABLE | (9UL << R_VAP_VF_MAX_VTX_NUM_SHIFT));
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
        HWV(EmitScissor)(c);
    if (d & D_RB3D)
    {
        R2E_Reg(cs, R_RB3D_CNTL, HWV(Rb3dCntl)(c));
        if (c->blend_x)
            d |= D_BLEND;           /* blending on or off: Compat_EmitBlend follows */
    }
    if (d & D_ZSTENCIL)
        R2E_Reg(cs, R_RB3D_ZSTENCILCNTL, R_DEPTH_FORMAT_16BIT_INT_Z
                | (Hw_DepthFunc(c->depth_func) << R_Z_TEST_SHIFT) | R_STENCIL_TEST_ALWAYS
                | (c->depth_mask ? R_Z_WRITE_ENABLE : 0));
    if (d & D_BLEND)
    {
        if (c->blend_x)
            Compat_EmitBlend(c);    /* equations, separate alpha factors (compat.c) */
        else
        {
            v[0] = R_COMB_FCN_ADD_CLAMP | (Hw_BlendFactor(c->blend_src) << R_SRC_BLEND_SHIFT)
                 | (Hw_BlendFactor(c->blend_dst) << R_DST_BLEND_SHIFT);
            R2E_Reg(cs, R_RB3D_BLENDCNTL, v[0]);
            R2E_Reg(cs, R_RB3D_ABLENDCNTL, v[0]);
            R2E_Reg(cs, R_RB3D_CBLENDCNTL, v[0]);
        }
    }
    if (d & D_ALPHA)
        R2E_Reg(cs, R_PP_MISC, Core_Unit255(c->alpha_ref)
                | (Hw_DepthFunc(c->alpha_func) << R_ALPHA_TEST_SHIFT));
    if (d & D_PLANEMASK)
        R2E_Reg(cs, R_RB3D_PLANEMASK, PlaneMask(c));
    if (d & D_SECNTL)
    {
        R2E_Reg(cs, R_SE_CNTL, HWV(SeCntl)(c));
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
    for (u = 0; u < HWV_UNITS; u++)
        if ((d & (D_TEX0 << u)) && (units & (1UL << u)))
            HWV(EmitTexture)(c, u);
    if (d & D_COMBINE)
        HWV(EmitCombiner)(c, units);
    if (d & D_PPCNTL)
    {
        v[0] = HWV(PpCntl)(c, units);
        if (v[0] != c->hw_ppcntl)
        {
            c->hw_ppcntl = v[0];
            R2E_Reg(cs, R_PP_CNTL, v[0]);
        }
    }
    if ((d & D_MATRIX) && (c->mv[c->mv_top].ver != c->hw_mv_ver
                           || c->proj[c->proj_top].ver != c->hw_proj_ver))
    {
        Mat_Mul(mvp, c->proj[c->proj_top].m, c->mv[c->mv_top].m);
        R2E_Matrix(cs, R_VS_MATRIX_2_MVP, mvp);
        c->hw_mv_ver = c->mv[c->mv_top].ver;
        c->hw_proj_ver = c->proj[c->proj_top].ver;
    }
    if ((d & D_MATRIX) && c->x_eye)
        Light_Matrices(c);          /* lighting, fog, eye texgen: MV and its inverse transpose */
    if (d & (D_LIGHT | D_TEXGEN | D_FOG))
        d = Hw_Rare(c, d);          /* light.c */
    /* a frame dropped while this was written (a flush in one of the
     * emitters): it went with the frame, and the rebase asked for all */
    c->dirty = (c->rebase_gen == gen) ? (d & D_VTXFMT) : D_ALL;
    return cs->error;
}


#undef HWV
#undef HWV_UNITS
