/*
 * tex13.c -- GL 1.3 phase 1 (docs/concept-gl13.md, 2026-11-12): what the
 * R200 can do that GL 1.3 asks for and that reaches the library as VALUES
 * of calls minigl.library already has -- no new slot.
 *
 *   texture units 2..5     the R200's six; vertices and array draws of more
 *                          than two units take this file's slow way
 *   cube maps              GL_TEXTURE_CUBE_MAP, six faces, reflection and
 *                          normal-map texgen (texgen.c), three coordinates
 *   the combiner           ARB_texture_env_combine, _dot3, _crossbar and ATI's
 *                          combine3 (the R200's multiply-add), stages
 *                          compacted and output registers chosen as Mesa does
 *   formats                GL_BGRA, the GL 1.2 packed types (the Amiga's ARGB32
 *                          is GL_BGRA + GL_UNSIGNED_INT_8_8_8_8_REV), ARGB1555
 *   sampling               border clamp, mirrored repeat, anisotropy, LOD
 *                          control (min/max LOD, base/max level), LOD bias
 *   lighting               separate specular colour, GL_RESCALE_NORMAL (light.c)
 *
 * The hot code (vertex.c, array.c, hwstate.c, texture.c, state.c) is
 * GLQuake's and keeps its instructions for what GLQuake does: it turns off
 * to this file only where a value it does not know arrives (an error
 * branch) or where one of struct Core's routing fields says so (wide_on,
 * x_route, comb_x, tex_x, cube_on). Linked after everything hot, like
 * compat.c: code moved in front of the hot objects costs GLQuake on the
 * 68060 (Makefile CORE_LATE).
 *
 * Register meanings: Mesa 21.3's r200 driver (r200_texstate.c
 * r200UpdateTextureEnv, r200UpdateAllTexEnv, setup_hardware_state;
 * r200_tex.c r200SetTexFilter, LOD bias), MIT licensed, read only.
 *
 * Limits (the ABI): GL_TEXTURE_BORDER_COLOR needs glTexParameterfv, which
 * minigl.library has no slot for: the border stays GL's (0, 0, 0, 0).
 * glTexEnvf truncates (the header sends it through glTexEnvi): a fractional
 * LOD bias needs glTexEnvfv. glTexCoord2f leaves a unit's r as it was (GL
 * sets it to 0): only cube maps read it. The cube target's default texture
 * (name 0) is the 2D one's object.
 */

#include "core.h"
#include "coreint.h"
#include "r200reg.h"
#include "arrayfetch.h"

/* --- Routing -------------------------------------------------------------------- */

/* x_route turns on: the bindings and enables by target from what texture.c
 * and state.c kept (before it, bound[u] is the 2D binding: no cube was on) */
static void SyncRoute(struct Core *c)
{
    ULONG u;

    for (u = 0; u < CORE_UNITS; u++)
    {
        if (c->cube_on & (1UL << u))
            continue;
        c->bound_2d[u] = c->bound[u];
        if (c->tex_enabled[u])
            c->en_2d |= 1UL << u;
        else
            c->en_2d &= ~(1UL << u);
    }
}

void Tex13_Update(struct Core *c)
{
    ULONG u;
    ULONG wide;
    ULONG route;

    wide = c->cube_on ? 1UL : 0UL;
    for (u = CORE_NARROW; u < CORE_UNITS; u++)
        if (c->tex_enabled[u])
            wide = 1;
    route = (c->active_unit >= CORE_NARROW || c->cube_on) ? 1UL : 0UL;
    if (route && !c->x_route)
        SyncRoute(c);
    c->x_route = route;
    if (wide != c->wide_on)
    {
        c->wide_on = wide;
        c->dirty |= D_VTXFMT;
    }
    c->vslow = (c->vnorm || c->wide_on) ? 1UL : 0UL;
}

/* Everything unit u draws must go out again */
static void DirtyUnit(struct Core *c, ULONG u)
{
    DIRTY_UNIT(c, u);
    c->hw_tex[u][6] = 0xffffffffUL;     /* TXOFFSET too, as texture.c's Touched */
}

/* Every unit that draws t (a parameter or its storage changed) */
static void Touched(struct Core *c, const struct CoreTex *t)
{
    ULONG u;

    for (u = 0; u < CORE_UNITS; u++)
        if (c->bound[u] == t->name)
            DirtyUnit(c, u);
}

/* --- Enables ------------------------------------------------------------------------ */

void Tex13_Enable2D(struct Core *c, ULONG on)
{
    ULONG u;
    ULONG bit;

    u = c->active_unit;
    bit = 1UL << u;
    if (on)
        c->en_2d |= bit;
    else
        c->en_2d &= ~bit;
    if (!(c->cube_on & bit) && (c->tex_enabled[u] != 0) != (on != 0))
    {
        c->tex_enabled[u] = on ? 1UL : 0UL;
        DIRTY_UNIT(c, u);
    }
    Tex13_Update(c);
}

static void CubeEnable(struct Core *c, ULONG on)
{
    ULONG u;
    ULONG bit;

    u = c->active_unit;
    bit = 1UL << u;
    if (((c->cube_on & bit) != 0) == (on != 0))
        return;
    if (!c->x_route)
        SyncRoute(c);               /* the 2D bindings, before the first cube is on */
    if (on)
    {
        c->cube_on |= bit;
        c->bound[u] = c->bound_cube[u];
        c->tex_enabled[u] = 1;
    }
    else
    {
        c->cube_on &= ~bit;
        c->bound[u] = c->bound_2d[u];
        c->tex_enabled[u] = (c->en_2d & bit) ? 1UL : 0UL;
    }
    DirtyUnit(c, u);
    c->dirty |= D_VTXFMT;           /* three coordinates or two */
    Tex13_Update(c);
}

ULONG Tex13_Enable(struct Core *c, ULONG cap, ULONG on)
{
    switch (cap)
    {
        case GL_TEXTURE_CUBE_MAP:
            CubeEnable(c, on);
            return 1;
        case GL_TEXTURE_GEN_R:
        case GL_TEXTURE_GEN_Q:
            Core_TexGenEnable(c, cap, on);      /* texgen.c */
            return 1;
        case GL_RESCALE_NORMAL:
            c->rescale = on ? 1UL : 0UL;
            c->dirty |= D_LIGHT;
            return 1;
    }
    return 0;
}

ULONG Tex13_IsEnabled(struct Core *c, ULONG cap, ULONG *on)
{
    switch (cap)
    {
        case GL_TEXTURE_CUBE_MAP:
            *on = (c->cube_on >> c->active_unit) & 1UL;
            return 1;
        case GL_TEXTURE_GEN_R:
        case GL_TEXTURE_GEN_Q:
            *on = Core_TexGenEnabled(c, cap);
            return 1;
        case GL_RESCALE_NORMAL:
            *on = c->rescale;
            return 1;
    }
    return 0;
}

/* --- Texture objects ------------------------------------------------------------------ */

/* The 2D binding of the active unit */
static ULONG Obj2D(struct Core *c)
{
    return c->x_route ? c->bound_2d[c->active_unit] : c->bound[c->active_unit];
}

/* GL_TEXTURE_CUBE_MAP_POSITIVE_X .. NEGATIVE_Z: 0..5, else -1 */
static LONG FaceOf(ULONG target)
{
    if (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)
        return (LONG) (target - GL_TEXTURE_CUBE_MAP_POSITIVE_X);
    return -1;
}

void Tex13_BindTexture(struct Core *c, ULONG target, ULONG name)
{
    struct CoreTex *t;
    ULONG u;
    ULONG bit;

    u = c->active_unit;
    bit = 1UL << u;
    if (target != GL_TEXTURE_2D && target != GL_TEXTURE_CUBE_MAP)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    t = Tex_Get(c, name);
    if (t == NULL)
    {
        Core_Error(c, name >= CORE_MAX_TEX ? GL_INVALID_VALUE : GL_OUT_OF_MEMORY);
        return;
    }
    if (!c->x_route)
        SyncRoute(c);               /* bound_2d from bound before anything changes */
    if (target == GL_TEXTURE_2D)
    {
        if (t->cube && name != 0)
        {
            Core_Error(c, GL_INVALID_OPERATION);    /* a cube map's name */
            return;
        }
        c->bound_2d[u] = name;
        if (!(c->cube_on & bit) && c->bound[u] != name)
        {
            c->bound[u] = name;
            DIRTY_UNIT(c, u);
        }
        return;
    }
    if (name != 0 && !t->cube)
    {
        if (t->gpu != 0 || t->levels != 0)
        {
            Core_Error(c, GL_INVALID_OPERATION);    /* a 2D texture already */
            return;
        }
        t->cube = 1;
        t->x13 = 1;
        c->tex_x = 1;               /* its registers: Tex13_TexRegs */
    }
    c->bound_cube[u] = name;
    if ((c->cube_on & bit) && c->bound[u] != name)
    {
        c->bound[u] = name;
        DIRTY_UNIT(c, u);
    }
}

/* The object a texture call with target names (NULL: none, said if wrong) */
static struct CoreTex *TargetTex(struct Core *c, ULONG target)
{
    if (target == GL_TEXTURE_2D)
        return c->tex[Obj2D(c)];
    if (target == GL_TEXTURE_CUBE_MAP || FaceOf(target) >= 0)
        return c->tex[c->bound_cube[c->active_unit]];
    Core_Error(c, GL_INVALID_ENUM);
    return NULL;
}

/* --- Texture parameters ------------------------------------------------------------- */

static ULONG IsMip(ULONG f)
{
    return (f != GL_NEAREST && f != GL_LINEAR) ? 1UL : 0UL;
}

void Tex13_TexParameter(struct Core *c, ULONG target, ULONG pname, LONG param)
{
    struct CoreTex *t;
    ULONG p;

    if (target != GL_TEXTURE_2D && target != GL_TEXTURE_CUBE_MAP)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    t = TargetTex(c, target);
    if (t == NULL)
        return;
    p = (ULONG) param;
    switch (pname)
    {
        case GL_TEXTURE_MIN_FILTER:
            if (p != GL_NEAREST && p != GL_LINEAR && p != GL_NEAREST_MIPMAP_NEAREST
                && p != GL_NEAREST_MIPMAP_LINEAR && p != GL_LINEAR_MIPMAP_NEAREST
                && p != GL_LINEAR_MIPMAP_LINEAR)
            {
                Core_Error(c, GL_INVALID_ENUM);
                return;
            }
            t->min_filter = p;
            break;
        case GL_TEXTURE_MAG_FILTER:
            if (p != GL_NEAREST && p != GL_LINEAR)
            {
                Core_Error(c, GL_INVALID_ENUM);
                return;
            }
            t->mag_filter = p;
            break;
        case GL_TEXTURE_WRAP_S:
        case GL_TEXTURE_WRAP_T:
        case GL_TEXTURE_WRAP_R:
            if (p == GL_CLAMP_TO_EDGE)
                p = GL_CLAMP;           /* R200: CLAMP_LAST either way (texture.c) */
            if (p != GL_REPEAT && p != GL_CLAMP && p != GL_MIRRORED_REPEAT && p != GL_CLAMP_TO_BORDER)
            {
                Core_Error(c, GL_INVALID_ENUM);
                return;
            }
            if (pname == GL_TEXTURE_WRAP_S)
                t->wrap_s = p;
            else if (pname == GL_TEXTURE_WRAP_T)
                t->wrap_t = p;
            else
                return;                 /* r: no 3D textures, a cube map clamps anyway */
            if (p == GL_MIRRORED_REPEAT || p == GL_CLAMP_TO_BORDER)
                t->xparm = 1;
            break;
        case GL_TEXTURE_MAX_ANISOTROPY_EXT:
            if (param < 1)
            {
                Core_Error(c, GL_INVALID_VALUE);
                return;
            }
            t->aniso = (p > 16) ? 16 : p;
            t->xparm = 1;
            break;
        case GL_TEXTURE_MIN_LOD:
        case GL_TEXTURE_MAX_LOD:
            if (!t->lod_set)
            {
                t->min_lod = -1000;
                t->max_lod = 1000;
                t->lod_set = 1;
            }
            if (pname == GL_TEXTURE_MIN_LOD)
                t->min_lod = param;
            else
                t->max_lod = param;
            t->xparm = 1;
            break;
        case GL_TEXTURE_BASE_LEVEL:
        case GL_TEXTURE_MAX_LEVEL:
            if (param < 0)
            {
                Core_Error(c, GL_INVALID_VALUE);
                return;
            }
            if (pname == GL_TEXTURE_BASE_LEVEL)
                t->base_level = p;
            else
            {
                t->max_level = p;
                t->max_level_set = 1;
            }
            t->xparm = 1;
            break;
        case GL_TEXTURE_PRIORITY:
            return;                     /* one heap: nothing to prefer (as glPrioritizeTextures) */
        default:
            Core_Error(c, GL_INVALID_ENUM);
            return;
    }
    if (t->xparm)
        c->tex_x = 1;
    Touched(c, t);
}

/* A clamp mode for TXFILTER's field: GL's wrap value (the mode texture.c
 * chose for REPEAT and CLAMP stays). GL_CLAMP_TO_BORDER is CLAMP_GL in
 * the D3D border mode, as Mesa r200SetTexWrap: CLAMP_BORDER in the OGL
 * border mode read the border left of the texture and the last texel
 * right of it (MEASURED, GLSmoke13 T5, 2026-11-14) */
static ULONG ClampOf(ULONG wrap, ULONG old)
{
    if (wrap == GL_MIRRORED_REPEAT)
        return R_CLAMP_MIRROR;
    if (wrap == GL_CLAMP_TO_BORDER)
        return R_CLAMP_GL;
    return old;
}

/* The highest level a mipmapped draw may use: consecutive levels from 0 */
static ULONG LastLevel(const struct CoreTex *t)
{
    ULONG l;

    l = 0;
    while (l + 1 < CORE_MAX_LEVELS && (t->levels & (1UL << (l + 1))))
        l++;
    return l;
}

static void Reg(struct Core *c, ULONG *shadow, ULONG reg, ULONG v)
{
    if (*shadow != v)
    {
        *shadow = v;
        (void) R2E_Reg(c->cs, reg, v);
    }
}

void Tex13_TexRegs(struct Core *c, ULONG u, struct CoreTex *t, struct R2Tex *r)
{
    ULONG cs;
    ULONG ct;
    ULONG n;
    ULONG first;
    ULONG last;
    ULONG avail;
    ULONG lw;
    ULONG lh;
    ULONG f;
    ULONG i;
    LONG b;
    float bias;

    if (c->hw_tex[u][0] == 0xffffffffUL)
        for (i = 0; i < 7; i++)
            c->hw_txx[u][i] = 0xffffffffUL;     /* the GPU's state was forgotten (Hw_Forget) */
    if (!t->xparm && !t->cube && c->lod_bias[u] == 0.0f)
        return;
    lw = t->lw;
    lh = t->lh;
    /* wrap modes: mirrored repeat, border clamp (NPOT clamps as texture.c set) */
    if (t->pot)
    {
        cs = ClampOf(t->wrap_s, (r->filter >> R_CLAMP_S_SHIFT) & R_CLAMP_MASK);
        ct = ClampOf(t->wrap_t, (r->filter >> R_CLAMP_T_SHIFT) & R_CLAMP_MASK);
        r->filter = (r->filter & ~((R_CLAMP_MASK << R_CLAMP_S_SHIFT) | (R_CLAMP_MASK << R_CLAMP_T_SHIFT)))
                  | (cs << R_CLAMP_S_SHIFT) | (ct << R_CLAMP_T_SHIFT);
        if (t->wrap_s == GL_CLAMP_TO_BORDER || t->wrap_t == GL_CLAMP_TO_BORDER)
            r->filter |= R_BORDER_MODE_D3D;
    }
    /* anisotropy: the aniso filters (Mesa r200SetTexFilter) and the ratio */
    if (t->aniso > 1)
    {
        n = (t->aniso >= 16) ? 4 : (t->aniso >= 8) ? 3 : (t->aniso >= 4) ? 2 : 1;
        switch (t->min_filter)
        {
            case GL_NEAREST:                f = R_MIN_FILTER_ANISO_NEAREST; break;
            case GL_LINEAR:                 f = R_MIN_FILTER_ANISO_LINEAR; break;
            case GL_NEAREST_MIPMAP_NEAREST:
            case GL_LINEAR_MIPMAP_NEAREST:  f = R_MIN_FILTER_ANISO_NEAREST_MIP_NEAREST; break;
            default:                        f = R_MIN_FILTER_ANISO_NEAREST_MIP_LINEAR; break;
        }
        r->filter = (r->filter & ~(R_MIN_FILTER_MASK | R_MAX_ANISO_MASK)) | f | (n << R_MAX_ANISO_SHIFT);
    }
    /* levels: base and max level, min and max LOD (whole levels) */
    if (t->pot && (t->base_level != 0 || t->max_level_set || t->lod_set))
    {
        avail = LastLevel(t);
        if (IsMip(t->min_filter))
        {
            first = t->base_level;
            last = avail;
            if (t->max_level_set && t->max_level < last)
                last = t->max_level;
            if (t->lod_set)
            {
                if (t->min_lod > 0)
                    first += (ULONG) t->min_lod;
                if ((LONG) t->base_level + t->max_lod < (LONG) last)
                    last = (t->base_level + t->max_lod > 0) ? (ULONG) (t->base_level + t->max_lod) : 0;
            }
            if (first > avail)
                first = avail;
            if (last < first)
                last = first;
            r->filter = (r->filter & ~R_MAX_MIP_LEVEL_MASK) | (last << R_MAX_MIP_LEVEL_SHIFT);
            r->format_x |= first << R_MIN_MIP_LEVEL_SHIFT;
        }
        else if (t->base_level != 0)
        {
            /* one level, the base: the unit points at it */
            first = (t->base_level <= avail) ? t->base_level : avail;
            r->offset += t->lofs[first];
            lw = (lw > first) ? lw - first : 0;
            lh = (lh > first) ? lh - first : 0;
            r->format = (r->format & ~(R_TXFORMAT_WIDTH_MASK | R_TXFORMAT_HEIGHT_MASK))
                      | (lw << R_TXFORMAT_WIDTH_SHIFT) | (lh << R_TXFORMAT_HEIGHT_SHIFT);
        }
    }
    /* LOD bias (EXT_texture_lod_bias, per unit): signed, 1.0 = FIXED_ONE */
    if (c->lod_bias[u] != 0.0f)
    {
        bias = c->lod_bias[u];
        if (bias < -16.0f)
            bias = -16.0f;
        if (bias > 15.9f)
            bias = 15.9f;
        b = (LONG) (bias * (float) R_LOD_BIAS_FIXED_ONE) + (LONG) R_LOD_BIAS_CORRECTION;
        r->format_x = (r->format_x & ~R_LOD_BIAS_MASK) | ((ULONG) b & R_LOD_BIAS_MASK);
    }
    /* cube maps: face 0 at TXOFFSET, faces 1..5 face_bytes apart, all the
     * size of face 0 (Mesa setup_hardware_state) */
    if (t->cube)
    {
        r->format |= R_TXFORMAT_CUBIC_MAP_ENABLE | (lw << R_TXFORMAT_F5_WIDTH_SHIFT)
                   | (lh << R_TXFORMAT_F5_HEIGHT_SHIFT);
        r->format_x |= R_TEXCOORD_CUBIC_ENV;
        f = (lw | (lh << 4)) * 0x01010101UL;
        Reg(c, &c->hw_txx[u][1], R_PP_CUBIC_FACES_0 + u * R_PP_TXUNIT_STRIDE, f);
        for (i = 1; i < 6; i++)
            Reg(c, &c->hw_txx[u][1 + i], R_PP_CUBIC_OFFSET_F1_0 + u * R_PP_TXOFFSET_STRIDE + 4 * (i - 1),
                r->offset + i * t->face_bytes);
    }
    if (t->wrap_s == GL_CLAMP_TO_BORDER || t->wrap_t == GL_CLAMP_TO_BORDER)
        Reg(c, &c->hw_txx[u][0], R_PP_BORDER_COLOR_0 + u * R_PP_TXUNIT_STRIDE, t->border);
}

/* --- Storage and uploads ------------------------------------------------------------- */

/* GL internal format -> base format and R200 storage, 0 if refused */
static ULONG Store(LONG internal, ULONG *hwfmt, ULONG *cpp)
{
    switch ((ULONG) internal)
    {
        case 1: case GL_LUMINANCE: case GL_LUMINANCE4: case GL_LUMINANCE8:
        case GL_LUMINANCE12: case GL_LUMINANCE16:
            *hwfmt = R_TXFORMAT_I8; *cpp = 1;
            return GL_LUMINANCE;
        case 2: case GL_LUMINANCE_ALPHA: case GL_LUMINANCE4_ALPHA4: case GL_LUMINANCE6_ALPHA2:
        case GL_LUMINANCE8_ALPHA8: case GL_LUMINANCE12_ALPHA4: case GL_LUMINANCE12_ALPHA12:
        case GL_LUMINANCE16_ALPHA16:
            *hwfmt = R_TXFORMAT_AI88 | R_TXFORMAT_ALPHA_IN_MAP; *cpp = 2;
            return GL_LUMINANCE_ALPHA;
        case GL_ALPHA: case GL_ALPHA4: case GL_ALPHA8: case GL_ALPHA12: case GL_ALPHA16:
            *hwfmt = R_TXFORMAT_I8 | R_TXFORMAT_ALPHA_IN_MAP; *cpp = 1;
            return GL_ALPHA;
        case GL_INTENSITY: case GL_INTENSITY4: case GL_INTENSITY8: case GL_INTENSITY12:
        case GL_INTENSITY16:
            *hwfmt = R_TXFORMAT_I8 | R_TXFORMAT_ALPHA_IN_MAP; *cpp = 1;
            return GL_INTENSITY;
        case 3: case GL_RGB: case GL_R3_G3_B2: case GL_RGB4: case GL_RGB5:
        case MGL_UNSIGNED_SHORT_5_6_5:
            *hwfmt = R_TXFORMAT_RGB565; *cpp = 2;
            return GL_RGB;
        case GL_RGB8: case GL_RGB10: case GL_RGB12: case GL_RGB16:
            *hwfmt = R_TXFORMAT_ARGB8888; *cpp = 4;
            return GL_RGB;
        case 4: case GL_RGBA: case GL_RGBA2: case GL_RGBA4: case MGL_UNSIGNED_SHORT_4_4_4_4:
            *hwfmt = R_TXFORMAT_ARGB4444 | R_TXFORMAT_ALPHA_IN_MAP; *cpp = 2;
            return GL_RGBA;
        case GL_RGB5_A1:
            *hwfmt = R_TXFORMAT_ARGB1555 | R_TXFORMAT_ALPHA_IN_MAP; *cpp = 2;
            return GL_RGBA;
        case GL_RGBA8: case GL_RGB10_A2: case GL_RGBA12: case GL_RGBA16:
            *hwfmt = R_TXFORMAT_ARGB8888 | R_TXFORMAT_ALPHA_IN_MAP; *cpp = 4;
            return GL_RGBA;
    }
    return 0;
}

/* What texture.c takes as it is (its BaseOf and SrcOf) */
static ULONG PlainInternal(LONG internal)
{
    switch ((ULONG) internal)
    {
        case 1: case GL_LUMINANCE: case GL_LUMINANCE8: case 2: case GL_LUMINANCE_ALPHA:
        case 3: case GL_RGB: case GL_RGB5: case GL_RGB8: case MGL_UNSIGNED_SHORT_5_6_5:
        case 4: case GL_RGBA: case GL_RGBA8: case MGL_UNSIGNED_SHORT_4_4_4_4:
        case GL_ALPHA: case GL_ALPHA8: case GL_INTENSITY: case GL_INTENSITY8:
            return 1;
    }
    return 0;
}

static ULONG PlainSource(ULONG format, ULONG type)
{
    if (format == MGL_UNSIGNED_SHORT_4_4_4_4 || type == MGL_UNSIGNED_SHORT_4_4_4_4
        || format == MGL_UNSIGNED_SHORT_5_6_5 || type == MGL_UNSIGNED_SHORT_5_6_5)
        return 1;
    return (type == GL_UNSIGNED_BYTE && (format == GL_RGBA || format == GL_RGB || format == GL_LUMINANCE
                                         || format == GL_ALPHA || format == GL_LUMINANCE_ALPHA)) ? 1UL : 0UL;
}

/* A source layout: its components in format order and how one pixel is
 * read. Packed types are one element of s bytes (native order, as GL has
 * them); MiniGL's 4444 and 565 are big-endian words whatever the host. */
#define K_BYTES     0       /* n components of a byte each */
#define K_P8        1       /* one byte */
#define K_P16       2       /* one 16-bit word */
#define K_P32       3       /* one 32-bit word */
#define K_MGL16     4       /* MiniGL's big-endian words */

struct Src
{
    ULONG kind;
    ULONG s;                /* bytes an element */
    ULONG n;                /* components an element holds (K_BYTES: elements a pixel) */
    ULONG bpp;              /* bytes a pixel */
    ULONG format, type;
    ULONG order[4];         /* component k of the format is R (0), G (1), B (2) or A (3) */
    ULONG comps;            /* components the format has */
};

static ULONG SrcOf(ULONG format, ULONG type, struct Src *s)
{
    ULONG i;

    s->format = format;
    s->type = type;
    if (format == MGL_UNSIGNED_SHORT_4_4_4_4 || type == MGL_UNSIGNED_SHORT_4_4_4_4
        || format == MGL_UNSIGNED_SHORT_5_6_5 || type == MGL_UNSIGNED_SHORT_5_6_5)
    {
        s->kind = K_MGL16;
        s->s = 2;
        s->n = 1;
        s->bpp = 2;
        return 1;
    }
    switch (format)
    {
        case GL_RGBA:   s->comps = 4; s->order[0] = 0; s->order[1] = 1; s->order[2] = 2; s->order[3] = 3; break;
        case GL_BGRA:   s->comps = 4; s->order[0] = 2; s->order[1] = 1; s->order[2] = 0; s->order[3] = 3; break;
        case GL_RGB:    s->comps = 3; s->order[0] = 0; s->order[1] = 1; s->order[2] = 2; break;
        case GL_BGR:    s->comps = 3; s->order[0] = 2; s->order[1] = 1; s->order[2] = 0; break;
        case GL_LUMINANCE:
        case GL_ALPHA:  s->comps = 1; break;
        case GL_LUMINANCE_ALPHA: s->comps = 2; break;
        default:        return 0;
    }
    switch (type)
    {
        case GL_UNSIGNED_BYTE:
            s->kind = K_BYTES;
            s->s = 1;
            s->n = s->comps;
            s->bpp = s->comps;
            return 1;
        case GL_UNSIGNED_BYTE_3_3_2:
        case GL_UNSIGNED_BYTE_2_3_3_REV:
            s->kind = K_P8;
            s->s = 1;
            break;
        case GL_UNSIGNED_SHORT_5_6_5:
        case GL_UNSIGNED_SHORT_5_6_5_REV:
        case GL_UNSIGNED_SHORT_4_4_4_4:
        case GL_UNSIGNED_SHORT_4_4_4_4_REV:
        case GL_UNSIGNED_SHORT_5_5_5_1:
        case GL_UNSIGNED_SHORT_1_5_5_5_REV:
            s->kind = K_P16;
            s->s = 2;
            break;
        case GL_UNSIGNED_INT_8_8_8_8:
        case GL_UNSIGNED_INT_8_8_8_8_REV:
        case GL_UNSIGNED_INT_10_10_10_2:
        case GL_UNSIGNED_INT_2_10_10_10_REV:
            s->kind = K_P32;
            s->s = 4;
            break;
        default:
            return 0;
    }
    /* the packed types: 3_3_2 and 5_6_5 for three components, the others four */
    i = (type == GL_UNSIGNED_BYTE_3_3_2 || type == GL_UNSIGNED_BYTE_2_3_3_REV
         || type == GL_UNSIGNED_SHORT_5_6_5 || type == GL_UNSIGNED_SHORT_5_6_5_REV) ? 3UL : 4UL;
    if (s->comps != i)
        return 0;
    s->n = 1;
    s->bpp = s->s;
    return 1;
}

/* An unaligned native word (bytes copied: no type punning) */
static ULONG Native16(const UBYTE *p, ULONG swap)
{
    UWORD w;
    UBYTE *d;

    d = (UBYTE *) &w;
    d[0] = p[swap ? 1 : 0];
    d[1] = p[swap ? 0 : 1];
    return w;
}

static ULONG Native32(const UBYTE *p, ULONG swap)
{
    ULONG w;
    UBYTE *d;

    d = (UBYTE *) &w;
    d[0] = p[swap ? 3 : 0];
    d[1] = p[swap ? 2 : 1];
    d[2] = p[swap ? 1 : 2];
    d[3] = p[swap ? 0 : 3];
    return w;
}

/* bits of v from bit lo, n wide, as a byte */
static ULONG Field(ULONG v, ULONG lo, ULONG n)
{
    ULONG m;

    m = (1UL << n) - 1;
    v = (v >> lo) & m;
    return (v * 255UL + m / 2) / m;
}

/* One source pixel as R, G, B, A bytes */
static void Fetch13(const struct Src *s, const UBYTE *p, ULONG swap, ULONG *rgba)
{
    ULONG v;
    ULONG k[4];
    ULONG i;

    rgba[0] = rgba[1] = rgba[2] = 0;
    rgba[3] = 255;
    if (s->kind == K_MGL16)
    {
        v = ((ULONG) p[0] << 8) | p[1];
        if (s->format == MGL_UNSIGNED_SHORT_4_4_4_4 || s->type == MGL_UNSIGNED_SHORT_4_4_4_4)
        {
            rgba[3] = Field(v, 12, 4);
            rgba[0] = Field(v, 8, 4);
            rgba[1] = Field(v, 4, 4);
            rgba[2] = Field(v, 0, 4);
        }
        else
        {
            rgba[0] = Field(v, 11, 5);
            rgba[1] = Field(v, 5, 6);
            rgba[2] = Field(v, 0, 5);
        }
        return;
    }
    if (s->kind == K_BYTES)
    {
        if (s->format == GL_LUMINANCE)
        {
            rgba[0] = rgba[1] = rgba[2] = p[0];
            return;
        }
        if (s->format == GL_ALPHA)
        {
            rgba[3] = p[0];
            return;
        }
        if (s->format == GL_LUMINANCE_ALPHA)
        {
            rgba[0] = rgba[1] = rgba[2] = p[0];
            rgba[3] = p[1];
            return;
        }
        for (i = 0; i < s->comps; i++)
            rgba[s->order[i]] = p[i];
        return;
    }
    /* packed: component k of the format, from the most significant end
     * (the _REV types from the least) */
    switch (s->type)
    {
        case GL_UNSIGNED_BYTE_3_3_2:
            v = p[0];
            k[0] = Field(v, 5, 3); k[1] = Field(v, 2, 3); k[2] = Field(v, 0, 2);
            break;
        case GL_UNSIGNED_BYTE_2_3_3_REV:
            v = p[0];
            k[0] = Field(v, 0, 3); k[1] = Field(v, 3, 3); k[2] = Field(v, 6, 2);
            break;
        case GL_UNSIGNED_SHORT_5_6_5:
            v = Native16(p, swap);
            k[0] = Field(v, 11, 5); k[1] = Field(v, 5, 6); k[2] = Field(v, 0, 5);
            break;
        case GL_UNSIGNED_SHORT_5_6_5_REV:
            v = Native16(p, swap);
            k[0] = Field(v, 0, 5); k[1] = Field(v, 5, 6); k[2] = Field(v, 11, 5);
            break;
        case GL_UNSIGNED_SHORT_4_4_4_4:
            v = Native16(p, swap);
            k[0] = Field(v, 12, 4); k[1] = Field(v, 8, 4); k[2] = Field(v, 4, 4); k[3] = Field(v, 0, 4);
            break;
        case GL_UNSIGNED_SHORT_4_4_4_4_REV:
            v = Native16(p, swap);
            k[0] = Field(v, 0, 4); k[1] = Field(v, 4, 4); k[2] = Field(v, 8, 4); k[3] = Field(v, 12, 4);
            break;
        case GL_UNSIGNED_SHORT_5_5_5_1:
            v = Native16(p, swap);
            k[0] = Field(v, 11, 5); k[1] = Field(v, 6, 5); k[2] = Field(v, 1, 5); k[3] = Field(v, 0, 1);
            break;
        case GL_UNSIGNED_SHORT_1_5_5_5_REV:
            v = Native16(p, swap);
            k[0] = Field(v, 0, 5); k[1] = Field(v, 5, 5); k[2] = Field(v, 10, 5); k[3] = Field(v, 15, 1);
            break;
        case GL_UNSIGNED_INT_8_8_8_8:
            v = Native32(p, swap);
            k[0] = (v >> 24) & 255; k[1] = (v >> 16) & 255; k[2] = (v >> 8) & 255; k[3] = v & 255;
            break;
        case GL_UNSIGNED_INT_8_8_8_8_REV:
            v = Native32(p, swap);
            k[0] = v & 255; k[1] = (v >> 8) & 255; k[2] = (v >> 16) & 255; k[3] = (v >> 24) & 255;
            break;
        case GL_UNSIGNED_INT_10_10_10_2:
            v = Native32(p, swap);
            k[0] = Field(v, 22, 10); k[1] = Field(v, 12, 10); k[2] = Field(v, 2, 10); k[3] = Field(v, 0, 2);
            break;
        default:    /* GL_UNSIGNED_INT_2_10_10_10_REV */
            v = Native32(p, swap);
            k[0] = Field(v, 0, 10); k[1] = Field(v, 10, 10); k[2] = Field(v, 20, 10); k[3] = Field(v, 30, 2);
            break;
    }
    for (i = 0; i < s->comps; i++)
        rgba[s->order[i]] = k[i];
}

/* One texel in t's storage, as the GPU reads it */
static ULONG Encode13(const struct CoreTex *t, const ULONG *c)
{
    ULONG a;

    a = c[3];
    switch (t->hwfmt & R_TXFORMAT_FORMAT_MASK)
    {
        case R_TXFORMAT_ARGB8888:
            if (t->base == GL_RGB)
                a = 255;
            return (a << 24) | (c[0] << 16) | (c[1] << 8) | c[2];
        case R_TXFORMAT_RGB565:
            return ((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3);
        case R_TXFORMAT_ARGB1555:
            return ((a >= 128) ? 0x8000UL : 0) | ((c[0] >> 3) << 10) | ((c[1] >> 3) << 5) | (c[2] >> 3);
        case R_TXFORMAT_AI88:
            return (a << 8) | c[0];
        case R_TXFORMAT_I8:
            return (t->base == GL_ALPHA) ? a : c[0];
    }
    return ((a >> 4) << 12) | ((c[0] >> 4) << 8) | ((c[1] >> 4) << 4) | (c[2] >> 4);    /* 4444 */
}

static ULONG Out(struct Core *c, ULONG g)
{
    if (c->tex_swap)
        R2_SWAPI(g);
    return g;
}

static ULONG In(struct Core *c, ULONG v)
{
    if (c->tex_swap)
        R2_SWAPI(v);
    return v;
}

/* w texels of one source row into byte offset byte0 of the heap */
static void Row13(struct Core *c, const struct CoreTex *t, ULONG byte0, ULONG w, const struct Src *s,
                  ULONG swap, const UBYTE *row)
{
    ULONG cpp;
    ULONG per;
    ULONG unit;
    ULONG first;
    ULONG slot;
    ULONG acc;
    ULONG mask;
    ULONG texel;
    ULONG i;
    ULONG rgba[4];
    ULONG *dst;

    cpp = t->cpp;
    dst = c->heap.cpu + (t->gpu - c->heap.gpu + byte0) / 4;
    if (cpp == 4 && s->kind == K_P32 && s->type == GL_UNSIGNED_INT_8_8_8_8_REV && s->format == GL_BGRA)
    {
        /* the Amiga's ARGB32: the texel itself (ARGB8888), one a dword */
        for (i = 0; i < w; i++, row += 4)
        {
            texel = Native32(row, swap);
            if (t->base == GL_RGB)
                texel |= 0xff000000UL;
            *dst++ = Out(c, texel);
        }
        return;
    }
    if (cpp == 4)
    {
        for (i = 0; i < w; i++, row += s->bpp)
        {
            Fetch13(s, row, swap, rgba);
            *dst++ = Out(c, Encode13(t, rgba));
        }
        return;
    }
    unit = (1UL << (cpp * 8)) - 1;
    per = 4 / cpp;
    first = (byte0 & 3) / cpp;
    acc = 0;
    mask = 0;
    slot = first;
    for (i = 0; i < w; i++, row += s->bpp)
    {
        Fetch13(s, row, swap, rgba);
        texel = Encode13(t, rgba);
        acc |= texel << (slot * cpp * 8);
        mask |= unit << (slot * cpp * 8);
        if (++slot == per)
        {
            if (mask != 0xffffffffUL)
                acc |= In(c, *dst) & ~mask;     /* a partial first dword */
            *dst++ = Out(c, acc);
            acc = 0;
            mask = 0;
            slot = 0;
        }
    }
    if (slot != 0)
    {
        acc |= In(c, *dst) & ~mask;
        *dst = Out(c, acc);
    }
}

/* w x h source pixels into level l of face f at (x, y): GL's unpack state
 * (compat.c may have laid the rows out already: unpack_done) */
static void Upload13(struct Core *c, struct CoreTex *t, ULONG f, ULONG l, ULONG x, ULONG y,
                     ULONG w, ULONG h, const struct Src *s, const UBYTE *pixels)
{
    ULONG rowlen;
    ULONG a;
    ULONG k;
    ULONG stride;
    ULONG swap;
    ULONG r;
    ULONG *p;
    const UBYTE *in;

    rowlen = w;
    a = 4;
    swap = 0;
    in = pixels;
    if (!c->unpack_done)
    {
        if (c->unpack_row)
            rowlen = c->unpack_row;
        a = c->unpack_align;
        swap = (s->s > 1) ? c->unpack_swap : 0;
    }
    /* GL 1.1 3.6.3: a row is k elements of s bytes */
    if (s->s >= a)
        k = s->n * rowlen;
    else
        k = (a / s->s) * ((s->s * s->n * rowlen + a - 1) / a);
    stride = s->s * k;
    if (!c->unpack_done)
        in += c->unpack_skip_rows * stride + c->unpack_skip_px * s->bpp;
    Core_TexHazard(c, t);           /* the GPU may still read the old texels */
    if (c->vram_begin != NULL)
    {
        p = c->vram_begin(c->env.env);
        if (p == NULL)
        {
            c->stat_vram_lost++;
            return;
        }
        c->heap.cpu = p;
    }
    for (r = 0; r < h; r++)
        Row13(c, t, f * t->face_bytes + t->lofs[l] + (y + r) * t->pitch[l] + x * t->cpp, w, s, swap,
              in + r * stride);
    if (c->vram_end != NULL)
        c->vram_end(c->env.env);
}

static void Release13(struct Core *c, struct CoreTex *t)
{
    ULONG f;

    if (t->gpu != 0)
    {
        Core_TexHazard(c, t);
        Heap_Free(c, t->gpu - c->heap.gpu);
        t->gpu = 0;
        t->bytes = 0;
    }
    t->levels = 0;
    for (f = 0; f < 6; f++)
        t->face_lv[f] = 0;
}

static ULONG Log2(ULONG v)
{
    ULONG n;

    n = 0;
    while ((1UL << n) < v)
        n++;
    return n;
}

/* Storage for level 0 of w x h (each of six faces for a cube map) */
static ULONG Allocate13(struct Core *c, struct CoreTex *t, ULONG w, ULONG h, ULONG base, ULONG hwfmt,
                        ULONG cpp)
{
    ULONG l;
    ULONG lw;
    ULONG lh;
    ULONG total;
    ULONG off;
    ULONG pot;

    pot = (w & (w - 1)) == 0 && (h & (h - 1)) == 0;
    if (t->gpu != 0 && t->w == w && t->h == h && t->hwfmt == hwfmt)
    {
        t->base = base;
        return GL_NO_ERROR;
    }
    Release13(c, t);
    total = 0;
    for (l = 0; l < CORE_MAX_LEVELS; l++)
    {
        lw = w >> l;
        lh = h >> l;
        if (lw == 0 && lh == 0)
            break;
        if (lw == 0) lw = 1;
        if (lh == 0) lh = 1;
        t->pitch[l] = (lw * cpp + 31) & ~31UL;
        t->lofs[l] = total;
        total += t->pitch[l] * lh;
        if (!pot)
            break;
    }
    t->face_bytes = total;          /* a multiple of 32: every row is */
    off = Heap_Alloc(c, t->cube ? 6 * total : total, 32);
    if (off == 0xffffffffUL)
        return GL_OUT_OF_MEMORY;
    t->gpu = c->heap.gpu + off;
    t->bytes = t->cube ? 6 * total : total;
    t->w = w;
    t->h = h;
    t->pot = pot;
    t->lw = pot ? Log2(w) : 0;
    t->lh = pot ? Log2(h) : 0;
    t->base = base;
    t->hwfmt = hwfmt;
    t->cpp = cpp;
    t->levels = 0;
    return GL_NO_ERROR;
}

/* texture.c's own call for a plain upload into a 2D texture of its own,
 * with the active unit's 2D binding where it looks */
#define NARROW_BEGIN(c, sv) \
    ((sv)[0] = (c)->x_route, (sv)[1] = (c)->bound[(c)->active_unit], (c)->x_route = 0, \
     (c)->bound[(c)->active_unit] = (c)->bound_2d[(c)->active_unit])
#define NARROW_END(c, sv) \
    ((c)->bound[(c)->active_unit] = (sv)[1], (c)->x_route = (sv)[0])

void Tex13_TexImage2D(struct Core *c, ULONG target, LONG level, LONG internal, LONG w, LONG h,
                      LONG border, ULONG format, ULONG type, const void *pixels)
{
    struct CoreTex *t;
    struct Src s;
    ULONG sv[2];
    ULONG base;
    ULONG hwfmt;
    ULONG cpp;
    ULONG err;
    ULONG f;
    ULONG lw;
    ULONG lh;
    LONG face;

    face = FaceOf(target);
    if (target != GL_TEXTURE_2D && face < 0)
    {
        Core_Error(c, GL_INVALID_ENUM);     /* GL_TEXTURE_CUBE_MAP itself, proxies */
        return;
    }
    t = TargetTex(c, target);
    if (t == NULL)
        return;
    if (face < 0 && t->cube)
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    if (face >= 0 && !t->cube)
    {
        Core_Error(c, GL_INVALID_OPERATION);    /* name 0, or a 2D texture */
        return;
    }
    if (face < 0 && !t->x13 && c->x_route && PlainInternal(internal) && PlainSource(format, type))
    {
        /* texture.c's own (it sent this here only for the routing) */
        NARROW_BEGIN(c, sv);
        Core_TexImage2D(c, target, level, internal, w, h, border, format, type, pixels);
        NARROW_END(c, sv);
        return;
    }
    base = Store(internal, &hwfmt, &cpp);
    if (base == 0 || !SrcOf(format, type, &s))
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    if (level < 0 || level >= CORE_MAX_LEVELS || border != 0 || w < 1 || h < 1
        || w > CORE_MAX_TEXSIZE || h > CORE_MAX_TEXSIZE
        || (face >= 0 && (w != h || (w & (w - 1)) != 0)))
    {
        Core_Error(c, GL_INVALID_VALUE);    /* a cube face: square, a power of two */
        return;
    }
    if (level == 0)
    {
        if (t->gpu != 0 && !t->x13)
            Release13(c, t);        /* texture.c's storage: tex13.c's from now on */
        t->x13 = 1;
        err = Allocate13(c, t, (ULONG) w, (ULONG) h, base, hwfmt, cpp);
        if (err != GL_NO_ERROR)
        {
            Core_Error(c, err);
            Touched(c, t);
            return;
        }
    }
    else
    {
        lw = (t->w >> level) ? (t->w >> level) : 1;
        lh = (t->h >> level) ? (t->h >> level) : 1;
        if (t->gpu == 0 || !t->pot || (ULONG) w != lw || (ULONG) h != lh || hwfmt != t->hwfmt)
        {
            Core_Error(c, GL_INVALID_VALUE);
            return;
        }
        t->x13 = 1;
    }
    f = (face >= 0) ? (ULONG) face : 0;
    if (pixels != NULL)
        Upload13(c, t, f, (ULONG) level, 0, 0, (ULONG) w, (ULONG) h, &s, (const UBYTE *) pixels);
    if (t->cube)
    {
        t->face_lv[f] |= 1UL << level;
        t->levels = t->face_lv[0] & t->face_lv[1] & t->face_lv[2] & t->face_lv[3]
                  & t->face_lv[4] & t->face_lv[5];
    }
    else
        t->levels |= 1UL << level;
    Touched(c, t);
}

void Tex13_TexSubImage2D(struct Core *c, ULONG target, LONG level, LONG x, LONG y, LONG w,
                         LONG h, ULONG format, ULONG type, const void *pixels)
{
    struct CoreTex *t;
    struct Src s;
    ULONG sv[2];
    ULONG lw;
    ULONG lh;
    ULONG have;
    LONG face;

    face = FaceOf(target);
    if (target != GL_TEXTURE_2D && face < 0)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    t = TargetTex(c, target);
    if (face < 0 && t != NULL && !t->x13 && !t->cube && c->x_route && PlainSource(format, type))
    {
        NARROW_BEGIN(c, sv);
        Core_TexSubImage2D(c, target, level, x, y, w, h, format, type, pixels);
        NARROW_END(c, sv);
        return;
    }
    if (!SrcOf(format, type, &s))
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    have = (t != NULL && level >= 0 && level < CORE_MAX_LEVELS)
         ? ((face >= 0) ? t->face_lv[face] : t->levels) & (1UL << level) : 0;
    if (t == NULL || t->gpu == 0 || !have || (face >= 0) != (t->cube != 0))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    lw = (t->w >> level) ? (t->w >> level) : 1;
    lh = (t->h >> level) ? (t->h >> level) : 1;
    if (x < 0 || y < 0 || w < 0 || h < 0 || (ULONG) (x + w) > lw || (ULONG) (y + h) > lh)
    {
        Core_Error(c, GL_INVALID_VALUE);
        return;
    }
    if (w == 0 || h == 0 || pixels == NULL)
        return;
    Upload13(c, t, (face >= 0) ? (ULONG) face : 0, (ULONG) level, (ULONG) x, (ULONG) y, (ULONG) w,
             (ULONG) h, &s, (const UBYTE *) pixels);
}

/* --- The texture environment ------------------------------------------------------------ */

static void CombInit(struct Core *c, ULONG u)
{
    struct CoreComb *k;
    ULONG i;

    k = &c->comb[u];
    if (k->rgb != 0)
        return;                     /* set (GL_COMBINE_RGB is never 0) */
    k->rgb = GL_MODULATE;
    k->a = GL_MODULATE;
    k->src_rgb[0] = k->src_a[0] = GL_TEXTURE;
    k->src_rgb[1] = k->src_a[1] = GL_PREVIOUS;
    k->src_rgb[2] = k->src_a[2] = GL_CONSTANT;
    for (i = 0; i < 3; i++)
    {
        k->op_rgb[i] = (i == 2) ? GL_SRC_ALPHA : GL_SRC_COLOR;
        k->op_a[i] = GL_SRC_ALPHA;
    }
    k->scale_rgb = 0;
    k->scale_a = 0;
}

static ULONG CombMode(ULONG m, ULONG rgb)
{
    switch (m)
    {
        case GL_REPLACE: case GL_MODULATE: case GL_ADD: case GL_ADD_SIGNED:
        case GL_INTERPOLATE: case GL_SUBTRACT:
        case GL_MODULATE_ADD_ATI: case GL_MODULATE_SIGNED_ADD_ATI: case GL_MODULATE_SUBTRACT_ATI:
            return 1;
        case GL_DOT3_RGB: case GL_DOT3_RGBA: case GL_DOT3_RGB_EXT: case GL_DOT3_RGBA_EXT:
            return rgb;
    }
    return 0;
}

static ULONG CombSource(ULONG v)
{
    return (v == GL_TEXTURE || v == GL_CONSTANT || v == GL_PRIMARY_COLOR || v == GL_PREVIOUS
            || v == GL_ZERO || v == GL_ONE
            || (v >= GL_TEXTURE0_ARB && v < GL_TEXTURE0_ARB + CORE_UNITS)) ? 1UL : 0UL;
}

void Tex13_TexEnvi(struct Core *c, ULONG target, ULONG pname, LONG param)
{
    struct CoreComb *k;
    ULONG u;
    ULONG p;
    ULONG i;

    u = c->active_unit;
    p = (ULONG) param;
    if (target == GL_TEXTURE_FILTER_CONTROL_EXT && pname == GL_TEXTURE_LOD_BIAS_EXT)
    {
        c->lod_bias[u] = (float) param;
        c->tex_x = 1;
        DIRTY_UNIT(c, u);
        return;
    }
    if (target != GL_TEXTURE_ENV)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    CombInit(c, u);
    k = &c->comb[u];
    switch (pname)
    {
        case GL_TEXTURE_ENV_MODE:
            if (p != GL_COMBINE)
            {
                Core_Error(c, GL_INVALID_ENUM);
                return;
            }
            c->tex_env[u] = GL_COMBINE;
            c->comb_x |= 1UL << u;
            c->dirty |= D_COMBINE | D_PPCNTL;
            return;
        case GL_COMBINE_RGB:
        case GL_COMBINE_ALPHA:
            if (!CombMode(p, pname == GL_COMBINE_RGB))
            {
                Core_Error(c, GL_INVALID_ENUM);
                return;
            }
            if (pname == GL_COMBINE_RGB)
                k->rgb = p;
            else
                k->a = p;
            break;
        case GL_SOURCE0_RGB: case GL_SOURCE1_RGB: case GL_SOURCE2_RGB:
        case GL_SOURCE0_ALPHA: case GL_SOURCE1_ALPHA: case GL_SOURCE2_ALPHA:
            if (!CombSource(p))
            {
                Core_Error(c, GL_INVALID_ENUM);
                return;
            }
            if (pname <= GL_SOURCE2_RGB)
                k->src_rgb[pname - GL_SOURCE0_RGB] = p;
            else
                k->src_a[pname - GL_SOURCE0_ALPHA] = p;
            break;
        case GL_OPERAND0_RGB: case GL_OPERAND1_RGB: case GL_OPERAND2_RGB:
            if (p != GL_SRC_COLOR && p != GL_ONE_MINUS_SRC_COLOR && p != GL_SRC_ALPHA
                && p != GL_ONE_MINUS_SRC_ALPHA)
            {
                Core_Error(c, GL_INVALID_ENUM);
                return;
            }
            k->op_rgb[pname - GL_OPERAND0_RGB] = p;
            break;
        case GL_OPERAND0_ALPHA: case GL_OPERAND1_ALPHA: case GL_OPERAND2_ALPHA:
            if (p != GL_SRC_ALPHA && p != GL_ONE_MINUS_SRC_ALPHA)
            {
                Core_Error(c, GL_INVALID_ENUM);
                return;
            }
            k->op_a[pname - GL_OPERAND0_ALPHA] = p;
            break;
        case GL_RGB_SCALE:
        case GL_ALPHA_SCALE:
            if (p != 1 && p != 2 && p != 4)
            {
                Core_Error(c, GL_INVALID_VALUE);
                return;
            }
            i = (p == 4) ? 2UL : p - 1;
            if (pname == GL_RGB_SCALE)
                k->scale_rgb = i;
            else
                k->scale_a = i;
            break;
        default:
            Core_Error(c, GL_INVALID_ENUM);
            return;
    }
    if (c->comb_x & (1UL << u))
        c->dirty |= D_COMBINE;
}

void Tex13_TexEnvfv(struct Core *c, ULONG target, ULONG pname, const float *v)
{
    ULONG u;

    if (target == GL_TEXTURE_FILTER_CONTROL_EXT && pname == GL_TEXTURE_LOD_BIAS_EXT)
    {
        u = c->active_unit;
        c->lod_bias[u] = v[0];      /* fractions too, unlike glTexEnvf */
        c->tex_x = 1;
        DIRTY_UNIT(c, u);
        return;
    }
    Tex13_TexEnvi(c, target, pname, Core_Floor(v[0] + 0.5f));
}

/* --- The combiner ------------------------------------------------------------------------ */

/* A classic environment as the combiner's terms (GL 1.3 tables 3.22, 3.23),
 * for its texture's base format */
static void Classic(ULONG env, ULONG base, struct CoreComb *k)
{
    ULONG rgb;
    ULONG alpha;
    ULONG i;

    rgb = (base != GL_ALPHA) ? 1UL : 0UL;
    alpha = (base == GL_RGBA || base == GL_ALPHA || base == GL_LUMINANCE_ALPHA
             || base == GL_INTENSITY) ? 1UL : 0UL;
    for (i = 0; i < 3; i++)
    {
        k->op_rgb[i] = GL_SRC_COLOR;
        k->op_a[i] = GL_SRC_ALPHA;
    }
    k->scale_rgb = 0;
    k->scale_a = 0;
    k->src_rgb[0] = k->src_a[0] = GL_TEXTURE;
    k->src_rgb[1] = k->src_a[1] = GL_PREVIOUS;
    k->src_rgb[2] = k->src_a[2] = GL_TEXTURE;
    k->rgb = GL_MODULATE;
    k->a = alpha ? GL_MODULATE : GL_REPLACE;
    if (!alpha)
        k->src_a[0] = GL_PREVIOUS;
    switch (env)
    {
        case GL_REPLACE:
            k->rgb = GL_REPLACE;
            if (!rgb)
                k->src_rgb[0] = GL_PREVIOUS;
            k->a = GL_REPLACE;
            break;
        case GL_DECAL:
            k->a = GL_REPLACE;
            k->src_a[0] = GL_PREVIOUS;
            if (base == GL_RGBA)
            {
                k->rgb = GL_INTERPOLATE;    /* Ct At + Cf (1 - At) */
                k->op_rgb[2] = GL_SRC_ALPHA;
            }
            else
            {
                k->rgb = GL_REPLACE;
                if (!rgb)
                    k->src_rgb[0] = GL_PREVIOUS;
            }
            break;
        case GL_BLEND:
            k->rgb = GL_INTERPOLATE;        /* Cc Ct + Cf (1 - Ct) */
            k->src_rgb[0] = GL_CONSTANT;
            if (!rgb)
            {
                k->rgb = GL_REPLACE;
                k->src_rgb[0] = GL_PREVIOUS;
            }
            if (base == GL_INTENSITY)
            {
                k->a = GL_INTERPOLATE;
                k->src_a[0] = GL_CONSTANT;
            }
            break;
        case GL_ADD:
            k->rgb = rgb ? GL_ADD : GL_REPLACE;
            if (!rgb)
                k->src_rgb[0] = GL_PREVIOUS;
            if (base == GL_INTENSITY)
                k->a = GL_ADD;
            break;
        default:        /* GL_MODULATE */
            if (!rgb)
            {
                k->rgb = GL_REPLACE;
                k->src_rgb[0] = GL_PREVIOUS;
            }
            break;
    }
}

/* One combiner argument: the register code and its complement bit, for a
 * source, its operand and whether it is the alpha half. prev: the previous
 * stage's output register, or ~0 for the first (the diffuse colour). */
static ULONG Arg(ULONG src, ULONG op, ULONG alpha, ULONG unit, ULONG prev, ULONG *comp)
{
    ULONG asalpha;
    ULONG reg;

    if (alpha)
    {
        *comp = (op == GL_ONE_MINUS_SRC_ALPHA) ? 1UL : 0UL;
        asalpha = 1;
    }
    else
    {
        *comp = (op == GL_ONE_MINUS_SRC_COLOR || op == GL_ONE_MINUS_SRC_ALPHA) ? 1UL : 0UL;
        asalpha = (op == GL_SRC_ALPHA || op == GL_ONE_MINUS_SRC_ALPHA) ? 1UL : 0UL;
    }
    switch (src)
    {
        case GL_ZERO:
            return 0;
        case GL_ONE:
            *comp ^= 1;
            return 0;
        case GL_CONSTANT:
            return alpha ? R_TXA_ARG_TFACTOR_ALPHA : (R_TXC_ARG_TFACTOR_COLOR + asalpha);
        case GL_PRIMARY_COLOR:
            return alpha ? R_TXA_ARG_DIFFUSE_ALPHA : (R_TXC_ARG_DIFFUSE_COLOR + asalpha);
        case GL_PREVIOUS:
            if (prev == 0xffffffffUL)
                return alpha ? R_TXA_ARG_DIFFUSE_ALPHA : (R_TXC_ARG_DIFFUSE_COLOR + asalpha);
            reg = prev;
            break;
        case GL_TEXTURE:
            reg = unit;
            break;
        default:        /* GL_TEXTUREn (crossbar) */
            reg = src - GL_TEXTURE0_ARB;
            break;
    }
    return alpha ? (R_TXA_ARG_R0_ALPHA + 2 * reg) : (R_TXC_ARG_R0_COLOR + 2 * reg + asalpha);
}

/* Combine arguments n into slot X (0 A, 1 B, 2 C) of a blend word */
static ULONG Put(ULONG code, ULONG comp, ULONG slot)
{
    return (code << (5 * slot)) | (comp ? (R_TXC_COMP_ARG_A << (4 * slot)) : 0);
}

/* One half of a stage (colour or alpha) for a combine mode */
static ULONG Blend(const struct CoreComb *k, ULONG alpha, ULONG unit, ULONG prev)
{
    ULONG mode;
    ULONG code[3];
    ULONG comp[3];
    ULONG i;
    ULONG v;

    mode = alpha ? k->a : k->rgb;
    code[0] = code[1] = code[2] = 0;    /* SAS/C cannot see the loop set them (W317) */
    comp[0] = comp[1] = comp[2] = 0;
    for (i = 0; i < 3; i++)
        code[i] = Arg(alpha ? k->src_a[i] : k->src_rgb[i], alpha ? k->op_a[i] : k->op_rgb[i], alpha,
                      unit, prev, &comp[i]);
    switch (mode)
    {
        case GL_REPLACE:
            return Put(code[0], comp[0], 2) | R_TXC_OP_MADD;                /* 0 * 0 + a0 */
        case GL_ADD:
            return Put(code[0], comp[0], 0) | Put(0, 1, 1) | Put(code[1], comp[1], 2) | R_TXC_OP_MADD;
        case GL_ADD_SIGNED:
            return Put(code[0], comp[0], 0) | Put(0, 1, 1) | Put(code[1], comp[1], 2)
                 | R_TXC_BIAS_ARG_C | R_TXC_OP_MADD;
        case GL_SUBTRACT:
            return Put(code[0], comp[0], 0) | Put(0, 1, 1) | Put(code[1], comp[1], 2)
                 | R_TXC_NEG_ARG_C | R_TXC_OP_MADD;
        case GL_INTERPOLATE:        /* LERP: A (1 - C) + B C */
            return Put(code[1], comp[1], 0) | Put(code[0], comp[0], 1) | Put(code[2], comp[2], 2)
                 | R_TXC_OP_LERP;
        case GL_DOT3_RGB: case GL_DOT3_RGBA: case GL_DOT3_RGB_EXT: case GL_DOT3_RGBA_EXT:
            /* both into [-1, 1]: (x - 0.5) * 2 (Mesa r200UpdateTextureEnv) */
            v = Put(code[0], comp[0], 0) | Put(code[1], comp[1], 1) | R_TXC_OP_DOT3
              | R_TXC_BIAS_ARG_A | R_TXC_BIAS_ARG_B | R_TXC_SCALE_ARG_A | R_TXC_SCALE_ARG_B;
            return v;
        case GL_MODULATE_ADD_ATI:   /* a0 a2 + a1 */
            return Put(code[0], comp[0], 0) | Put(code[2], comp[2], 1) | Put(code[1], comp[1], 2)
                 | R_TXC_OP_MADD;
        case GL_MODULATE_SIGNED_ADD_ATI:
            return Put(code[0], comp[0], 0) | Put(code[2], comp[2], 1) | Put(code[1], comp[1], 2)
                 | R_TXC_BIAS_ARG_C | R_TXC_OP_MADD;
        case GL_MODULATE_SUBTRACT_ATI:
            return Put(code[0], comp[0], 0) | Put(code[2], comp[2], 1) | Put(code[1], comp[1], 2)
                 | R_TXC_NEG_ARG_C | R_TXC_OP_MADD;
    }
    return Put(code[0], comp[0], 0) | Put(code[1], comp[1], 1) | R_TXC_OP_MADD;    /* GL_MODULATE */
}

/* The arguments a combine mode reads */
static ULONG NArgs(ULONG mode)
{
    switch (mode)
    {
        case GL_REPLACE:
            return 1;
        case GL_INTERPOLATE: case GL_MODULATE_ADD_ATI: case GL_MODULATE_SIGNED_ADD_ATI:
        case GL_MODULATE_SUBTRACT_ATI:
            return 3;
    }
    return 2;
}

/* Mark the texture registers a stage reads (they must survive to it) */
static void Reads(const struct CoreComb *k, ULONG unit, ULONG *busy)
{
    ULONG i;
    ULONG s;
    ULONG nr;
    ULONG na;

    nr = NArgs(k->rgb);
    na = NArgs(k->a);
    for (i = 0; i < 6; i++)
    {
        if ((i < 3 && i >= nr) || (i >= 3 && i - 3 >= na))
            continue;
        s = (i < 3) ? k->src_rgb[i] : k->src_a[i - 3];
        if (s == GL_TEXTURE)
            *busy |= 1UL << unit;
        else if (s >= GL_TEXTURE0_ARB && s < GL_TEXTURE0_ARB + CORE_UNITS)
            *busy |= 1UL << (s - GL_TEXTURE0_ARB);
    }
}

/* The environment of unit u in the combiner's terms */
static void Terms(struct Core *c, ULONG u, struct CoreComb *k)
{
    struct CoreTex *t;

    if (c->comb_x & (1UL << u))
    {
        CombInit(c, u);
        *k = c->comb[u];
        return;
    }
    t = Core_Texture(c, u);
    Classic(c->tex_env[u], (t != NULL) ? t->base : GL_RGBA, k);
}

LONG Tex13_Combiner(struct Core *c, ULONG units)
{
    struct CoreComb k[CORE_UNITS];
    ULONG list[CORE_UNITS];
    ULONG out[CORE_UNITS];
    ULONG n;
    ULONG u;
    ULONG i;
    ULONG j;
    ULONG busy;
    ULONG prev;
    ULONG cb, cb2, ab, ab2;
    ULONG dot;
    ULONG *sh;

    n = 0;
    for (u = 0; u < CORE_UNITS; u++)
        if (units & (1UL << u))
        {
            Terms(c, u, &k[n]);
            list[n++] = u;
        }
    if (n == 0)
    {
        /* no texture: one stage, the diffuse colour */
        cb = (R_TXC_ARG_DIFFUSE_COLOR << R_TXC_ARG_C_SHIFT) | R_TXC_OP_MADD;
        ab = (R_TXA_ARG_DIFFUSE_ALPHA << R_TXC_ARG_C_SHIFT) | R_TXC_OP_MADD;
        cb2 = R_TXC_CLAMP_0_1 | R_TXC_OUTPUT_REG_R0;
        sh = c->hw_comb[0];
        if (sh[0] != cb || sh[1] != cb2 || sh[2] != ab || sh[3] != cb2)
        {
            sh[0] = cb; sh[1] = cb2; sh[2] = ab; sh[3] = cb2;
            (void) R2E_Combiner(c->cs, 0, cb, cb2, ab, cb2);
        }
        return c->cs->error;
    }
    /* output registers, from the last stage back (Mesa r200UpdateAllTexEnv):
     * the lowest register no later stage still reads as a texture -- the
     * last stage's is R0 */
    busy = 0;
    for (i = n; i-- > 0;)
    {
        for (j = 0; j < CORE_UNITS && (busy & (1UL << j)); j++)
            ;
        out[i] = (j < CORE_UNITS) ? j : 0;
        Reads(&k[i], list[i], &busy);
    }
    prev = 0xffffffffUL;
    for (i = 0; i < n; i++)
    {
        u = list[i];
        cb = Blend(&k[i], 0, u, prev);
        ab = Blend(&k[i], 1, u, prev);
        cb2 = R_TXC_CLAMP_0_1 | ((out[i] + 1) << R_TXC_OUTPUT_REG_SHIFT) | (u << R_TXC_TFACTOR_SEL_SHIFT);
        ab2 = cb2;
        dot = (k[i].rgb == GL_DOT3_RGB || k[i].rgb == GL_DOT3_RGBA
               || k[i].rgb == GL_DOT3_RGB_EXT || k[i].rgb == GL_DOT3_RGBA_EXT) ? 1UL : 0UL;
        /* the scale; EXT_texture_env_dot3 has none */
        if (!(k[i].rgb == GL_DOT3_RGB_EXT || k[i].rgb == GL_DOT3_RGBA_EXT))
            cb2 |= k[i].scale_rgb << R_TXC_SCALE_SHIFT;
        if (dot && (k[i].rgb == GL_DOT3_RGBA || k[i].rgb == GL_DOT3_RGBA_EXT))
            ab2 |= R_TXA_DOT_ALPHA | (cb2 & (7UL << R_TXC_SCALE_SHIFT));     /* alpha = the dot too */
        else
            ab2 |= k[i].scale_a << R_TXC_SCALE_SHIFT;
        sh = c->hw_comb[i];
        if (sh[0] != cb || sh[1] != cb2 || sh[2] != ab || sh[3] != ab2 || c->hw_tfs[u] != c->tf_color[u])
        {
            /* the unit's constant colour with its stage (as compat.c Core_CombExtra:
             * a forgotten stage forgets it too) */
            if (sh[0] == 0xffffffffUL || c->hw_tfs[u] != c->tf_color[u])
            {
                c->hw_tfs[u] = c->tf_color[u];
                (void) R2E_Reg(c->cs, R_PP_TFACTOR_0 + R_PP_TFACTOR_STRIDE * u, c->tf_color[u]);
            }
            if (sh[0] != cb || sh[1] != cb2 || sh[2] != ab || sh[3] != ab2)
            {
                sh[0] = cb; sh[1] = cb2; sh[2] = ab; sh[3] = ab2;
                (void) R2E_Combiner(c->cs, i, cb, cb2, ab, ab2);
            }
        }
        prev = out[i];
    }
    return c->cs->error;
}

ULONG Tex13_PpCntl(struct Core *c, ULONG v, ULONG units)
{
    ULONG n;
    ULONG u;

    (void) c;
    n = 0;
    for (u = 0; u < CORE_UNITS; u++)
        if (units & (1UL << u))
            n++;
    if (n == 0)
        n = 1;
    v &= ~(0x7fUL * R_TEX_BLEND_0_ENABLE);
    return v | (((1UL << n) - 1) * R_TEX_BLEND_0_ENABLE);
}

/* --- Wide vertices ------------------------------------------------------------------------ */

static ULONG Comps(struct Core *c, ULONG u)
{
    return (c->cube_on & (1UL << u)) ? 3UL : 2UL;
}

ULONG Tex13_Dw(struct Core *c, ULONG units)
{
    ULONG n;
    ULONG u;

    n = 4 + c->x_vdw;
    for (u = 0; u < CORE_UNITS; u++)
        if (units & (1UL << u))
            n += Comps(c, u);
    return n;
}

ULONG Tex13_Fmt1(struct Core *c, ULONG units)
{
    ULONG f;
    ULONG u;

    f = 0;
    for (u = 0; u < CORE_UNITS; u++)
        if (units & (1UL << u))
            f |= Comps(c, u) << (3 * u);
    return f;
}

/* RE_CNTL: STQn_D3D for each drawn cube unit's coordinate set (set u,
 * as Tex13_Fmt1 lays them out), the rest as R2E_RasterDefaults left it */
void Tex13_ReCntl(struct Core *c, ULONG units)
{
    ULONG d3d;
    ULONG u;

    d3d = 0;
    for (u = 0; u < CORE_UNITS; u++)
        if ((units & c->cube_on) & (1UL << u))
            d3d |= R_VTX_STQ0_D3D << (2 * u);
    if (d3d == c->hw_stq_d3d)
        return;
    (void) R2E_Reg(c->cs, R_RE_CNTL, R_PERSPECTIVE_ENABLE | R_SCISSOR_ENABLE | d3d);
    c->hw_stq_d3d = d3d;
}

void Tex13_Emit(struct Core *c, ULONG *p, ULONG x, ULONG y, ULONG z)
{
    ULONG b[3 + 3 + 1 + 3 * CORE_UNITS];
    ULONG n;
    ULONG u;
    ULONG k;

    b[0] = x;
    b[1] = y;
    b[2] = z;
    n = 3;
    if (c->vnorm)
    {
        b[n++] = R2_FBITS(c->cur_normal[0]);
        b[n++] = R2_FBITS(c->cur_normal[1]);
        b[n++] = R2_FBITS(c->cur_normal[2]);
    }
    b[n++] = c->cur_color;
    for (u = 0; u < CORE_UNITS; u++)
    {
        if (!(c->units_on & (1UL << u)))
            continue;
        b[n++] = R2_FBITS(c->cur_st[u][0]);
        b[n++] = R2_FBITS(c->cur_st[u][1]);
        if (c->cube_on & (1UL << u))
            b[n++] = R2_FBITS(c->cur_r[u]);
    }
    for (k = 0; k < n; k++)
    {
        if (c->cs->swap)
            R2_SWAPI(b[k]);
        p[k] = b[k];
    }
}

/* --- Wide array draws: glArrayElement's way, every unit, r too ------------------------------ */

static void FetchWide(struct Core *c, ULONG i, float *pos)
{
    const UBYTE *p;
    ULONG u;
    ULONG t;

    Fetch(c, i, pos);               /* the colour, units 0 and 1's s and t, the position */
    for (u = 0; u < CORE_UNITS; u++)
    {
        if (!c->ta[u].enabled)
            continue;
        p = Elem(&c->ta[u], i);
        t = c->ta[u].type;
        if (u >= CORE_NARROW)
        {
            c->cur_st[u][0] = Comp(p, t, 0);
            c->cur_st[u][1] = (c->ta[u].size >= 2) ? Comp(p, t, 1) : 0.0f;
        }
        c->cur_r[u] = (c->ta[u].size >= 3) ? Comp(p, t, 2) : 0.0f;
    }
}

static void WideVertex(struct Core *c, ULONG i)
{
    float pos[4];

    if (c->vnorm && c->na.enabled)
        FetchNormal(c, i);
    FetchWide(c, i, pos);
    Core_Vertex3f(c, pos[0], pos[1], pos[2]);   /* Fetch divided by w */
}

void Tex13_DrawArrays(struct Core *c, ULONG mode, LONG first, LONG count)
{
    LONG i;

    if (!c->va.enabled || count == 0 || CORE_SCISSOR_EMPTY(c))
        return;
    Core_Begin(c, mode);
    for (i = 0; i < count; i++)
        WideVertex(c, (ULONG) (first + i));
    Core_End(c);
}

void Tex13_DrawElements(struct Core *c, ULONG mode, LONG count, ULONG type, const void *idx)
{
    LONG i;
    ULONG k;

    if (!c->va.enabled || count == 0 || idx == NULL || CORE_SCISSOR_EMPTY(c))
        return;
    Core_Begin(c, mode);
    for (i = 0; i < count; i++)
    {
        if (type == GL_UNSIGNED_BYTE)
            k = ((const UBYTE *) idx)[i];
        else if (type == GL_UNSIGNED_SHORT)
            k = ((const UWORD *) idx)[i];
        else
            k = ((const ULONG *) idx)[i];
        WideVertex(c, k);
    }
    Core_End(c);
}

void Tex13_ArrayElement(struct Core *c, LONG i)
{
    float pos[4];

    if (c->na.enabled)
        FetchNormal(c, (ULONG) i);
    if (c->ea.enabled && c->ea.ptr != NULL)
        Core_EdgeFlag(c, c->ea.ptr[(ULONG) i * (c->ea.stride ? c->ea.stride : 1UL)]);
    FetchWide(c, (ULONG) i, pos);
    if (c->va.enabled)
        Core_Vertex3f(c, pos[0], pos[1], pos[2]);
}

/* --- Queries, the light model ------------------------------------------------------------- */

ULONG Tex13_Query(struct Core *c, ULONG pname, float *v)
{
    ULONG u;

    u = c->active_unit;
    switch (pname)
    {
        case GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT: v[0] = 16.0f; return 1;
        case GL_MAX_CUBE_MAP_TEXTURE_SIZE:      v[0] = (float) CORE_MAX_TEXSIZE; return 1;
        case GL_MAX_TEXTURE_LOD_BIAS_EXT:       v[0] = 15.9f; return 1;
        case GL_TEXTURE_BINDING_CUBE_MAP:       v[0] = (float) c->bound_cube[u]; return 1;
        case GL_TEXTURE_BINDING_2D:
            if (!c->x_route)
                return 0;                       /* get.c's */
            v[0] = (float) c->bound_2d[u];
            return 1;
        case GL_CURRENT_TEXTURE_COORDS:
            v[0] = c->cur_st[u][0];
            v[1] = c->cur_st[u][1];
            v[2] = c->cur_r[u];
            v[3] = 1.0f;
            return 4;
        case GL_TEXTURE_CUBE_MAP:
        case GL_TEXTURE_GEN_R:
        case GL_TEXTURE_GEN_Q:
        case GL_RESCALE_NORMAL:
            v[0] = (float) Core_IsEnabled(c, pname);
            return 1;
        case GL_LIGHT_MODEL_COLOR_CONTROL:
            v[0] = (float) (c->spec_sep ? GL_SEPARATE_SPECULAR_COLOR : GL_SINGLE_COLOR);
            return 1;
    }
    return 0;
}

void Tex13_LightModel(struct Core *c, ULONG pname, float v)
{
    ULONG p;

    p = (ULONG) Core_Floor(v + 0.5f);
    if (pname != GL_LIGHT_MODEL_COLOR_CONTROL || (p != GL_SINGLE_COLOR && p != GL_SEPARATE_SPECULAR_COLOR))
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    c->spec_sep = (p == GL_SEPARATE_SPECULAR_COLOR) ? 1UL : 0UL;
    Core_Extras(c);                 /* the specular output, PP_CNTL's SPECULAR_ENABLE (light.c) */
    c->dirty |= D_LIGHT | D_PPCNTL | D_VTXFMT;
}
