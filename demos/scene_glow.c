/*
 * scene_glow.c -- Glow: sparks with comet tails circling inside a slowly
 * turning crystal of tinted glass panes.
 *
 * Exercises blending: the panes alpha-blended (GL_SRC_ALPHA,
 * GL_ONE_MINUS_SRC_ALPHA, the colour's alpha), the sparks added
 * (GL_ONE, GL_ONE) as camera-facing quads with a luminance texture
 * (I8 on the card) modulated by each spark's colour; depth writes off.
 * The sparks are placed in view space on the CPU, so the modelview is the
 * identity for them -- many small quads in one glBegin(GL_QUADS).
 */

#include <exec/types.h>
#include <proto/exec.h>
#include <proto/minigl.h>

#include "demo.h"

#define SPARKS 96
#define TAIL 4                  /* the spark and three ghosts behind it */
#define TEX 32
#define PANES 6

static UBYTE gw_tex[TEX * TEX];
static GLuint gw_name;
static BOOL gw_have;

static BOOL Init(void)
{
    LONG x;
    LONG y;
    LONG d2;
    float v;

    /* a soft round spot: bright in the middle, gone at the edge */
    for (y = 0; y < TEX; y++)
        for (x = 0; x < TEX; x++)
        {
            d2 = (2 * x - TEX + 1) * (2 * x - TEX + 1) + (2 * y - TEX + 1) * (2 * y - TEX + 1);
            v = 1.0f - (float) d2 / (float) (TEX * TEX);
            if (v < 0.0f)
                v = 0.0f;
            gw_tex[y * TEX + x] = (UBYTE) Demo_Byte(v * v * v);
        }
    glGenTextures(1, &gw_name);
    glBindTexture(GL_TEXTURE_2D, gw_name);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, TEX, TEX, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, gw_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    gw_have = TRUE;
    return TRUE;
}

/* Spark k at time t, in view space (the crystal's turn applied). */
static void Spark(ULONG k, float t, float spin, float *p)
{
    float a;
    float r;

    a = t * 0.8f + (float) k * 0.61f;
    r = 1.05f + 0.35f * Demo_Sin(a * 0.7f + (float) k);
    p[0] = r * Demo_Sin(a * 1.3f + (float) k * 0.2f);
    p[1] = 0.95f * Demo_Sin(a * 1.7f + (float) k * 1.3f);
    p[2] = r * Demo_Cos(a * 1.1f + (float) k * 0.5f);
    Demo_Rotate(p, 1, spin);
    Demo_Rotate(p, 0, 18.0f);
    p[2] -= 5.5f;
}

static void Frame(float t, LONG w, LONG h)
{
    static const float tint[PANES][3] = { { 0.9f, 0.3f, 0.3f }, { 0.9f, 0.7f, 0.2f },
                                          { 0.3f, 0.9f, 0.3f }, { 0.2f, 0.8f, 0.9f },
                                          { 0.3f, 0.4f, 1.0f }, { 0.8f, 0.3f, 0.9f } };
    float spin;
    float p[3];
    float s;
    float fade;
    float x0;
    float x1;
    float z0;
    float z1;
    ULONG k;
    ULONG g;
    ULONG r;
    ULONG gg;
    ULONG b;

    glClearColor(0.02f, 0.02f, 0.06f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    Demo_Perspective(50.0f, (float) w / (float) h, 0.5f, 20.0f);
    spin = t * 20.0f;
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);

    /* the crystal: six tinted panes round the y axis, brighter at the top */
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -5.5f);
    glRotatef(18.0f, 1.0f, 0.0f, 0.0f);
    glRotatef(spin, 0.0f, 1.0f, 0.0f);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    for (k = 0; k < PANES; k++)
    {
        x0 = 1.7f * Demo_Cos((float) k * (DEMO_PI / 3.0f));
        z0 = 1.7f * Demo_Sin((float) k * (DEMO_PI / 3.0f));
        x1 = 1.7f * Demo_Cos((float) (k + 1) * (DEMO_PI / 3.0f));
        z1 = 1.7f * Demo_Sin((float) (k + 1) * (DEMO_PI / 3.0f));
        glColor4f(tint[k][0] * 0.5f, tint[k][1] * 0.5f, tint[k][2] * 0.5f, 0.30f);
        glVertex3f(x0, -1.3f, z0);
        glVertex3f(x1, -1.3f, z1);
        glColor4f(tint[k][0], tint[k][1], tint[k][2], 0.22f);
        glVertex3f(x1, 1.3f, z1);
        glVertex3f(x0, 1.3f, z0);
    }
    glEnd();

    /* the sparks, added: each with three fading ghosts where it was */
    glLoadIdentity();
    glBlendFunc(GL_ONE, GL_ONE);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, gw_name);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glBegin(GL_QUADS);
    for (k = 0; k < SPARKS; k++)
    {
        r = Demo_Byte(0.55f + 0.45f * Demo_Sin((float) k * 0.37f + t * 0.5f));
        gg = Demo_Byte(0.55f + 0.45f * Demo_Sin((float) k * 0.37f + t * 0.5f + 2.1f));
        b = Demo_Byte(0.55f + 0.45f * Demo_Sin((float) k * 0.37f + t * 0.5f + 4.2f));
        for (g = 0; g < TAIL; g++)
        {
            Spark(k, t - (float) g * 0.06f, spin - (float) g * 1.2f, p);
            fade = 1.0f / (float) (1 + g * 2);
            s = 0.16f - (float) g * 0.025f;
            glColor4ub((GLubyte) ((float) r * fade), (GLubyte) ((float) gg * fade),
                       (GLubyte) ((float) b * fade), 255);
            glTexCoord2f(0.0f, 0.0f);
            glVertex3f(p[0] - s, p[1] - s, p[2]);
            glTexCoord2f(1.0f, 0.0f);
            glVertex3f(p[0] + s, p[1] - s, p[2]);
            glTexCoord2f(1.0f, 1.0f);
            glVertex3f(p[0] + s, p[1] + s, p[2]);
            glTexCoord2f(0.0f, 1.0f);
            glVertex3f(p[0] - s, p[1] + s, p[2]);
        }
    }
    glEnd();

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}

static void Done(void)
{
    if (gw_have)
        glDeleteTextures(1, &gw_name);
    gw_have = FALSE;
}

const struct Scene scene_glow = { "Glow", Init, Frame, Done };
