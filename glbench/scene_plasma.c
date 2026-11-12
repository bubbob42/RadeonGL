/*
 * scene_plasma.c -- Plasma: a wall of twelve tiles. Texture uploads,
 * formats and environments.
 *
 *   PLASMA   128 x 128 RGB computed and sent every frame (glTexSubImage2D)
 *   PALETTE  64 x 64 colour-index image, the shared palette turned every
 *            frame (glColorTable) and the image sent again: RadeonGL, as
 *            MiniGL 29.1, applies the palette when the image arrives
 *   RGB565, ARGB4444, ARGB8888, I8, AI88, INDEX8: one tile each, with
 *            nearest, linear, mipmaps, repeat and clamp among them, two of
 *            them moved by the texture matrix
 *   REPLACE, DECAL, BLEND, ADD: the texture environments over a quad with
 *            coloured corners (BLEND with an environment colour that turns)
 */

#include <exec/types.h>
#include <proto/exec.h>
#include <proto/minigl.h>

#include "bench.h"

#define PTEX 128               /* 48 KB a frame (256: 5 fps on the A4000D, ~1 MB/s uploads) */
#define ITEX 64
#define STEX 64
#define TILES 12

#define T_PLASMA 0
#define T_PAL    1
#define T_565    2
#define T_4444   3
#define T_8888   4
#define T_I8     5
#define T_AI88   6
#define T_IDX    7
#define T_ENV    8              /* one texture for the four environments */
#define NTEX     9

static const char *const pm_names[TILES] =
{
    "PLASMA", "PALETTE", "RGB565", "ARGB4444", "ARGB8888", "I8", "AI88", "INDEX8",
    "REPLACE", "DECAL", "BLEND", "ADD"
};

static GLuint pm_tex[NTEX];
static UBYTE *pm_plasma;        /* PTEX x PTEX x 3 */
static UBYTE *pm_index;         /* ITEX x ITEX */
static UBYTE pm_fire[256 * 3];
static UBYTE pm_pal[256 * 3];
static BOOL pm_have;

static void Done(void);

/* a palette of 256 colours from three phases */
static void Palette(UBYTE *pal, LONG turn, LONG pr, LONG pg, LONG pb)
{
    LONG i;

    for (i = 0; i < 256; i++)
    {
        pal[i * 3] = (UBYTE) B_Byte(0.5f + 0.5f * B_SinT(i * 4 * 3 + pr + turn));
        pal[i * 3 + 1] = (UBYTE) B_Byte(0.5f + 0.5f * B_SinT(i * 4 * 3 + pg + turn));
        pal[i * 3 + 2] = (UBYTE) B_Byte(0.5f + 0.5f * B_SinT(i * 4 * 3 + pb + turn));
    }
}

static BOOL Init(LONG w, LONG h)
{
    UBYTE *t;
    LONG x, y, v, dx, dy;
    float a, d;

    (void) w;
    (void) h;
    pm_plasma = (UBYTE *) B_Alloc(PTEX * PTEX * 3);
    pm_index = (UBYTE *) B_Alloc(ITEX * ITEX);
    t = (UBYTE *) B_Alloc(STEX * STEX * 4);
    if (pm_plasma == NULL || pm_index == NULL || t == NULL)
    {
        B_Free(t);
        Done();
        return FALSE;
    }
    /* the plasma's colours: violet through red to yellow and back */
    for (v = 0; v < 256; v++)
    {
        a = B_SinT(v * 4);
        pm_fire[v * 3] = (UBYTE) B_Byte(0.55f + 0.45f * a);
        pm_fire[v * 3 + 1] = (UBYTE) B_Byte(0.30f + 0.40f * B_SinT(v * 8 + 200));
        pm_fire[v * 3 + 2] = (UBYTE) B_Byte(0.50f - 0.45f * a);
    }
    glEnable(GL_TEXTURE_2D);
    pm_tex[T_PLASMA] = B_Texture(GL_RGB, PTEX, PTEX, GL_RGB, pm_plasma, FALSE, GL_REPEAT);
    /* the index image: rings and spokes */
    for (y = 0; y < ITEX; y++)
        for (x = 0; x < ITEX; x++)
        {
            dx = x - ITEX / 2;
            dy = y - ITEX / 2;
            d = (float) (dx * dx + dy * dy);
            v = (LONG) (d / 40.0f) + B_Floor(8.0f + 8.0f * B_SinT(dx * 3 + dy * 5)) * 4;
            pm_index[y * ITEX + x] = (UBYTE) (v & 255);
        }
    Palette(pm_pal, 0, 0, 340, 680);
    glEnable(GL_SHARED_TEXTURE_PALETTE_EXT);
    glColorTable(GL_SHARED_TEXTURE_PALETTE_EXT, GL_RGB, 256, GL_RGB, GL_UNSIGNED_BYTE, pm_pal);
    glGenTextures(1, &pm_tex[T_PAL]);
    glBindTexture(GL_TEXTURE_2D, pm_tex[T_PAL]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT, ITEX, ITEX, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE,
                 pm_index);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    /* INDEX8: expanded once, with this palette, and kept */
    glGenTextures(1, &pm_tex[T_IDX]);
    glBindTexture(GL_TEXTURE_2D, pm_tex[T_IDX]);
    for (y = 0; y < STEX; y++)
        for (x = 0; x < STEX; x++)
            t[y * STEX + x] = (UBYTE) (((x / 8) ^ (y / 8)) * 16 + x);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT, STEX, STEX, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    /* RGB565: a checker of colour gradients, nearest, repeating */
    for (y = 0; y < STEX; y++)
        for (x = 0; x < STEX; x++)
        {
            v = (y * STEX + x) * 3;
            t[v] = (UBYTE) (x * 4);
            t[v + 1] = (UBYTE) (y * 4);
            t[v + 2] = (UBYTE) ((((x / 16) + (y / 16)) & 1) ? 230 : 40);
        }
    pm_tex[T_565] = B_Texture(GL_RGB, STEX, STEX, GL_RGB, t, FALSE, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    /* ARGB4444: a ramp of alpha over a colour, clamped at the edges */
    for (y = 0; y < STEX; y++)
        for (x = 0; x < STEX; x++)
        {
            v = (y * STEX + x) * 4;
            t[v] = 240;
            t[v + 1] = (UBYTE) (120 + y * 2);
            t[v + 2] = 40;
            t[v + 3] = (UBYTE) (x * 4);
        }
    pm_tex[T_4444] = B_Texture(GL_RGBA, STEX, STEX, GL_RGBA, t, FALSE, GL_CLAMP);
    /* ARGB8888: a smooth colour wheel, mipmapped */
    for (y = 0; y < STEX; y++)
        for (x = 0; x < STEX; x++)
        {
            v = (y * STEX + x) * 4;
            t[v] = (UBYTE) B_Byte(0.5f + 0.5f * B_SinT(x * 16));
            t[v + 1] = (UBYTE) B_Byte(0.5f + 0.5f * B_SinT(y * 16 + 340));
            t[v + 2] = (UBYTE) B_Byte(0.5f + 0.5f * B_SinT((x + y) * 8 + 680));
            t[v + 3] = 255;
        }
    pm_tex[T_8888] = B_Texture(GL_RGBA8, STEX, STEX, GL_RGBA, t, TRUE, GL_REPEAT);
    /* I8: rings */
    for (y = 0; y < STEX; y++)
        for (x = 0; x < STEX; x++)
        {
            dx = x - STEX / 2;
            dy = y - STEX / 2;
            t[y * STEX + x] = (UBYTE) B_Byte(0.5f + 0.5f * B_SinT((dx * dx + dy * dy) * 3));
        }
    pm_tex[T_I8] = B_Texture(GL_LUMINANCE, STEX, STEX, GL_LUMINANCE, t, FALSE, GL_REPEAT);
    /* AI88: soft blobs with alpha */
    for (y = 0; y < STEX; y++)
        for (x = 0; x < STEX; x++)
        {
            dx = (x % 32) - 16;
            dy = (y % 32) - 16;
            d = 1.0f - (float) (dx * dx + dy * dy) / 256.0f;
            v = (y * STEX + x) * 2;
            t[v] = (UBYTE) B_Byte(0.6f + 0.4f * d);
            t[v + 1] = (UBYTE) B_Byte(d);
        }
    pm_tex[T_AI88] = B_Texture(GL_LUMINANCE_ALPHA, STEX, STEX, GL_LUMINANCE_ALPHA, t, FALSE, GL_REPEAT);
    /* the environments' texture: a pale window frame (opaque) round dark
     * blue panes (clear): REPLACE shows it as it is, DECAL lets the corners'
     * colours through the panes, BLEND and ADD mix them */
    for (y = 0; y < STEX; y++)
        for (x = 0; x < STEX; x++)
        {
            v = (y * STEX + x) * 4;
            if ((x % 32) < 5 || (y % 32) < 5)
            {
                t[v] = 235;
                t[v + 1] = 225;
                t[v + 2] = 200;
                t[v + 3] = 255;
            }
            else
            {
                t[v] = 30;
                t[v + 1] = (UBYTE) (40 + y);
                t[v + 2] = (UBYTE) (90 + x);
                t[v + 3] = 0;
            }
        }
    pm_tex[T_ENV] = B_Texture(GL_RGBA, STEX, STEX, GL_RGBA, t, FALSE, GL_REPEAT);
    pm_have = TRUE;
    B_Free(t);
    glDisable(GL_TEXTURE_2D);
    return TRUE;
}

/* the plasma at frame f: a sum of three table rows, through the colours */
static void Plasma(ULONG f)
{
    UBYTE a[PTEX];
    UBYTE b[PTEX];
    UBYTE c[PTEX * 2];
    LONG x, y, k;
    UBYTE *p;
    const UBYTE *q;
    UBYTE by;

    for (x = 0; x < PTEX; x++)
    {
        a[x] = (UBYTE) B_Floor(42.0f + 41.0f * B_SinT(x * 9 + (LONG) f * 7));
        b[x] = (UBYTE) B_Floor(42.0f + 41.0f * B_SinT(x * 6 - (LONG) f * 5 + 100));
    }
    for (x = 0; x < PTEX * 2; x++)
        c[x] = (UBYTE) B_Floor(42.0f + 41.0f * B_SinT(x * 4 + (LONG) f * 11 + 300));
    p = pm_plasma;
    for (y = 0; y < PTEX; y++)
    {
        by = b[y];
        for (x = 0; x < PTEX; x++)
        {
            k = (LONG) (UBYTE) (a[x] + by + c[x + y]);
            q = &pm_fire[k * 3];
            *p++ = q[0];
            *p++ = q[1];
            *p++ = q[2];
        }
    }
}

static void Tile(LONG k, LONG w, LONG h, float s0, float t0, float s1, float t1)
{
    LONG gap, tw, th, x, y;

    gap = 8;
    tw = (w - 5 * gap) / 4;
    th = (h - 16 - 4 * gap) / 3;
    x = gap + (k % 4) * (tw + gap);
    y = 16 + gap + (2 - k / 4) * (th + gap);
    glBegin(GL_QUADS);
    glColor4f(1.0f, 0.35f, 0.25f, 1.0f);
    glTexCoord2f(s0, t1);
    glVertex2f((float) x, (float) y);
    glColor4f(0.30f, 0.85f, 0.35f, 1.0f);
    glTexCoord2f(s1, t1);
    glVertex2f((float) (x + tw), (float) y);
    glColor4f(0.25f, 0.45f, 1.0f, 1.0f);
    glTexCoord2f(s1, t0);
    glVertex2f((float) (x + tw), (float) (y + th));
    glColor4f(0.95f, 0.85f, 0.30f, 1.0f);
    glTexCoord2f(s0, t0);
    glVertex2f((float) x, (float) (y + th));
    glEnd();
    bench_prims += 2;
}

static void Frame(ULONG f, LONG w, LONG h)
{
    static const GLenum envs[4] = { GL_REPLACE, GL_DECAL, GL_BLEND, GL_ADD };
    GLfloat envc[4];
    LONG k, gap, tw, th;

    glClearColor(0.06f, 0.07f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    B_Ortho2D(w, h);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);

    /* PLASMA, sent anew */
    Plasma(f);
    glBindTexture(GL_TEXTURE_2D, pm_tex[T_PLASMA]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, PTEX, PTEX, GL_RGB, GL_UNSIGNED_BYTE, pm_plasma);
    bench_bytes += PTEX * PTEX * 3;
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    Tile(0, w, h, 0.0f, 0.0f, 1.0f, 1.0f);
    /* PALETTE: the palette turned, the image sent again */
    Palette(pm_pal, (LONG) f * 9, 0, 340, 680);
    glColorTable(GL_SHARED_TEXTURE_PALETTE_EXT, GL_RGB, 256, GL_RGB, GL_UNSIGNED_BYTE, pm_pal);
    glBindTexture(GL_TEXTURE_2D, pm_tex[T_PAL]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ITEX, ITEX, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, pm_index);
    bench_bytes += ITEX * ITEX + 256 * 3;
    Tile(1, w, h, 0.0f, 0.0f, 1.0f, 1.0f);

    /* the formats */
    glBindTexture(GL_TEXTURE_2D, pm_tex[T_565]);
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glTranslatef((float) f * 0.004f, (float) f * 0.0025f, 0.0f);
    glMatrixMode(GL_MODELVIEW);
    Tile(2, w, h, 0.0f, 0.0f, 3.0f, 3.0f);
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glBindTexture(GL_TEXTURE_2D, pm_tex[T_4444]);
    Tile(3, w, h, -0.25f, -0.25f, 1.25f, 1.25f);
    glDisable(GL_BLEND);
    glBindTexture(GL_TEXTURE_2D, pm_tex[T_8888]);
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glTranslatef(0.5f, 0.5f, 0.0f);
    glRotatef((float) f * 0.6f, 0.0f, 0.0f, 1.0f);
    glScalef(1.0f + 0.8f * (0.5f + 0.5f * B_SinT((LONG) f * 3)), 1.0f + 0.8f * (0.5f + 0.5f * B_SinT((LONG) f * 3)), 1.0f);
    glTranslatef(-0.5f, -0.5f, 0.0f);
    glMatrixMode(GL_MODELVIEW);
    Tile(4, w, h, 0.0f, 0.0f, 4.0f, 4.0f);
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glBindTexture(GL_TEXTURE_2D, pm_tex[T_I8]);
    Tile(5, w, h, 0.0f, 0.0f, 2.0f, 2.0f);
    glEnable(GL_BLEND);
    glBindTexture(GL_TEXTURE_2D, pm_tex[T_AI88]);
    Tile(6, w, h, 0.0f, 0.0f, 2.0f, 2.0f);
    glDisable(GL_BLEND);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glBindTexture(GL_TEXTURE_2D, pm_tex[T_IDX]);
    Tile(7, w, h, 0.0f, 0.0f, 1.0f, 1.0f);

    /* the environments over coloured corners */
    glBindTexture(GL_TEXTURE_2D, pm_tex[T_ENV]);
    envc[0] = 0.5f + 0.5f * B_SinT((LONG) f * 6);
    envc[1] = 0.5f + 0.5f * B_SinT((LONG) f * 6 + 340);
    envc[2] = 0.5f + 0.5f * B_SinT((LONG) f * 6 + 680);
    envc[3] = 1.0f;
    glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, envc);
    for (k = 0; k < 4; k++)
    {
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, (GLint) envs[k]);
        Tile(8 + k, w, h, 0.0f, 0.0f, 2.0f, 2.0f);
    }
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glDisable(GL_TEXTURE_2D);

    gap = 8;
    tw = (w - 5 * gap) / 4;
    th = (h - 16 - 4 * gap) / 3;
    for (k = 0; k < TILES; k++)
        B_Label(gap + (k % 4) * (tw + gap) + 4, 16 + gap + (2 - k / 4) * (th + gap) + 4, 1, pm_names[k],
                1.0f, 1.0f, 1.0f);
}

static void Done(void)
{
    if (pm_have)
        glDeleteTextures(NTEX, pm_tex);
    pm_have = FALSE;
    glDisable(GL_SHARED_TEXTURE_PALETTE_EXT);
    B_Free(pm_plasma);
    B_Free(pm_index);
    pm_plasma = NULL;
    pm_index = NULL;
}

const struct BenchScene scene_plasma =
{
    "Plasma", "texture uploads, palettes, every format and environment", 300, TRUE, Init, Frame, Done
};
