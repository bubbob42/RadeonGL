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
    for (u = 0; u < CORE_NARROW; u++)
        if (c->tex_enabled[u] && Core_Texture(c, u) != NULL)
            on |= 1UL << u;
    /* Units 2.. only once one of them is on: wide_on (tex13.c sets it with
     * every enable above unit 1), so GLQuake loops twice, as in 0.16.
     * ab_gl13x 2026-11-18: a CORE_UNITS 2 build won half of phase 1's
     * GLQuake cost, the per-draw unit loops being what it shortens. Their
     * loop is tex13.c's: here it took seven registers on every call. */
    if (c->wide_on)
        return Tex13_UnitsOn(c, on);
    return on;
}

/* RB3D_PLANEMASK for glColorMask's bits (1 R, 2 G, 4 B, 8 A) in the
 * target's format; ARGB1555 since W1 (a program's 15-bit bitmap) */
ULONG Core_PlaneMask(ULONG format, ULONG m)
{
    if (format == R_COLOR_FORMAT_RGB565)
        return ((m & 1) ? 0xf800UL : 0) | ((m & 2) ? 0x07e0UL : 0) | ((m & 4) ? 0x001fUL : 0);
    if (format == R_COLOR_FORMAT_ARGB1555)
        return ((m & 8) ? 0x8000UL : 0) | ((m & 1) ? 0x7c00UL : 0) | ((m & 2) ? 0x03e0UL : 0)
             | ((m & 4) ? 0x001fUL : 0);
    return ((m & 8) ? 0xff000000UL : 0) | ((m & 1) ? 0x00ff0000UL : 0)
         | ((m & 2) ? 0x0000ff00UL : 0) | ((m & 4) ? 0x000000ffUL : 0);
}

static ULONG PlaneMask(struct Core *c)
{
    return Core_PlaneMask(c->target.color_format, c->color_mask);
}

static ULONG UcpCtl(struct Core *c)
{
    ULONG v;

    v = R_UCP_IN_CLIP_SPACE | c->x_ucp;     /* the TCL's fog mode (Core_Extras) */
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

void Hw_Forget(struct Core *c)
{
    ULONG u;
    ULONG i;

    c->hw_units = 0xffffffffUL;
    for (u = 0; u < CORE_UNITS; u++)
    {
        for (i = 0; i < 7; i++)
            c->hw_tex[u][i] = 0xffffffffUL;
        for (i = 0; i < 4; i++)
            c->hw_comb[u][i] = 0xffffffffUL;
    }
    c->hw_ppcntl = 0xffffffffUL;
    c->hw_mv_ver = 0xffffffffUL;
    c->hw_proj_ver = 0xffffffffUL;
    c->hw_mvl_ver = 0xffffffffUL;
    for (i = 0; i < 6; i++)
        c->hw_light[i] = 0xffffffffUL;
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

void Hw_VertexFormat(struct Core *c, ULONG units)
{
    ULONG fmt1;
    ULONG compsel;

    /* lighting switched sets D_VTXFMT (state.c), so units decide alone */
    if (units == c->hw_units && !(c->dirty & D_VTXFMT))
        return;
    fmt1 = ((units & 1) ? 2UL : 0) | ((units & 2) ? 2UL << 3 : 0);
    if (c->wide_on)
        fmt1 = Tex13_Fmt1(c, units);        /* units above 1, cube maps' three (tex13.c) */
    /* COMP_SEL names the outputs TCL COMPUTES: the lit colour (lighting
     * on) and transformed texture coordinates (a texture matrix or texgen).
     * Neither is in the core yet, so colour and coordinates pass through.
     * Mesa sets them the same way (r200UpdateSpecular,
     * update_texturematrix); MEASURED on the A4000D (Tri, 2026-11-02):
     * OUTPUT_COLOR_0 with lighting off draws uninitialised material
     * colours. */
    /* lighting, fog, texgen (light.c Core_Extras): the normal, the lit
     * colour, the fog factor's colour, computed texture coordinates */
    compsel = R_OUTPUT_XYZW | c->x_compsel;
    R2E_VertexFormat(c->cs, R_VTX_Z0 | c->x_fmt0 | (R_VTX_PK_RGBA << R_VTX_COLOR_0_SHIFT),
                     fmt1, R_VTX_Z0 | R_VTX_W0 | (R_VTX_FP_RGBA << R_VTX_COLOR_0_SHIFT) | c->x_out0,
                     fmt1, compsel);
    /* A cube map's third coordinate is r, not q: RE_CNTL's STQn_D3D for its
     * coordinate set, or the rasterizer hands the sampler s/q, t/q and no r
     * -- every face read as +Z (MEASURED, GLSmoke13 T4, 2026-11-14; Mesa
     * set_re_cntl_d3d does the same for cube, 3D and rectangle textures) */
    if (c->wide_on || c->hw_stq_d3d != 0)
        Tex13_ReCntl(c, units);
    c->hw_units = units;
    c->dirty &= ~D_VTXFMT;
}

ULONG Core_ScissorEmpty(struct Core *c)
{
    LONG H;

    H = (LONG) c->target.height;
    if (c->sc_w <= 0 || c->sc_h <= 0 || c->sc_x >= (LONG) c->target.width
        || c->sc_y >= H || c->sc_x + c->sc_w <= 0 || c->sc_y + c->sc_h <= 0)
        return 1;
    return 0;
}

/* The narrow copy (hwvalid.h): where ValidateOnce always was, on GLQuake's way */
#define HWV_WIDE 0
#include "hwvalid.h"
#undef HWV_WIDE

static LONG ValidateOnce_W(struct Core *c);

/* GL 1.3's escapes only for a context that uses a GL 1.3 feature (hwvalid.h) */
#define VALIDATE_ONCE(c) (((c)->tex_x | (c)->comb_x | (c)->wide_on) != 0 \
                         ? ValidateOnce_W(c) : ValidateOnce_N(c))

LONG Core_Validate(struct Core *c)
{
    LONG e;

    if ((c->dirty & ~D_VTXFMT) == 0)
        return R2_OK;               /* most draws: nothing to send */
    e = VALIDATE_ONCE(c);
    if ((c->dirty & ~D_VTXFMT) != 0)
        e = VALIDATE_ONCE(c);       /* into the fresh buffer: room enough */
    return e;
}

/* The wide copy (hwvalid.h): GL 1.3, away from the hot code */
#define HWV_WIDE 1
#include "hwvalid.h"
#undef HWV_WIDE
