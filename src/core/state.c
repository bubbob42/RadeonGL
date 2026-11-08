/*
 * state.c -- the GL state setters. Each stores the GL value and marks the
 * R200 register blocks it affects; hwstate.c encodes them before the next
 * draw. Validation follows the GL 1.1 rules (MiniGLV3D's checks, with the
 * clamps it missed: alpha reference, depth range, scissor).
 */

#include "core.h"
#include "coreint.h"

static float Clamp01(double v)
{
    if (!(v > 0.0))
        return 0.0f;
    if (v > 1.0)
        return 1.0f;
    return (float) v;
}

static ULONG Bit(ULONG cap)
{
    switch (cap)
    {
        case GL_ALPHA_TEST:             return EN_ALPHA_TEST;
        case GL_BLEND:                  return EN_BLEND;
        case GL_CULL_FACE:              return EN_CULL_FACE;
        case GL_DEPTH_TEST:             return EN_DEPTH_TEST;
        case GL_DITHER:                 return EN_DITHER;
        case GL_FOG:                    return EN_FOG;
        case GL_SCISSOR_TEST:           return EN_SCISSOR;
        case GL_POLYGON_OFFSET_FILL:    return EN_POLY_OFFSET;
        case MGL_Z_OFFSET:              return EN_ZOFFSET;
        case GL_LIGHTING:               return EN_LIGHTING;
        case GL_STENCIL_TEST:           return EN_STENCIL;
        case MGL_PERSPECTIVE_MAPPING:   return EN_PERSPECTIVE;
        case GL_NORMALIZE:              return EN_NORMALIZE;
        case GL_COLOR_MATERIAL:         return EN_COLOR_MATERIAL;
        case GL_LINE_STIPPLE:           return EN_LINE_STIPPLE;
        case GL_POLYGON_STIPPLE:        return EN_POLY_STIPPLE;
        case GL_COLOR_LOGIC_OP:         return EN_LOGIC_OP;
        case GL_TEXTURE_GEN_S:          return EN_TEXGEN_S;
        case GL_TEXTURE_GEN_T:          return EN_TEXGEN_T;
        case GL_POINT_SMOOTH:
        case GL_LINE_SMOOTH:
        case GL_POLYGON_SMOOTH:         return EN_SMOOTH;
    }
    if (cap >= GL_LIGHT0 && cap <= GL_LIGHT0 + 7)
        return EN_LIGHT0 << (cap - GL_LIGHT0);
    return 0;
}

static ULONG Affects(ULONG bit)
{
    switch (bit)
    {
        case EN_ALPHA_TEST:     return D_PPCNTL;
        case EN_BLEND:          return D_RB3D;
        case EN_CULL_FACE:      return D_SECNTL;
        case EN_DEPTH_TEST:     return D_RB3D;
        case EN_DITHER:         return D_RB3D;
        case EN_SCISSOR:        return D_SCISSOR;
        case EN_POLY_OFFSET:    return D_SECNTL | D_ZBIAS;
        case EN_ZOFFSET:        return D_SECNTL | D_ZBIAS;
        /* lighting: the TCL's controls, the normals in the vertices, the
         * modelview and its inverse transpose */
        case EN_LIGHTING:       return D_LIGHT | D_VTXFMT | D_MATRIX;
        case EN_NORMALIZE:      return D_LIGHT;
        case EN_COLOR_MATERIAL: return D_LIGHT;
        /* fog (fog.c): PP_CNTL, the TCL's fog mode, the specular output
         * colour that carries the factor, the modelview for the depth */
        case EN_FOG:            return D_FOG | D_PPCNTL | D_SECNTL | D_VTXFMT | D_MATRIX;
    }
    if (bit >= EN_LIGHT0 && bit <= (EN_LIGHT0 << 7))
        return D_LIGHT;         /* lights not drawn before: their vectors (light.c) */
    return 0;
}

void Core_Enable(struct Core *c, ULONG cap, ULONG on)
{
    ULONG bit;

    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    if (cap == GL_TEXTURE_2D)
    {
        if ((c->tex_enabled[c->active_unit] != 0) != (on != 0))
        {
            c->tex_enabled[c->active_unit] = on ? 1 : 0;
            DIRTY_UNIT(c, c->active_unit);
        }
        return;
    }
    if (cap == GL_TEXTURE_GEN_S || cap == GL_TEXTURE_GEN_T)
    {
        Core_TexGenEnable(c, cap, on);  /* per unit, as GL_TEXTURE_2D (texgen.c) */
        return;
    }
    bit = Bit(cap);
    if (bit == 0)
    {
        Compat_Enable(c, cap, on);  /* 29.1's other caps, else GL_INVALID_ENUM (compat.c) */
        return;
    }
    if (((c->enables & bit) != 0) == (on != 0))
        return;
    c->enables ^= bit;
    c->dirty |= Affects(bit);
    if (bit & (EN_LIGHTING | EN_FOG))
        Core_UpdateVnorm(c);        /* normals, and what lighting and fog add (Core_Extras) */
}

ULONG Core_IsEnabled(struct Core *c, ULONG cap)
{
    ULONG bit;

    if (cap == GL_TEXTURE_2D)
        return c->tex_enabled[c->active_unit];
    if (cap == GL_TEXTURE_GEN_S || cap == GL_TEXTURE_GEN_T)
        return Core_TexGenEnabled(c, cap);
    bit = Bit(cap);
    if (bit == 0)
        return Compat_IsEnabled(c, cap);    /* 29.1's other caps, else GL_INVALID_ENUM */
    return (c->enables & bit) ? 1UL : 0UL;
}

void Core_BlendFunc(struct Core *c, ULONG src, ULONG dst)
{
    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    /* GL 1.1: SRC_COLOR / ONE_MINUS_SRC_COLOR only as destination,
     * DST_COLOR / ONE_MINUS_DST_COLOR / SRC_ALPHA_SATURATE only as source */
    if (Hw_BlendFactor(src) == 0 || src == GL_SRC_COLOR || src == GL_ONE_MINUS_SRC_COLOR
        || Hw_BlendFactor(dst) == 0 || dst == GL_DST_COLOR || dst == GL_ONE_MINUS_DST_COLOR
        || dst == GL_SRC_ALPHA_SATURATE)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    if (c->blend_sep)
        Compat_BlendFuncReset(c);   /* glBlendFuncSeparate's alpha factors end (compat.c) */
    if (src == c->blend_src && dst == c->blend_dst)
        return;
    c->blend_src = src;
    c->blend_dst = dst;
    c->dirty |= D_BLEND;
}

void Core_AlphaFunc(struct Core *c, ULONG func, float ref)
{
    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    if (Hw_DepthFunc(func) == 0xffffffffUL)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    c->alpha_func = func;
    c->alpha_ref = Clamp01(ref);
    c->dirty |= D_ALPHA;
}

void Core_DepthFunc(struct Core *c, ULONG func)
{
    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    if (Hw_DepthFunc(func) == 0xffffffffUL)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    if (func == c->depth_func)
        return;
    c->depth_func = func;
    c->dirty |= D_ZSTENCIL;
}

void Core_DepthMask(struct Core *c, ULONG flag)
{
    flag = flag ? 1 : 0;
    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    if (flag == c->depth_mask)
        return;
    c->depth_mask = flag;
    c->dirty |= D_ZSTENCIL;
}

void Core_DepthRange(struct Core *c, double n, double f)
{
    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    c->depth_near = Clamp01(n);
    c->depth_far = Clamp01(f);
    c->dirty |= D_VIEWPORT;
}

void Core_CullFace(struct Core *c, ULONG mode)
{
    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    if (mode != GL_FRONT && mode != GL_BACK && mode != GL_FRONT_AND_BACK)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    c->cull_mode = mode;
    c->dirty |= D_SECNTL;
}

void Core_FrontFace(struct Core *c, ULONG mode)
{
    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    if (mode != GL_CW && mode != GL_CCW)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    c->front_face = mode;
    c->dirty |= D_SECNTL;
}

void Core_ShadeModel(struct Core *c, ULONG mode)
{
    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    if (mode != GL_FLAT && mode != GL_SMOOTH)
    {
        Core_Error(c, GL_INVALID_ENUM);   /* MiniGLV3D accepted anything */
        return;
    }
    if (mode == c->shade_model)
        return;
    c->shade_model = mode;
    c->dirty |= D_SECNTL;
}

void Core_ColorMask(struct Core *c, ULONG r, ULONG g, ULONG b, ULONG a)
{
    ULONG m;

    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    m = (r ? 1UL : 0) | (g ? 2UL : 0) | (b ? 4UL : 0) | (a ? 8UL : 0);
    if (m == c->color_mask)
        return;
    c->color_mask = m;
    c->dirty |= D_PLANEMASK | D_RB3D;
}

void Core_Viewport(struct Core *c, LONG x, LONG y, LONG w, LONG h)
{
    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    if (w < 0 || h < 0)
    {
        Core_Error(c, GL_INVALID_VALUE);
        return;
    }
    if (w > CORE_MAX_TEXSIZE * 2)
        w = CORE_MAX_TEXSIZE * 2;
    if (h > CORE_MAX_TEXSIZE * 2)
        h = CORE_MAX_TEXSIZE * 2;
    c->vp_x = x;
    c->vp_y = y;
    c->vp_w = w;
    c->vp_h = h;
    c->dirty |= D_VIEWPORT;
}

void Core_Scissor(struct Core *c, LONG x, LONG y, LONG w, LONG h)
{
    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    if (w < 0 || h < 0)
    {
        Core_Error(c, GL_INVALID_VALUE);
        return;
    }
    c->sc_x = x;
    c->sc_y = y;
    c->sc_w = w;
    c->sc_h = h;
    c->dirty |= D_SCISSOR;
}

void Core_PolygonOffset(struct Core *c, float factor, float units)
{
    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    c->poly_factor = factor;
    c->poly_units = units;
    c->dirty |= D_ZBIAS;
}

void Core_PolygonMode(struct Core *c, ULONG face, ULONG mode)
{
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
    c->polygon_mode = mode;     /* stored: only GL_FILL is drawn so far */
}

void Core_PointSize(struct Core *c, float size)
{
    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    if (!(size > 0.0f))
    {
        Core_Error(c, GL_INVALID_VALUE);
        return;
    }
    c->point_size = size;
    c->dirty |= D_POINTSIZE;
}

void Core_ClearColor(struct Core *c, float r, float g, float b, float a)
{
    c->clear_color[0] = Clamp01(r);
    c->clear_color[1] = Clamp01(g);
    c->clear_color[2] = Clamp01(b);
    c->clear_color[3] = Clamp01(a);
}

void Core_ClearDepth(struct Core *c, double d)
{
    c->clear_depth = Clamp01(d);
}

void Core_Hint(struct Core *c, ULONG target, ULONG mode)
{
    /* RadeonGL's static arrays (GL_RGL_static_arrays): GL_NICEST on,
     * GL_DONT_CARE off, GL_FASTEST off and every address key forgotten */
    if (target == GL_RGL_STATIC_ARRAYS_HINT || target == GL_RGL_STATIC_INDICES_HINT)
    {
        ULONG *flag = (target == GL_RGL_STATIC_ARRAYS_HINT) ? &c->static_arrays : &c->static_indices;

        if (mode == GL_NICEST)
            *flag = 1;
        else if (mode == GL_DONT_CARE)
            *flag = 0;
        else if (mode == GL_FASTEST)
        {
            *flag = 0;
            c->static_gen++;        /* every address key, arrays and indices */
        }
        else
            Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    /* GL_RGL_arrays_tag: any value, kept as it is */
    if (target == GL_RGL_ARRAYS_TAG_HINT)
    {
        c->static_tag = mode;
        return;
    }
    /* GL_RGL_dynamic_arrays: GL_NICEST on, GL_DONT_CARE and GL_FASTEST off */
    if (target == GL_RGL_DYNAMIC_ARRAYS_HINT)
    {
        if (mode == GL_NICEST)
            c->dynamic_arrays = 1;
        else if (mode == GL_DONT_CARE || mode == GL_FASTEST)
            c->dynamic_arrays = 0;
        else
            Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    /* GL's five targets and MiniGL's two (glHint(MGL_W_ONE_HINT): GLQuake);
     * every other hint is accepted and changes nothing */
    if ((target < GL_PERSPECTIVE_CORRECTION_HINT || target > GL_FOG_HINT)
        && target != MGL_W_ONE_HINT && target != MGL_FIXPOINTTRANS_HINT)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    if (mode != GL_FASTEST && mode != GL_NICEST && mode != GL_DONT_CARE)
        Core_Error(c, GL_INVALID_ENUM);
}

void Core_SetZOffset(struct Core *c, float offset)
{
    c->zoffset = offset;
    c->dirty |= D_ZBIAS;
}

void Core_ActiveTexture(struct Core *c, ULONG unit)
{
    if (unit < GL_TEXTURE0_ARB || unit >= GL_TEXTURE0_ARB + CORE_UNITS)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    c->active_unit = unit - GL_TEXTURE0_ARB;
}

void Core_ClientActiveTexture(struct Core *c, ULONG unit)
{
    if (unit < GL_TEXTURE0_ARB || unit >= GL_TEXTURE0_ARB + CORE_UNITS)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    c->client_unit = unit - GL_TEXTURE0_ARB;
}

void Core_TexEnvi(struct Core *c, ULONG target, ULONG pname, LONG param)
{
    ULONG u;

    if (target != GL_TEXTURE_ENV || pname != GL_TEXTURE_ENV_MODE)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    if ((ULONG) param != GL_MODULATE && (ULONG) param != GL_REPLACE
        && (ULONG) param != GL_DECAL && (ULONG) param != GL_BLEND
        && (ULONG) param != GL_ADD)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    u = c->active_unit;
    if (c->tex_env[u] == (ULONG) param)
        return;
    c->tex_env[u] = (ULONG) param;
    c->dirty |= D_COMBINE;
}
