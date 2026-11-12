/*
 * scene_chrome.c -- Chrome: chrome bodies on plinths circling over a
 * striped floor. Texture coordinate generation and display lists.
 *
 * The bodies -- a sphere, a ring and a lathed vase -- are static arrays
 * made once (48 x 32, 64 x 24 and 48 x 9 quads) with 16-bit indices, kept
 * on the card by GL_RGL_static_arrays: the card's TCL does the work. Each
 * stands on a plinth from a display list nested in a stand list: the plinth
 * (glutSolidCube) and a label on its face, which lies in the face's plane
 * and comes through by glPolygonOffset. A fourth body, a GLU sphere, floats
 * above them from a display list of its own. The chrome is a sphere-mapped
 * environment (GL_SPHERE_MAP on S and T) modulated by two lights with a
 * local viewer. The floor: an interleaved array (GL_N3F_V3F) drawn by
 * glArrayElement inside glBegin, its stripes by object-linear generation.
 * The camera orbits with the double-precision
 * matrix calls (glLoadMatrixd, glMultMatrixd, glTranslated, glRotated).
 */

#include <exec/types.h>
#include <proto/exec.h>
#include <proto/minigl.h>

#include "bench.h"

#define ETEX 128
#define STEX 64
#define LTEX 64
#define FG 8                    /* floor quads a side */
#define NF ((FG + 1) * (FG + 1))

static GLuint ch_list;          /* 6 lists from here */
static GLuint ch_etex, ch_stex, ch_ltex;
static float *ch_floor;         /* NF x (3 normal + 3 position) */
static GLUquadricObj *ch_quad;
static BOOL ch_tex;
static BOOL ch_have;

#define L_SPHERE 0              /* the floating GLU sphere */
#define L_PLINTH 1
#define L_LABEL  2
#define L_STAND  3              /* plinth and label: nested lists */
#define LISTS    4

#define MESHES 3                /* sphere, ring, vase */

struct Mesh
{
    float *v;                   /* nv x 3 */
    float *n;
    UWORD *idx;                 /* ni */
    LONG nv, ni;
};

static struct Mesh ch_mesh[MESHES];

static void Done(void);

/* the environment: a sky over a dark horizon and a pale ground, a sun */
static void MakeEnv(UBYTE *t)
{
    LONG x, y;
    float u, v, d, r, g, b, sun;

    for (y = 0; y < ETEX; y++)
        for (x = 0; x < ETEX; x++)
        {
            u = ((float) x + 0.5f) / ETEX * 2.0f - 1.0f;
            v = ((float) y + 0.5f) / ETEX * 2.0f - 1.0f;
            if (v > 0.06f)
            {
                r = 0.45f + 0.50f * v;
                g = 0.55f + 0.40f * v;
                b = 0.80f + 0.20f * v;
            }
            else if (v > -0.04f)
            {
                r = 0.10f;
                g = 0.11f;
                b = 0.14f;
            }
            else
            {
                r = 0.82f + 0.10f * v;
                g = 0.78f + 0.10f * v;
                b = 0.70f + 0.10f * v;
            }
            d = (u + 0.35f) * (u + 0.35f) + (v - 0.55f) * (v - 0.55f);
            sun = (d < 0.02f) ? 1.0f : (d < 0.06f ? (0.06f - d) * 18.0f : 0.0f);
            t[(y * ETEX + x) * 3] = (UBYTE) B_Byte(r + sun);
            t[(y * ETEX + x) * 3 + 1] = (UBYTE) B_Byte(g + sun * 0.9f);
            t[(y * ETEX + x) * 3 + 2] = (UBYTE) B_Byte(b + sun * 0.6f);
        }
}

/* body b's point and normal at (i of nu round the axis, j of nv along it) */
static void BodyPoint(LONG b, LONG i, LONG nu, LONG j, LONG nv, float *p, float *q)
{
    static const float prof[10][2] =
    {
        { 0.00f, -0.70f }, { 0.30f, -0.70f }, { 0.42f, -0.55f }, { 0.50f, -0.30f }, { 0.48f, -0.05f },
        { 0.36f, 0.18f }, { 0.22f, 0.36f }, { 0.18f, 0.52f }, { 0.24f, 0.66f }, { 0.30f, 0.72f }
    };
    float cu, su, cv, sv, y, z, c70, s70, dr, dy;
    LONG j0, j1;

    cu = B_CosT(i * B_TABLE / nu);
    su = B_SinT(i * B_TABLE / nu);
    if (b == 0)
    {
        /* the sphere: j from the top pole to the bottom one */
        cv = B_CosT(j * (B_TABLE / 2) / nv);
        sv = B_SinT(j * (B_TABLE / 2) / nv);
        q[0] = sv * cu;
        q[1] = cv;
        q[2] = sv * su;
        p[0] = 0.62f * q[0];
        p[1] = 0.62f * q[1];
        p[2] = 0.62f * q[2];
    }
    else if (b == 1)
    {
        /* the ring, tipped 70 degrees about x */
        cv = B_CosT(j * B_TABLE / nv);
        sv = B_SinT(j * B_TABLE / nv);
        c70 = 0.342f;
        s70 = 0.940f;
        p[0] = (0.55f + 0.17f * cv) * cu;
        y = 0.17f * sv;
        z = (0.55f + 0.17f * cv) * su;
        p[1] = y * c70 - z * s70;
        p[2] = y * s70 + z * c70;
        q[0] = cv * cu;
        y = sv;
        z = cv * su;
        q[1] = y * c70 - z * s70;
        q[2] = y * s70 + z * c70;
    }
    else
    {
        /* the vase: the profile turned, its slope for the normal */
        j0 = (j > 0) ? j - 1 : j;
        j1 = (j < nv) ? j + 1 : j;
        dr = prof[j1][0] - prof[j0][0];
        dy = prof[j1][1] - prof[j0][1];
        p[0] = prof[j][0] * cu;
        p[1] = prof[j][1];
        p[2] = prof[j][0] * su;
        q[0] = dy * cu;
        q[1] = -dr;
        q[2] = dy * su;
    }
}

static BOOL MakeMesh(struct Mesh *m, LONG b, LONG nu, LONG nv)
{
    LONG i, j, k, v;

    m->nv = (nu + 1) * (nv + 1);
    m->ni = nu * nv * 6;
    m->v = (float *) B_Alloc(m->nv * 3 * sizeof(float));
    m->n = (float *) B_Alloc(m->nv * 3 * sizeof(float));
    m->idx = (UWORD *) B_Alloc(m->ni * sizeof(UWORD));
    if (m->v == NULL || m->n == NULL || m->idx == NULL)
        return FALSE;
    for (i = 0; i <= nu; i++)
        for (j = 0; j <= nv; j++)
        {
            v = i * (nv + 1) + j;
            BodyPoint(b, i, nu, j, nv, &m->v[v * 3], &m->n[v * 3]);
        }
    k = 0;
    for (i = 0; i < nu; i++)
        for (j = 0; j < nv; j++)
        {
            v = i * (nv + 1) + j;
            m->idx[k++] = (UWORD) v;
            m->idx[k++] = (UWORD) (v + nv + 1);
            m->idx[k++] = (UWORD) (v + nv + 2);
            m->idx[k++] = (UWORD) v;
            m->idx[k++] = (UWORD) (v + nv + 2);
            m->idx[k++] = (UWORD) (v + 1);
        }
    return TRUE;
}

static void FreeMesh(struct Mesh *m)
{
    B_Free(m->v);
    B_Free(m->n);
    B_Free(m->idx);
    m->v = m->n = NULL;
    m->idx = NULL;
}

static void DrawMesh(const struct Mesh *m)
{
    /* locked: RadeonGL keeps only locked element draws on the card */
    glVertexPointer(3, GL_FLOAT, 0, m->v);
    glNormalPointer(GL_FLOAT, 0, m->n);
    glLockArraysEXT(0, m->nv);
    glDrawElements(GL_TRIANGLES, m->ni, GL_UNSIGNED_SHORT, m->idx);
    glUnlockArraysEXT();
    bench_prims += (ULONG) (m->ni / 3);
}

static BOOL Init(LONG w, LONG h)
{
    UBYTE *et;
    UBYTE *st;
    UBYTE *lt;
    LONG i, j, v;
    float x, z;
    static const GLfloat grey[4] = { 0.55f, 0.56f, 0.60f, 1.0f };

    (void) w;
    (void) h;
    et = (UBYTE *) B_Alloc(ETEX * ETEX * 3);
    st = (UBYTE *) B_Alloc(STEX * 4 * 3);
    lt = (UBYTE *) B_Alloc(LTEX * LTEX * 4);
    ch_floor = (float *) B_Alloc(NF * 6 * sizeof(float));
    ch_quad = gluNewQuadric();
    if (et == NULL || st == NULL || lt == NULL || ch_floor == NULL || ch_quad == NULL)
    {
        B_Free(et);
        B_Free(st);
        B_Free(lt);
        Done();
        return FALSE;
    }
    MakeEnv(et);
    /* floor stripes: dark and pale, soft edges */
    for (i = 0; i < STEX; i++)
        for (j = 0; j < 4; j++)
        {
            x = B_SinT(i * B_TABLE / STEX);
            x = (x > 0.15f) ? 1.0f : (x < -0.15f ? 0.0f : (x + 0.15f) / 0.3f);
            st[(j * STEX + i) * 3] = (UBYTE) B_Byte(0.22f + 0.62f * x);
            st[(j * STEX + i) * 3 + 1] = (UBYTE) B_Byte(0.22f + 0.58f * x);
            st[(j * STEX + i) * 3 + 2] = (UBYTE) B_Byte(0.26f + 0.50f * x);
        }
    /* the label: a red plate, a white diamond chain, a clear border */
    for (j = 0; j < LTEX; j++)
        for (i = 0; i < LTEX; i++)
        {
            v = (j * LTEX + i) * 4;
            x = (float) ((i % 16) - 8);
            z = (float) (j - LTEX / 2);
            lt[v] = 178;
            lt[v + 1] = 65;
            lt[v + 2] = 42;
            if ((x < 0.0f ? -x : x) + (z < 0.0f ? -z : z) * 0.5f < 6.0f)
                lt[v] = lt[v + 1] = lt[v + 2] = 240;
            lt[v + 3] = (UBYTE) ((i < 3 || i >= LTEX - 3 || j < 10 || j >= LTEX - 10) ? 0 : 255);
        }
    ch_etex = B_Texture(GL_RGB, ETEX, ETEX, GL_RGB, et, TRUE, GL_CLAMP);
    ch_stex = B_Texture(GL_RGB, STEX, 4, GL_RGB, st, FALSE, GL_REPEAT);
    ch_ltex = B_Texture(GL_RGBA, LTEX, LTEX, GL_RGBA, lt, FALSE, GL_CLAMP);
    ch_tex = TRUE;
    B_Free(et);
    B_Free(st);
    B_Free(lt);
    /* the floor's interleaved array: normal, then position */
    for (j = 0; j <= FG; j++)
        for (i = 0; i <= FG; i++)
        {
            v = (j * (FG + 1) + i) * 6;
            ch_floor[v] = 0.0f;
            ch_floor[v + 1] = 1.0f;
            ch_floor[v + 2] = 0.0f;
            ch_floor[v + 3] = ((float) i / FG - 0.5f) * 12.0f;
            ch_floor[v + 4] = -0.95f;
            ch_floor[v + 5] = ((float) j / FG - 0.5f) * 12.0f;
        }

    if (!MakeMesh(&ch_mesh[0], 0, 48, 32) || !MakeMesh(&ch_mesh[1], 1, 64, 24) || !MakeMesh(&ch_mesh[2], 2, 48, 9))
    {
        Done();
        return FALSE;
    }
    gluQuadricNormals(ch_quad, GLU_SMOOTH);
    ch_list = glGenLists(LISTS);
    if (ch_list == 0)
    {
        B_Say("GLBench: Chrome: no display lists\n", 0, 0, 0, 0);
        Done();
        return FALSE;
    }
    ch_have = TRUE;
    /* the floating body: a GLU sphere, chrome set round the call */
    glNewList(ch_list + L_SPHERE, GL_COMPILE);
    gluSphere(ch_quad, 0.62, 28, 18);
    glEndList();
    /* the plinth: a grey box, no texture */
    glNewList(ch_list + L_PLINTH, GL_COMPILE);
    glDisable(GL_TEXTURE_2D);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, grey);
    glPushMatrix();
    glTranslatef(0.0f, -0.80f, 0.0f);
    glScalef(0.9f, 0.3f, 0.9f);
    glutSolidCube(1.0);
    glPopMatrix();
    glEndList();
    /* the label on the plinth's front face, in its plane: polygon offset */
    glNewList(ch_list + L_LABEL, GL_COMPILE);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, ch_ltex);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -2.0f);
    glBegin(GL_QUADS);
    glNormal3f(0.0f, 0.0f, 1.0f);
    glTexCoord2f(0.0f, 0.0f);
    glVertex3f(-0.36f, -0.92f, 0.45f);
    glTexCoord2f(1.0f, 0.0f);
    glVertex3f(0.36f, -0.92f, 0.45f);
    glTexCoord2f(1.0f, 1.0f);
    glVertex3f(0.36f, -0.68f, 0.45f);
    glTexCoord2f(0.0f, 1.0f);
    glVertex3f(-0.36f, -0.68f, 0.45f);
    glEnd();
    glDisable(GL_POLYGON_OFFSET_FILL);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glEndList();
    /* the stand: plinth and label -- nested lists */
    glNewList(ch_list + L_STAND, GL_COMPILE);
    glCallList(ch_list + L_PLINTH);
    glCallList(ch_list + L_LABEL);
    glEndList();
    return TRUE;
}

static void Frame(ULONG f, LONG w, LONG h)
{
    static const GLfloat white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    static const GLfloat spec[4] = { 0.9f, 0.9f, 0.9f, 1.0f };
    static const GLfloat grey[4] = { 0.55f, 0.56f, 0.60f, 1.0f };
    static const GLfloat l0pos[4] = { -3.0f, 4.0f, 3.0f, 1.0f };
    static const GLfloat l1pos[4] = { 3.0f, 1.5f, -2.0f, 1.0f };
    static const GLfloat l0dif[4] = { 1.0f, 0.97f, 0.90f, 1.0f };
    static const GLfloat l1dif[4] = { 0.40f, 0.48f, 0.65f, 1.0f };
    static const GLfloat amb[4] = { 0.25f, 0.25f, 0.28f, 1.0f };
    static const GLfloat floormat[4] = { 0.9f, 0.9f, 0.9f, 1.0f };
    static const GLfloat splane[4] = { 0.35f, 0.0f, 0.35f, 0.0f };
    GLdouble m[16];
    LONG i, j, k;
    float bob;

    glClear(GL_DEPTH_BUFFER_BIT);
    B_Background(0.16f, 0.22f, 0.31f, 0.52f, 0.57f, 0.66f);
    B_Perspective(45.0f, (float) w / (float) h, 1.0f, 30.0f);
    /* the camera: a double matrix loaded, then turned and moved */
    for (k = 0; k < 16; k++)
        m[k] = (k % 5 == 0) ? 1.0 : 0.0;
    m[13] = -0.35;
    glLoadMatrixd(m);
    glTranslated(0.0, 0.0, -6.4);
    glRotated(17.0 + 5.0 * (double) B_SinT((LONG) f * 3), 1.0, 0.0, 0.0);
    glRotated((double) f * 0.45, 0.0, 1.0, 0.0);
    glLightfv(GL_LIGHT0, GL_POSITION, l0pos);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, l0dif);
    glLightfv(GL_LIGHT0, GL_SPECULAR, l0dif);
    glLightfv(GL_LIGHT1, GL_POSITION, l1pos);
    glLightfv(GL_LIGHT1, GL_DIFFUSE, l1dif);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);
    glLightModelf(GL_LIGHT_MODEL_LOCAL_VIEWER, 1.0f);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_LIGHT1);
    glEnable(GL_NORMALIZE);
    glEnable(GL_DEPTH_TEST);

    /* the floor: interleaved normals and positions, element by element */
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, floormat);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, ch_stex);
    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR);
    glTexGenfv(GL_S, GL_OBJECT_PLANE, splane);
    glEnable(GL_TEXTURE_GEN_S);
    glTexCoord2f(0.0f, 0.5f);
    glInterleavedArrays(GL_N3F_V3F, 0, ch_floor);
    glBegin(GL_QUADS);
    for (j = 0; j < FG; j++)
        for (i = 0; i < FG; i++)
        {
            k = j * (FG + 1) + i;
            glArrayElement(k);
            glArrayElement(k + FG + 1);
            glArrayElement(k + FG + 2);
            glArrayElement(k + 1);
        }
    glEnd();
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisable(GL_TEXTURE_GEN_S);
    bench_prims += FG * FG * 2;

    /* the three stands round the middle, each body turning on its own: the
     * stand from its lists, the body from its static arrays in chrome */
    if (bench_rgl)
    {
        glHint(GL_RGL_STATIC_ARRAYS_HINT, GL_NICEST);
        glHint(GL_RGL_STATIC_INDICES_HINT, GL_NICEST);
    }
    for (k = 0; k < 3; k++)
    {
        glPushMatrix();
        glRotated((double) (k * 120) + (double) f * 0.30, 0.0, 1.0, 0.0);
        glTranslated(1.9, 0.0, 0.0);
        glRotated((double) f * (1.1 + 0.4 * (double) k), 0.0, 1.0, 0.0);
        glCallList(ch_list + L_STAND);
        bench_prims += 12 + 2;
        glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, white);
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, spec);
        glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 48.0f);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, ch_etex);
        glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
        glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
        glEnable(GL_TEXTURE_GEN_S);
        glEnable(GL_TEXTURE_GEN_T);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_NORMAL_ARRAY);
        glTranslatef(0.0f, 0.12f, 0.0f);
        DrawMesh(&ch_mesh[k]);
        glDisableClientState(GL_NORMAL_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        glDisable(GL_TEXTURE_GEN_S);
        glDisable(GL_TEXTURE_GEN_T);
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, grey);
        glPopMatrix();
    }
    if (bench_rgl)
    {
        glHint(GL_RGL_STATIC_ARRAYS_HINT, GL_DONT_CARE);
        glHint(GL_RGL_STATIC_INDICES_HINT, GL_DONT_CARE);
    }
    /* and a fourth body floating above them, the sphere alone */
    bob = 0.25f * B_SinT((LONG) f * 5);
    glPushMatrix();
    for (k = 0; k < 16; k++)
        m[k] = (k % 5 == 0) ? 0.55 : 0.0;
    m[15] = 1.0;
    m[13] = 0.9 + (double) bob;
    glMultMatrixd(m);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, ch_etex);
    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
    glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
    glEnable(GL_TEXTURE_GEN_S);
    glEnable(GL_TEXTURE_GEN_T);
    glCallList(ch_list + L_SPHERE);
    glDisable(GL_TEXTURE_GEN_S);
    glDisable(GL_TEXTURE_GEN_T);
    glPopMatrix();
    bench_prims += 28 * 18 * 2;
}

static void Done(void)
{
    LONG k;

    if (bench_rgl)
    {
        glHint(GL_RGL_STATIC_ARRAYS_HINT, GL_FASTEST);
        glHint(GL_RGL_STATIC_INDICES_HINT, GL_FASTEST);
        glHint(GL_RGL_STATIC_ARRAYS_HINT, GL_DONT_CARE);
        glHint(GL_RGL_STATIC_INDICES_HINT, GL_DONT_CARE);
    }
    for (k = 0; k < MESHES; k++)
        FreeMesh(&ch_mesh[k]);
    if (ch_have)
        glDeleteLists(ch_list, LISTS);
    ch_have = FALSE;
    if (ch_tex)
    {
        glDeleteTextures(1, &ch_etex);
        glDeleteTextures(1, &ch_stex);
        glDeleteTextures(1, &ch_ltex);
    }
    ch_tex = FALSE;
    if (ch_quad != NULL)
        gluDeleteQuadric(ch_quad);
    ch_quad = NULL;
    B_Free(ch_floor);
    ch_floor = NULL;
}

const struct BenchScene scene_chrome =
{
    "Chrome", "sphere maps on static bodies, texgen, nested display lists, polygon offset", 300, TRUE, Init, Frame, Done
};
