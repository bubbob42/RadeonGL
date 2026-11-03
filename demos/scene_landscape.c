/*
 * scene_landscape.c -- Landscape: a flight low over rolling hills under a
 * dusk sky.
 *
 * Exercises the library's fast path for vertex arrays, the one GLQuake's
 * world takes: one interleaved array of x y z, s t, s t floats (28 bytes a
 * vertex), glLockArrays once a frame and glDrawElements with 16-bit
 * indices for every tile, two texture units -- a ground texture modulated
 * by a light texture shaded from the slopes (GL_LUMINANCE, I8 on the
 * card), as GLQuake's lightmaps modulate its walls. The ground is one
 * tileable 32 x 32 patch drawn 3 x 3 times around the flight path with
 * glTranslatef in between: the same locked vertices every time, which the
 * library's upload cache sends to the card only once.
 */

#include <exec/types.h>
#include <proto/exec.h>
#include <proto/minigl.h>

#include "demo.h"

#define LN 32                   /* cells along a tile's side */
#define LV (LN + 1)             /* vertices along it */
#define LCELL 1.0f              /* a cell's size in world units */
#define TILE (LN * LCELL)
#define BASE 128                /* the ground texture */
#define LIGHT 64                /* the light texture, one per tile */
#define SPEED 3.5f              /* units a second */

static float ls_vtx[LV * LV * 7];
static UWORD ls_idx[LN * LN * 6];
static UBYTE ls_base[BASE * BASE * 3];
static UBYTE ls_light[LIGHT * LIGHT];
static GLuint ls_name[2];
static BOOL ls_have;

/* The ground's height, periodic in TILE in both directions: the tiles
 * meet without a seam. */
static float Height(float x, float z)
{
    float u;
    float v;

    u = x * (2.0f * DEMO_PI / TILE);
    v = z * (2.0f * DEMO_PI / TILE);
    return 2.4f * Demo_Sin(u) * Demo_Cos(v)
         + 1.1f * Demo_Sin(2.0f * (u + v) + 0.7f)
         + 0.45f * Demo_Sin(4.0f * u - 2.0f * v + 1.9f)
         + 0.2f * Demo_Cos(8.0f * v + 4.0f * u);
}

/* Value noise on a grid of cell texels, wrapping at BASE: smooth patches
 * for grass and earth. */
static float Noise(ULONG x, ULONG y, ULONG cell, ULONG salt)
{
    ULONG n;
    ULONG gx;
    ULONG gy;
    ULONG s;
    float fx;
    float fy;
    float c[4];
    ULONG k;

    n = BASE / cell;
    gx = x / cell;
    gy = y / cell;
    fx = (float) (x % cell) / (float) cell;
    fy = (float) (y % cell) / (float) cell;
    for (k = 0; k < 4; k++)
    {
        s = (((gx + (k & 1)) % n) * 73UL + ((gy + (k >> 1)) % n) * 179UL + salt) * 2654435761UL;
        c[k] = (float) ((s >> 13) & 1023) / 1023.0f;
    }
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    return (c[0] + (c[1] - c[0]) * fx) * (1.0f - fy) + (c[2] + (c[3] - c[2]) * fx) * fy;
}

static void MakeTextures(void)
{
    ULONG x;
    ULONG y;
    ULONG seed;
    float n;
    float m;
    float e;
    float hx;
    float hz;
    float l;
    UBYTE *p;

    seed = 7;
    for (y = 0; y < BASE; y++)
        for (x = 0; x < BASE; x++)
        {
            n = 0.6f * Noise(x, y, 16, 1) + 0.4f * Noise(x, y, 4, 2);
            m = (float) (Demo_Rand(&seed) & 31) / 255.0f;
            e = n > 0.62f ? (n - 0.62f) * 2.6f : 0.0f;      /* earth patches */
            p = &ls_base[(y * BASE + x) * 3];
            p[0] = (UBYTE) Demo_Byte(0.28f + 0.30f * n + 0.35f * e + m);
            p[1] = (UBYTE) Demo_Byte(0.42f + 0.32f * n - 0.10f * e + m);
            p[2] = (UBYTE) Demo_Byte(0.16f + 0.12f * n + 0.05f * e + m * 0.5f);
        }
    /* the light: the slope against a low sun from the left, valleys darker */
    for (y = 0; y < LIGHT; y++)
        for (x = 0; x < LIGHT; x++)
        {
            hx = (float) x * (TILE / (float) LIGHT);
            hz = (float) y * (TILE / (float) LIGHT);
            n = Height(hx - 0.5f, hz) - Height(hx + 0.5f, hz);       /* -dh/dx */
            m = Height(hx, hz - 0.5f) - Height(hx, hz + 0.5f);       /* -dh/dz */
            l = 0.62f + 0.30f * (0.8f * n - 0.4f * m) / (1.0f + (n < 0.0f ? -n : n)
                                                         + (m < 0.0f ? -m : m));
            l *= 0.70f + 0.30f * (Height(hx, hz) + 4.2f) / 8.4f;
            ls_light[y * LIGHT + x] = (UBYTE) Demo_Byte(l * 1.25f);
        }
}

static BOOL Init(void)
{
    ULONG i;
    ULONG j;
    ULONG k;
    ULONG v;
    float *p;

    for (j = 0; j < LV; j++)
        for (i = 0; i < LV; i++)
        {
            p = &ls_vtx[(j * LV + i) * 7];
            p[0] = (float) i * LCELL;
            p[1] = Height((float) i * LCELL, (float) j * LCELL);
            p[2] = (float) j * LCELL;
            p[3] = (float) i * 0.25f;                      /* the ground, 8 times a tile */
            p[4] = (float) j * 0.25f;
            p[5] = (float) i / (float) LN;                 /* the light, once a tile */
            p[6] = (float) j / (float) LN;
        }
    k = 0;
    for (j = 0; j < LN; j++)
        for (i = 0; i < LN; i++)
        {
            v = j * LV + i;
            ls_idx[k++] = (UWORD) v;
            ls_idx[k++] = (UWORD) (v + LV);
            ls_idx[k++] = (UWORD) (v + 1);
            ls_idx[k++] = (UWORD) (v + 1);
            ls_idx[k++] = (UWORD) (v + LV);
            ls_idx[k++] = (UWORD) (v + LV + 1);
        }
    MakeTextures();
    glGenTextures(2, ls_name);
    glBindTexture(GL_TEXTURE_2D, ls_name[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, BASE, BASE, 0, GL_RGB, GL_UNSIGNED_BYTE, ls_base);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, ls_name[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, LIGHT, LIGHT, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
                 ls_light);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    ls_have = TRUE;
    return TRUE;
}

/* The tile a world coordinate is in (floor, for negatives too). The cast
 * may round or truncate (demo.c, Demo_Sin: GCC picks per cast, and a
 * different -m flag can change the pick), so the loops settle the floor. */
static LONG TileOf(float c)
{
    float f;
    LONG k;

    f = c / TILE;
    k = (LONG) f;
    while ((float) k > f)
        k--;
    while ((float) (k + 1) <= f)
        k++;
    return k;
}

static void Frame(float t, LONG w, LONG h)
{
    float cx;
    float cy;
    float cz;
    float ahead;
    float yaw;
    LONG tx0;
    LONG tz0;
    LONG tx;
    LONG tz;

    glClear(GL_DEPTH_BUFFER_BIT);
    Demo_Background(0.10f, 0.12f, 0.35f, 0.95f, 0.55f, 0.30f);
    Demo_Perspective(60.0f, (float) w / (float) h, 0.3f, 62.0f);

    /* forward along -z, swaying, high enough above the ground below and
     * just ahead */
    cz = -t * SPEED;
    cx = 6.0f * Demo_Sin(t * 0.21f) + 16.0f;
    cy = Height(cx, cz);
    ahead = Height(cx, cz - 4.0f);
    if (ahead > cy)
        cy = ahead;
    cy += 2.3f;
    yaw = 14.0f * Demo_Cos(t * 0.21f);

    glLoadIdentity();
    glRotatef(14.0f, 1.0f, 0.0f, 0.0f);
    glRotatef(yaw, 0.0f, 1.0f, 0.0f);
    glTranslatef(-cx, -cy, -cz);

    /* the arrays: GLQuake's world layout, both units from the one array */
    glVertexPointer(3, GL_FLOAT, 28, ls_vtx);
    glEnableClientState(GL_VERTEX_ARRAY);
    glClientActiveTextureARB(GL_TEXTURE0_ARB);
    glTexCoordPointer(2, GL_FLOAT, 28, ls_vtx + 3);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glClientActiveTextureARB(GL_TEXTURE1_ARB);
    glTexCoordPointer(2, GL_FLOAT, 28, ls_vtx + 5);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glClientActiveTextureARB(GL_TEXTURE0_ARB);

    glActiveTextureARB(GL_TEXTURE0_ARB);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, ls_name[0]);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glActiveTextureARB(GL_TEXTURE1_ARB);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, ls_name[1]);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    /* the tile under the flight, one to each side, two ahead */
    tx0 = TileOf(cx);
    tz0 = TileOf(cz);
    glLockArrays(0, LV * LV);
    for (tz = tz0 - 2; tz <= tz0; tz++)
        for (tx = tx0 - 1; tx <= tx0 + 1; tx++)
        {
            glPushMatrix();
            glTranslatef((float) tx * TILE, 0.0f, (float) tz * TILE);
            glDrawElements(GL_TRIANGLES, LN * LN * 6, GL_UNSIGNED_SHORT, ls_idx);
            glPopMatrix();
        }
    glUnlockArrays();

    glDisable(GL_DEPTH_TEST);
    glActiveTextureARB(GL_TEXTURE1_ARB);
    glDisable(GL_TEXTURE_2D);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glDisable(GL_TEXTURE_2D);
    glClientActiveTextureARB(GL_TEXTURE1_ARB);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glClientActiveTextureARB(GL_TEXTURE0_ARB);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

static void Done(void)
{
    if (ls_have)
        glDeleteTextures(2, ls_name);
    ls_have = FALSE;
}

const struct Scene scene_landscape = { "Landscape", Init, Frame, Done };
