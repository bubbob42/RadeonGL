/*
 * scene_lanterns.c -- Lanterns: eight coloured lanterns swinging in front
 * of a banner that waves in the wind. The TCL: vertices and lights.
 *
 * The banner is a 48 x 32 grid in 32 wave positions computed once (a
 * whole wave cycle: positions and unnormalised normals, GL_NORMALIZE on
 * the card), kept on the card as static arrays (GL_RGL_static_arrays); a
 * frame picks one, every second frame the next. Drawn twice from the same
 * locked arrays as one triangle strip with 32-bit indices (the rows joined
 * by degenerate triangles), so the card does the work: first an ambient
 * pass with
 * caustics projected onto it (eye-linear texture coordinate generation and
 * a moving texture matrix), then the lit pass added on top with
 * glDepthFunc(GL_EQUAL), no depth writes, glBlendFunc(GL_ONE, GL_ONE).
 * The eight lights: five local lights with attenuation, two spot lights
 * and one directional; two-sided lighting (the banner's folds show their
 * backs), exp2 fog. The lanterns themselves are big points.
 */

#include <exec/types.h>
#include <proto/exec.h>
#include <proto/minigl.h>

#include "bench.h"

#define GX 48
#define GY 32
#define NV ((GX + 1) * (GY + 1))
/* one triangle strip, row by row, joined by two repeated indices (64 x 40
 * as triangles was 9 fps on the A4000D: 120 KB of indices a frame) */
#define NI (GY * 2 * (GX + 1) + 2 * (GY - 1))
#define CTEX 64
#define LIGHTS 8
#define KEYS 32                 /* wave positions: B_TABLE / KEYS a step */

static float *ln_pos;           /* KEYS x NV x 3 */
static float *ln_nrm;
static float *ln_z;             /* NV: one position's heights, while made */
static GLuint *ln_idx;
static GLuint ln_ctex;
static BOOL ln_tex;

static const GLfloat ln_col[LIGHTS][4] =
{
    { 1.0f, 0.45f, 0.20f, 1.0f }, { 0.35f, 0.65f, 1.0f, 1.0f }, { 1.0f, 0.85f, 0.35f, 1.0f },
    { 0.45f, 1.0f, 0.55f, 1.0f }, { 1.0f, 0.35f, 0.70f, 1.0f }, { 0.75f, 0.55f, 1.0f, 1.0f },
    { 1.0f, 0.65f, 0.35f, 1.0f }, { 0.25f, 0.30f, 0.45f, 1.0f }
};

static void Done(void);
static void Wave(LONG key);

static BOOL Init(LONG w, LONG h)
{
    UBYTE *ct;
    LONG i, j, k, v;
    float a, b;

    (void) w;
    (void) h;
    ln_pos = (float *) B_Alloc(KEYS * NV * 3 * sizeof(float));
    ln_nrm = (float *) B_Alloc(KEYS * NV * 3 * sizeof(float));
    ln_z = (float *) B_Alloc(NV * sizeof(float));
    ln_idx = (GLuint *) B_Alloc(NI * sizeof(GLuint));
    ct = (UBYTE *) B_Alloc(CTEX * CTEX);
    if (ln_pos == NULL || ln_nrm == NULL || ln_z == NULL || ln_idx == NULL || ct == NULL)
    {
        B_Free(ct);
        Done();
        return FALSE;
    }
    k = 0;
    for (j = 0; j < GY; j++)
    {
        if (j > 0)
        {
            /* the join: the last index again, the next row's first twice */
            ln_idx[k] = ln_idx[k - 1];
            k++;
            ln_idx[k++] = (GLuint) (j * (GX + 1));
        }
        for (i = 0; i <= GX; i++)
        {
            v = j * (GX + 1) + i;
            ln_idx[k++] = (GLuint) v;
            ln_idx[k++] = (GLuint) (v + GX + 1);
        }
    }
    /* caustics: bright crossing ridges of two wave trains */
    for (j = 0; j < CTEX; j++)
        for (i = 0; i < CTEX; i++)
        {
            a = B_SinT(i * 3 * B_TABLE / CTEX + B_Floor(40.0f * B_SinT(j * 2 * B_TABLE / CTEX)));
            b = B_SinT(j * 3 * B_TABLE / CTEX + B_Floor(40.0f * B_SinT(i * 2 * B_TABLE / CTEX)));
            a = 1.0f - (a < 0.0f ? -a : a);
            b = 1.0f - (b < 0.0f ? -b : b);
            ct[j * CTEX + i] = (UBYTE) B_Byte(0.25f + 0.75f * a * a * b * b * 2.5f);
        }
    ln_ctex = B_Texture(GL_LUMINANCE, CTEX, CTEX, GL_LUMINANCE, ct, TRUE, GL_REPEAT);
    ln_tex = TRUE;
    B_Free(ct);
    for (v = 0; v < KEYS; v++)
        Wave(v);
    return TRUE;
}

/* wave position key: z from two wave trains, more at the free edge; every
 * phase a whole multiple of the key, so key KEYS is key 0 again */
static void Wave(LONG key)
{
    LONG i, j, v, ph, step;
    float x, y, edge;
    float *pos, *nrm;

    step = key * (B_TABLE / KEYS);
    pos = ln_pos + key * NV * 3;
    nrm = ln_nrm + key * NV * 3;
    for (j = 0; j <= GY; j++)
        for (i = 0; i <= GX; i++)
        {
            v = j * (GX + 1) + i;
            edge = 0.15f + 0.85f * (float) i / (float) GX;
            ph = i * 22 - step;
            ln_z[v] = edge * (0.42f * B_SinT(ph + j * 6) + 0.16f * B_SinT(ph * 2 + j * 17 + step));
        }
    for (j = 0; j <= GY; j++)
        for (i = 0; i <= GX; i++)
        {
            v = j * (GX + 1) + i;
            x = ((float) i / (float) GX - 0.5f) * 5.2f;
            y = ((float) j / (float) GY - 0.5f) * 3.2f;
            pos[v * 3] = x;
            pos[v * 3 + 1] = y;
            pos[v * 3 + 2] = ln_z[v];
            /* the normal from the neighbours, not normalised: GL_NORMALIZE */
            nrm[v * 3] = ln_z[(i > 0) ? v - 1 : v] - ln_z[(i < GX) ? v + 1 : v];
            nrm[v * 3 + 1] = ln_z[(j > 0) ? v - GX - 1 : v] - ln_z[(j < GY) ? v + GX + 1 : v];
            nrm[v * 3 + 2] = 2.0f * 5.2f / (float) GX;
        }
}

static void LightAt(ULONG k, ULONG f, GLfloat *p)
{
    p[0] = -2.3f + (float) k * 0.66f + 0.25f * B_SinT((LONG) f * 4 + (LONG) k * 90);
    p[1] = 1.15f + 0.30f * B_CosT((LONG) f * 6 + (LONG) k * 140);
    p[2] = 0.75f + 0.45f * B_SinT((LONG) f * 3 + (LONG) k * 200);
    p[3] = 1.0f;
}

static void Frame(ULONG f, LONG w, LONG h)
{
    static const GLfloat zero[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    static const GLfloat amb[4] = { 0.30f, 0.30f, 0.34f, 1.0f };
    static const GLfloat mat[4] = { 0.85f, 0.82f, 0.78f, 1.0f };
    static GLfloat fogc[4] = { 0.05f, 0.06f, 0.10f, 1.0f };
    static const GLfloat dir7[4] = { 0.2f, 0.6f, 1.0f, 0.0f };
    static const GLfloat spotdir[3] = { 0.0f, -0.35f, -1.0f };
    static const GLfloat splane[4] = { 0.45f, 0.0f, 0.0f, 0.0f };
    static const GLfloat tplane[4] = { 0.0f, 0.45f, 0.0f, 0.0f };
    GLfloat p[4];
    GLfloat dif[4];
    ULONG k;
    LONG key;
    GLenum light;

    key = (LONG) ((f / 2) % KEYS);
    glClear(GL_DEPTH_BUFFER_BIT);
    B_Background(0.04f, 0.05f, 0.09f, 0.08f, 0.10f, 0.15f);
    B_Perspective(48.0f, (float) w / (float) h, 1.0f, 20.0f);
    glLoadIdentity();
    glTranslatef(0.0f, -0.15f, -5.2f);
    glRotatef(-24.0f + 6.0f * B_SinT((LONG) f * 2), 0.0f, 1.0f, 0.0f);
    glRotatef(8.0f, 1.0f, 0.0f, 0.0f);

    /* the lights, placed in the banner's space */
    for (k = 0; k < LIGHTS; k++)
    {
        light = (GLenum) (GL_LIGHT0 + k);
        dif[0] = ln_col[k][0] * 0.65f;      /* full strength washed the banner out */
        dif[1] = ln_col[k][1] * 0.65f;
        dif[2] = ln_col[k][2] * 0.65f;
        dif[3] = 1.0f;
        glLightfv(light, GL_DIFFUSE, dif);
        glLightfv(light, GL_SPECULAR, zero);
        if (k == LIGHTS - 1)
        {
            glLightfv(light, GL_POSITION, dir7);
            continue;
        }
        LightAt(k, f, p);
        glLightfv(light, GL_POSITION, p);
        glLightf(light, GL_CONSTANT_ATTENUATION, 0.35f);
        glLightf(light, GL_LINEAR_ATTENUATION, 0.30f);
        glLightf(light, GL_QUADRATIC_ATTENUATION, 0.40f);
        if (k == 2 || k == 5)
        {
            glLightfv(light, GL_SPOT_DIRECTION, spotdir);
            glLightf(light, GL_SPOT_CUTOFF, 38.0f);
            glLightf(light, GL_SPOT_EXPONENT, 10.0f);
        }
        else
            glLightf(light, GL_SPOT_CUTOFF, 180.0f);
    }
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, mat);
    glLightModelf(GL_LIGHT_MODEL_TWO_SIDE, 1.0f);
    glEnable(GL_NORMALIZE);
    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, GL_EXP2);
    glFogf(GL_FOG_DENSITY, 0.10f);
    glFogfv(GL_FOG_COLOR, fogc);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    if (bench_rgl)
        glHint(GL_RGL_STATIC_ARRAYS_HINT, GL_NICEST);
    glVertexPointer(3, GL_FLOAT, 0, ln_pos + key * NV * 3);
    glNormalPointer(GL_FLOAT, 0, ln_nrm + key * NV * 3);
    glLockArraysEXT(0, NV);

    /* pass 1: ambient light only, the caustics projected from the eye */
    glEnable(GL_LIGHTING);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, ln_ctex);
    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_EYE_LINEAR);
    glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_EYE_LINEAR);
    glTexGenfv(GL_S, GL_EYE_PLANE, splane);
    glTexGenfv(GL_T, GL_EYE_PLANE, tplane);
    glEnable(GL_TEXTURE_GEN_S);
    glEnable(GL_TEXTURE_GEN_T);
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glTranslatef((float) f * 0.004f, (float) f * -0.0025f, 0.0f);
    glRotatef((float) f * 0.15f, 0.0f, 0.0f, 1.0f);
    glMatrixMode(GL_MODELVIEW);
    glDrawElements(GL_TRIANGLE_STRIP, NI, GL_UNSIGNED_INT, ln_idx);
    glDisable(GL_TEXTURE_GEN_S);
    glDisable(GL_TEXTURE_GEN_T);
    glDisable(GL_TEXTURE_2D);
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);

    /* pass 2: the eight lights, added where pass 1 left its depth */
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, zero);
    for (k = 0; k < LIGHTS; k++)
        glEnable((GLenum) (GL_LIGHT0 + k));
    glDepthFunc(GL_EQUAL);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDisable(GL_FOG);
    glDrawElements(GL_TRIANGLE_STRIP, NI, GL_UNSIGNED_INT, ln_idx);
    glUnlockArraysEXT();
    glDisableClientState(GL_NORMAL_ARRAY);
    if (bench_rgl)
        glHint(GL_RGL_STATIC_ARRAYS_HINT, GL_DONT_CARE);
    bench_prims += 2 * (NI - 2);

    /* the lanterns: big points, added */
    glDisable(GL_LIGHTING);
    glDepthFunc(GL_LESS);
    glPointSize(9.0f);
    glBegin(GL_POINTS);
    for (k = 0; k < LIGHTS - 1; k++)
    {
        LightAt(k, f, p);
        glColor4f(ln_col[k][0], ln_col[k][1], ln_col[k][2], 1.0f);
        glVertex3f(p[0], p[1], p[2]);
    }
    glEnd();
    glPointSize(3.0f);
    glBegin(GL_POINTS);
    for (k = 0; k < LIGHTS - 1; k++)
    {
        LightAt(k, f, p);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glVertex3f(p[0], p[1], p[2]);
    }
    glEnd();
    bench_prims += 2 * (LIGHTS - 1);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}

static void Done(void)
{
    if (bench_rgl)
    {
        glHint(GL_RGL_STATIC_ARRAYS_HINT, GL_FASTEST);
        glHint(GL_RGL_STATIC_ARRAYS_HINT, GL_DONT_CARE);
    }
    if (ln_tex)
        glDeleteTextures(1, &ln_ctex);
    ln_tex = FALSE;
    B_Free(ln_pos);
    B_Free(ln_nrm);
    B_Free(ln_z);
    B_Free(ln_idx);
    ln_pos = ln_nrm = ln_z = NULL;
    ln_idx = NULL;
}

const struct BenchScene scene_lanterns =
{
    "Lanterns", "the TCL: eight lights, two passes over a static waving banner", 300, TRUE, Init, Frame, Done
};
