/*
 * scene_spinner.c -- Spinner: a textured torus turning about three axes in
 * front of a colour gradient.
 *
 * Exercises the immediate path: glBegin(GL_QUAD_STRIP) per segment, a
 * texture coordinate and a colour for every vertex (Gouraud shading, the
 * colours lit per vertex on the CPU against a light that stays put while
 * the torus turns), the depth test, back-face culling, a repeating
 * RGB texture with linear filtering, and the matrix stack (glFrustum,
 * glTranslatef, three glRotatef).
 */

#include <exec/types.h>
#include <proto/exec.h>
#include <proto/minigl.h>

#include "demo.h"

#define SU 28                   /* segments around the ring */
#define SV 14                   /* segments around the tube */
#define RING 1.0f
#define TUBE 0.42f
#define TEX 64

static float sp_pos[SU + 1][SV + 1][3];
static float sp_nrm[SU + 1][SV + 1][3];
static float sp_st[SU + 1][SV + 1][2];
static UBYTE sp_col[SU + 1][3];        /* the base colour along the ring */
static UBYTE sp_tex[TEX * TEX * 3];
static GLuint sp_name;
static BOOL sp_have;

/* Tiles with dark joints: two shades of warm grey in a staggered bond. */
static void MakeTexture(void)
{
    ULONG x;
    ULONG y;
    ULONG xs;
    ULONG v;
    UBYTE *p;

    for (y = 0; y < TEX; y++)
        for (x = 0; x < TEX; x++)
        {
            p = &sp_tex[(y * TEX + x) * 3];
            xs = (x + (((y >> 4) & 1) ? 8 : 0)) & (TEX - 1);
            if ((y & 15) == 0 || (xs & 15) == 0)
                v = 70;
            else
                v = ((((xs >> 4) + (y >> 4)) & 1) ? 235 : 200) - ((x * 7 + y * 13) & 15);
            p[0] = (UBYTE) v;
            p[1] = (UBYTE) (v * 15 / 16);
            p[2] = (UBYTE) (v * 13 / 16);
        }
}

static BOOL Init(void)
{
    ULONG i;
    ULONG j;
    float u;
    float v;
    float cu;
    float su;
    float cv;
    float sv;

    for (i = 0; i <= SU; i++)
    {
        u = (float) i * (2.0f * DEMO_PI / (float) SU);
        cu = Demo_Cos(u);
        su = Demo_Sin(u);
        sp_col[i][0] = (UBYTE) Demo_Byte(0.6f + 0.4f * Demo_Sin(u));
        sp_col[i][1] = (UBYTE) Demo_Byte(0.6f + 0.4f * Demo_Sin(u + 2.1f));
        sp_col[i][2] = (UBYTE) Demo_Byte(0.6f + 0.4f * Demo_Sin(u + 4.2f));
        for (j = 0; j <= SV; j++)
        {
            v = (float) j * (2.0f * DEMO_PI / (float) SV);
            cv = Demo_Cos(v);
            sv = Demo_Sin(v);
            sp_pos[i][j][0] = (RING + TUBE * cv) * cu;
            sp_pos[i][j][1] = (RING + TUBE * cv) * su;
            sp_pos[i][j][2] = TUBE * sv;
            sp_nrm[i][j][0] = cv * cu;
            sp_nrm[i][j][1] = cv * su;
            sp_nrm[i][j][2] = sv;
            sp_st[i][j][0] = (float) i * (6.0f / (float) SU);
            sp_st[i][j][1] = (float) j * (2.0f / (float) SV);
        }
    }
    MakeTexture();
    glGenTextures(1, &sp_name);
    glBindTexture(GL_TEXTURE_2D, sp_name);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, TEX, TEX, 0, GL_RGB, GL_UNSIGNED_BYTE, sp_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    sp_have = TRUE;
    return TRUE;
}

static void Vertex(ULONG i, ULONG j, const float *light)
{
    const float *n;
    float d;
    float k;

    n = sp_nrm[i][j];
    d = n[0] * light[0] + n[1] * light[1] + n[2] * light[2];
    if (d < 0.0f)
        d = 0.0f;
    k = 0.28f + 0.72f * d + 0.35f * d * d * d * d;      /* ambient, diffuse, a gloss */
    glColor4ub((GLubyte) Demo_Byte((float) sp_col[i][0] * (1.0f / 255.0f) * k),
               (GLubyte) Demo_Byte((float) sp_col[i][1] * (1.0f / 255.0f) * k),
               (GLubyte) Demo_Byte((float) sp_col[i][2] * (1.0f / 255.0f) * k), 255);
    glTexCoord2f(sp_st[i][j][0], sp_st[i][j][1]);
    glVertex3f(sp_pos[i][j][0], sp_pos[i][j][1], sp_pos[i][j][2]);
}

static void Frame(float t, LONG w, LONG h)
{
    float ax;
    float ay;
    float az;
    float light[3];
    ULONG i;
    ULONG j;

    glClear(GL_DEPTH_BUFFER_BIT);
    Demo_Background(0.05f, 0.08f, 0.22f, 0.35f, 0.20f, 0.30f);
    Demo_Perspective(45.0f, (float) w / (float) h, 0.5f, 20.0f);
    ax = t * 37.0f;
    ay = t * 53.0f;
    az = t * 21.0f;
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -4.0f);
    glRotatef(ax, 1.0f, 0.0f, 0.0f);
    glRotatef(ay, 0.0f, 1.0f, 0.0f);
    glRotatef(az, 0.0f, 0.0f, 1.0f);

    /* the light, fixed in view space, into the torus's own space: the
     * rotations undone in reverse order */
    light[0] = 0.48f;
    light[1] = 0.60f;
    light[2] = 0.64f;
    Demo_Rotate(light, 0, -ax);
    Demo_Rotate(light, 1, -ay);
    Demo_Rotate(light, 2, -az);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, sp_name);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    for (i = 0; i < SU; i++)
    {
        /* (i, j), (i + 1, j) ...: each quad runs +u then +v, counter-
         * clockwise seen from outside (the normal is du x dv) */
        glBegin(GL_QUAD_STRIP);
        for (j = 0; j <= SV; j++)
        {
            Vertex(i, j, light);
            Vertex(i + 1, j, light);
        }
        glEnd();
    }
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
}

static void Done(void)
{
    if (sp_have)
        glDeleteTextures(1, &sp_name);
    sp_have = FALSE;
}

const struct Scene scene_spinner = { "Spinner", Init, Frame, Done };
