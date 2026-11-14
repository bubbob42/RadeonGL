/*
 * util.c -- GLBench's helpers (bench.h): maths, the sine table, the
 * projections, textures, memory, output.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/minigl.h>

#include "bench.h"

static float b_table[B_TABLE];

/* As the demos' Demo_Sin: to [-0.5, 0.5] turns, then [-pi/2, pi/2] by
 * symmetry. GCC -m68881 casts a float to LONG rounding or truncating,
 * cast by cast, so the cast only comes near and the loops settle it. */
float B_Sin(float a)
{
    float x;
    float x2;
    LONG k;

    x = a * (1.0f / (2.0f * B_PI));
    k = (LONG) x;
    while (x - (float) k > 0.5f)
        k++;
    while (x - (float) k < -0.5f)
        k--;
    x = (x - (float) k) * (2.0f * B_PI);
    if (x > 0.5f * B_PI)
        x = B_PI - x;
    else if (x < -0.5f * B_PI)
        x = -B_PI - x;
    x2 = x * x;
    return x * (1.0f - x2 * (1.0f / 6.0f - x2 * (1.0f / 120.0f - x2 * (1.0f / 5040.0f
                - x2 * (1.0f / 362880.0f)))));
}

float B_Cos(float a)
{
    return B_Sin(a + 0.5f * B_PI);
}

void B_TableInit(void)
{
    LONG i;

    for (i = 0; i < B_TABLE; i++)
        b_table[i] = B_Sin((float) i * (2.0f * B_PI / (float) B_TABLE));
}

float B_SinT(LONG phase)
{
    return b_table[phase & (B_TABLE - 1)];
}

float B_CosT(LONG phase)
{
    return b_table[(phase + B_TABLE / 4) & (B_TABLE - 1)];
}

LONG B_Floor(float v)
{
    LONG n;

    n = (LONG) v;
    while ((float) n > v)
        n--;
    while ((float) (n + 1) <= v)
        n++;
    return n;
}

void B_Perspective(float fovy, float aspect, float znear, float zfar)
{
    float h;

    h = znear * B_Sin(fovy * (B_PI / 360.0f)) / B_Cos(fovy * (B_PI / 360.0f));
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum((GLdouble) (-h * aspect), (GLdouble) (h * aspect), (GLdouble) -h, (GLdouble) h,
              (GLdouble) znear, (GLdouble) zfar);
    glMatrixMode(GL_MODELVIEW);
}

void B_Ortho2D(LONG w, LONG h)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (GLdouble) w, 0.0, (GLdouble) h, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void B_Background(float r0, float g0, float b0, float r1, float g1, float b1)
{
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glBegin(GL_QUADS);
    glColor4f(r1, g1, b1, 1.0f);
    glVertex2f(-1.0f, -1.0f);
    glVertex2f(1.0f, -1.0f);
    glColor4f(r0, g0, b0, 1.0f);
    glVertex2f(1.0f, 1.0f);
    glVertex2f(-1.0f, 1.0f);
    glEnd();
    bench_prims += 2;
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

ULONG B_Rand(ULONG *seed)
{
    *seed = *seed * 1103515245UL + 12345UL;
    return (*seed >> 16) & 0x7fffUL;
}

ULONG B_Byte(float v)
{
    LONG n;

    if (v <= 0.0f)
        return 0;
    if (v >= 1.0f)
        return 255;
    n = B_Floor(v * 255.0f + 0.5f);
    return (ULONG) (n > 255 ? 255 : n);
}

void B_Say(const char *fmt, LONG a, LONG b, LONG c, LONG d)
{
    if (Output() != 0)
        Printf((CONST_STRPTR) fmt, a, b, c, d);
}

APTR B_Alloc(ULONG bytes)
{
    APTR p;

    p = AllocVec(bytes, MEMF_ANY | MEMF_CLEAR);
    if (p == NULL)
        B_Say("GLBench: no memory for %ld bytes\n", (LONG) bytes, 0, 0, 0);
    return p;
}

void B_Free(APTR p)
{
    if (p != NULL)
        FreeVec(p);
}

GLuint B_Texture(GLint internal, LONG w, LONG h, GLenum format, const void *pixels,
                 BOOL mipmap, GLenum wrap)
{
    GLuint name;

    name = 0;
    glGenTextures(1, &name);
    glBindTexture(GL_TEXTURE_2D, name);
    if (mipmap)
    {
        gluBuild2DMipmaps(GL_TEXTURE_2D, internal, (GLint) w, (GLint) h, format, GL_UNSIGNED_BYTE,
                          (void *) pixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    }
    else
    {
        glTexImage2D(GL_TEXTURE_2D, 0, internal, (GLsizei) w, (GLsizei) h, 0, format,
                     GL_UNSIGNED_BYTE, (void *) pixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint) wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint) wrap);
    return name;
}
