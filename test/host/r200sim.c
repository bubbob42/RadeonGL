/*
 * r200sim.c -- see r200sim.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "r200sim.h"
#include "r2pm4.h"
#include "r200reg.h"

#define REG(s, r)  ((s)->reg[(r) >> 2])

static ULONG Swap32(ULONG v)
{
    return (v >> 24) | ((v >> 8) & 0xff00UL) | ((v << 8) & 0xff0000UL) | (v << 24);
}

static float FBits(ULONG b)
{
    float f;

    memcpy(&f, &b, 4);
    return f;
}

void Sim_Init(struct Sim *s, ULONG *vram, ULONG vram_gpu, ULONG vram_size,
              ULONG swap, ULONG tex_swap)
{
    memset(s, 0, sizeof(*s));
    s->vram = vram;
    s->vram_gpu = vram_gpu;
    s->vram_size = vram_size;
    s->swap = swap;
    s->tex_swap = tex_swap;
}

/* --- VRAM as the GPU sees it: little-endian bytes ----------------------------- */

static ULONG *Dw(const struct Sim *s, ULONG a)
{
    if (a < s->vram_gpu || a - s->vram_gpu + 4 > s->vram_size)
        return NULL;
    return s->vram + (a - s->vram_gpu) / 4;
}

static ULONG Rd32(const struct Sim *s, ULONG a)
{
    ULONG *p = Dw(s, a & ~3UL);

    if (p == NULL)
        return 0;
    return s->tex_swap ? Swap32(*p) : *p;
}

/* Vertex and index data: in the stream's byte order (the core packs them
 * as it writes the ring), which need not be the texels'. */
static ULONG RdStream(const struct Sim *s, ULONG a)
{
    ULONG *p = Dw(s, a & ~3UL);

    if (p == NULL)
        return 0;
    return s->swap ? Swap32(*p) : *p;
}

static void Wr32(struct Sim *s, ULONG a, ULONG g)
{
    ULONG *p = Dw(s, a & ~3UL);

    if (p != NULL)
        *p = s->tex_swap ? Swap32(g) : g;
}

static ULONG Rd(const struct Sim *s, ULONG a, ULONG bytes)
{
    ULONG g = Rd32(s, a);
    ULONG sh = (a & 3) * 8;

    if (bytes == 4)
        return g;
    return (g >> sh) & (bytes == 2 ? 0xffffUL : 0xffUL);
}

static void Wr(struct Sim *s, ULONG a, ULONG bytes, ULONG v)
{
    ULONG g;
    ULONG sh;
    ULONG m;

    if (bytes == 4)
    {
        Wr32(s, a, v);
        return;
    }
    g = Rd32(s, a);
    sh = (a & 3) * 8;
    m = (bytes == 2 ? 0xffffUL : 0xffUL) << sh;
    Wr32(s, a, (g & ~m) | ((v << sh) & m));
}

/* --- Vertices --------------------------------------------------------------------- */

struct V
{
    float x, y, z, w;           /* clip (TCL) or window (no TCL) */
    float col[4];
    float st[6][2];
    int ok;
};

/* Unpack one vertex of the current format from dwords d (logical values). */
static void Unpack(struct Sim *s, const ULONG *d, struct V *v)
{
    ULONG f0 = REG(s, R_SE_VTX_FMT_0);
    ULONG f1 = REG(s, R_SE_VTX_FMT_1);
    ULONG k = 0;
    ULONG c;
    ULONG u;
    ULONG n;

    memset(v, 0, sizeof(*v));
    v->col[0] = v->col[1] = v->col[2] = v->col[3] = 1.0f;
    v->x = FBits(d[k++]);
    v->y = FBits(d[k++]);
    v->z = (f0 & R_VTX_Z0) ? FBits(d[k++]) : 0.0f;
    v->w = (f0 & R_VTX_W0) ? FBits(d[k++]) : 1.0f;
    if (f0 & (R_VTX_PV_MATRIX_SEL | (7UL << R_VTX_WEIGHT_COUNT_SHIFT) | R_VTX_POINT_SIZE
              | R_VTX_DISCRETE_FOG | R_VTX_SHININESS_0 | R_VTX_SHININESS_1 | R_VTX_XY1
              | R_VTX_Z1 | R_VTX_W1 | R_VTX_N1))
        s->unsupported++;
    if (f0 & R_VTX_N0)
        k += 3;
    c = (f0 >> R_VTX_COLOR_0_SHIFT) & 3;
    if (c == R_VTX_PK_RGBA)
    {
        /* a << 24 | b << 16 | g << 8 | r, VF_COLOR_ORDER_RGBA */
        v->col[0] = (float) (d[k] & 0xff) / 255.0f;
        v->col[1] = (float) ((d[k] >> 8) & 0xff) / 255.0f;
        v->col[2] = (float) ((d[k] >> 16) & 0xff) / 255.0f;
        v->col[3] = (float) ((d[k] >> 24) & 0xff) / 255.0f;
        k++;
    }
    else if (c == R_VTX_FP_RGB || c == R_VTX_FP_RGBA)
    {
        v->col[0] = FBits(d[k++]);
        v->col[1] = FBits(d[k++]);
        v->col[2] = FBits(d[k++]);
        if (c == R_VTX_FP_RGBA)
            v->col[3] = FBits(d[k++]);
    }
    for (u = 0; u < 6; u++)
    {
        n = (f1 >> (3 * u)) & 7;
        if (n > 4)
            n = 0;
        if (n >= 1) v->st[u][0] = FBits(d[k]);
        if (n >= 2) v->st[u][1] = FBits(d[k + 1]);
        k += n;
    }
    v->ok = 1;
}

/* Clip space -> window space (or window space as given without TCL). */
static void ToWindow(struct Sim *s, struct V *v)
{
    ULONG vte = REG(s, R_SE_VTE_CNTL);
    float iw;

    if (REG(s, R_SE_VAP_CNTL) & R_VAP_TCL_ENABLE)
    {
        iw = 1.0f / v->w;
        v->x *= iw;
        v->y *= iw;
        v->z *= iw;
        v->w = iw;              /* keep 1/w for perspective-correct texturing */
    }
    else
        v->w = 1.0f;
    if (vte & R_VPORT_X_SCALE_ENA)  v->x *= FBits(REG(s, R_SE_VPORT_XSCALE));
    if (vte & R_VPORT_X_OFFSET_ENA) v->x += FBits(REG(s, R_SE_VPORT_XOFFSET));
    if (vte & R_VPORT_Y_SCALE_ENA)  v->y *= FBits(REG(s, R_SE_VPORT_YSCALE));
    if (vte & R_VPORT_Y_OFFSET_ENA) v->y += FBits(REG(s, R_SE_VPORT_YOFFSET));
    if (vte & R_VPORT_Z_SCALE_ENA)  v->z *= FBits(REG(s, R_SE_VPORT_ZSCALE));
    if (vte & R_VPORT_Z_OFFSET_ENA) v->z += FBits(REG(s, R_SE_VPORT_ZOFFSET));
}

static void Transform(struct Sim *s, struct V *v)
{
    float x = v->x, y = v->y, z = v->z, w = v->w;
    const float *m = &s->vec[R_VS_MATRIX_2_MVP * 4];   /* four rows */

    if (!(REG(s, R_SE_VAP_CNTL) & R_VAP_TCL_ENABLE))
        return;
    v->x = m[0] * x + m[1] * y + m[2] * z + m[3] * w;
    v->y = m[4] * x + m[5] * y + m[6] * z + m[7] * w;
    v->z = m[8] * x + m[9] * y + m[10] * z + m[11] * w;
    v->w = m[12] * x + m[13] * y + m[14] * z + m[15] * w;
}

/* --- Pixels ------------------------------------------------------------------------- */

static int Cmp(ULONG func, ULONG a, ULONG b)
{
    switch (func)       /* R200 order */
    {
        case 0: return 0;
        case 1: return a < b;
        case 2: return a <= b;
        case 3: return a == b;
        case 4: return a >= b;
        case 5: return a > b;
        case 6: return a != b;
    }
    return 1;
}

static void Texel(struct Sim *s, ULONG u, float sc, float tc, float *out)
{
    ULONG base = R_PP_TXFILTER_0 + u * R_PP_TXUNIT_STRIDE;
    ULONG filt = REG(s, base);
    ULONG fmt = REG(s, base + 4);
    ULONG gpu = REG(s, R_PP_TXOFFSET_0 + u * R_PP_TXOFFSET_STRIDE) & R_TXO_OFFSET_MASK;
    ULONG w, h, pitch, cpp, x, y, t;
    LONG xi, yi;

    switch (fmt & R_TXFORMAT_FORMAT_MASK)
    {
        case 0: cpp = 1; break;
        case 1: case 4: case 5: cpp = 2; break;
        case 6: cpp = 4; break;
        default: cpp = 0; break;
    }
    if (cpp == 0)
    {
        s->unsupported++;
        out[0] = out[1] = out[2] = out[3] = 1.0f;
        return;
    }
    if (fmt & R_TXFORMAT_NON_POWER2)
    {
        w = (REG(s, base + 0xc) & 0x7ff) + 1;
        h = ((REG(s, base + 0xc) >> 16) & 0x7ff) + 1;
        pitch = REG(s, base + 0x10) + 32;
    }
    else
    {
        w = 1UL << ((fmt >> R_TXFORMAT_WIDTH_SHIFT) & 15);
        h = 1UL << ((fmt >> R_TXFORMAT_HEIGHT_SHIFT) & 15);
        pitch = (w * cpp + 31) & ~31UL;
    }
    xi = (LONG) (sc * (float) w + (sc < 0 ? -1.0f : 0.0f));
    yi = (LONG) (tc * (float) h + (tc < 0 ? -1.0f : 0.0f));
    if (((filt >> 23) & 7) == 0 && !(fmt & R_TXFORMAT_NON_POWER2))
        xi &= (LONG) w - 1;                         /* wrap */
    if (((filt >> 27) & 7) == 0 && !(fmt & R_TXFORMAT_NON_POWER2))
        yi &= (LONG) h - 1;
    x = xi < 0 ? 0 : (xi >= (LONG) w ? w - 1 : (ULONG) xi);
    y = yi < 0 ? 0 : (yi >= (LONG) h ? h - 1 : (ULONG) yi);
    t = Rd(s, gpu + y * pitch + x * cpp, cpp);
    switch (fmt & R_TXFORMAT_FORMAT_MASK)
    {
        case 4:     /* RGB565 */
            out[0] = (float) ((t >> 11) & 31) / 31.0f;
            out[1] = (float) ((t >> 5) & 63) / 63.0f;
            out[2] = (float) (t & 31) / 31.0f;
            out[3] = 1.0f;
            break;
        case 5:     /* ARGB4444 */
            out[3] = (float) ((t >> 12) & 15) / 15.0f;
            out[0] = (float) ((t >> 8) & 15) / 15.0f;
            out[1] = (float) ((t >> 4) & 15) / 15.0f;
            out[2] = (float) (t & 15) / 15.0f;
            break;
        case 6:     /* ARGB8888 */
            out[3] = (float) ((t >> 24) & 255) / 255.0f;
            out[0] = (float) ((t >> 16) & 255) / 255.0f;
            out[1] = (float) ((t >> 8) & 255) / 255.0f;
            out[2] = (float) (t & 255) / 255.0f;
            break;
        case 1:     /* AI88 */
            out[0] = out[1] = out[2] = (float) (t & 255) / 255.0f;
            out[3] = (float) ((t >> 8) & 255) / 255.0f;
            break;
        default:    /* I8 */
            out[0] = out[1] = out[2] = (float) t / 255.0f;
            out[3] = (fmt & R_TXFORMAT_ALPHA_IN_MAP) ? out[0] : 1.0f;
            break;
    }
}

/* A combiner argument: code 0 zero, 4/5 diffuse, 10 + 2k / 11 + 2k register k. */
static float Arg(ULONG code, const float *diff, float r[6][4], int ch, int alpha)
{
    if (code == 0)
        return 0.0f;
    if (code == 4 || code == 5)
        return (alpha || code == 5) ? diff[3] : diff[ch];
    if (code >= 10 && code <= 21)
    {
        ULONG k = (code - 10) / 2;
        return (alpha || (code & 1)) ? r[k][3] : r[k][ch];
    }
    return 0.0f;
}

static float Comp(float v, ULONG bits, int which)
{
    /* COMP_ARG_A/B/C at bits 16, 20, 24: 1 - x */
    return (bits & (1UL << (16 + 4 * which))) ? 1.0f - v : v;
}

static void Combine(struct Sim *s, const float *diff, float r[6][4], float *out)
{
    ULONG pp = REG(s, R_PP_CNTL);
    ULONG st;
    ULONG cb, cb2, ab, ab2;
    float a, b, c, res[4];
    int ch;
    ULONG reg;

    for (st = 0; st < 8; st++)
    {
        if (!(pp & (R_TEX_BLEND_0_ENABLE << st)))
            continue;
        cb = REG(s, R_PP_TXCBLEND_0 + st * R_PP_TXBLEND_STRIDE);
        cb2 = REG(s, R_PP_TXCBLEND2_0 + st * R_PP_TXBLEND_STRIDE);
        ab = REG(s, R_PP_TXABLEND_0 + st * R_PP_TXBLEND_STRIDE);
        ab2 = REG(s, R_PP_TXABLEND2_0 + st * R_PP_TXBLEND_STRIDE);
        if ((cb >> 28) & 7 || (ab >> 28) & 7)
            s->unsupported++;           /* only MADD */
        for (ch = 0; ch < 3; ch++)
        {
            a = Comp(Arg(cb & 31, diff, r, ch, 0), cb, 0);
            b = Comp(Arg((cb >> 5) & 31, diff, r, ch, 0), cb, 1);
            c = Comp(Arg((cb >> 10) & 31, diff, r, ch, 0), cb, 2);
            res[ch] = a * b + c;
            if (cb2 & R_TXC_CLAMP_0_1)
                res[ch] = res[ch] < 0 ? 0 : (res[ch] > 1 ? 1 : res[ch]);
        }
        a = Comp(Arg(ab & 31, diff, r, 3, 1), ab, 0);
        b = Comp(Arg((ab >> 5) & 31, diff, r, 3, 1), ab, 1);
        c = Comp(Arg((ab >> 10) & 31, diff, r, 3, 1), ab, 2);
        res[3] = a * b + c;
        if (ab2 & R_TXC_CLAMP_0_1)
            res[3] = res[3] < 0 ? 0 : (res[3] > 1 ? 1 : res[3]);
        reg = (cb2 >> 16) & 7;
        if (reg >= 1)
            for (ch = 0; ch < 3; ch++)
                r[reg - 1][ch] = res[ch];
        reg = (ab2 >> 16) & 7;
        if (reg >= 1)
            r[reg - 1][3] = res[3];
    }
    memcpy(out, r[0], 4 * sizeof(float));
}

static float Factor(ULONG f, const float *src, const float *dst, int ch)
{
    switch (f)
    {
        case 32: return 0.0f;
        case 33: return 1.0f;
        case 34: return src[ch];
        case 35: return 1.0f - src[ch];
        case 36: return dst[ch];
        case 37: return 1.0f - dst[ch];
        case 38: return src[3];
        case 39: return 1.0f - src[3];
        case 40: return dst[3];
        case 41: return 1.0f - dst[3];
    }
    return 1.0f;
}

static void Frag(struct Sim *s, LONG x, LONG y, float z, const float *col, float st[6][2])
{
    ULONG rb = REG(s, R_RB3D_CNTL);
    ULONG zs = REG(s, R_RB3D_ZSTENCILCNTL);
    ULONG pp = REG(s, R_PP_CNTL);
    ULONG fmt = (rb >> R_COLOR_FORMAT_SHIFT) & 15;
    ULONG cgpu = REG(s, R_RB3D_COLOROFFSET);
    ULONG cpitch = REG(s, R_RB3D_COLORPITCH) & R_COLORPITCH_MASK;
    ULONG cpp = (fmt == 6) ? 4 : 2;
    ULONG za, zv, zold, d, out, mask;
    ULONG u, route;
    float r[6][4];
    float frag[4], dst[4], res[4];
    int ch;

    if (rb & R_Z_ENABLE)
    {
        za = REG(s, R_RB3D_DEPTHOFFSET) + ((ULONG) y * (REG(s, R_RB3D_DEPTHPITCH) & R_DEPTHPITCH_MASK)
                                           + (ULONG) x) * 2;
        zv = (ULONG) (z <= 0 ? 0 : (z >= 1 ? 65535 : z * 65535.0f + 0.5f));
        zold = Rd(s, za, 2);
        if (!Cmp((zs >> R_Z_TEST_SHIFT) & 7, zv, zold))
            return;
        if (zs & R_Z_WRITE_ENABLE)
            Wr(s, za, 2, zv);
    }
    memset(r, 0, sizeof(r));
    for (u = 0; u < 6; u++)
        if (pp & (R_TEX_0_ENABLE << u))
        {
            route = (REG(s, R_PP_TXFORMAT_0 + u * R_PP_TXUNIT_STRIDE) >> R_TXFORMAT_ST_ROUTE_SHIFT) & 7;
            Texel(s, u, st[route][0], st[route][1], r[u]);
        }
    Combine(s, col, r, frag);
    if (pp & R_ALPHA_TEST_ENABLE)
    {
        ULONG misc = REG(s, R_PP_MISC);
        ULONG a = (ULONG) (frag[3] * 255.0f + 0.5f);
        if (!Cmp((misc >> R_ALPHA_TEST_SHIFT) & 7, a, misc & 255))
            return;
    }
    d = Rd(s, cgpu + ((ULONG) y * cpitch + (ULONG) x) * cpp, cpp);
    if (fmt == 6)
    {
        dst[0] = (float) ((d >> 16) & 255) / 255.0f;
        dst[1] = (float) ((d >> 8) & 255) / 255.0f;
        dst[2] = (float) (d & 255) / 255.0f;
        dst[3] = (float) ((d >> 24) & 255) / 255.0f;
    }
    else
    {
        dst[0] = (float) ((d >> 11) & 31) / 31.0f;
        dst[1] = (float) ((d >> 5) & 63) / 63.0f;
        dst[2] = (float) (d & 31) / 31.0f;
        dst[3] = 1.0f;
    }
    memcpy(res, frag, sizeof(res));
    if (rb & R_ALPHA_BLEND_ENABLE)
    {
        ULONG bl = REG(s, R_RB3D_BLENDCNTL);
        ULONG sf = (bl >> R_SRC_BLEND_SHIFT) & 63, df = (bl >> R_DST_BLEND_SHIFT) & 63;
        for (ch = 0; ch < 4; ch++)
        {
            res[ch] = frag[ch] * Factor(sf, frag, dst, ch) + dst[ch] * Factor(df, frag, dst, ch);
            res[ch] = res[ch] > 1 ? 1 : (res[ch] < 0 ? 0 : res[ch]);
        }
    }
    if (fmt == 6)
        out = ((ULONG) (res[3] * 255 + 0.5f) << 24) | ((ULONG) (res[0] * 255 + 0.5f) << 16)
            | ((ULONG) (res[1] * 255 + 0.5f) << 8) | (ULONG) (res[2] * 255 + 0.5f);
    else
        out = ((ULONG) (res[0] * 31 + 0.5f) << 11) | ((ULONG) (res[1] * 63 + 0.5f) << 5)
            | (ULONG) (res[2] * 31 + 0.5f);
    mask = (rb & R_PLANE_MASK_ENABLE) ? REG(s, R_RB3D_PLANEMASK) : 0xffffffffUL;
    if (fmt != 6)
        mask &= 0xffff;
    out = (out & mask) | (d & ~mask);
    Wr(s, cgpu + ((ULONG) y * cpitch + (ULONG) x) * cpp, cpp, out);
    s->pixels++;
}

static float Edge(const struct V *a, const struct V *b, float x, float y)
{
    return (b->x - a->x) * (y - a->y) - (b->y - a->y) * (x - a->x);
}

/* A window-space triangle. */
static void Raster(struct Sim *s, const struct V *v0, const struct V *v1, const struct V *v2,
                   const struct V *provoke)
{
    ULONG tl = REG(s, R_RE_TOP_LEFT), br = REG(s, R_RE_WIDTH_HEIGHT);
    LONG sx0 = (LONG) (tl & 0xffff), sy0 = (LONG) (tl >> 16);
    LONG sx1 = (LONG) (br & 0xffff), sy1 = (LONG) (br >> 16);
    float area, minx, maxx, miny, maxy, px, py, w0, w1, w2, iw, z;
    float col[4], st[6][2];
    LONG x, y, x0, x1, y0, y1;
    int ch, u;
    int flat = ((REG(s, R_SE_CNTL) >> 8) & 3) == 1;

    area = Edge(v0, v1, v2->x, v2->y);
    if (area == 0)
        return;
    minx = v0->x < v1->x ? v0->x : v1->x; if (v2->x < minx) minx = v2->x;
    maxx = v0->x > v1->x ? v0->x : v1->x; if (v2->x > maxx) maxx = v2->x;
    miny = v0->y < v1->y ? v0->y : v1->y; if (v2->y < miny) miny = v2->y;
    maxy = v0->y > v1->y ? v0->y : v1->y; if (v2->y > maxy) maxy = v2->y;
    x0 = (LONG) minx; if (x0 < sx0) x0 = sx0;
    y0 = (LONG) miny; if (y0 < sy0) y0 = sy0;
    x1 = (LONG) maxx + 1; if (x1 > sx1) x1 = sx1;
    y1 = (LONG) maxy + 1; if (y1 > sy1) y1 = sy1;
    s->tris++;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
        {
            px = (float) x + 0.5f;
            py = (float) y + 0.5f;
            w0 = Edge(v1, v2, px, py) / area;
            w1 = Edge(v2, v0, px, py) / area;
            w2 = Edge(v0, v1, px, py) / area;
            if (w0 < 0 || w1 < 0 || w2 < 0)
                continue;
            z = w0 * v0->z + w1 * v1->z + w2 * v2->z;
            iw = w0 * v0->w + w1 * v1->w + w2 * v2->w;
            for (ch = 0; ch < 4; ch++)
                col[ch] = flat ? provoke->col[ch]
                               : w0 * v0->col[ch] + w1 * v1->col[ch] + w2 * v2->col[ch];
            for (u = 0; u < 6; u++)
            {
                st[u][0] = (w0 * v0->st[u][0] * v0->w + w1 * v1->st[u][0] * v1->w
                            + w2 * v2->st[u][0] * v2->w) / iw;
                st[u][1] = (w0 * v0->st[u][1] * v0->w + w1 * v1->st[u][1] * v1->w
                            + w2 * v2->st[u][1] * v2->w) / iw;
            }
            Frag(s, x, y, z, col, st);
        }
}

/* Clip against the near plane (z >= -w) in clip space, then cull, then draw. */
static void Lerp(struct V *o, const struct V *a, const struct V *b, float t)
{
    int ch, u;

    o->x = a->x + (b->x - a->x) * t;
    o->y = a->y + (b->y - a->y) * t;
    o->z = a->z + (b->z - a->z) * t;
    o->w = a->w + (b->w - a->w) * t;
    for (ch = 0; ch < 4; ch++)
        o->col[ch] = a->col[ch] + (b->col[ch] - a->col[ch]) * t;
    for (u = 0; u < 6; u++)
    {
        o->st[u][0] = a->st[u][0] + (b->st[u][0] - a->st[u][0]) * t;
        o->st[u][1] = a->st[u][1] + (b->st[u][1] - a->st[u][1]) * t;
    }
    o->ok = 1;
}

static void Tri(struct Sim *s, struct V a, struct V b, struct V c)
{
    struct V in[3], out[6], w[6];
    int n = 0, i, j;
    float da, db, area;
    ULONG ucp = REG(s, R_SE_TCL_UCP_VERT_BLEND_CTL);
    ULONG se = REG(s, R_SE_CNTL);
    int tcl = (REG(s, R_SE_VAP_CNTL) & R_VAP_TCL_ENABLE) != 0;
    int front;
    struct V prov;

    prov = c;
    in[0] = a; in[1] = b; in[2] = c;
    if (tcl)
    {
        for (i = 0; i < 3; i++)
        {
            j = (i + 1) % 3;
            da = in[i].z + in[i].w;
            db = in[j].z + in[j].w;
            if (da >= 0)
                out[n++] = in[i];
            if ((da >= 0) != (db >= 0))
                Lerp(&out[n++], &in[i], &in[j], da / (da - db));
        }
        if (n < 3)
            return;
        /* TCL culling in clip space, y up: GL's own sense (UCP_VERT_BLEND_CTL) */
        area = (out[1].x / out[1].w - out[0].x / out[0].w) * (out[2].y / out[2].w - out[0].y / out[0].w)
             - (out[1].y / out[1].w - out[0].y / out[0].w) * (out[2].x / out[2].w - out[0].x / out[0].w);
        front = ((area > 0) == ((ucp & R_CULL_FRONT_IS_CCW) != 0));
        if ((front && (ucp & R_CULL_FRONT)) || (!front && (ucp & R_CULL_BACK)))
        {
            s->culled++;
            return;
        }
    }
    else
    {
        out[0] = a; out[1] = b; out[2] = c;
        n = 3;
        if (!(se & R_BFACE_SOLID) || !(se & R_FFACE_SOLID))
            s->unsupported++;   /* window-space culling: not needed by the core */
    }
    for (i = 0; i < n; i++)
    {
        w[i] = out[i];
        ToWindow(s, &w[i]);
    }
    ToWindow(s, &prov);
    for (i = 1; i + 1 < n; i++)
        Raster(s, &w[0], &w[i], &w[i + 1], &prov);
}

/* --- Draws ------------------------------------------------------------------------- */

/* A point: a square of RE_POINTSIZE / 16 pixels around the vertex,
 * with the vertex's colour and texture coordinates throughout. */
static void Point(struct Sim *s, struct V p)
{
    ULONG tl = REG(s, R_RE_TOP_LEFT), br = REG(s, R_RE_WIDTH_HEIGHT);
    float size = (float) (REG(s, R_RE_POINTSIZE) & 0xffff) / 16.0f;
    float half;
    LONG x, y, x0, y0, x1, y1;

    if (REG(s, R_SE_VAP_CNTL) & R_VAP_TCL_ENABLE)
    {
        if (p.w <= 0 || p.z < -p.w)
            return;
    }
    ToWindow(s, &p);
    if (size < 1.0f)
        size = 1.0f;
    half = size * 0.5f;
    x0 = (LONG) (p.x - half + 0.5f);
    y0 = (LONG) (p.y - half + 0.5f);
    x1 = x0 + (LONG) (size + 0.5f) - 1;
    y1 = y0 + (LONG) (size + 0.5f) - 1;
    if (x0 < (LONG) (tl & 0xffff)) x0 = (LONG) (tl & 0xffff);
    if (y0 < (LONG) (tl >> 16)) y0 = (LONG) (tl >> 16);
    if (x1 > (LONG) (br & 0xffff)) x1 = (LONG) (br & 0xffff);
    if (y1 > (LONG) (br >> 16)) y1 = (LONG) (br >> 16);
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            Frag(s, x, y, p.z, p.col, p.st);
}

/* SE_TCL_OUTPUT_VTX_COMP_SEL picks the outputs TCL COMPUTES: the lit colour
 * (OUTPUT_COLOR_0) and transformed texture coordinates (OUTPUT_TEX_n). The
 * sim passes the vertex's colour and coordinates through, which is what the
 * R200 does only when those bits are clear (Mesa r200UpdateSpecular,
 * update_texturematrix). MEASURED on the A4000D (Tri, 2026-11-02): with
 * OUTPUT_COLOR_0 set and lighting off, the colours came out of uninitialised
 * material state. So a request for a computed output is not modelled. */
static void TclOutputs(struct Sim *s)
{
    ULONG compsel;

    if (!(REG(s, R_SE_VAP_CNTL) & R_VAP_TCL_ENABLE))
        return;
    compsel = REG(s, R_SE_TCL_OUTPUT_VTX_COMP_SEL);
    if (((compsel & R_OUTPUT_COLOR_0) && !(REG(s, R_SE_TCL_LIGHT_MODEL_CTL_0) & R_LIGHTING_ENABLE))
        || (compsel & R_OUTPUT_TEX_MASK))
        s->unsupported++;
}

static void DrawPrim(struct Sim *s, ULONG prim, struct V *v, ULONG n)
{
    ULONG i;
    struct V d;

    TclOutputs(s);
    switch (prim)
    {
        case R_VF_PRIM_TRIANGLES:
            for (i = 0; i + 2 < n; i += 3) Tri(s, v[i], v[i + 1], v[i + 2]);
            break;
        case R_VF_PRIM_TRIANGLE_STRIP:
            for (i = 0; i + 2 < n; i++)
            {
                if (i & 1) Tri(s, v[i + 1], v[i], v[i + 2]);
                else Tri(s, v[i], v[i + 1], v[i + 2]);
            }
            break;
        case R_VF_PRIM_TRIANGLE_FAN:
        case R_VF_PRIM_POLYGON:
            for (i = 1; i + 1 < n; i++) Tri(s, v[0], v[i], v[i + 1]);
            break;
        case R_VF_PRIM_QUADS:
            for (i = 0; i + 3 < n; i += 4)
            {
                Tri(s, v[i], v[i + 1], v[i + 2]);
                Tri(s, v[i], v[i + 2], v[i + 3]);
            }
            break;
        case R_VF_PRIM_QUAD_STRIP:
            for (i = 0; i + 3 < n; i += 2)
            {
                Tri(s, v[i], v[i + 1], v[i + 3]);
                Tri(s, v[i], v[i + 3], v[i + 2]);
            }
            break;
        case R_VF_PRIM_RECT_LIST:
            for (i = 0; i + 2 < n; i += 3)
            {
                /* the fourth corner: v0 + v2 - v1 */
                Lerp(&d, &v[i + 1], &v[i + 1], 0);
                d.x = v[i].x + v[i + 2].x - v[i + 1].x;
                d.y = v[i].y + v[i + 2].y - v[i + 1].y;
                d.z = v[i].z;
                d.st[0][0] = v[i].st[0][0] + v[i + 2].st[0][0] - v[i + 1].st[0][0];
                d.st[0][1] = v[i].st[0][1] + v[i + 2].st[0][1] - v[i + 1].st[0][1];
                memcpy(d.col, v[i].col, sizeof(d.col));
                Tri(s, v[i], v[i + 1], v[i + 2]);
                Tri(s, v[i], v[i + 2], d);
            }
            break;
        case R_VF_PRIM_POINTS:
            for (i = 0; i < n; i++)
                Point(s, v[i]);
            break;
        default:
            s->unsupported++;   /* lines: not drawn */
            break;
    }
}

static void Immd(struct Sim *s, const ULONG *p, ULONG count)
{
    ULONG cntl = p[0];
    ULONG n = cntl >> 16;
    ULONG vd;
    ULONG i;
    static struct V v[0x4000];

    if (n == 0)
        return;
    vd = (count - 1) / n;
    for (i = 0; i < n && i < 0x4000; i++)
    {
        Unpack(s, p + 1 + i * vd, &v[i]);
        Transform(s, &v[i]);
    }
    DrawPrim(s, cntl & R_VF_PRIM_MASK, v, n);
}

static void ArrayVertex(struct Sim *s, ULONG idx, struct V *v)
{
    ULONG d[32];
    ULONG k = 0;
    ULONG a, c;

    for (a = 0; a < s->aos_n; a++)
        for (c = 0; c < s->aos_comps[a] && k < 32; c++)
            d[k++] = RdStream(s, s->aos_gpu[a] + 4 * (s->aos_stride[a] * idx + c));
    Unpack(s, d, v);
    Transform(s, v);
}

static void Arrays(struct Sim *s, ULONG cntl, ULONG n, ULONG idx_gpu)
{
    static struct V v[0x10000];
    ULONG i;
    ULONG k;

    for (i = 0; i < n && i < 0x10000; i++)
    {
        k = i;
        if (idx_gpu != 0)
            k = (RdStream(s, idx_gpu + 2 * i) >> (((idx_gpu + 2 * i) & 2) * 8)) & 0xffffUL;
                                                /* first index in the low half */
        ArrayVertex(s, k, &v[i]);
    }
    DrawPrim(s, cntl & R_VF_PRIM_MASK, v, n);
}

void Sim_Run(struct Sim *s, const ULONG *dwraw, ULONG n)
{
    static ULONG buf[0x4002];
    ULONG i = 0, h, cnt, k, reg, val, op;

    while (i < n)
    {
        h = s->swap ? Swap32(dwraw[i]) : dwraw[i];
        if (R2_PKT_TYPE(h) == 2)
        {
            i++;
            continue;
        }
        cnt = R2_PKT_COUNT(h) + 1;
        if (i + 1 + cnt > n || cnt > 0x4001)
            return;
        for (k = 0; k < cnt; k++)
            buf[k] = s->swap ? Swap32(dwraw[i + 1 + k]) : dwraw[i + 1 + k];
        if (R2_PKT_TYPE(h) == 0)
        {
            for (k = 0; k < cnt; k++)
            {
                reg = R2_PKT0_ONE_REG_WR(h) ? R2_PKT0_REG(h) : R2_PKT0_REG(h) + 4 * k;
                val = buf[k];
                if (reg == R_SE_TCL_VECTOR_INDX_REG)
                    s->vec_addr = (val & 0xffff) * 4;
                else if (reg == R_SE_TCL_VECTOR_DATA_REG)
                {
                    if (s->vec_addr < 0x200 * 4)
                        s->vec[s->vec_addr] = FBits(val);
                    s->vec_addr++;
                }
                else if (reg == R_SE_TCL_SCALAR_INDX_REG)
                    s->scal_addr = val & 0xffff;
                else if (reg == R_SE_TCL_SCALAR_DATA_REG)
                {
                    if (s->scal_addr < 0x200)
                        s->scal[s->scal_addr] = FBits(val);
                    s->scal_addr++;
                }
                if (reg < 0x8000)
                    REG(s, reg) = val;
            }
        }
        else if (R2_PKT_TYPE(h) == 3)
        {
            op = R2_PKT3_OPCODE(h);
            if (op == R2_OP_3D_DRAW_IMMD_2)
                Immd(s, buf, cnt);
            else if (op == R2_OP_3D_LOAD_VBPNTR)
            {
                ULONG nr = buf[0], kk = 1, a;
                s->aos_n = nr > 12 ? 12 : nr;
                for (a = 0; a < s->aos_n; a += 2)
                {
                    s->aos_comps[a] = buf[kk] & 0xff;
                    s->aos_stride[a] = (buf[kk] >> 8) & 0x7f;
                    s->aos_gpu[a] = buf[kk + 1];
                    if (a + 1 < s->aos_n)
                    {
                        s->aos_comps[a + 1] = (buf[kk] >> 16) & 0xff;
                        s->aos_stride[a + 1] = (buf[kk] >> 24) & 0x7f;
                        s->aos_gpu[a + 1] = buf[kk + 2];
                        kk += 3;
                    }
                    else
                        kk += 2;
                }
            }
            else if (op == R2_OP_3D_DRAW_VBUF_2)
                Arrays(s, buf[0], buf[0] >> 16, 0);
            else if (op == R2_OP_3D_DRAW_INDX_2)
            {
                s->pend_cntl = buf[0];
                s->pend_nidx = buf[0] >> 16;
            }
            else if (op == R2_OP_INDX_BUFFER)
                Arrays(s, s->pend_cntl, s->pend_nidx, buf[1]);
        }
        i += 1 + cnt;
    }
}

ULONG Sim_Pixel(const struct Sim *s, ULONG gpu, ULONG pitch_px, ULONG format,
                ULONG x, ULONG y)
{
    ULONG d;

    if (format == R_COLOR_FORMAT_ARGB8888)
        return Rd(s, gpu + (y * pitch_px + x) * 4, 4) & 0xffffffUL;
    d = Rd(s, gpu + (y * pitch_px + x) * 2, 2);
    return ((((d >> 11) & 31) * 255 / 31) << 16) | ((((d >> 5) & 63) * 255 / 63) << 8)
         | ((d & 31) * 255 / 31);
}

int Sim_WritePPM(const struct Sim *s, const char *path, ULONG gpu, ULONG pitch_px,
                 ULONG format, ULONG w, ULONG h)
{
    FILE *f;
    ULONG x, y, p;

    f = fopen(path, "wb");
    if (f == NULL)
        return -1;
    fprintf(f, "P6\n%lu %lu\n255\n", (unsigned long) w, (unsigned long) h);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
        {
            p = Sim_Pixel(s, gpu, pitch_px, format, x, y);
            fputc((int) ((p >> 16) & 255), f);
            fputc((int) ((p >> 8) & 255), f);
            fputc((int) (p & 255), f);
        }
    fclose(f);
    return 0;
}

static ULONG BE(const unsigned char *p)
{
    return ((ULONG) p[0] << 24) | ((ULONG) p[1] << 16) | ((ULONG) p[2] << 8) | p[3];
}

int Sim_RenderDump(const char *dump, const char *ppm, struct Sim *s)
{
    FILE *f;
    long len;
    unsigned char *b;
    ULONG h[15];
    ULONG i;
    ULONG vsize;
    ULONG *vram;
    ULONG *stream;
    ULONG end;

    f = fopen(dump, "rb");
    if (f == NULL)
    {
        fprintf(stderr, "r200view: cannot open %s\n", dump);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = (unsigned char *) malloc((size_t) len);
    if (b == NULL || fread(b, 1, (size_t) len, f) != (size_t) len || len < 60)
    {
        fclose(f);
        free(b);
        fprintf(stderr, "r200view: cannot read %s\n", dump);
        return -1;
    }
    fclose(f);
    for (i = 0; i < 15; i++)
        h[i] = BE(b + 4 * i);
    if (h[0] != 0x5232444dUL || h[1] != 1
        || (unsigned long) len < 60 + 4UL * h[13] + h[12])
    {
        free(b);
        fprintf(stderr, "r200view: %s is no frame dump (version 1)\n", dump);
        return -1;
    }
    /* VRAM from GPU address 0 up to the highest buffer */
    end = h[11] + h[12];
    if (h[6] + h[7] * h[5] * 4 > end) end = h[6] + h[7] * h[5] * 4;
    if (h[9] + h[10] * h[5] * 2 > end) end = h[9] + h[10] * h[5] * 2;
    vsize = (end + 0xfffUL) & ~0xfffUL;
    vram = (ULONG *) calloc(1, vsize);
    stream = (ULONG *) malloc(4 * (size_t) h[13] + 4);
    if (vram == NULL || stream == NULL)
    {
        free(b);
        free(vram);
        free(stream);
        fprintf(stderr, "r200view: out of memory\n");
        return -1;
    }
    for (i = 0; i < h[13]; i++)
        stream[i] = BE(b + 60 + 4 * i);
    for (i = 0; i < h[12] / 4; i++)
        vram[(h[11] / 4) + i] = BE(b + 60 + 4 * h[13] + 4 * i);
    Sim_Init(s, vram, 0, vsize, h[2], h[3]);
    Sim_Run(s, stream, h[13]);
    i = (ULONG) Sim_WritePPM(s, ppm, h[6], h[7], h[8], h[4], h[5]);
    free(stream);
    free(vram);
    free(b);
    s->vram = NULL;
    return (int) (LONG) i;
}
