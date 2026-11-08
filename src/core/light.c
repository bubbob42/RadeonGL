/*
 * light.c -- GL 1.1 lighting on the R200's TCL (2026-11-07, for Reflector's
 * lit previews).
 *
 * The TCL lights every vertex itself: the core keeps GL's state, and when
 * it changes (D_LIGHT) writes the TCL's controls, its light and material
 * vectors and scalars, the global ambient term and the eye vector. Light
 * positions and spot directions are kept in eye coordinates (glLightfv
 * transforms them by the modelview of its call), and the TCL lights in eye
 * space: it gets the modelview and its inverse transpose for the normals
 * (Light_Matrices). The layout and the rules are Mesa's r200 driver's
 * (r200_state.c update_light, r200Lightfv, r200ColorMaterial,
 * r200UpdateMaterial; r200_state_init.c), MIT licensed:
 *
 *   vector memory, one 4-float vector each, light n at + n:
 *     0x28 ambient, 0x30 diffuse, 0x38 specular, 0x40 position (an
 *     infinite light: its unit direction, w 0), 0x48 spot direction or,
 *     for an infinite light, the half vector, 0x50 attenuation
 *     (quadratic, linear, constant, 0);
 *     0x5c the global ambient, 0x5e the eye vector (0, 0, -1, rescale);
 *     0xb0..0xb3 the front material (emission, ambient, diffuse,
 *     specular), 0xb4..0xb7 the back one
 *   scalar memory, light n at + n: 0x00 DCD, 0x08 DCM, 0x10 spot exponent,
 *     0x18 spot cutoff (its cosine), 0x20 specular threshold, 0x28 range
 *     cutoff, 0x30 1 / constant attenuation; 0x100, 0x101 the shininess
 *     of the front and the back material
 *
 * The checker accepts contiguous vector writes only, so each of the six
 * light vectors goes as one write for lights 0..k (k the highest light
 * on), and the scalars as one write from 0x00.
 *
 * Normals: with lighting on, every vertex carries one (vertex.c); locked
 * arrays then go the immediate way (array.c DrawLocked).
 */

#include "core.h"
#include "coreint.h"
#include "r200reg.h"

/* SE_TCL_LIGHT_MODEL_CTL_0 */
#define LM_LIGHTING     (1UL << 0)
#define LM_LOCAL_VIEWER (1UL << 2)
#define LM_NORMALIZE    (1UL << 3)
#define LM_SPECULAR     (1UL << 5)
#define LM_COMBINE      (1UL << 6)      /* diffuse + specular into one colour */
#define LM_LOCAL_GL     (1UL << 8)      /* local light vectors as GL defines them */
#define LM_TWOSIDE      (1UL << 10)
#define LM_BACK_SHINE_MAT1 (1UL << 13)  /* the back shininess from material 1 */
/* SE_TCL_LIGHT_MODEL_CTL_1 sources, 4 bits each: emissive, ambient,
 * diffuse, specular for the front (0..15), then the back (16..31) */
#define SRC_MATERIAL_0  1UL
#define SRC_VERTEX_0    2UL
#define SRC_MATERIAL_1  0xfUL
/* SE_TCL_PER_LIGHT_CTL_n, light 2n in the low half, 2n + 1 in the high */
#define PL_ENABLE       (1UL << 0)
#define PL_AMBIENT      (1UL << 1)
#define PL_SPECULAR     (1UL << 2)
#define PL_LOCAL        (1UL << 3)
#define PL_SPOT         (1UL << 4)
#define PL_RANGE_ATTEN  (1UL << 6)
#define PL_CONST_ATTEN  (1UL << 7)

#define VS_LIGHT_AMBIENT  0x28
#define VS_LIGHT_DIFFUSE  0x30
#define VS_LIGHT_SPECULAR 0x38
#define VS_LIGHT_DIRPOS   0x40
#define VS_LIGHT_SPOT     0x48
#define VS_LIGHT_ATTEN    0x50
#define VS_GLOBAL_AMBIENT 0x5c
#define VS_EYE_VECTOR     0x5e
#define VS_MAT_0          0xb0
#define SS_MAT_0_SHININESS 0x100

#define FLT_BIG 3.402823466e+38f

static void Set4(float *d, float a, float b, float c, float e)
{
    d[0] = a;
    d[1] = b;
    d[2] = c;
    d[3] = e;
}

static void Copy4(float *d, const float *s)
{
    d[0] = s[0];
    d[1] = s[1];
    d[2] = s[2];
    d[3] = s[3];
}

/* v normalised in place; a zero vector stays zero */
static void Normalize3(float *v)
{
    float l;

    l = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];    /* a float local first: SAS/C */
    l = Core_Sqrt(l);
    if (l > 0.0f)
    {
        l = 1.0f / l;
        v[0] *= l;
        v[1] *= l;
        v[2] *= l;
    }
}

void Light_Defaults(struct Core *c)
{
    ULONG i;
    struct CoreLight *l;
    struct CoreMaterial *m;

    for (i = 0; i < CORE_LIGHTS; i++)
    {
        l = &c->light[i];
        Set4(l->ambient, 0.0f, 0.0f, 0.0f, 1.0f);
        if (i == 0)
        {
            Set4(l->diffuse, 1.0f, 1.0f, 1.0f, 1.0f);
            Set4(l->specular, 1.0f, 1.0f, 1.0f, 1.0f);
        }
        else
        {
            Set4(l->diffuse, 0.0f, 0.0f, 0.0f, 1.0f);
            Set4(l->specular, 0.0f, 0.0f, 0.0f, 1.0f);
        }
        Set4(l->pos, 0.0f, 0.0f, 1.0f, 0.0f);
        l->dir[0] = 0.0f;
        l->dir[1] = 0.0f;
        l->dir[2] = -1.0f;
        l->spot_exp = 0.0f;
        l->spot_cut = 180.0f;
        l->att[0] = 1.0f;
        l->att[1] = 0.0f;
        l->att[2] = 0.0f;
    }
    for (i = 0; i < 2; i++)
    {
        m = &c->mtl[i];
        Set4(m->emission, 0.0f, 0.0f, 0.0f, 1.0f);
        Set4(m->ambient, 0.2f, 0.2f, 0.2f, 1.0f);
        Set4(m->diffuse, 0.8f, 0.8f, 0.8f, 1.0f);
        Set4(m->specular, 0.0f, 0.0f, 0.0f, 1.0f);
        m->shininess = 0.0f;
    }
    Set4(c->lm_ambient, 0.2f, 0.2f, 0.2f, 1.0f);
    c->lm_local = 0;
    c->lm_twoside = 0;
    c->cm_face = GL_FRONT_AND_BACK;
    c->cm_mode = GL_AMBIENT_AND_DIFFUSE;
    c->light_dirty = LD_ALL;
    c->hw_mvl_ver = 0xffffffffUL;
}

/* --- GL's calls -------------------------------------------------------------------- */

void Core_Lightfv(struct Core *c, ULONG light, ULONG pname, const float *v, ULONG n)
{
    struct CoreLight *l;
    const float *m;
    float p[4];
    ULONG i;

    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    i = light - GL_LIGHT0;
    if (i >= CORE_LIGHTS || v == NULL)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    l = &c->light[i];
    m = c->mv[c->mv_top].m;
    switch (pname)
    {
        case GL_AMBIENT:
        case GL_DIFFUSE:
        case GL_SPECULAR:
        case GL_POSITION:
            if (n < 4)
            {
                Core_Error(c, GL_INVALID_ENUM);     /* glLightf takes none of these */
                return;
            }
            break;
        case GL_SPOT_DIRECTION:
            if (n < 3)
            {
                Core_Error(c, GL_INVALID_ENUM);
                return;
            }
            break;
    }
    switch (pname)
    {
        case GL_AMBIENT:    Copy4(l->ambient, v); break;
        case GL_DIFFUSE:    Copy4(l->diffuse, v); break;
        case GL_SPECULAR:   Copy4(l->specular, v); break;
        case GL_POSITION:
            /* into eye coordinates by the current modelview */
            for (i = 0; i < 4; i++)
                p[i] = m[i] * v[0] + m[i + 4] * v[1] + m[i + 8] * v[2] + m[i + 12] * v[3];
            Copy4(l->pos, p);
            break;
        case GL_SPOT_DIRECTION:
            for (i = 0; i < 3; i++)
                l->dir[i] = m[i] * v[0] + m[i + 4] * v[1] + m[i + 8] * v[2];
            break;
        case GL_SPOT_EXPONENT:
            if (v[0] < 0.0f || v[0] > 128.0f)
            {
                Core_Error(c, GL_INVALID_VALUE);
                return;
            }
            l->spot_exp = v[0];
            break;
        case GL_SPOT_CUTOFF:
            if ((v[0] < 0.0f || v[0] > 90.0f) && v[0] != 180.0f)
            {
                Core_Error(c, GL_INVALID_VALUE);
                return;
            }
            l->spot_cut = v[0];
            break;
        case GL_CONSTANT_ATTENUATION:
        case GL_LINEAR_ATTENUATION:
        case GL_QUADRATIC_ATTENUATION:
            if (v[0] < 0.0f)
            {
                Core_Error(c, GL_INVALID_VALUE);
                return;
            }
            l->att[pname - GL_CONSTANT_ATTENUATION] = v[0];
            break;
        default:
            Core_Error(c, GL_INVALID_ENUM);
            return;
    }
    c->light_dirty |= 1UL << (light - GL_LIGHT0);
    c->dirty |= D_LIGHT;
}

void Core_LightModelfv(struct Core *c, ULONG pname, const float *v, ULONG n)
{
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
        case GL_LIGHT_MODEL_AMBIENT:
            if (n < 4)
            {
                Core_Error(c, GL_INVALID_ENUM);
                return;
            }
            Copy4(c->lm_ambient, v);
            break;
        case GL_LIGHT_MODEL_LOCAL_VIEWER:
            c->lm_local = (v[0] != 0.0f) ? 1UL : 0UL;
            break;
        case GL_LIGHT_MODEL_TWO_SIDE:
            c->lm_twoside = (v[0] != 0.0f) ? 1UL : 0UL;
            break;
        default:
            Core_Error(c, GL_INVALID_ENUM);
            return;
    }
    c->light_dirty |= LD_MODEL;
    c->dirty |= D_LIGHT;
}

/* One material property into m (face's material) */
static BOOL SetMaterial(struct CoreMaterial *m, ULONG pname, const float *v)
{
    switch (pname)
    {
        case GL_EMISSION:   Copy4(m->emission, v); return TRUE;
        case GL_AMBIENT:    Copy4(m->ambient, v); return TRUE;
        case GL_DIFFUSE:    Copy4(m->diffuse, v); return TRUE;
        case GL_SPECULAR:   Copy4(m->specular, v); return TRUE;
        case GL_AMBIENT_AND_DIFFUSE:
            Copy4(m->ambient, v);
            Copy4(m->diffuse, v);
            return TRUE;
        case GL_SHININESS:  m->shininess = v[0]; return TRUE;
    }
    return FALSE;
}

/* glMaterial is allowed inside glBegin/glEnd: the material changes from
 * the next draw on (a packet's vertices share one state) */
void Core_Materialfv(struct Core *c, ULONG face, ULONG pname, const float *v, ULONG n)
{
    if (v == NULL || (face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK))
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    if (pname == GL_SHININESS)
    {
        if (v[0] < 0.0f || v[0] > 128.0f)
        {
            Core_Error(c, GL_INVALID_VALUE);
            return;
        }
    }
    else if (n < 4)
    {
        Core_Error(c, GL_INVALID_ENUM);         /* glMaterialf takes GL_SHININESS only */
        return;
    }
    if (face != GL_BACK && !SetMaterial(&c->mtl[0], pname, v))
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    if (face != GL_FRONT && !SetMaterial(&c->mtl[1], pname, v))
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    c->light_dirty |= LD_MATERIAL;
    c->dirty |= D_LIGHT;
}

void Core_ColorMaterial(struct Core *c, ULONG face, ULONG mode)
{
    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    if ((face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK)
        || (mode != GL_EMISSION && mode != GL_AMBIENT && mode != GL_DIFFUSE
            && mode != GL_SPECULAR && mode != GL_AMBIENT_AND_DIFFUSE))
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    c->cm_face = face;
    c->cm_mode = mode;
    c->light_dirty |= LD_MODEL;
    c->dirty |= D_LIGHT;
}

void Core_GetLightfv(struct Core *c, ULONG light, ULONG pname, float *v)
{
    struct CoreLight *l;
    ULONG i;

    i = light - GL_LIGHT0;
    if (i >= CORE_LIGHTS || v == NULL)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    l = &c->light[i];
    switch (pname)
    {
        case GL_AMBIENT:    Copy4(v, l->ambient); return;
        case GL_DIFFUSE:    Copy4(v, l->diffuse); return;
        case GL_SPECULAR:   Copy4(v, l->specular); return;
        case GL_POSITION:   Copy4(v, l->pos); return;
        case GL_SPOT_DIRECTION:
            v[0] = l->dir[0];
            v[1] = l->dir[1];
            v[2] = l->dir[2];
            return;
        case GL_SPOT_EXPONENT:          v[0] = l->spot_exp; return;
        case GL_SPOT_CUTOFF:            v[0] = l->spot_cut; return;
        case GL_CONSTANT_ATTENUATION:   v[0] = l->att[0]; return;
        case GL_LINEAR_ATTENUATION:     v[0] = l->att[1]; return;
        case GL_QUADRATIC_ATTENUATION:  v[0] = l->att[2]; return;
    }
    Core_Error(c, GL_INVALID_ENUM);
}

void Core_GetMaterialfv(struct Core *c, ULONG face, ULONG pname, float *v)
{
    struct CoreMaterial *m;

    if (v == NULL || (face != GL_FRONT && face != GL_BACK))
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    m = &c->mtl[face == GL_BACK ? 1 : 0];
    switch (pname)
    {
        case GL_EMISSION:   Copy4(v, m->emission); return;
        case GL_AMBIENT:    Copy4(v, m->ambient); return;
        case GL_DIFFUSE:    Copy4(v, m->diffuse); return;
        case GL_SPECULAR:   Copy4(v, m->specular); return;
        case GL_SHININESS:  v[0] = m->shininess; return;
    }
    Core_Error(c, GL_INVALID_ENUM);
}

/* --- To the TCL ---------------------------------------------------------------------- */

/* LIGHT_MODEL_CTL_1: where emission, ambient, diffuse and specular come
 * from -- the materials, or with GL_COLOR_MATERIAL the vertex colour for
 * the properties glColorMaterial named (Mesa r200ColorMaterial) */
static ULONG Sources(struct Core *c)
{
    ULONG src[8];
    ULONG v;
    ULONG i;
    ULONG mask;

    for (i = 0; i < 4; i++)
    {
        src[i] = SRC_MATERIAL_0;
        src[i + 4] = SRC_MATERIAL_1;
    }
    if (c->enables & EN_COLOR_MATERIAL)
    {
        /* bit 0 emission, 1 ambient, 2 diffuse, 3 specular */
        switch (c->cm_mode)
        {
            case GL_EMISSION:   mask = 1; break;
            case GL_AMBIENT:    mask = 2; break;
            case GL_DIFFUSE:    mask = 4; break;
            case GL_SPECULAR:   mask = 8; break;
            default:            mask = 6; break;    /* GL_AMBIENT_AND_DIFFUSE */
        }
        for (i = 0; i < 4; i++)
        {
            if (!(mask & (1UL << i)))
                continue;
            if (c->cm_face != GL_BACK)
                src[i] = SRC_VERTEX_0;
            if (c->cm_face != GL_FRONT)
                src[i + 4] = SRC_VERTEX_0;
        }
    }
    v = 0;
    for (i = 0; i < 8; i++)
        v |= src[i] << (4 * i);
    return v;
}

/* The highest light that is on, or -1 */
static LONG TopLight(struct Core *c)
{
    LONG i;

    for (i = CORE_LIGHTS - 1; i >= 0; i--)
        if (c->enables & (EN_LIGHT0 << i))
            return i;
    return -1;
}

static ULONG PerLight(struct Core *c, ULONG i)
{
    struct CoreLight *l;
    ULONG f;

    if (!(c->enables & (EN_LIGHT0 << i)))
        return 0;
    l = &c->light[i];
    f = PL_ENABLE | PL_AMBIENT | PL_SPECULAR;
    if (l->pos[3] != 0.0f)
    {
        f |= PL_LOCAL;
        if (l->spot_cut != 180.0f)
            f |= PL_SPOT;
        /* attenuation only where it is not 1 (Mesa r200Lightfv) */
        if ((l->att[0] == 0.0f || l->att[0] == 1.0f) && l->att[1] == 0.0f && l->att[2] == 0.0f)
            ;
        else if (l->att[1] == 0.0f && l->att[2] == 0.0f)
            f |= PL_RANGE_ATTEN | PL_CONST_ATTEN;
        else
            f |= PL_RANGE_ATTEN;
    }
    return f << ((i & 1) ? 16 : 0);
}

static void Vec(ULONG *d, const float *s)
{
    d[0] = R2Cs_FloatBits(s[0]);
    d[1] = R2Cs_FloatBits(s[1]);
    d[2] = R2Cs_FloatBits(s[2]);
    d[3] = R2Cs_FloatBits(s[3]);
}

static void EmitLights(struct Core *c, LONG top)
{
    struct R2Cs *cs;
    ULONG v[6][4 * CORE_LIGHTS];
    ULONG s[0x30 + CORE_LIGHTS];
    struct CoreLight *l;
    float p[4];
    float d[4];
    float t;
    ULONG n;
    ULONG i;
    ULONG k;

    cs = c->cs;
    n = (ULONG) top + 1;
    for (i = 0; i < 0x30 + CORE_LIGHTS; i++)
        s[i] = 0;
    for (i = 0; i < n; i++)
    {
        l = &c->light[i];
        Vec(&v[0][i * 4], l->ambient);
        Vec(&v[1][i * 4], l->diffuse);
        Vec(&v[2][i * 4], l->specular);
        if (l->pos[3] == 0.0f)
        {
            /* an infinite light: the unit vector towards it, and the half
             * vector with the viewer at infinity, (0, 0, 1) */
            p[0] = l->pos[0];
            p[1] = l->pos[1];
            p[2] = l->pos[2];
            Normalize3(p);
            p[3] = 0.0f;
            d[0] = p[0];
            d[1] = p[1];
            d[2] = p[2] + 1.0f;
            Normalize3(d);
            d[3] = 0.0f;
        }
        else
        {
            /* a local light: its place, and the spot's direction reversed */
            p[0] = l->pos[0] / l->pos[3];
            p[1] = l->pos[1] / l->pos[3];
            p[2] = l->pos[2] / l->pos[3];
            p[3] = 1.0f;
            d[0] = l->dir[0];
            d[1] = l->dir[1];
            d[2] = l->dir[2];
            Normalize3(d);
            d[0] = -d[0];
            d[1] = -d[1];
            d[2] = -d[2];
            d[3] = 0.0f;
        }
        Vec(&v[3][i * 4], p);
        Vec(&v[4][i * 4], d);
        p[0] = l->att[2];
        p[1] = l->att[1];
        p[2] = l->att[0];
        p[3] = 0.0f;
        Vec(&v[5][i * 4], p);
        /* scalars: DCD, DCM 0; the spot; specular threshold 0 */
        s[0x10 + i] = R2Cs_FloatBits(l->spot_exp);
        t = (l->spot_cut == 180.0f) ? -1.0f : Core_Cos(l->spot_cut);   /* float locals: SAS/C */
        s[0x18 + i] = R2Cs_FloatBits(t);
        s[0x28 + i] = R2Cs_FloatBits(FLT_BIG);
        t = (l->att[0] == 0.0f) ? FLT_BIG : 1.0f / l->att[0];
        s[0x30 + i] = R2Cs_FloatBits(t);
    }
    for (k = 0; k < 6; k++)
        R2E_Vectors(cs, VS_LIGHT_AMBIENT + k * 8, n * 4, v[k]);
    for (i = n; i < CORE_LIGHTS; i++)
        s[0x28 + i] = R2Cs_FloatBits(FLT_BIG);
    R2E_Scalars(cs, 0x00, 0x30 + n, s);
}

static void EmitMaterials(struct Core *c)
{
    ULONG v[32];
    ULONG s[2];
    ULONG i;

    for (i = 0; i < 2; i++)
    {
        Vec(&v[i * 16 + 0], c->mtl[i].emission);
        Vec(&v[i * 16 + 4], c->mtl[i].ambient);
        Vec(&v[i * 16 + 8], c->mtl[i].diffuse);
        Vec(&v[i * 16 + 12], c->mtl[i].specular);
        s[i] = R2Cs_FloatBits(c->mtl[i].shininess);
    }
    R2E_Vectors(c->cs, VS_MAT_0, 32, v);
    R2E_Scalars(c->cs, SS_MAT_0_SHININESS, 2, s);
}

static void EmitModel(struct Core *c)
{
    ULONG v[4];
    float e[4];

    /* the global ambient as GL gives it: with the materials as sources
     * (never Mesa's premultiplied way) the TCL multiplies it itself */
    Vec(v, c->lm_ambient);
    R2E_Vectors(c->cs, VS_GLOBAL_AMBIENT, 4, v);
    /* the eye vector in eye space, Mesa's (0, 0, -1), rescale factor 1 */
    e[0] = 0.0f;
    e[1] = 0.0f;
    e[2] = -1.0f;
    e[3] = 1.0f;
    Vec(v, e);
    R2E_Vectors(c->cs, VS_EYE_VECTOR, 4, v);
}

void Light_Reset(struct Core *c)
{
    /* R2E_TclDefaults: lighting off, materials 0 and 1, no lights */
    c->hw_light[0] = LM_SPECULAR | LM_COMBINE | LM_LOCAL_GL | LM_BACK_SHINE_MAT1;
    c->hw_light[1] = 0xffff1111UL;
    c->hw_light[2] = 0;
    c->hw_light[3] = 0;
    c->hw_light[4] = 0;
    c->hw_light[5] = 0;
    c->light_dirty = LD_ALL;
    c->hw_mvl_ver = 0xffffffffUL;
}

void Light_Emit(struct Core *c)
{
    ULONG v[6];
    LONG top;
    ULONG i;
    ULONG same;

    v[0] = LM_SPECULAR | LM_COMBINE | LM_LOCAL_GL | LM_BACK_SHINE_MAT1;
    if (c->enables & EN_LIGHTING)
        v[0] |= LM_LIGHTING;
    if (c->lm_local || c->tg_sphere)
        v[0] |= LM_LOCAL_VIEWER;    /* a sphere map needs it (Mesa r200UpdateLocalViewer) */
    if (c->enables & EN_NORMALIZE)
        v[0] |= LM_NORMALIZE;
    if (c->lm_twoside)
        v[0] |= LM_TWOSIDE;
    v[1] = Sources(c);
    for (i = 0; i < 4; i++)
        v[2 + i] = PerLight(c, 2 * i) | PerLight(c, 2 * i + 1);
    same = 1;
    for (i = 0; i < 6; i++)
        if (v[i] != c->hw_light[i])
            same = 0;
    if (!same)
    {
        R2E_Regs(c->cs, R_SE_TCL_LIGHT_MODEL_CTL_0, 6, v);
        for (i = 0; i < 6; i++)
            c->hw_light[i] = v[i];
    }
    if (!(c->enables & EN_LIGHTING))
        return;                     /* the rest waits until lighting is on */
    top = TopLight(c);
    if (top >= 0 && (c->light_dirty & ((2UL << top) - 1)))
    {
        EmitLights(c, top);
        c->light_dirty &= ~((2UL << top) - 1);
    }
    if (c->light_dirty & LD_MATERIAL)
        EmitMaterials(c);
    if (c->light_dirty & LD_MODEL)
        EmitModel(c);
    c->light_dirty &= ~(LD_MATERIAL | LD_MODEL);
}

/* The inverse of m's upper 3 x 3, transposed, as a 4 x 4 column-major
 * matrix for R2E_Matrix (Mesa uploads the inverse modelview transposed:
 * upload_matrix_t). A singular m gives the identity. */
static void NormalMatrix(float *out, const float *m)
{
    float a, b, cc, d, e, f, g, h, i;
    float det;
    float inv[9];
    ULONG k;

    a = m[0]; b = m[4]; cc = m[8];
    d = m[1]; e = m[5]; f = m[9];
    g = m[2]; h = m[6]; i = m[10];
    inv[0] = e * i - f * h;
    inv[1] = cc * h - b * i;
    inv[2] = b * f - cc * e;
    inv[3] = f * g - d * i;
    inv[4] = a * i - cc * g;
    inv[5] = cc * d - a * f;
    inv[6] = d * h - e * g;
    inv[7] = b * g - a * h;
    inv[8] = a * e - b * d;
    det = a * inv[0] + b * inv[3] + cc * inv[6];
    for (k = 0; k < 16; k++)
        out[k] = (k % 5 == 0) ? 1.0f : 0.0f;
    if (det == 0.0f)
        return;
    det = 1.0f / det;
    /* inv is the inverse row by row (inv[r * 3 + col]); its transpose,
     * column-major, has column r = row r of the inverse */
    for (k = 0; k < 3; k++)
    {
        out[k * 4 + 0] = inv[k * 3 + 0] * det;
        out[k * 4 + 1] = inv[k * 3 + 1] * det;
        out[k * 4 + 2] = inv[k * 3 + 2] * det;
    }
}

void Core_Extras(struct Core *c)
{
    c->x_vdw = c->vnorm ? 3UL : 0UL;
    c->x_fmt0 = c->vnorm ? R_VTX_N0 : 0;
    c->x_compsel = c->tcl_tex << 16;
    if (c->enables & EN_LIGHTING)
        c->x_compsel |= R_OUTPUT_COLOR_0;
    c->x_out0 = 0;
    c->x_pp = 0;
    c->x_ucp = 0;
    if (c->enables & EN_FOG)
    {
        c->x_compsel |= R_OUTPUT_COLOR_1;           /* the fog factor (fog.c) */
        c->x_out0 = R_VTX_FP_RGBA << (R_VTX_COLOR_0_SHIFT + 2);
        c->x_pp = 0x00400000UL;                     /* FOG_ENABLE */
        c->x_ucp = c->fog_tcl;
    }
    c->x_se = c->wide_line ? (1UL << 20) : 0;       /* WIDELINE_ENABLE */
    c->x_eye = ((c->enables & (EN_LIGHTING | EN_FOG)) || c->tg_eyepos) ? 1UL : 0UL;
}

ULONG Hw_Rare(struct Core *c, ULONG d)
{
    if (d & D_LIGHT)
        Light_Emit(c);
    if ((d & D_TEXGEN) && Texgen_Update(c))
    {
        Core_Extras(c);
        d |= D_VTXFMT;              /* COMP_SEL names the units the TCL computes */
    }
    if (d & D_FOG)
        Fog_Emit(c);
    return d;
}

void Light_Matrices(struct Core *c)
{
    float n[16];

    if (!((c->enables & (EN_LIGHTING | EN_FOG)) || c->tg_eyepos) || c->mv[c->mv_top].ver == c->hw_mvl_ver)
        return;
    R2E_Matrix(c->cs, R_VS_MATRIX_0_MV, c->mv[c->mv_top].m);
    NormalMatrix(n, c->mv[c->mv_top].m);
    R2E_Matrix(c->cs, R_VS_MATRIX_1_INV_MV, n);
    c->hw_mvl_ver = c->mv[c->mv_top].ver;
}
