/*
 * scene_swarm.c -- Swarm: a flock of small faceted darts round a few
 * cubes and dodecahedra. Geometry throughput on the card.
 *
 * The darts (1920, four flat faces each) never change: eight groups of 240,
 * each a static array of positions and normals made once, kept on the card
 * by GL_RGL_static_arrays. Every frame a group only gets a matrix (it turns
 * round the flock's centre at its own pace) and a colour (colour material),
 * and is drawn locked (glLockArraysEXT) by glDrawElements with 16-bit
 * indices -- RadeonGL keeps only locked element draws on the card; a
 * glDrawArrays sends every vertex through the 68k. The card transforms and
 * lights 23,040 vertices a frame, the 68k sends a few dozen packets. Flat
 * shaded. The hexagon markers come from one glMultiDrawArrays. The cubes are one static array drawn by glDrawElements with
 * 8-bit indices as GL_QUADS, each under its own matrix; the dodecahedra
 * come from GLUT. Around them every other primitive: the ground in quad
 * strips, a sun as a triangle fan, a ribbon behind the flock as a triangle
 * strip, hexagon markers as polygons. Exp fog. The last third of the frames
 * is drawn twice, through red and through cyan colour masks with the eye
 * moved, for red/cyan glasses.
 */

#include <exec/types.h>
#include <proto/exec.h>
#include <proto/minigl.h>

#include "bench.h"

#define GROUPS 8
#define PER 240                 /* darts a group */
#define DV 12                   /* vertices a dart */
#define NDV (PER * DV)          /* vertices a group */
#define HEXES 4
#define CUBES 48
#define QUEENS 6
#define RIBBON 32

static float *sw_pos;           /* GROUPS x PER x DV x 3 */
static float *sw_nrm;
static UWORD sw_idx[NDV];       /* 0, 1, 2 ...: a group's draw, the same for all */
static float sw_hex[HEXES * 6 * 3];
static GLint sw_first[HEXES];
static GLsizei sw_count[HEXES];
static float sw_cubev[24 * 3];
static float sw_cuben[24 * 3];
static UBYTE sw_cubei[24];

static const float sw_gcol[GROUPS][3] =
{
    { 0.95f, 0.55f, 0.25f }, { 0.25f, 0.60f, 0.95f }, { 0.95f, 0.80f, 0.30f }, { 0.35f, 0.85f, 0.55f },
    { 0.90f, 0.40f, 0.60f }, { 0.55f, 0.50f, 0.95f }, { 0.95f, 0.65f, 0.45f }, { 0.40f, 0.80f, 0.90f }
};

static void Done(void);

static void Sub(const float *a, const float *b, float *o)
{
    o[0] = a[0] - b[0];
    o[1] = a[1] - b[1];
    o[2] = a[2] - b[2];
}

static BOOL Init(LONG w, LONG h)
{
    static const float tip[4][3] =
    {
        { 0.13f, 0.0f, 0.0f }, { -0.08f, 0.045f, 0.0f }, { -0.08f, -0.03f, 0.05f }, { -0.08f, -0.03f, -0.05f }
    };
    static const UBYTE face[4][3] = { { 0, 1, 2 }, { 0, 3, 1 }, { 0, 2, 3 }, { 1, 3, 2 } };
    static const float cn[6][3] =
    {
        { 0, 0, 1 }, { 0, 0, -1 }, { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }
    };
    static const BYTE cv[6][4][3] =
    {
        { { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 } },
        { { 1, -1, -1 }, { -1, -1, -1 }, { -1, 1, -1 }, { 1, 1, -1 } },
        { { 1, -1, 1 }, { 1, -1, -1 }, { 1, 1, -1 }, { 1, 1, 1 } },
        { { -1, -1, -1 }, { -1, -1, 1 }, { -1, 1, 1 }, { -1, 1, -1 } },
        { { -1, 1, 1 }, { 1, 1, 1 }, { 1, 1, -1 }, { -1, 1, -1 } },
        { { -1, -1, -1 }, { 1, -1, -1 }, { 1, -1, 1 }, { -1, -1, 1 } }
    };
    float bodyv[DV * 3], bodyn[DV * 3];
    float e1[3], e2[3];
    float r, hgt, cy, sy, px, py, pz;
    float *o, *n;
    ULONG seed;
    LONG g, i, k, v, th;

    (void) w;
    (void) h;
    sw_pos = (float *) B_Alloc(GROUPS * PER * DV * 3 * sizeof(float));
    sw_nrm = (float *) B_Alloc(GROUPS * PER * DV * 3 * sizeof(float));
    if (sw_pos == NULL || sw_nrm == NULL)
    {
        Done();
        return FALSE;
    }
    /* one dart: four faces, a normal each (cross product of two edges) */
    for (k = 0; k < 4; k++)
    {
        Sub(tip[face[k][1]], tip[face[k][0]], e1);
        Sub(tip[face[k][2]], tip[face[k][0]], e2);
        for (i = 0; i < 3; i++)
        {
            v = k * 3 + i;
            bodyv[v * 3] = tip[face[k][i]][0];
            bodyv[v * 3 + 1] = tip[face[k][i]][1];
            bodyv[v * 3 + 2] = tip[face[k][i]][2];
            bodyn[v * 3] = e1[1] * e2[2] - e1[2] * e2[1];
            bodyn[v * 3 + 1] = e1[2] * e2[0] - e1[0] * e2[2];
            bodyn[v * 3 + 2] = e1[0] * e2[1] - e1[1] * e2[0];
        }
    }
    /* the groups: each dart at its place on a ring round the group's axis,
     * heading along the ring (the group's turn moves it forward) */
    seed = 1985UL;
    o = sw_pos;
    n = sw_nrm;
    for (g = 0; g < GROUPS; g++)
        for (i = 0; i < PER; i++)
        {
            th = (LONG) (B_Rand(&seed) % B_TABLE);
            r = 0.35f + (float) (B_Rand(&seed) % 1000) * 0.0014f;
            hgt = ((float) (B_Rand(&seed) % 1000) - 500.0f) * 0.0012f;
            px = r * B_CosT(th);
            py = hgt + 0.18f * B_SinT(th * 3);
            pz = r * B_SinT(th);
            cy = B_CosT(th + B_TABLE / 4);
            sy = B_SinT(th + B_TABLE / 4);
            for (k = 0; k < DV; k++)
            {
                o[0] = px + cy * bodyv[k * 3] - sy * bodyv[k * 3 + 2];
                o[1] = py + bodyv[k * 3 + 1];
                o[2] = pz + sy * bodyv[k * 3] + cy * bodyv[k * 3 + 2];
                n[0] = cy * bodyn[k * 3] - sy * bodyn[k * 3 + 2];
                n[1] = bodyn[k * 3 + 1];
                n[2] = sy * bodyn[k * 3] + cy * bodyn[k * 3 + 2];
                o += 3;
                n += 3;
            }
        }
    for (i = 0; i < NDV; i++)
        sw_idx[i] = (UWORD) i;
    for (i = 0; i < HEXES; i++)
    {
        sw_first[i] = (GLint) (i * 6);
        sw_count[i] = 6;
    }
    for (k = 0; k < 6; k++)
        for (i = 0; i < 4; i++)
        {
            v = k * 4 + i;
            sw_cubev[v * 3] = (float) cv[k][i][0];
            sw_cubev[v * 3 + 1] = (float) cv[k][i][1];
            sw_cubev[v * 3 + 2] = (float) cv[k][i][2];
            sw_cuben[v * 3] = cn[k][0];
            sw_cuben[v * 3 + 1] = cn[k][1];
            sw_cuben[v * 3 + 2] = cn[k][2];
            sw_cubei[v] = (UBYTE) v;
        }
    return TRUE;
}

static void Centre(LONG f, float *c)
{
    c[0] = 1.6f * B_SinT(f * 2);
    c[1] = 0.35f * B_SinT(f * 3 + 100);
    c[2] = -8.2f + 1.2f * B_CosT(f * 2);
}

static void World(ULONG f, float eye)
{
    static const GLfloat lpos[4] = { -0.4f, 0.8f, 0.5f, 0.0f };
    static const GLfloat ldif[4] = { 1.0f, 0.96f, 0.90f, 1.0f };
    static const GLfloat amb[4] = { 0.30f, 0.30f, 0.34f, 1.0f };
    float c[3];
    float x, a;
    LONG i, j, k;

    glLoadIdentity();
    glTranslatef(-eye, -0.2f, 0.0f);
    glRotatef(8.0f, 1.0f, 0.0f, 0.0f);
    glLightfv(GL_LIGHT0, GL_POSITION, lpos);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, ldif);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);

    /* the sun: a fan, unlit, far back */
    glDisable(GL_LIGHTING);
    glBegin(GL_TRIANGLE_FAN);
    glColor4f(1.0f, 0.95f, 0.75f, 1.0f);
    glVertex3f(3.0f, 2.6f, -16.0f);
    glColor4f(1.0f, 0.55f, 0.25f, 1.0f);
    for (i = 0; i <= 24; i++)
        glVertex3f(3.0f + 1.8f * B_CosT(i * B_TABLE / 24), 2.6f + 1.8f * B_SinT(i * B_TABLE / 24), -16.0f);
    glEnd();
    bench_prims += 24;
    /* the ground: quad strips, rows of alternating colours */
    for (j = 0; j < 10; j++)
    {
        glBegin(GL_QUAD_STRIP);
        if (j & 1)
            glColor4f(0.42f, 0.52f, 0.40f, 1.0f);
        else
            glColor4f(0.36f, 0.45f, 0.34f, 1.0f);
        for (i = 0; i <= 20; i++)
        {
            x = ((float) i / 20.0f - 0.5f) * 16.0f;
            glVertex3f(x, -2.2f, -2.0f - (float) j * 1.4f);
            glVertex3f(x, -2.2f, -3.4f - (float) j * 1.4f);
        }
        glEnd();
    }
    bench_prims += 10 * 20 * 2;
    /* the ribbon behind the flock's centre: a triangle strip */
    glBegin(GL_TRIANGLE_STRIP);
    for (k = 0; k < RIBBON; k++)
    {
        Centre((LONG) f - k * 3, c);
        a = 1.0f - (float) k / (float) RIBBON;
        glColor4f(0.9f * a + 0.1f, 0.8f * a + 0.15f, 0.3f * a + 0.3f, 1.0f);
        glVertex3f(c[0], c[1] - 0.05f, c[2]);
        glVertex3f(c[0], c[1] + 0.05f, c[2]);
    }
    glEnd();
    bench_prims += RIBBON * 2 - 2;
    /* four hexagon markers: polygons, all from one glMultiDrawArrays */
    for (k = 0; k < HEXES; k++)
        for (i = 0; i < 6; i++)
        {
            sw_hex[(k * 6 + i) * 3] = -3.0f + (float) k * 2.0f + 0.22f * B_CosT(i * B_TABLE / 6 + (LONG) f * 4);
            sw_hex[(k * 6 + i) * 3 + 1] = 1.7f + 0.22f * B_SinT(i * B_TABLE / 6 + (LONG) f * 4);
            sw_hex[(k * 6 + i) * 3 + 2] = -8.0f;
        }
    glColor4f(0.95f, 0.85f, 0.30f, 1.0f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, sw_hex);
    glMultiDrawArrays(GL_POLYGON, sw_first, sw_count, HEXES);
    glDisableClientState(GL_VERTEX_ARRAY);
    bench_prims += HEXES * 4;

    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_CULL_FACE);     /* the closed bodies only: back faces away */

    /* the darts: eight static groups, a matrix and a colour each, locked
     * element draws (kept on the card) */
    if (bench_rgl)
    {
        glHint(GL_RGL_STATIC_ARRAYS_HINT, GL_NICEST);
        glHint(GL_RGL_STATIC_INDICES_HINT, GL_NICEST);
    }
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    Centre((LONG) f, c);
    for (k = 0; k < GROUPS; k++)
    {
        glPushMatrix();
        glTranslatef(c[0], c[1], c[2]);
        glRotatef(8.0f * B_SinT((LONG) f * 2 + k * 128), 1.0f, 0.0f, 0.0f);
        glRotatef((float) f * (1.4f + 0.35f * (float) k) + (float) (k * 45), 0.0f, 1.0f, 0.0f);
        glColor4f(sw_gcol[k][0], sw_gcol[k][1], sw_gcol[k][2], 1.0f);
        glVertexPointer(3, GL_FLOAT, 0, sw_pos + k * NDV * 3);
        glNormalPointer(GL_FLOAT, 0, sw_nrm + k * NDV * 3);
        glLockArraysEXT(0, NDV);
        glDrawElements(GL_TRIANGLES, NDV, GL_UNSIGNED_SHORT, sw_idx);
        glUnlockArraysEXT();
        glPopMatrix();
    }
    bench_prims += GROUPS * PER * 4;

    /* the cubes: a static array, 8-bit indices, a matrix each */
    glVertexPointer(3, GL_FLOAT, 0, sw_cubev);
    glNormalPointer(GL_FLOAT, 0, sw_cuben);
    for (k = 0; k < CUBES; k++)
    {
        glPushMatrix();
        glTranslatef(c[0] + 2.4f * B_CosT(k * B_TABLE / CUBES + (LONG) f * 2),
                     c[1] + 0.6f * B_SinT(k * 3 * B_TABLE / CUBES + (LONG) f * 3),
                     c[2] + 2.4f * B_SinT(k * B_TABLE / CUBES + (LONG) f * 2));
        glRotatef((float) (f * 3 + k * 37), 0.3f, 1.0f, 0.2f);
        glScalef(0.09f, 0.09f, 0.09f);
        glColor4f(0.85f, 0.85f, 0.90f, 1.0f);
        glDrawElements(GL_QUADS, 24, GL_UNSIGNED_BYTE, sw_cubei);
        glPopMatrix();
    }
    bench_prims += CUBES * 12;
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    if (bench_rgl)
    {
        glHint(GL_RGL_STATIC_ARRAYS_HINT, GL_DONT_CARE);
        glHint(GL_RGL_STATIC_INDICES_HINT, GL_DONT_CARE);
    }

    /* the queens: GLUT's dodecahedra */
    for (k = 0; k < QUEENS; k++)
    {
        glPushMatrix();
        glTranslatef(c[0] + 1.1f * B_CosT(k * B_TABLE / QUEENS - (LONG) f * 3), c[1] + 0.9f,
                     c[2] + 1.1f * B_SinT(k * B_TABLE / QUEENS - (LONG) f * 3));
        glRotatef((float) (f * 2 + k * 60), 1.0f, 1.0f, 0.0f);
        glScalef(0.13f, 0.13f, 0.13f);
        glColor4f(0.95f, 0.45f, 0.35f, 1.0f);
        glutSolidDodecahedron();
        glPopMatrix();
    }
    bench_prims += QUEENS * 36;
    glDisable(GL_CULL_FACE);
    glDisable(GL_COLOR_MATERIAL);
    glDisable(GL_LIGHTING);
}

static void Frame(ULONG f, LONG w, LONG h)
{
    static GLfloat fogc[4] = { 0.62f, 0.70f, 0.78f, 1.0f };
    BOOL glasses;

    glasses = (bench_frames != 0) ? (f >= bench_frames * 2 / 3) : ((f % 600) >= 400);
    glClear(GL_DEPTH_BUFFER_BIT);
    if (glasses)
        B_Background(0.55f, 0.55f, 0.55f, 0.80f, 0.80f, 0.80f);
    else
        B_Background(0.50f, 0.60f, 0.72f, 0.82f, 0.86f, 0.90f);
    B_Perspective(50.0f, (float) w / (float) h, 0.5f, 30.0f);
    glEnable(GL_DEPTH_TEST);
    glShadeModel(GL_FLAT);
    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, GL_EXP);
    glFogf(GL_FOG_DENSITY, 0.07f);
    glFogfv(GL_FOG_COLOR, fogc);
    if (!glasses)
        World(f, 0.0f);
    else
    {
        glColorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_TRUE);
        World(f, -0.07f);
        glClear(GL_DEPTH_BUFFER_BIT);
        glColorMask(GL_FALSE, GL_TRUE, GL_TRUE, GL_TRUE);
        World(f, 0.07f);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }
    glShadeModel(GL_SMOOTH);
}

static void Done(void)
{
    if (bench_rgl)
    {
        glHint(GL_RGL_STATIC_ARRAYS_HINT, GL_FASTEST);
        glHint(GL_RGL_STATIC_INDICES_HINT, GL_FASTEST);
        glHint(GL_RGL_STATIC_ARRAYS_HINT, GL_DONT_CARE);
        glHint(GL_RGL_STATIC_INDICES_HINT, GL_DONT_CARE);
    }
    B_Free(sw_pos);
    B_Free(sw_nrm);
    sw_pos = sw_nrm = NULL;
}

const struct BenchScene scene_swarm =
{
    "Swarm", "geometry on the card: static dart groups, every primitive, colour masks", 300, TRUE, Init, Frame, Done
};
