/*
 * scene_pelt.c -- Pelt: a furred ring turning in a breeze. The burner.
 *
 * The torus is drawn SHELLS times (20), each shell a copy pushed out along
 * the normals. Shell 0 is the skin, opaque. Every other shell takes its
 * strands from a noise texture on the second unit: a texel's alpha is the
 * height of its strand, and the alpha test lets through only the strands
 * that reach the shell (GL_GREATER k / SHELLS), so they thin out towards
 * the tips. The shells are blended and write no depth. The breeze bends
 * the strands through the second unit's texture matrix, more at the tips.
 *
 * Every shell's positions are computed once and never change: compiled
 * vertex arrays (glLockArraysEXT) with 16-bit indices, and with RadeonGL's
 * GL_RGL_static_arrays and static indices the card keeps them. Lit (two
 * lights, colour material darker at the roots), the colour texture
 * mipmapped (gluBuild2DMipmaps), linear fog.
 */

#include <exec/types.h>
#include <proto/exec.h>
#include <proto/minigl.h>

#include "bench.h"

#define SU 48                   /* segments round the ring */
#define SV 20                   /* segments round the tube */
#define NV ((SU + 1) * (SV + 1))
#define NI (SU * SV * 6)
#define RING 1.0f
#define TUBE 0.40f
#define FUR 0.17f
#define CTEX 64
#define NTEX 128

static float *pl_pos;           /* shells x NV x 3 */
static float *pl_nrm;           /* NV x 3 */
static float *pl_tc0;           /* NV x 2: colour */
static float *pl_tc1;           /* NV x 2: strands */
static UWORD *pl_idx;           /* NI */
static GLuint pl_ctex, pl_ntex;
static ULONG pl_shells;
static BOOL pl_tex;

static void Done(void);

static BOOL Init(LONG w, LONG h)
{
    UBYTE *ct;
    UBYTE *nt;
    ULONG seed;
    LONG i, j, k, v;
    float cu, su, cv, sv, off, stripe, belly;

    (void) w;
    (void) h;
    pl_shells = bench_shells;
    pl_pos = (float *) B_Alloc(pl_shells * NV * 3 * sizeof(float));
    pl_nrm = (float *) B_Alloc(NV * 3 * sizeof(float));
    pl_tc0 = (float *) B_Alloc(NV * 2 * sizeof(float));
    pl_tc1 = (float *) B_Alloc(NV * 2 * sizeof(float));
    pl_idx = (UWORD *) B_Alloc(NI * sizeof(UWORD));
    ct = (UBYTE *) B_Alloc(CTEX * CTEX * 3);
    nt = (UBYTE *) B_Alloc(NTEX * NTEX * 2);
    if (pl_pos == NULL || pl_nrm == NULL || pl_tc0 == NULL || pl_tc1 == NULL || pl_idx == NULL
        || ct == NULL || nt == NULL)
    {
        B_Free(ct);
        B_Free(nt);
        Done();
        return FALSE;
    }
    /* the ring: (i, j) = (round the ring, round the tube) */
    for (i = 0; i <= SU; i++)
        for (j = 0; j <= SV; j++)
        {
            v = i * (SV + 1) + j;
            cu = B_CosT(i * B_TABLE / SU);
            su = B_SinT(i * B_TABLE / SU);
            cv = B_CosT(j * B_TABLE / SV);
            sv = B_SinT(j * B_TABLE / SV);
            pl_nrm[v * 3] = cv * cu;
            pl_nrm[v * 3 + 1] = cv * su;
            pl_nrm[v * 3 + 2] = sv;
            pl_tc0[v * 2] = (float) i * (4.0f / SU);
            pl_tc0[v * 2 + 1] = (float) j * (1.0f / SV);
            pl_tc1[v * 2] = (float) i * (26.0f / SU);
            pl_tc1[v * 2 + 1] = (float) j * (7.0f / SV);
            for (k = 0; k < (LONG) pl_shells; k++)
            {
                off = (pl_shells > 1) ? FUR * (float) k / (float) (pl_shells - 1) : 0.0f;
                pl_pos[(k * NV + v) * 3] = (RING + TUBE * cv) * cu + off * pl_nrm[v * 3];
                pl_pos[(k * NV + v) * 3 + 1] = (RING + TUBE * cv) * su + off * pl_nrm[v * 3 + 1];
                pl_pos[(k * NV + v) * 3 + 2] = TUBE * sv + off * pl_nrm[v * 3 + 2];
            }
        }
    k = 0;
    for (i = 0; i < SU; i++)
        for (j = 0; j < SV; j++)
        {
            v = i * (SV + 1) + j;
            pl_idx[k++] = (UWORD) v;
            pl_idx[k++] = (UWORD) (v + SV + 1);
            pl_idx[k++] = (UWORD) (v + SV + 2);
            pl_idx[k++] = (UWORD) v;
            pl_idx[k++] = (UWORD) (v + SV + 2);
            pl_idx[k++] = (UWORD) (v + 1);
        }
    /* the coat: tabby stripes round the ring, a paler belly */
    for (j = 0; j < CTEX; j++)
        for (i = 0; i < CTEX; i++)
        {
            stripe = 0.5f + 0.5f * B_SinT(i * 4 * B_TABLE / CTEX + B_Floor(24.0f * B_SinT(j * 6 * B_TABLE / CTEX)));
            belly = 0.5f + 0.5f * B_CosT(j * B_TABLE / CTEX);
            stripe = stripe * stripe * stripe;
            ct[(j * CTEX + i) * 3] = (UBYTE) B_Byte(0.92f - 0.55f * stripe + 0.08f * belly);
            ct[(j * CTEX + i) * 3 + 1] = (UBYTE) B_Byte(0.58f - 0.40f * stripe + 0.25f * belly);
            ct[(j * CTEX + i) * 3 + 2] = (UBYTE) B_Byte(0.30f - 0.22f * stripe + 0.35f * belly);
        }
    /* the strands: a height (alpha) and a little light (luminance) each */
    seed = 20261108UL;
    for (i = 0; i < NTEX * NTEX; i++)
    {
        v = (LONG) (B_Rand(&seed) & 255);
        nt[i * 2] = (UBYTE) (190 + (B_Rand(&seed) % 66));
        nt[i * 2 + 1] = (UBYTE) (255 - ((v * v) >> 8));     /* more short strands than long */
    }
    pl_ctex = B_Texture(GL_RGB, CTEX, CTEX, GL_RGB, ct, TRUE, GL_REPEAT);
    pl_ntex = B_Texture(GL_LUMINANCE_ALPHA, NTEX, NTEX, GL_LUMINANCE_ALPHA, nt, FALSE, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    pl_tex = TRUE;
    B_Free(ct);
    B_Free(nt);
    return TRUE;
}

static void Frame(ULONG f, LONG w, LONG h)
{
    static const GLfloat l0pos[4] = { -0.5f, 0.7f, 0.6f, 0.0f };
    static const GLfloat l0dif[4] = { 1.0f, 0.94f, 0.82f, 1.0f };
    static const GLfloat l1pos[4] = { 0.6f, -0.3f, -0.8f, 0.0f };
    static const GLfloat l1dif[4] = { 0.35f, 0.42f, 0.60f, 1.0f };
    static const GLfloat amb[4] = { 0.22f, 0.20f, 0.20f, 1.0f };
    static GLfloat fogc[4] = { 0.20f, 0.12f, 0.09f, 1.0f };
    ULONG k;
    float t, shade, bx, by;

    glClear(GL_DEPTH_BUFFER_BIT);
    B_Background(0.10f, 0.07f, 0.13f, 0.20f, 0.12f, 0.09f);
    B_Perspective(42.0f, (float) w / (float) h, 1.0f, 20.0f);
    glLoadIdentity();
    glLightfv(GL_LIGHT0, GL_POSITION, l0pos);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, l0dif);
    glLightfv(GL_LIGHT1, GL_POSITION, l1pos);
    glLightfv(GL_LIGHT1, GL_DIFFUSE, l1dif);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_LIGHT1);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, 3.8f);
    glFogf(GL_FOG_END, 8.5f);
    glFogfv(GL_FOG_COLOR, fogc);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    /* a three-quarter view that rocks between 25 and 65 degrees */
    glTranslatef(0.0f, 0.0f, -4.6f);
    glRotatef(45.0f + 20.0f * B_SinT((LONG) f * 3), 1.0f, 0.0f, 0.0f);
    glRotatef((float) f * 0.85f, 0.0f, 0.0f, 1.0f);

    if (bench_rgl)
    {
        glHint(GL_RGL_STATIC_ARRAYS_HINT, GL_NICEST);
        glHint(GL_RGL_STATIC_INDICES_HINT, GL_NICEST);
    }
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glNormalPointer(GL_FLOAT, 0, pl_nrm);
    glClientActiveTextureARB(GL_TEXTURE0_ARB);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, 0, pl_tc0);
    glClientActiveTextureARB(GL_TEXTURE1_ARB);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, 0, pl_tc1);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, pl_ctex);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    /* the breeze: the tips swing further than the roots */
    bx = 0.020f * B_SinT((LONG) f * 7) + 0.008f * B_SinT((LONG) f * 23 + 300);
    by = 0.014f * B_CosT((LONG) f * 5 + 100);

    for (k = 0; k < pl_shells; k++)
    {
        t = (float) k / (float) pl_shells;
        shade = 0.42f + 0.58f * t;
        glColor4f(shade, shade, shade, 1.0f);
        glVertexPointer(3, GL_FLOAT, 0, pl_pos + k * NV * 3);
        if (k == 0)
        {
            glActiveTextureARB(GL_TEXTURE1_ARB);
            glDisable(GL_TEXTURE_2D);
            glActiveTextureARB(GL_TEXTURE0_ARB);
            glDisable(GL_BLEND);
            glDisable(GL_ALPHA_TEST);
            glDepthMask(GL_TRUE);
        }
        else
        {
            if (k == 1)
            {
                glActiveTextureARB(GL_TEXTURE1_ARB);
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, pl_ntex);
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
                glActiveTextureARB(GL_TEXTURE0_ARB);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glEnable(GL_ALPHA_TEST);
                glDepthMask(GL_FALSE);
            }
            glAlphaFunc(GL_GREATER, t);
            glActiveTextureARB(GL_TEXTURE1_ARB);
            glMatrixMode(GL_TEXTURE);
            glLoadIdentity();
            glTranslatef(bx * t * t * 30.0f, by * t * t * 30.0f, 0.0f);
            glMatrixMode(GL_MODELVIEW);
            glActiveTextureARB(GL_TEXTURE0_ARB);
        }
        glLockArraysEXT(0, NV);
        glDrawElements(GL_TRIANGLES, NI, GL_UNSIGNED_SHORT, pl_idx);
        glUnlockArraysEXT();
        bench_prims += NI / 3;
    }
    glDepthMask(GL_TRUE);
    if (bench_rgl)
    {
        glHint(GL_RGL_STATIC_ARRAYS_HINT, GL_DONT_CARE);
        glHint(GL_RGL_STATIC_INDICES_HINT, GL_DONT_CARE);
    }
}

static void Done(void)
{
    if (bench_rgl)
    {
        /* forget the arrays by address before their memory goes */
        glHint(GL_RGL_STATIC_ARRAYS_HINT, GL_FASTEST);
        glHint(GL_RGL_STATIC_INDICES_HINT, GL_FASTEST);
        glHint(GL_RGL_STATIC_ARRAYS_HINT, GL_DONT_CARE);
        glHint(GL_RGL_STATIC_INDICES_HINT, GL_DONT_CARE);
    }
    if (pl_tex)
    {
        glDeleteTextures(1, &pl_ctex);
        glDeleteTextures(1, &pl_ntex);
    }
    pl_tex = FALSE;
    B_Free(pl_pos);
    B_Free(pl_nrm);
    B_Free(pl_tc0);
    B_Free(pl_tc1);
    B_Free(pl_idx);
    pl_pos = pl_nrm = pl_tc0 = pl_tc1 = NULL;
    pl_idx = NULL;
}

const struct BenchScene scene_pelt =
{
    "Pelt", "fill rate: fur shells, alpha test, blending, multitexture", 300, TRUE, Init, Frame, Done
};
