/*
 * texture.c -- texture objects, their VRAM storage and uploads.
 *
 * Storage (concept 6.6): the R200 formats a 16-bit Quake needs, chosen
 * from the internal format:
 *
 *   RGB (3, GL_RGB, GL_RGB5, MGL 5_6_5)        RGB565
 *   RGBA (4, GL_RGBA, MGL 4_4_4_4, ...)        ARGB4444
 *   GL_RGB8, GL_RGBA8 (W2, 2026-11-07)          ARGB8888 -- true colour where a
 *                                              program asks for 8 bits a
 *                                              component by name (GLQuake asks
 *                                              for 3 and 4: it stays 16 bit)
 *   LUMINANCE (1)                              I8      (alpha from the fragment)
 *   INTENSITY, ALPHA                           I8 | ALPHA_IN_MAP (Mesa's table)
 *   LUMINANCE_ALPHA (2)                        AI88
 *
 * Every mip level's rows are padded to 32 bytes and the levels follow each
 * other (Mesa radeon_mipmap_tree); level 0 is 32-byte aligned. The chain
 * for a power-of-two level 0 is allocated at once.
 *
 * Writes go to VRAM a dword at a time. A dword is first packed the way the
 * little-endian GPU reads it (texel k in the k-th lowest bytes), then
 * stored byte-reversed when the CPU must swap (tex_swap, as the stream's
 * swap: the Mediator does not swap). Rows whose ends do not fall on dword
 * boundaries read the edge dwords back first.
 *
 * Sources: GL_RGBA, GL_RGB, GL_LUMINANCE, GL_ALPHA, GL_LUMINANCE_ALPHA with
 * GL_UNSIGNED_BYTE, MiniGL's MGL_UNSIGNED_SHORT_4_4_4_4 (A4R4G4B4 from the
 * most significant bit, big-endian words) and _5_6_5 (R5G6B5), as format
 * or as type -- GLQuake uploads its lightmaps as the former.
 */

#include <stddef.h>
#include "core.h"
#include "coreint.h"
#include "r200reg.h"

void Tex_Defaults(struct CoreTex *t, ULONG name)
{
    UBYTE *p;
    ULONG i;

    p = (UBYTE *) t;
    for (i = 0; i < sizeof(*t); i++)
        p[i] = 0;
    t->name = name;
    t->min_filter = GL_NEAREST_MIPMAP_LINEAR;
    t->mag_filter = GL_LINEAR;
    t->wrap_s = GL_REPEAT;
    t->wrap_t = GL_REPEAT;
    t->base = GL_RGBA;
}

struct CoreTex *Tex_Get(struct Core *c, ULONG name)
{
    struct CoreTex *t;

    if (name >= CORE_MAX_TEX)
        return NULL;
    t = c->tex[name];
    if (t == NULL)
    {
        t = (struct CoreTex *) c->env.alloc(c->env.env, sizeof(struct CoreTex));
        if (t == NULL)
            return NULL;
        Tex_Defaults(t, name);
        c->tex[name] = t;
    }
    return t;
}

struct CoreTex *Core_Texture(struct Core *c, ULONG unit)
{
    struct CoreTex *t;

    t = c->tex[c->bound[unit]];
    if (t == NULL || t->gpu == 0 || !(t->levels & 1))
        return NULL;
    return t;
}

/* Every unit that has t bound must re-emit it, TXOFFSET included even
 * when the address stayed: the emission after a glTexImage2D wrote that
 * register before the shadow registers (hwstate.c) existed, and whether
 * the write is what drops the GPU's cached texels is not measured. */
static void Touched(struct Core *c, const struct CoreTex *t)
{
    ULONG u;

    for (u = 0; u < CORE_UNITS; u++)
        if (c->bound[u] == t->name)
        {
            DIRTY_UNIT(c, u);
            c->hw_tex[u][6] = 0xffffffffUL;
        }
}

void Core_BindTexture(struct Core *c, ULONG target, ULONG name)
{
    ULONG u;

    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    if (target != GL_TEXTURE_2D)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    u = c->active_unit;
    if (c->bound[u] == name)
        return;
    if (Tex_Get(c, name) == NULL)
    {
        Core_Error(c, name >= CORE_MAX_TEX ? GL_INVALID_VALUE : GL_OUT_OF_MEMORY);
        return;
    }
    c->bound[u] = name;
    DIRTY_UNIT(c, u);
}

void Core_GenTextures(struct Core *c, LONG n, ULONG *names)
{
    LONG i;
    ULONG k;

    if (n < 0)
    {
        Core_Error(c, GL_INVALID_VALUE);
        return;
    }
    k = 1;
    for (i = 0; i < n; i++)
    {
        /* the lowest free names; the object is made now, so the name
         * stays taken until glDeleteTextures */
        while (k < CORE_MAX_TEX && c->tex[k] != NULL)
            k++;
        if (k >= CORE_MAX_TEX || Tex_Get(c, k) == NULL)
        {
            Core_Error(c, GL_OUT_OF_MEMORY);
            for (; i < n; i++)
                names[i] = 0;
            return;
        }
        names[i] = k;
    }
}

ULONG Core_IsTexture(struct Core *c, ULONG name)
{
    return (name != 0 && name < CORE_MAX_TEX && c->tex[name] != NULL) ? 1UL : 0UL;
}

/* Two 16-bit source texels from one longword load (Pair16, below). */
#if defined(__GNUC__) && defined(__mc68000__)
#define LOAD_TEX32(q) (*(const ULONG *) (const void *) (q))
#else
#define LOAD_TEX32(q) (((ULONG) (q)[0] << 24) | ((ULONG) (q)[1] << 16) | ((ULONG) (q)[2] << 8) \
                       | (ULONG) (q)[3])
#endif
/* q on a longword boundary (size_t: a host pointer may be wider than a
 * ULONG) */
#define TEX_ALIGNED(q) ((((size_t) (const void *) (q)) & 3) == 0)

static void Release(struct Core *c, struct CoreTex *t)
{
    if (t->gpu != 0)
    {
        Core_TexHazard(c, t);       /* the GPU may still read the memory */
        Heap_Free(c, t->gpu - c->heap.gpu);
        t->gpu = 0;
        t->bytes = 0;
        t->levels = 0;
    }
}

void Core_DeleteTextures(struct Core *c, LONG n, const ULONG *names)
{
    LONG i;
    ULONG u;
    struct CoreTex *t;

    if (IN_BEGIN(c))
    {
        /* GL forbids it; the hazard flush would also close the open packet */
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    if (n < 0)
    {
        Core_Error(c, GL_INVALID_VALUE);
        return;
    }
    for (i = 0; i < n; i++)
    {
        if (names[i] == 0 || names[i] >= CORE_MAX_TEX || c->tex[names[i]] == NULL)
            continue;
        t = c->tex[names[i]];
        Release(c, t);
        for (u = 0; u < CORE_UNITS; u++)
            if (c->bound[u] == names[i])
            {
                c->bound[u] = 0;
                DIRTY_UNIT(c, u);
            }
        c->env.free(c->env.env, t, sizeof(*t));
        c->tex[names[i]] = NULL;
    }
}

void Core_TexParameteri(struct Core *c, ULONG target, ULONG pname, LONG param)
{
    struct CoreTex *t;
    ULONG p;

    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    if (target != GL_TEXTURE_2D)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    t = c->tex[c->bound[c->active_unit]];
    if (t == NULL)
        return;
    p = (ULONG) param;
    switch (pname)
    {
        case GL_TEXTURE_MIN_FILTER:
            if (p != GL_NEAREST && p != GL_LINEAR && p != GL_NEAREST_MIPMAP_NEAREST
                && p != GL_NEAREST_MIPMAP_LINEAR && p != GL_LINEAR_MIPMAP_NEAREST
                && p != GL_LINEAR_MIPMAP_LINEAR)
                break;
            t->min_filter = p;
            Touched(c, t);
            return;
        case GL_TEXTURE_MAG_FILTER:
            if (p != GL_NEAREST && p != GL_LINEAR)
                break;
            t->mag_filter = p;
            Touched(c, t);
            return;
        case GL_TEXTURE_WRAP_S:
        case GL_TEXTURE_WRAP_T:
            if (p == GL_CLAMP_TO_EDGE)
                p = GL_CLAMP;           /* R200: CLAMP_LAST either way */
            if (p != GL_REPEAT && p != GL_CLAMP)
                break;
            if (pname == GL_TEXTURE_WRAP_S)
                t->wrap_s = p;
            else
                t->wrap_t = p;
            Touched(c, t);
            return;
    }
    Core_Error(c, GL_INVALID_ENUM);
}

/* --- Formats -------------------------------------------------------------------- */

/* GL internal format -> base format, or 0 if unsupported. */
static ULONG BaseOf(LONG internal)
{
    switch ((ULONG) internal)
    {
        case 1: case GL_LUMINANCE: case GL_LUMINANCE8:
            return GL_LUMINANCE;
        case 2: case GL_LUMINANCE_ALPHA:
            return GL_LUMINANCE_ALPHA;
        case 3: case GL_RGB: case GL_RGB5: case GL_RGB8: case MGL_UNSIGNED_SHORT_5_6_5:
            return GL_RGB;
        case 4: case GL_RGBA: case GL_RGBA8: case GL_RGB5_A1: case MGL_UNSIGNED_SHORT_4_4_4_4:
            return GL_RGBA;
        case GL_ALPHA: case GL_ALPHA8:
            return GL_ALPHA;
        case GL_INTENSITY: case GL_INTENSITY8:
            return GL_INTENSITY;
    }
    return 0;
}

/* GL_RGB8 and GL_RGBA8: stored with 8 bits a component (W2) */
static ULONG DeepOf(LONG internal)
{
    return ((ULONG) internal == GL_RGB8 || (ULONG) internal == GL_RGBA8) ? 1UL : 0UL;
}

static void HwOf(ULONG base, ULONG deep, ULONG *hwfmt, ULONG *cpp)
{
    if (deep && (base == GL_RGB || base == GL_RGBA))
    {
        /* RGB: no alpha in the map, the R200 reads alpha 1 (Mesa XRGB8888) */
        *hwfmt = R_TXFORMAT_ARGB8888 | ((base == GL_RGBA) ? R_TXFORMAT_ALPHA_IN_MAP : 0);
        *cpp = 4;
        return;
    }
    switch (base)
    {
        case GL_RGB:            *hwfmt = R_TXFORMAT_RGB565; *cpp = 2; return;
        case GL_LUMINANCE:      *hwfmt = R_TXFORMAT_I8; *cpp = 1; return;
        case GL_INTENSITY:
        case GL_ALPHA:          *hwfmt = R_TXFORMAT_I8 | R_TXFORMAT_ALPHA_IN_MAP; *cpp = 1; return;
        case GL_LUMINANCE_ALPHA: *hwfmt = R_TXFORMAT_AI88 | R_TXFORMAT_ALPHA_IN_MAP; *cpp = 2; return;
    }
    *hwfmt = R_TXFORMAT_ARGB4444 | R_TXFORMAT_ALPHA_IN_MAP;
    *cpp = 2;
}

/* Source layouts */
#define SRC_RGBA    1
#define SRC_RGB     2
#define SRC_L       3
#define SRC_A       4
#define SRC_LA      5
#define SRC_4444    6   /* big-endian A4R4G4B4 words */
#define SRC_565     7   /* big-endian R5G6B5 words */
#define SRC_ARGB32  8   /* GL_BGRA + GL_UNSIGNED_INT_8_8_8_8_REV: native 0xAARRGGBB words */

/* One native 32-bit word (the host tests' byte order is the host's, as GL
 * means for a packed type); the source is longword aligned (GL's unpack
 * alignment 4 and 4 bytes a pixel) */
#define NATIVE32(q) (*(const ULONG *) (const void *) (q))

static ULONG SrcOf(ULONG format, ULONG type, ULONG *bpp)
{
    if (format == MGL_UNSIGNED_SHORT_4_4_4_4 || type == MGL_UNSIGNED_SHORT_4_4_4_4)
    {
        *bpp = 2;
        return SRC_4444;
    }
    if (format == MGL_UNSIGNED_SHORT_5_6_5 || type == MGL_UNSIGNED_SHORT_5_6_5)
    {
        *bpp = 2;
        return SRC_565;
    }
    if (format == GL_BGRA && type == GL_UNSIGNED_INT_8_8_8_8_REV)
    {
        *bpp = 4;
        return SRC_ARGB32;
    }
    if (type != GL_UNSIGNED_BYTE)
        return 0;
    switch (format)
    {
        case GL_RGBA:               *bpp = 4; return SRC_RGBA;
        case GL_RGB:                *bpp = 3; return SRC_RGB;
        case GL_LUMINANCE:          *bpp = 1; return SRC_L;
        case GL_ALPHA:              *bpp = 1; return SRC_A;
        case GL_LUMINANCE_ALPHA:    *bpp = 2; return SRC_LA;
    }
    return 0;
}

/* One source pixel as r, g, b, a bytes. */
static void Fetch(ULONG src, const UBYTE *p, ULONG *r, ULONG *g, ULONG *b, ULONG *a)
{
    ULONG w;

    switch (src)
    {
        case SRC_RGBA: *r = p[0]; *g = p[1]; *b = p[2]; *a = p[3]; return;
        case SRC_RGB:  *r = p[0]; *g = p[1]; *b = p[2]; *a = 255; return;
        case SRC_L:    *r = *g = *b = p[0]; *a = 255; return;
        case SRC_A:    *r = *g = *b = 255; *a = p[0]; return;
        case SRC_LA:   *r = *g = *b = p[0]; *a = p[1]; return;
        case SRC_ARGB32:
            w = NATIVE32(p);
            *a = w >> 24;
            *r = (w >> 16) & 255;
            *g = (w >> 8) & 255;
            *b = w & 255;
            return;
        case SRC_4444:
            w = ((ULONG) p[0] << 8) | p[1];
            *a = ((w >> 12) & 15) * 17;
            *r = ((w >> 8) & 15) * 17;
            *g = ((w >> 4) & 15) * 17;
            *b = (w & 15) * 17;
            return;
        default:    /* SRC_565 */
            w = ((ULONG) p[0] << 8) | p[1];
            *r = ((w >> 11) & 31) * 255 / 31;
            *g = ((w >> 5) & 63) * 255 / 63;
            *b = (w & 31) * 255 / 31;
            *a = 255;
            return;
    }
}

/* One texel in the storage format, as the GPU reads it (cpp 4: ARGB8888). */
static ULONG Encode(ULONG base, ULONG cpp, ULONG r, ULONG g, ULONG b, ULONG a)
{
    if (cpp == 4)
        return (a << 24) | (r << 16) | (g << 8) | b;
    switch (base)
    {
        case GL_RGB:
            return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        case GL_LUMINANCE:
            return r;
        case GL_INTENSITY:
            return r;
        case GL_ALPHA:
            return a;
        case GL_LUMINANCE_ALPHA:
            return (a << 8) | r;
    }
    return ((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4);
}

/* A source pixel straight into the storage format, without the bytes in
 * between, where the layouts already agree. */
static ULONG Direct(ULONG src, ULONG base, ULONG cpp, const UBYTE *p, ULONG *texel)
{
    if (cpp == 4)
        return 0;
    if ((src == SRC_4444 && base == GL_RGBA) || (src == SRC_565 && base == GL_RGB))
    {
        *texel = ((ULONG) p[0] << 8) | p[1];
        return 1;
    }
    if (src == SRC_L && (base == GL_LUMINANCE || base == GL_INTENSITY))
    {
        *texel = p[0];
        return 1;
    }
    return 0;
}

static ULONG Out(struct Core *c, ULONG g)
{
    if (c->tex_swap)
        R2_SWAPI(g);        /* in line on the 68k (r2cs.h) */
    return g;
}

static ULONG In(struct Core *c, ULONG v)
{
    if (c->tex_swap)
        R2_SWAPI(v);
    return v;
}

/* Two 16-bit source texels (big-endian bytes) from one longword load, as
 * the dword VRAM holds them: the first texel in the low half, byte-reversed
 * when tex_swap -- which is the load with the bytes of each half swapped.
 * MEASURED on the A4000D: four byte loads, shifts and ors a dword cost as
 * much as the bus writes they fed. */
static ULONG Pair16(const struct Core *c, ULONG w)
{
    if (c->tex_swap)
        return ((w & 0x00ff00ffUL) << 8) | ((w >> 8) & 0x00ff00ffUL);
    return (w << 16) | (w >> 16);
}

/* 16-bit source texels already in the storage layout (GLQuake's 4444
 * lightmaps, every frame): two a dword, the generic path's dword without
 * its per-texel work. A texel in slot 0 is the dword's low half. */
static void WriteRow16(struct Core *c, ULONG *dst, ULONG first, ULONG w, const UBYTE *row)
{
    ULONG i;
    ULONG texel;
    const UBYTE *p;

    i = 0;
    if (first != 0)
    {
        /* the row starts in a dword's high half: keep the low one */
        texel = ((ULONG) row[0] << 8) | row[1];
        *dst = Out(c, (In(c, *dst) & 0x0000ffffUL) | (texel << 16));
        dst++;
        i = 1;
    }
    p = row + i * 2;
    if (TEX_ALIGNED(p))
        for (; i + 2 <= w; i += 2)
        {
            *dst++ = Pair16(c, LOAD_TEX32(p));
            p += 4;
        }
    else
        for (; i + 2 <= w; i += 2)
        {
            *dst++ = Out(c, ((ULONG) p[0] << 8) | p[1] | ((ULONG) p[2] << 24) | ((ULONG) p[3] << 16));
            p += 4;
        }
    if (i < w)
    {
        texel = ((ULONG) p[0] << 8) | p[1];
        *dst = Out(c, (In(c, *dst) & 0xffff0000UL) | texel);
    }
}

/* 8-bit texels that need no conversion (luminance into GL_LUMINANCE or
 * GL_INTENSITY: GLQuake's -lm_LUMINANCE lightmaps): texel k of a dword is
 * byte k in VRAM, so a longword load of four source bytes is already the
 * dword the 68k writes when it swaps (tex_swap), and the byte-reversed one
 * when it does not. MEASURED on the A4000D: through the general way these
 * lightmaps cost more than the 16-bit ones. Partial dwords at the ends
 * keep the bytes beside them. */
static void WriteRow8(struct Core *c, ULONG *dst, ULONG first, ULONG w, const UBYTE *row)
{
    ULONG i;
    ULONG k;
    ULONG v;
    ULONG acc;
    ULONG mask;
    const UBYTE *p;

    i = 0;
    if (first != 0)
    {
        acc = 0;
        mask = 0;
        for (; i < w && first + i < 4; i++)
        {
            acc |= (ULONG) row[i] << ((first + i) * 8);
            mask |= 0xffUL << ((first + i) * 8);
        }
        *dst = Out(c, (In(c, *dst) & ~mask) | acc);
        dst++;
    }
    p = row + i;
    if (TEX_ALIGNED(p))
        for (; i + 4 <= w; i += 4, p += 4)
        {
            v = LOAD_TEX32(p);
            if (!c->tex_swap)
                R2_SWAPI(v);
            *dst++ = v;
        }
    else
        for (; i + 4 <= w; i += 4, p += 4)
            *dst++ = Out(c, (ULONG) p[0] | ((ULONG) p[1] << 8) | ((ULONG) p[2] << 16)
                            | ((ULONG) p[3] << 24));
    if (i < w)
    {
        acc = 0;
        mask = 0;
        for (k = 0; i < w; i++, k++)
        {
            acc |= (ULONG) row[i] << (k * 8);
            mask |= 0xffUL << (k * 8);
        }
        *dst = Out(c, (In(c, *dst) & ~mask) | acc);
    }
}

/* Write w texels of one row: level l, row y, from column x. */
static void WriteRow(struct Core *c, struct CoreTex *t, ULONG l, ULONG x, ULONG y,
                     ULONG w, ULONG src, ULONG bpp, const UBYTE *row)
{
    ULONG cpp;
    ULONG per;          /* texels a dword */
    ULONG byte0;
    ULONG first;        /* first texel position within its dword */
    ULONG *dst;
    ULONG acc;
    ULONG mask;
    ULONG unit;
    ULONG i;
    ULONG slot;
    ULONG texel;
    ULONG r, g, b, a;

    cpp = t->cpp;
    unit = (cpp == 4) ? 0xffffffffUL : ((1UL << (cpp * 8)) - 1);
    per = 4 / cpp;
    byte0 = t->lofs[l] + y * t->pitch[l] + x * cpp;
    dst = c->heap.cpu + (t->gpu - c->heap.gpu + byte0) / 4;
    first = (byte0 & 3) / cpp;
    if (cpp == 2 && bpp == 2
        && ((src == SRC_4444 && t->base == GL_RGBA) || (src == SRC_565 && t->base == GL_RGB)))
    {
        WriteRow16(c, dst, first, w, row);
        return;
    }
    if (cpp == 1 && bpp == 1 && src == SRC_L && (t->base == GL_LUMINANCE || t->base == GL_INTENSITY))
    {
        WriteRow8(c, dst, first, w, row);
        return;
    }
    if (cpp == 4 && bpp == 4 && (src == SRC_RGBA || src == SRC_ARGB32))
    {
        /* true colour: one texel a dword (W2). 2026-11-14 (Herald's card
         * uploads, ~46 ms for 172 KB against ~22 ms of bus on the A4000D):
         * the texel from one longword load -- a rotate for RGBA bytes,
         * nothing for native ARGB words -- instead of four byte loads,
         * shifts and ors */
        r = (t->base == GL_RGB) ? 0xff000000UL : 0;     /* RGB8: alpha 1 */
        if (src == SRC_ARGB32)
        {
            for (i = 0; i < w; i++)
            {
                *dst++ = Out(c, NATIVE32(row) | r);
                row += 4;
            }
            return;
        }
        if (TEX_ALIGNED(row))
        {
            for (i = 0; i < w; i++)
            {
                texel = LOAD_TEX32(row);                /* R G B A */
                *dst++ = Out(c, ((texel >> 8) | (texel << 24)) | r);
                row += 4;
            }
            return;
        }
        for (i = 0; i < w; i++)
        {
            texel = ((ULONG) row[3] << 24) | ((ULONG) row[0] << 16) | ((ULONG) row[1] << 8) | row[2];
            *dst++ = Out(c, texel | r);
            row += 4;
        }
        return;
    }
    acc = 0;
    mask = 0;
    slot = first;
    for (i = 0; i < w; i++)
    {
        if (!Direct(src, t->base, cpp, row + i * bpp, &texel))
        {
            Fetch(src, row + i * bpp, &r, &g, &b, &a);
            texel = Encode(t->base, cpp, r, g, b, a);
        }
        acc |= texel << (slot * cpp * 8);
        mask |= unit << (slot * cpp * 8);
        slot++;
        if (slot == per)
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
        acc |= In(c, *dst) & ~mask;             /* a partial last dword */
        *dst = Out(c, acc);
    }
}

static void Upload(struct Core *c, struct CoreTex *t, ULONG l, ULONG x, ULONG y,
                   ULONG w, ULONG h, ULONG src, ULONG bpp, const UBYTE *pixels)
{
    ULONG row;
    ULONG stride;
    ULONG *p;

    /* GL_UNPACK_ALIGNMENT 4, GL's default (glPixelStorei is not in
     * GLQuake's slots; the rest of the pixel store comes with it) */
    stride = (w * bpp + 3) & ~3UL;
    /* the GPU may still read the old texels: wait (a staging buffer the
     * GPU copies from in ring order is the fast-path answer, concept 6.6) */
    Core_TexHazard(c, t);
    /* where the heap is now (a back end's video RAM may have moved) */
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
    for (row = 0; row < h; row++)
        WriteRow(c, t, l, x, y + row, w, src, bpp, pixels + row * stride);
    if (c->vram_end != NULL)
        c->vram_end(c->env.env);
}

static ULONG Log2(ULONG v)
{
    ULONG n;

    n = 0;
    while ((1UL << n) < v)
        n++;
    return n;
}

/* Storage for level 0 of w x h in base: allocate the chain. */
static ULONG Allocate(struct Core *c, struct CoreTex *t, ULONG w, ULONG h, ULONG base, ULONG deep)
{
    ULONG hwfmt;
    ULONG cpp;
    ULONG l;
    ULONG lw;
    ULONG lh;
    ULONG total;
    ULONG off;
    ULONG pot;

    HwOf(base, deep, &hwfmt, &cpp);
    pot = (w & (w - 1)) == 0 && (h & (h - 1)) == 0;
    if (t->gpu != 0 && t->w == w && t->h == h && t->hwfmt == hwfmt)
    {
        t->base = base;
        return GL_NO_ERROR;
    }
    Release(c, t);
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
            break;          /* NPOT: one level */
    }
    off = Heap_Alloc(c, total, 32);
    if (off == 0xffffffffUL)
        return GL_OUT_OF_MEMORY;
    t->gpu = c->heap.gpu + off;
    t->bytes = total;
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

void Core_TexImage2D(struct Core *c, ULONG target, LONG level, LONG internal,
                     LONG w, LONG h, LONG border, ULONG format, ULONG type,
                     const void *pixels)
{
    struct CoreTex *t;
    ULONG base;
    ULONG src;
    ULONG bpp;
    ULONG err;

    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    if (target != GL_TEXTURE_2D)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    base = BaseOf(internal);
    src = SrcOf(format, type, &bpp);
    if (base == 0 || src == 0)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    if (level < 0 || level >= CORE_MAX_LEVELS || border != 0 || w < 1 || h < 1
        || w > CORE_MAX_TEXSIZE || h > CORE_MAX_TEXSIZE)
    {
        Core_Error(c, GL_INVALID_VALUE);
        return;
    }
    t = c->tex[c->bound[c->active_unit]];
    if (t == NULL)
        return;
    if (level == 0)
    {
        err = Allocate(c, t, (ULONG) w, (ULONG) h, base, DeepOf(internal));
        if (err != GL_NO_ERROR)
        {
            Core_Error(c, err);
            Touched(c, t);
            return;
        }
    }
    else
    {
        /* a level of the chain level 0 set up */
        if (t->gpu == 0 || !t->pot
            || (ULONG) w != ((t->w >> level) ? (t->w >> level) : 1)
            || (ULONG) h != ((t->h >> level) ? (t->h >> level) : 1)
            || BaseOf(internal) != t->base || (DeepOf(internal) ? 4UL : t->cpp) != t->cpp)
        {
            Core_Error(c, GL_INVALID_VALUE);
            return;
        }
    }
    if (pixels != NULL)     /* NULL: storage only (MiniGLV3D read address 0 here) */
        Upload(c, t, (ULONG) level, 0, 0, (ULONG) w, (ULONG) h, src, bpp,
               (const UBYTE *) pixels);
    t->levels |= 1UL << level;
    Touched(c, t);
}

void Core_TexSubImage2D(struct Core *c, ULONG target, LONG level, LONG x, LONG y,
                        LONG w, LONG h, ULONG format, ULONG type, const void *pixels)
{
    struct CoreTex *t;
    ULONG src;
    ULONG bpp;
    ULONG lw;
    ULONG lh;

    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    if (target != GL_TEXTURE_2D)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    src = SrcOf(format, type, &bpp);
    if (src == 0)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    t = c->tex[c->bound[c->active_unit]];
    if (t == NULL || t->gpu == 0 || level < 0 || level >= CORE_MAX_LEVELS
        || !(t->levels & (1UL << level)))
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
    Upload(c, t, (ULONG) level, (ULONG) x, (ULONG) y, (ULONG) w, (ULONG) h, src, bpp,
           (const UBYTE *) pixels);
}
