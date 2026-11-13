/*
 * get.c -- glGet*, glGetString.
 *
 * The queries a GL 1.1 client and GLQuake use; the rest raise
 * GL_INVALID_ENUM. MiniGLV3D's full table (others.c), fixed where the D2
 * review found gaps, comes with the remaining slots.
 */

#include "core.h"
#include "coreint.h"

ULONG Core_GetCount(ULONG pname)
{
    switch (pname)
    {
        case GL_MODELVIEW_MATRIX:
        case GL_PROJECTION_MATRIX:
        case GL_TEXTURE_MATRIX:     return 16;
        case GL_VIEWPORT:
        case GL_SCISSOR_BOX:
        case GL_CURRENT_COLOR:
        case GL_COLOR_CLEAR_VALUE:
        case GL_CURRENT_TEXTURE_COORDS:
        case GL_COLOR_WRITEMASK:    return 4;
        case GL_CURRENT_NORMAL:     return 3;
        case GL_DEPTH_RANGE:        return 2;
    }
    return 1;
}

/* Every query as floats; GetIntegerv converts. Returns 0 for an unknown
 * pname. */
static ULONG Query(struct Core *c, ULONG pname, float *v)
{
    ULONG i;
    const float *m;
    ULONG col;

    switch (pname)
    {
        case GL_MODELVIEW_MATRIX:
        case GL_PROJECTION_MATRIX:
        case GL_TEXTURE_MATRIX:
            m = Core_TopMatrix(c, pname == GL_MODELVIEW_MATRIX ? GL_MODELVIEW
                               : pname == GL_PROJECTION_MATRIX ? GL_PROJECTION : GL_TEXTURE)->m;
            for (i = 0; i < 16; i++)
                v[i] = m[i];
            return 1;
        case GL_VIEWPORT:
            v[0] = (float) c->vp_x; v[1] = (float) c->vp_y;
            v[2] = (float) c->vp_w; v[3] = (float) c->vp_h;
            return 1;
        case GL_SCISSOR_BOX:
            v[0] = (float) c->sc_x; v[1] = (float) c->sc_y;
            v[2] = (float) c->sc_w; v[3] = (float) c->sc_h;
            return 1;
        case GL_DEPTH_RANGE:
            v[0] = c->depth_near; v[1] = c->depth_far;
            return 1;
        case GL_CURRENT_COLOR:
            if (c->cur_colorf[0] < 0.0f)
            {
                col = c->cur_color;
                for (i = 0; i < 4; i++)
                    c->cur_colorf[i] = (float) ((col >> (8 * i)) & 0xff) / 255.0f;
            }
            for (i = 0; i < 4; i++)
                v[i] = c->cur_colorf[i];
            return 1;
        case GL_CURRENT_TEXTURE_COORDS:
            v[0] = c->cur_st[c->active_unit][0];
            v[1] = c->cur_st[c->active_unit][1];
            v[2] = 0.0f;
            v[3] = 1.0f;
            return 1;
        case GL_CURRENT_NORMAL:
            v[0] = c->cur_normal[0]; v[1] = c->cur_normal[1]; v[2] = c->cur_normal[2];
            return 1;
        case GL_COLOR_CLEAR_VALUE:
            for (i = 0; i < 4; i++)
                v[i] = c->clear_color[i];
            return 1;
        case GL_COLOR_WRITEMASK:
            for (i = 0; i < 4; i++)
                v[i] = (c->color_mask & (1UL << i)) ? 1.0f : 0.0f;
            return 1;
        case GL_DEPTH_CLEAR_VALUE:  v[0] = c->clear_depth; return 1;
        case GL_ALPHA_TEST_REF:     v[0] = c->alpha_ref; return 1;
        case GL_POINT_SIZE:         v[0] = c->point_size; return 1;
        case GL_MAX_TEXTURE_SIZE:   v[0] = (float) CORE_MAX_TEXSIZE; return 1;
        case GL_MAX_TEXTURE_UNITS_ARB: v[0] = (float) CORE_UNITS; return 1;
        case GL_MAX_MODELVIEW_STACK_DEPTH:  v[0] = (float) CORE_MV_DEPTH; return 1;
        case GL_MAX_PROJECTION_STACK_DEPTH: v[0] = (float) CORE_P_DEPTH; return 1;
        case GL_MAX_TEXTURE_STACK_DEPTH:    v[0] = (float) CORE_T_DEPTH; return 1;
        case GL_MATRIX_MODE:        v[0] = (float) c->matrix_mode; return 1;
        case GL_DEPTH_FUNC:         v[0] = (float) c->depth_func; return 1;
        case GL_DEPTH_WRITEMASK:    v[0] = (float) c->depth_mask; return 1;
        case GL_BLEND_SRC:          v[0] = (float) c->blend_src; return 1;
        case GL_BLEND_DST:          v[0] = (float) c->blend_dst; return 1;
        case GL_ALPHA_TEST_FUNC:    v[0] = (float) c->alpha_func; return 1;
        case GL_CULL_FACE_MODE:     v[0] = (float) c->cull_mode; return 1;
        case GL_FRONT_FACE:         v[0] = (float) c->front_face; return 1;
        case GL_SHADE_MODEL:        v[0] = (float) c->shade_model; return 1;
        case GL_TEXTURE_BINDING_2D: v[0] = (float) c->bound[c->active_unit]; return 1;
        case GL_ACTIVE_TEXTURE_ARB: v[0] = (float) (GL_TEXTURE0_ARB + c->active_unit); return 1;
        case GL_CLIENT_ACTIVE_TEXTURE_ARB:
            v[0] = (float) (GL_TEXTURE0_ARB + c->client_unit);
            return 1;
    }
    /* the enable caps are queries too */
    switch (pname)
    {
        case GL_ALPHA_TEST: case GL_BLEND: case GL_CULL_FACE: case GL_DEPTH_TEST:
        case GL_DITHER: case GL_FOG: case GL_SCISSOR_TEST: case GL_POLYGON_OFFSET_FILL:
        case GL_LIGHTING: case GL_TEXTURE_2D:
            v[0] = (float) Core_IsEnabled(c, pname);
            return 1;
    }
    return 0;
}

void Core_GetFloatv(struct Core *c, ULONG pname, float *out)
{
    float v[16];
    ULONG i;

    if (!Query(c, pname, v))
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    for (i = 0; i < Core_GetCount(pname); i++)
        out[i] = v[i];
}

void Core_GetIntegerv(struct Core *c, ULONG pname, LONG *out)
{
    float v[16];
    ULONG i;
    ULONG n;

    if (!Query(c, pname, v))
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    n = Core_GetCount(pname);
    for (i = 0; i < n; i++)
    {
        /* colours and depth values in [0, 1] map to the full integer range
         * (GL 1.1 sec. 6.1.2); everything else rounds */
        if (pname == GL_CURRENT_COLOR || pname == GL_COLOR_CLEAR_VALUE
            || pname == GL_DEPTH_RANGE || pname == GL_DEPTH_CLEAR_VALUE)
            out[i] = (v[i] >= 1.0f) ? 0x7fffffffL : Core_Floor(v[i] * 2147483647.0f);
        else
            out[i] = Core_Floor(v[i] + 0.5f);
    }
}

/* The space 0.12's GL_EXTENSIONS literal took here (156 bytes, in the code
 * hunk in front of frame.c and array.c): kept, so the hot array and emit
 * code stays where the 0.12 A/B measured it (tools/hot_layout.py). The
 * string itself lives in compat.c now. Remove it with the next accepted
 * layout change (then gmake hot-layout-ref). */
const char Core_GetPad[156] = "layout pad";

const char *Core_GetString(struct Core *c, ULONG name)
{
    switch (name)
    {
        case GL_VENDOR:     return "RadeonGL";
        case GL_RENDERER:   return "RadeonGL R200 (ATI Radeon 9200)";
        case GL_VERSION:    return "1.1";
        case GL_EXTENSIONS:
            /* compat.c holds it: a literal here sits in front of the hot
             * array code, so every new name moved it (hot_layout.py).
             * The library answers with Rglc_Extensions (rglcompat.c). */
            return Core_Extensions;
    }
    Core_Error(c, GL_INVALID_ENUM);
    return NULL;
}
