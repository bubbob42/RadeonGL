/*
 * scene_embers.c -- Embers: a storm of sparks with trails, the screen cut
 * in four, each quarter blending with another equation. Blending, points,
 * lines.
 *
 * Each quarter is a viewport with the scissor round it (its own clear):
 *   top left      GL_FUNC_ADD, glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE,
 *                 GL_ZERO, GL_ONE): the light adds up, the alpha stays
 *   top right     GL_FUNC_SUBTRACT for the sparks, GL_FUNC_REVERSE_SUBTRACT
 *                 for their trails, on grey
 *   bottom left   GL_MIN: soot on a pale ground
 *   bottom right  GL_MAX: the brightest wins, nothing piles up
 * The sparks are soft sprites, 6 to 24 pixels by their heat, a luminance
 * texture on quads from vertex, colour and texture arrays: the blending is
 * the card's work (the MIN quarter takes the spot inverted, GL_ADD, so its
 * edges leave the ground alone). Points in three sizes (glPointSize) fly
 * among them, the trails are wide lines, a shock ring a 10-pixel line
 * loop, a spiral a line strip. Every spark's place is a function of the
 * frame number.
 */

#include <exec/types.h>
#include <proto/exec.h>
#include <proto/minigl.h>

#include "bench.h"

#define SPARKS 240              /* sprites a quarter */
#define POINTS 60               /* points a quarter, three sizes */
#define TRAILS (SPARKS / 3)
#define STEX 32
#define RINGPTS 48
#define SPIRAL 72

struct Spark
{
    LONG ang;                   /* table phase */
    float speed;
    ULONG life;                 /* frames */
    ULONG phase;
};

static struct Spark *em_s;
static float *em_pt;            /* SPARKS x 2 */
static UBYTE *em_pc;            /* SPARKS x 4 */
static float *em_lt;            /* TRAILS x 2 x 2 */
static UBYTE *em_lc;            /* TRAILS x 2 x 4 */
static float *em_sv;            /* SPARKS x 4 x 2: the sprites' corners */
static float *em_st;            /* SPARKS x 4 x 2: their texture corners */
static UBYTE *em_sc;            /* SPARKS x 4 x 4 */
static GLuint em_tex[2];        /* the spot, and the spot inverted */
static BOOL em_have;

static void Done(void);

static BOOL Init(LONG w, LONG h)
{
    ULONG seed;
    ULONG i;
    UBYTE *spot;
    LONG x, y;
    float d;

    (void) w;
    (void) h;
    em_s = (struct Spark *) B_Alloc(SPARKS * sizeof(struct Spark));
    em_pt = (float *) B_Alloc(SPARKS * 2 * sizeof(float));
    em_pc = (UBYTE *) B_Alloc(SPARKS * 4);
    em_lt = (float *) B_Alloc(TRAILS * 4 * sizeof(float));
    em_lc = (UBYTE *) B_Alloc(TRAILS * 8);
    em_sv = (float *) B_Alloc(SPARKS * 8 * sizeof(float));
    em_st = (float *) B_Alloc(SPARKS * 8 * sizeof(float));
    em_sc = (UBYTE *) B_Alloc(SPARKS * 16);
    spot = (UBYTE *) B_Alloc(STEX * STEX * 2);
    if (em_s == NULL || em_pt == NULL || em_pc == NULL || em_lt == NULL || em_lc == NULL
        || em_sv == NULL || em_st == NULL || em_sc == NULL || spot == NULL)
    {
        B_Free(spot);
        Done();
        return FALSE;
    }
    /* the spot: bright in the middle, gone at the edge; and inverted */
    for (y = 0; y < STEX; y++)
        for (x = 0; x < STEX; x++)
        {
            d = 1.0f - (float) ((2 * x - STEX + 1) * (2 * x - STEX + 1) + (2 * y - STEX + 1) * (2 * y - STEX + 1))
                / (float) (STEX * STEX);
            if (d < 0.0f)
                d = 0.0f;
            spot[y * STEX + x] = (UBYTE) B_Byte(d * d);
            spot[STEX * STEX + y * STEX + x] = (UBYTE) (255 - spot[y * STEX + x]);
        }
    em_tex[0] = B_Texture(GL_LUMINANCE, STEX, STEX, GL_LUMINANCE, spot, FALSE, GL_CLAMP);
    em_tex[1] = B_Texture(GL_LUMINANCE, STEX, STEX, GL_LUMINANCE, spot + STEX * STEX, FALSE, GL_CLAMP);
    em_have = TRUE;
    B_Free(spot);
    for (i = 0; i < SPARKS; i++)
    {
        em_st[i * 8] = 0.0f;
        em_st[i * 8 + 1] = 0.0f;
        em_st[i * 8 + 2] = 1.0f;
        em_st[i * 8 + 3] = 0.0f;
        em_st[i * 8 + 4] = 1.0f;
        em_st[i * 8 + 5] = 1.0f;
        em_st[i * 8 + 6] = 0.0f;
        em_st[i * 8 + 7] = 1.0f;
    }
    seed = 4711UL;
    for (i = 0; i < SPARKS; i++)
    {
        em_s[i].ang = (LONG) (B_TABLE / 4 + (B_Rand(&seed) % (B_TABLE / 3)) - B_TABLE / 6);
        em_s[i].speed = 0.8f + (float) (B_Rand(&seed) % 1000) * 0.0011f;
        em_s[i].life = 50 + B_Rand(&seed) % 90;
        em_s[i].phase = B_Rand(&seed) % em_s[i].life;
    }
    return TRUE;
}

/* spark i of quarter q at frame f: where, and how far through its life */
static float Place(ULONG i, ULONG q, ULONG f, LONG back, float *x, float *y)
{
    const struct Spark *s;
    float t;
    LONG age;

    s = &em_s[i];
    age = (LONG) ((f + s->phase + q * 37) % s->life) - back;
    if (age < 0)
        age = 0;
    t = (float) age / (float) s->life;
    *x = B_CosT(s->ang + (LONG) q * 40) * s->speed * t * 1.25f;
    *y = -0.85f + B_SinT(s->ang) * s->speed * t * 2.2f - 1.9f * t * t;
    return t;
}

static void Colour(UBYTE *c, float t, ULONG q)
{
    float heat;

    heat = 1.0f - t;
    if (q == 2)                 /* soot for the MIN quarter: dark, warm */
    {
        c[0] = (UBYTE) B_Byte(0.25f + 0.6f * t);
        c[1] = (UBYTE) B_Byte(0.18f + 0.6f * t);
        c[2] = (UBYTE) B_Byte(0.15f + 0.6f * t);
    }
    else
    {
        c[0] = (UBYTE) B_Byte(0.4f + 0.6f * heat);
        c[1] = (UBYTE) B_Byte(heat * heat * 0.85f + 0.05f);
        c[2] = (UBYTE) B_Byte(heat * heat * heat * 0.6f);
    }
    c[3] = (UBYTE) B_Byte(heat);
}

static void Quarter(ULONG q, ULONG f, LONG w, LONG h)
{
    static const float bg[4][3] =
    {
        { 0.03f, 0.02f, 0.04f }, { 0.45f, 0.45f, 0.48f }, { 0.93f, 0.86f, 0.76f }, { 0.02f, 0.03f, 0.07f }
    };
    LONG qx, qy, qw, qh;
    ULONG i, n, k;
    float t, x, y, r, aspect, px;

    qw = w / 2;
    qh = h / 2;
    qx = (q & 1) ? qw : 0;
    qy = (q & 2) ? 0 : qh;
    glViewport((GLint) qx, (GLint) qy, (GLsizei) qw, (GLsizei) qh);
    glScissor((GLint) qx, (GLint) qy, (GLsizei) qw, (GLsizei) qh);
    glClearColor(bg[q][0], bg[q][1], bg[q][2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    aspect = (float) qw / (float) qh;
    px = 2.0f / (float) qh;     /* a pixel in the quarter's units */
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho((GLdouble) -aspect, (GLdouble) aspect, -1.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    for (i = 0; i < SPARKS; i++)
    {
        t = Place(i, q, f, 0, &x, &y);
        Colour(&em_sc[i * 16], t, q);
        for (k = 1; k < 4; k++)
        {
            em_sc[i * 16 + k * 4] = em_sc[i * 16];
            em_sc[i * 16 + k * 4 + 1] = em_sc[i * 16 + 1];
            em_sc[i * 16 + k * 4 + 2] = em_sc[i * 16 + 2];
            em_sc[i * 16 + k * 4 + 3] = em_sc[i * 16 + 3];
        }
        r = (3.0f + 9.0f * (1.0f - t)) * px;        /* half the sprite: 6-24 pixels */
        em_sv[i * 8] = x - r;
        em_sv[i * 8 + 1] = y - r;
        em_sv[i * 8 + 2] = x + r;
        em_sv[i * 8 + 3] = y - r;
        em_sv[i * 8 + 4] = x + r;
        em_sv[i * 8 + 5] = y + r;
        em_sv[i * 8 + 6] = x - r;
        em_sv[i * 8 + 7] = y + r;
    }
    for (i = 0; i < POINTS; i++)
    {
        t = Place(i * 4 + 1, q, f, 2, &em_pt[i * 2], &em_pt[i * 2 + 1]);
        Colour(&em_pc[i * 4], t, q);
    }
    for (i = 0; i < TRAILS; i++)
    {
        t = Place(i * 3, q, f, 0, &em_lt[i * 4], &em_lt[i * 4 + 1]);
        Colour(&em_lc[i * 8], t, q);
        t = Place(i * 3, q, f, 5, &em_lt[i * 4 + 2], &em_lt[i * 4 + 3]);
        Colour(&em_lc[i * 8 + 4], t, q);
        em_lc[i * 8 + 7] = 0;
    }

    glEnable(GL_BLEND);
    switch (q)
    {
    case 0:
        glBlendEquation(GL_FUNC_ADD);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
        break;
    case 1:
        glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        break;
    case 2:
        glBlendEquation(GL_MIN);
        glBlendFunc(GL_ONE, GL_ONE);
        break;
    default:
        glBlendEquation(GL_MAX);
        glBlendFunc(GL_ONE, GL_ONE);
        break;
    }
    /* the trails first, wide lines */
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, em_lt);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, em_lc);
    glLineWidth(2.0f);
    glDrawArrays(GL_LINES, 0, TRAILS * 2);
    /* the sprites: the spot modulated by each spark's colour; in the MIN
     * quarter the spot inverted and added, so the edges stay white */
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, em_tex[q == 2 ? 1 : 0]);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, q == 2 ? GL_ADD : GL_MODULATE);
    glClientActiveTextureARB(GL_TEXTURE0_ARB);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, 0, em_st);
    glVertexPointer(2, GL_FLOAT, 0, em_sv);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, em_sc);
    glDrawArrays(GL_QUADS, 0, SPARKS * 4);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glDisable(GL_TEXTURE_2D);
    /* the points: three sizes, a third each */
    if (q == 1)
        glBlendEquation(GL_FUNC_SUBTRACT);
    glVertexPointer(2, GL_FLOAT, 0, em_pt);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, em_pc);
    n = POINTS / 3;
    glPointSize(2.0f);
    glDrawArrays(GL_POINTS, 0, (GLsizei) n);
    glPointSize(4.0f);
    glDrawArrays(GL_POINTS, (GLint) n, (GLsizei) n);
    glPointSize(7.0f);
    glDrawArrays(GL_POINTS, (GLint) (2 * n), (GLsizei) (POINTS - 2 * n));
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    bench_prims += SPARKS * 2 + POINTS + TRAILS;

    if (q == 0)
    {
        /* a shock ring, a wide line loop that grows and fades */
        r = (float) (f % 90) / 90.0f;
        glLineWidth(10.0f);
        glColor4f(1.0f, 0.75f, 0.35f, 1.0f - r);
        glBegin(GL_LINE_LOOP);
        for (i = 0; i < RINGPTS; i++)
            glVertex2f(B_CosT((LONG) (i * B_TABLE / RINGPTS)) * (0.15f + 0.8f * r),
                       -0.3f + B_SinT((LONG) (i * B_TABLE / RINGPTS)) * (0.15f + 0.8f * r) * 0.6f);
        glEnd();
        bench_prims += RINGPTS;
    }
    if (q == 3)
    {
        /* a spiral turning, a line strip */
        glLineWidth(3.0f);
        glBegin(GL_LINE_STRIP);
        for (i = 0; i < SPIRAL; i++)
        {
            r = (float) i / (float) SPIRAL;
            glColor4f(0.3f + 0.7f * r, 0.5f * r, 1.0f - 0.6f * r, 1.0f);
            x = B_CosT((LONG) (i * 40) + (LONG) f * 9) * r * 0.9f;
            y = B_SinT((LONG) (i * 40) + (LONG) f * 9) * r * 0.9f;
            glVertex2f(x, y);
        }
        glEnd();
        bench_prims += SPIRAL - 1;
    }
    glBlendEquation(GL_FUNC_ADD);
    glDisable(GL_BLEND);
}

static void Frame(ULONG f, LONG w, LONG h)
{
    ULONG q;

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_SCISSOR_TEST);
    for (q = 0; q < 4; q++)
        Quarter(q, f, w, h);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, (GLsizei) w, (GLsizei) h);
    B_Label(8, h - 14, 1, "ADD", 0.95f, 0.85f, 0.70f);
    B_Label(w / 2 + 8, h - 14, 1, "SUBTRACT", 0.10f, 0.10f, 0.12f);
    B_Label(8, h / 2 - 14, 1, "MIN", 0.15f, 0.12f, 0.10f);
    B_Label(w / 2 + 8, h / 2 - 14, 1, "MAX", 0.95f, 0.85f, 0.70f);
}

static void Done(void)
{
    if (em_have)
        glDeleteTextures(2, em_tex);
    em_have = FALSE;
    B_Free(em_sv);
    B_Free(em_st);
    B_Free(em_sc);
    em_sv = em_st = NULL;
    em_sc = NULL;
    B_Free(em_s);
    B_Free(em_pt);
    B_Free(em_pc);
    B_Free(em_lt);
    B_Free(em_lc);
    em_s = NULL;
    em_pt = em_lt = NULL;
    em_pc = em_lc = NULL;
}

const struct BenchScene scene_embers =
{
    "Embers", "blending: all five equations, separate alpha, points, wide lines", 300, TRUE,
    Init, Frame, Done
};
