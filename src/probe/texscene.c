/*
 * texscene.c -- the textured scene of the Tri probe (texscene.h).
 *
 * Texel (i, j): column i (s), row j (t); row 0 is the first row of the
 * image, t = 0, the bottom of a quad in GL coordinates. A quad at GL
 * (x0, y0) shows texel (i, j) around GL (x0 + 32 i + 16, y0 + 32 j + 16),
 * memory row 255 minus that.
 */

#include "texscene.h"

#define Q 128.0f                /* quad size */

/* RGBA texel: multiples of 17 survive ARGB4444 exactly */
static ULONG TexA(ULONG i, ULONG j)
{
    ULONG r;
    ULONG g;
    ULONG b;

    r = ((i * 3 + j) % 16) * 17;
    g = ((i + j * 5) % 16) * 17;
    b = ((i * 7 + j * 3) % 16) * 17;
    return (r << 16) | (g << 8) | b;
}

/* RGB texel: 0 and 255 survive RGB565 exactly */
static ULONG TexB(ULONG i, ULONG j)
{
    ULONG r;
    ULONG g;
    ULONG b;

    r = (i & 1) ? 255UL : 0UL;
    g = (j & 1) ? 255UL : 0UL;
    b = ((i ^ j) & 2) ? 255UL : 0UL;
    return (r << 16) | (g << 8) | b;
}

/* LUMINANCE texel */
static ULONG TexC(ULONG i, ULONG j)
{
    return (i + 4 * j) * 17;
}

/* the 2 x 2 lightmap: a checker of 255 and 0 */
static ULONG Light(ULONG a, ULONG b)
{
    return ((a ^ b) & 1) ? 0UL : 255UL;
}

static void Upload(struct Core *c, ULONG name, LONG internal, ULONG format, ULONG w,
                   ULONG h, const UBYTE *texels)
{
    Core_BindTexture(c, GL_TEXTURE_2D, name);
    Core_TexImage2D(c, GL_TEXTURE_2D, 0, internal, (LONG)w, (LONG)h, 0, format,
                    GL_UNSIGNED_BYTE, texels);
    Core_TexParameteri(c, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    Core_TexParameteri(c, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

/* A quad at GL (x0, y0), texture coordinates 0..1 on the units in mask. */
static void Quad(struct Core *c, float x0, float y0, ULONG mask)
{
    static const float st[4][2] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
    ULONG k;
    float x;
    float y;

    Core_Begin(c, GL_QUADS);
    for (k = 0; k < 4; k++)
    {
        if (mask & 1)
            Core_MultiTexCoord2f(c, GL_TEXTURE0_ARB, st[k][0], st[k][1]);
        if (mask & 2)
            Core_MultiTexCoord2f(c, GL_TEXTURE1_ARB, st[k][0], st[k][1]);
        /* float locals: SAS/C evaluates a float expression as double (W88) */
        x = x0 + st[k][0] * Q;
        y = y0 + st[k][1] * Q;
        Core_Vertex3f(c, x, y, 0.0f);
    }
    Core_End(c);
}

void TexScene_Draw(struct Core *c)
{
    static UBYTE ta[4 * 4 * 4];
    static UBYTE tb[4 * 4 * 3];
    static UBYTE tc[4 * 4];
    static UBYTE lm[2 * 4];     /* rows padded to 4 bytes: GL's unpack alignment */
    ULONG i;
    ULONG j;
    ULONG v;

    for (j = 0; j < 4; j++)
        for (i = 0; i < 4; i++)
        {
            v = TexA(i, j);
            ta[(j * 4 + i) * 4 + 0] = (UBYTE)(v >> 16);
            ta[(j * 4 + i) * 4 + 1] = (UBYTE)(v >> 8);
            ta[(j * 4 + i) * 4 + 2] = (UBYTE)v;
            ta[(j * 4 + i) * 4 + 3] = 255;
            v = TexB(i, j);
            tb[(j * 4 + i) * 3 + 0] = (UBYTE)(v >> 16);
            tb[(j * 4 + i) * 3 + 1] = (UBYTE)(v >> 8);
            tb[(j * 4 + i) * 3 + 2] = (UBYTE)v;
            tc[j * 4 + i] = (UBYTE)TexC(i, j);
        }
    for (j = 0; j < 2; j++)
        for (i = 0; i < 2; i++)
            lm[j * 4 + i] = (UBYTE)Light(i, j);

    Core_Viewport(c, 0, 0, TRI_W, TRI_H);
    Core_MatrixMode(c, GL_PROJECTION);
    Core_LoadIdentity(c);
    Core_Ortho(c, 0.0, (double) TRI_W, 0.0, (double) TRI_H, -1.0, 1.0);
    Core_MatrixMode(c, GL_MODELVIEW);
    Core_LoadIdentity(c);
    Core_ClearColor(c, 0.0f, 0.0f, 0.2509804f, 1.0f);     /* 64 / 255 */
    Core_Clear(c, GL_COLOR_BUFFER_BIT);
    Core_Color4ub(c, 255, 255, 255, 255);

    Upload(c, 1, 4, GL_RGBA, 4, 4, ta);
    Upload(c, 2, 3, GL_RGB, 4, 4, tb);
    Upload(c, 3, 1, GL_LUMINANCE, 4, 4, tc);
    Core_Enable(c, GL_TEXTURE_2D, 1);
    Core_TexEnvi(c, GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

    Core_BindTexture(c, GL_TEXTURE_2D, 1);
    Quad(c, 0.0f, Q, 1);
    Core_BindTexture(c, GL_TEXTURE_2D, 2);
    Quad(c, Q, Q, 1);
    Core_BindTexture(c, GL_TEXTURE_2D, 3);
    Quad(c, 0.0f, 0.0f, 1);

    /* unit 1: the lightmap, MODULATE */
    Core_ActiveTexture(c, GL_TEXTURE1_ARB);
    Upload(c, 4, 1, GL_LUMINANCE, 2, 2, lm);
    Core_Enable(c, GL_TEXTURE_2D, 1);
    Core_TexEnvi(c, GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    Core_ActiveTexture(c, GL_TEXTURE0_ARB);
    Core_BindTexture(c, GL_TEXTURE_2D, 2);
    Quad(c, Q, 0.0f, 3);
}

void TexScene_Pixels(struct TriPixel *out)
{
    static const UBYTE pick[5][2] = { { 0, 0 }, { 3, 0 }, { 0, 3 }, { 3, 3 }, { 1, 2 } };
    static const UBYTE pickd[6][2] = { { 0, 0 }, { 1, 0 }, { 2, 1 }, { 3, 2 }, { 1, 3 }, { 2, 3 } };
    ULONG n;
    ULONG k;
    ULONG i;
    ULONG j;
    ULONG l;
    ULONG v;

    n = 0;
    for (k = 0; k < 5; k++)             /* top left: RGBA as ARGB4444 */
    {
        i = pick[k][0];
        j = pick[k][1];
        out[n].x = 16 + 32 * i;
        out[n].y = 111 - 32 * j;
        out[n].rgb = TexA(i, j);
        out[n].exact = 1;
        out[n].what = "RGBA texel, stored ARGB4444";
        n++;
    }
    for (k = 0; k < 5; k++)             /* top right: RGB as RGB565 */
    {
        i = pick[k][0];
        j = pick[k][1];
        out[n].x = 144 + 32 * i;
        out[n].y = 111 - 32 * j;
        out[n].rgb = TexB(i, j);
        out[n].exact = 1;
        out[n].what = "RGB texel, stored RGB565";
        n++;
    }
    for (k = 0; k < 4; k++)             /* bottom left: LUMINANCE as I8 */
    {
        i = pick[k][0];
        j = pick[k][1];
        l = TexC(i, j);
        out[n].x = 16 + 32 * i;
        out[n].y = 239 - 32 * j;
        out[n].rgb = (l << 16) | (l << 8) | l;
        out[n].exact = 1;
        out[n].what = "LUMINANCE texel, stored I8";
        n++;
    }
    for (k = 0; k < 6; k++)             /* bottom right: RGB x lightmap */
    {
        i = pickd[k][0];
        j = pickd[k][1];
        v = Light(i / 2, j / 2) ? TexB(i, j) : 0;
        out[n].x = 144 + 32 * i;
        out[n].y = 239 - 32 * j;
        out[n].rgb = v;
        out[n].exact = 1;
        out[n].what = "RGB x lightmap (two units, MODULATE)";
        n++;
    }
}
