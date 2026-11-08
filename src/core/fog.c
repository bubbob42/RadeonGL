/*
 * fog.c -- GL fog and wide lines on the R200 (2026-11-07, for Reflector's
 * floor fading into the distance and its thicker outlines).
 *
 * Fog is the TCL's: with a fog mode in SE_TCL_UCP_VERT_BLEND_CTL it puts
 * each vertex's fog factor into the specular colour's alpha (an output
 * colour of its own, COMP_SEL COLOR_1), and the pixel pipeline blends the
 * fog colour in by it (PP_FOG_COLOR: FOG_USE_SPEC_ALPHA; PP_CNTL
 * FOG_ENABLE). The factor comes from the eye-space depth, so the modelview
 * goes to the TCL (light.c Light_Matrices). The coefficients are Mesa's
 * (r200_state.c r200Fogfv), in the fog vector at 0x5d: (0, c, d, 0) with
 *   GL_LINEAR  c = end / (end - start), d = -1 / (end - start)
 *   GL_EXP     c = 0, d = -density
 *   GL_EXP2    c = 0, d = -density^2
 *
 * Lines wider than one pixel: SE_LINE_WIDTH in 1/16 pixels (U6.4) and
 * SE_CNTL's WIDELINE_ENABLE (Mesa r200LineWidth).
 */

#include "core.h"
#include "coreint.h"
#include "r200reg.h"

#define FOG_USE_SPEC_ALPHA (3UL << 25)
#define VS_FOG_PARAM       0x5d

void Fog_Defaults(struct Core *c)
{
    c->fog_mode = GL_EXP;
    c->fog_tcl = 1UL << 8;
    c->fog_density = 1.0f;
    c->fog_start = 0.0f;
    c->fog_end = 1.0f;
    c->fog_color[0] = c->fog_color[1] = c->fog_color[2] = c->fog_color[3] = 0.0f;
    c->line_width = 1.0f;
    c->pack_align = 4;              /* glPixelStorei's defaults (compat.c) */
    c->unpack_align = 4;
    Compat_Defaults(c);             /* the rest of 29.1's state (compat.c) */
    Fog_Forget(c);
}

void Fog_Forget(struct Core *c)
{
    c->hw_fog[0] = c->hw_fog[1] = c->hw_fog[2] = c->hw_linew = 0xffffffffUL;
}

void Core_Fogfv(struct Core *c, ULONG pname, const float *v, ULONG n)
{
    ULONG mode;

    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    if (v == NULL)
    {
        Core_Error(c, GL_INVALID_VALUE);
        return;
    }
    switch (pname)
    {
        case GL_FOG_MODE:
            mode = (ULONG) v[0];
            if (mode != GL_LINEAR && mode != GL_EXP && mode != GL_EXP2)
            {
                Core_Error(c, GL_INVALID_ENUM);
                return;
            }
            c->fog_mode = mode;
            c->fog_tcl = (mode == GL_LINEAR) ? (3UL << 8) : (mode == GL_EXP2) ? (2UL << 8) : (1UL << 8);
            Core_Extras(c);
            c->dirty |= D_SECNTL;   /* the TCL's fog mode */
            break;
        case GL_FOG_DENSITY:
            if (v[0] < 0.0f)
            {
                Core_Error(c, GL_INVALID_VALUE);
                return;
            }
            c->fog_density = v[0];
            break;
        case GL_FOG_START:
            c->fog_start = v[0];
            break;
        case GL_FOG_END:
            c->fog_end = v[0];
            break;
        case GL_FOG_COLOR:
            if (n < 4)
            {
                Core_Error(c, GL_INVALID_ENUM);
                return;
            }
            c->fog_color[0] = v[0];
            c->fog_color[1] = v[1];
            c->fog_color[2] = v[2];
            c->fog_color[3] = v[3];
            break;
        case GL_FOG_INDEX:
            return;                 /* colour-index mode only */
        default:
            Core_Error(c, GL_INVALID_ENUM);
            return;
    }
    c->dirty |= D_FOG;
}

void Core_LineWidth(struct Core *c, float w)
{
    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    if (!(w > 0.0f))
    {
        Core_Error(c, GL_INVALID_VALUE);
        return;
    }
    if (w > 64.0f)
        w = 64.0f;                  /* U6.4 */
    c->line_width = w;
    c->wide_line = (w > 1.0f) ? 1UL : 0UL;
    Core_Extras(c);
    c->dirty |= D_FOG | D_SECNTL;
}

void Fog_Emit(struct Core *c)
{
    ULONG v[4];
    ULONG col;
    float cc, d;

    v[0] = (ULONG) Core_Floor(c->line_width * 16.0f + 0.5f) & 0xffffUL;
    if (v[0] != c->hw_linew)
    {
        R2E_Reg(c->cs, R_SE_LINE_WIDTH, v[0]);
        c->hw_linew = v[0];
    }
    if (!(c->enables & EN_FOG))
        return;
    col = (Core_Unit255(c->fog_color[0]) << 16) | (Core_Unit255(c->fog_color[1]) << 8)
        | Core_Unit255(c->fog_color[2]) | FOG_USE_SPEC_ALPHA;
    if (col != c->hw_fog[0])
    {
        R2E_Reg(c->cs, R_PP_FOG_COLOR, col);
        c->hw_fog[0] = col;
    }
    if (c->fog_mode == GL_LINEAR)
    {
        if (c->fog_end == c->fog_start)
        {
            cc = 1.0f;
            d = 1.0f;
        }
        else
        {
            cc = c->fog_end / (c->fog_end - c->fog_start);
            d = -1.0f / (c->fog_end - c->fog_start);
        }
    }
    else
    {
        cc = 0.0f;
        d = (c->fog_mode == GL_EXP2) ? -(c->fog_density * c->fog_density) : -c->fog_density;
    }
    v[0] = 0;
    v[1] = R2Cs_FloatBits(cc);
    v[2] = R2Cs_FloatBits(d);
    v[3] = 0;
    if (v[1] != c->hw_fog[1] || v[2] != c->hw_fog[2])
    {
        R2E_Vectors(c->cs, VS_FOG_PARAM, 4, v);
        c->hw_fog[1] = v[1];
        c->hw_fog[2] = v[2];
    }
}
