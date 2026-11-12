/*
 * texgen.c -- texture coordinate generation and texture matrices on the
 * R200's TCL (2026-11-07, for Reflector's chrome and planar mapping).
 *
 * Per texture unit the TCL either passes the vertex's coordinates through
 * or computes them: from the object or eye position through a matrix
 * (GL_OBJECT_LINEAR, GL_EYE_LINEAR: the planes are the matrix's rows), as
 * a sphere map (GL_SPHERE_MAP: from the eye position and the normal), and
 * any of these times the texture matrix. The registers and the rules are
 * Mesa's r200 driver's (r200_texstate.c r200_validate_texgen,
 * set_texgen_matrix; r200_state.c update_texturematrix), MIT licensed:
 *
 *   SE_TCL_TEX_PROC_CTL_2  per unit 4 bits: the components PASSED THROUGH
 *                          from the input (S T R Q) -- the others are
 *                          generated (Mesa r200_validate_texgen; MEASURED
 *                          2026-11-07: the opposite reading gave s = t = 0)
 *   SE_TCL_TEX_PROC_CTL_3  per unit: which vertex coordinate set it reads
 *   SE_TCL_TEX_PROC_CTL_0  bit u: the TCL computes unit u's coordinates;
 *                          bit 8 + u: through the unit's matrix
 *   SE_TCL_TEX_PROC_CTL_1  per unit 4 bits: the input -- a coordinate set,
 *                          8 the object position, 9 the eye position,
 *                          0xd the sphere map
 *   the matrices at R_VS_MATRIX_3_TEX0 + 4 u; OUTPUT_TEX_u in the vertex
 *   format's COMP_SEL (hwstate.c Hw_VertexFormat, from c->tcl_tex)
 *
 * S and T share one input on the R200: a unit whose S and T ask for
 * different modes is drawn with S's (Mesa falls back to software there).
 * A sphere map needs the normals in the vertices (c->vnorm) and the
 * local viewer (light.c), and both it and GL_EYE_LINEAR need the modelview
 * in the TCL (light.c Light_Matrices).
 */

#include "core.h"
#include "coreint.h"
#include "r200reg.h"

#define TG_INPUT_OBJ    8UL
#define TG_INPUT_EYE    9UL
#define TG_INPUT_SPHERE 0xdUL
#define TG_COMP_S       1UL
#define TG_COMP_T       2UL

static const float ident[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

void Texgen_Defaults(struct Core *c)
{
    ULONG u;
    ULONG k;

    for (u = 0; u < CORE_UNITS; u++)
    {
        c->tg_on[u] = 0;
        for (k = 0; k < 2; k++)
        {
            c->tg_mode[u][k] = GL_EYE_LINEAR;
            c->tg_obj[u][k][0] = c->tg_eye[u][k][0] = (k == 0) ? 1.0f : 0.0f;
            c->tg_obj[u][k][1] = c->tg_eye[u][k][1] = (k == 1) ? 1.0f : 0.0f;
            c->tg_obj[u][k][2] = c->tg_eye[u][k][2] = 0.0f;
            c->tg_obj[u][k][3] = c->tg_eye[u][k][3] = 0.0f;
        }
    }
    Texgen_Forget(c);
}

void Texgen_Forget(struct Core *c)
{
    ULONG u;
    ULONG i;

    /* the registers as R2E_TclDefaults leaves them; the matrices unknown */
    c->hw_tg[0] = 0x00ffffffUL;
    c->hw_tg[1] = 0x00543210UL;
    c->hw_tg[2] = 0;
    c->hw_tg[3] = 0x00543210UL;
    c->hw_tg[4] = 0;
    for (u = 0; u < CORE_UNITS; u++)
        for (i = 0; i < 16; i++)
            c->hw_tm[u][i] = -12345.0f;
}

/* Which units take a sphere map (they need normals and the local viewer),
 * and whether any needs the eye position (sphere or eye linear) */
static void Needs(struct Core *c)
{
    ULONG u;
    ULONG m;

    c->tg_sphere = 0;
    c->tg_eyepos = 0;
    for (u = 0; u < CORE_UNITS; u++)
    {
        if (c->tg_on[u] == 0)
            continue;
        m = c->tg_mode[u][(c->tg_on[u] & 1) ? 0 : 1];
        if (m == GL_SPHERE_MAP)
            c->tg_sphere |= 1UL << u;
        if (m == GL_SPHERE_MAP || m == GL_EYE_LINEAR)
            c->tg_eyepos |= 1UL << u;
    }
}

/* Who hears of a vnorm or pm_mask change (the library's task table, lib/
 * rgltable.c): one function for every context, set once */
CoreStateHook core_state_hook = NULL;

void Core_SetStateHook(CoreStateHook fn)
{
    core_state_hook = fn;
}

/* lighting on, or a sphere map: the vertices carry normals (vertex.c) */
void Core_UpdateVnorm(struct Core *c)
{
    ULONG v;

    v = ((c->enables & EN_LIGHTING) || c->tg_sphere) ? 1UL : 0UL;
    if (v != c->vnorm)
    {
        c->vnorm = v;
        c->dirty |= D_VTXFMT;
        if (core_state_hook != NULL)
            core_state_hook(c);
    }
    Core_Extras(c);
}

static void Changed(struct Core *c)
{
    Needs(c);
    Core_UpdateVnorm(c);            /* and Core_Extras: x_eye follows tg_eyepos */
    c->dirty |= D_TEXGEN | D_VTXFMT | D_MATRIX | D_LIGHT;
}

void Core_TexGenEnable(struct Core *c, ULONG cap, ULONG on)
{
    ULONG bit;
    ULONG u;

    bit = (cap == GL_TEXTURE_GEN_S) ? 1UL : 2UL;
    u = c->active_unit;
    if (((c->tg_on[u] & bit) != 0) == (on != 0))
        return;
    c->tg_on[u] ^= bit;
    Changed(c);
}

ULONG Core_TexGenEnabled(struct Core *c, ULONG cap)
{
    return (c->tg_on[c->active_unit] & ((cap == GL_TEXTURE_GEN_S) ? 1UL : 2UL)) ? 1UL : 0UL;
}

/* The inverse of a 4 x 4 column-major matrix by Gauss-Jordan; FALSE when
 * it is singular (out is then the identity) */
static BOOL Invert(float *out, const float *m)
{
    float a[4][8];
    float t;
    float f;
    ULONG r, col, k, p;

    for (r = 0; r < 4; r++)
        for (col = 0; col < 4; col++)
        {
            a[r][col] = m[col * 4 + r];
            a[r][col + 4] = (r == col) ? 1.0f : 0.0f;
        }
    for (col = 0; col < 4; col++)
    {
        p = col;
        for (r = col + 1; r < 4; r++)
            if ((a[r][col] < 0.0f ? -a[r][col] : a[r][col]) > (a[p][col] < 0.0f ? -a[p][col] : a[p][col]))
                p = r;
        if (a[p][col] == 0.0f)
        {
            for (k = 0; k < 16; k++)
                out[k] = ident[k];
            return FALSE;
        }
        if (p != col)
            for (k = 0; k < 8; k++)
            {
                t = a[col][k];
                a[col][k] = a[p][k];
                a[p][k] = t;
            }
        f = 1.0f / a[col][col];
        for (k = 0; k < 8; k++)
            a[col][k] *= f;
        for (r = 0; r < 4; r++)
        {
            if (r == col || a[r][col] == 0.0f)
                continue;
            f = a[r][col];
            for (k = 0; k < 8; k++)
                a[r][k] -= f * a[col][k];
        }
    }
    for (r = 0; r < 4; r++)
        for (col = 0; col < 4; col++)
            out[col * 4 + r] = a[r][col + 4];
    return TRUE;
}

void Core_TexGenfv(struct Core *c, ULONG coord, ULONG pname, const float *v, ULONG n)
{
    ULONG u;
    ULONG k;
    ULONG mode;
    float inv[16];

    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    if ((coord != GL_S && coord != GL_T) || v == NULL)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    u = c->active_unit;
    k = (coord == GL_S) ? 0 : 1;
    switch (pname)
    {
        case GL_TEXTURE_GEN_MODE:
            mode = (ULONG) v[0];
            if (mode != GL_OBJECT_LINEAR && mode != GL_EYE_LINEAR && mode != GL_SPHERE_MAP)
            {
                Core_Error(c, GL_INVALID_ENUM);
                return;
            }
            c->tg_mode[u][k] = mode;
            break;
        case GL_OBJECT_PLANE:
            if (n < 4)
            {
                Core_Error(c, GL_INVALID_ENUM);
                return;
            }
            c->tg_obj[u][k][0] = v[0];
            c->tg_obj[u][k][1] = v[1];
            c->tg_obj[u][k][2] = v[2];
            c->tg_obj[u][k][3] = v[3];
            break;
        case GL_EYE_PLANE:
            if (n < 4)
            {
                Core_Error(c, GL_INVALID_ENUM);
                return;
            }
            /* GL: the plane times the inverse of the modelview of the call */
            (void) Invert(inv, c->mv[c->mv_top].m);
            for (n = 0; n < 4; n++)
                c->tg_eye[u][k][n] = v[0] * inv[n * 4 + 0] + v[1] * inv[n * 4 + 1]
                                   + v[2] * inv[n * 4 + 2] + v[3] * inv[n * 4 + 3];
            break;
        default:
            Core_Error(c, GL_INVALID_ENUM);
            return;
    }
    Changed(c);
}

void Core_TexGeni(struct Core *c, ULONG coord, ULONG pname, ULONG param)
{
    float f;

    if (pname != GL_TEXTURE_GEN_MODE)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    f = (float) param;
    Core_TexGenfv(c, coord, pname, &f, 1);
}

static BOOL IsIdentity(const float *m)
{
    const ULONG *a;
    const ULONG *b;
    ULONG i;

    a = (const ULONG *) (const void *) m;
    b = (const ULONG *) (const void *) ident;
    for (i = 0; i < 16; i++)
        if (a[i] != b[i])
            return FALSE;
    return TRUE;
}

ULONG Texgen_Update(struct Core *c)
{
    float m[16];
    float pl[16];
    const float *tm;
    const float *s;
    const float *t;
    ULONG v[5];
    ULONG tcl;
    ULONG mats;
    ULONG u;
    ULONG i;
    ULONG mode;
    ULONG same;
    ULONG old;

    v[0] = 0x00ffffffUL;
    v[1] = 0x00543210UL;
    v[2] = 0;
    v[3] = 0x00543210UL;
    v[4] = 0;
    tcl = 0;
    for (u = 0; u < CORE_UNITS; u++)
    {
        tm = c->texm[u][c->tex_top[u]].m;
        mats = 0;
        if (c->tg_on[u] != 0)
        {
            mode = c->tg_mode[u][(c->tg_on[u] & 1) ? 0 : 1];
            v[0] &= ~(0xfUL << (4 * u));
            v[0] |= (0xcUL | ((c->tg_on[u] & 1) ? 0 : TG_COMP_S)
                           | ((c->tg_on[u] & 2) ? 0 : TG_COMP_T)) << (4 * u);
            v[3] &= ~(0xfUL << (4 * u));
            if (mode == GL_SPHERE_MAP)
                v[3] |= TG_INPUT_SPHERE << (4 * u);
            else
            {
                v[3] |= ((mode == GL_OBJECT_LINEAR) ? TG_INPUT_OBJ : TG_INPUT_EYE) << (4 * u);
                /* the planes as the matrix's rows (Mesa set_texgen_matrix),
                 * S and T from the planes when generated, else the identity */
                s = (mode == GL_OBJECT_LINEAR) ? c->tg_obj[u][0] : c->tg_eye[u][0];
                t = (mode == GL_OBJECT_LINEAR) ? c->tg_obj[u][1] : c->tg_eye[u][1];
                if (!(c->tg_on[u] & 1))
                    s = ident;
                if (!(c->tg_on[u] & 2))
                    t = ident + 4;
                for (i = 0; i < 4; i++)
                {
                    pl[i * 4 + 0] = s[i];
                    pl[i * 4 + 1] = t[i];
                    pl[i * 4 + 2] = ident[8 + i];
                    pl[i * 4 + 3] = ident[12 + i];
                }
                mats = 1;
            }
            v[2] |= 1UL << u;
        }
        if (!IsIdentity(tm))
        {
            if (mats)
                Mat_Mul(m, tm, pl);
            else
                for (i = 0; i < 16; i++)
                    m[i] = tm[i];
            mats = 1;
            v[2] |= 1UL << u;
        }
        else if (mats)
            for (i = 0; i < 16; i++)
                m[i] = pl[i];
        if (mats)
        {
            v[2] |= 0x100UL << u;
            same = 1;
            for (i = 0; i < 16; i++)
                if (m[i] != c->hw_tm[u][i])
                    same = 0;
            if (!same)
            {
                R2E_Matrix(c->cs, R_VS_MATRIX_3_TEX0 + 4 * u, m);
                for (i = 0; i < 16; i++)
                    c->hw_tm[u][i] = m[i];
            }
        }
        if (v[2] & (1UL << u))
            tcl |= 1UL << u;
    }
    same = 1;
    for (i = 0; i < 5; i++)
        if (v[i] != c->hw_tg[i])
            same = 0;
    if (!same)
    {
        R2E_Regs(c->cs, R_SE_TCL_TEX_PROC_CTL_2, 5, v);
        for (i = 0; i < 5; i++)
            c->hw_tg[i] = v[i];
    }
    old = c->tcl_tex;
    c->tcl_tex = tcl;
    return (old != tcl) ? 1UL : 0UL;    /* the vertex format's COMP_SEL changes */
}
