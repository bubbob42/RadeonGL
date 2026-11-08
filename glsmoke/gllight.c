/*
 * GLLight -- RadeonGL's lighting on the card (2026-11-07): each case lights
 * a quad that fills the frame, reads pixels back and compares them with
 * the colour GL 1.1's lighting equation gives, worked out by hand below.
 *
 *   GLLight [DEPTH=16|32] [VERBOSE]
 *
 * It opens a public screen of its own (800 x 600, 32 bit unless DEPTH=16),
 * a window on it, and a bitmap context on a bitmap of 128 x 128: the
 * Radeon draws straight into the bitmap, the pixels are read from there
 * (p96ReadPixel), the bitmap is shown in the window. Prints PASS or FAIL
 * for each case; rc 0 when all passed, 5 when one failed, 20 without a
 * context. Tolerance: 6 of 255 a channel at 32 bit, 12 at 16.
 *
 * GCC only: it links the MiniGL SDK's import library, as GLSmoke does.
 */
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <libraries/Picasso96.h>
#include <proto/Picasso96.h>
#include <proto/minigl.h>
#include <string.h>
#include <math.h>

#define TEMPLATE "DEPTH/K/N,VERBOSE/S"
#ifndef GL_RGL_STATIC_ARRAYS_HINT
#define GL_RGL_STATIC_ARRAYS_HINT 0x52474c01UL   /* RadeonGL: arrays never change */
#endif
#define S 128                   /* the frame: S x S */

const char ver[] = "$VER: GLLight 0.1 (07.11.2026)";
struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *P96Base;

static struct Screen *scr;
static struct Window *win;
static struct BitMap *bm;
static struct RastPort rp;
static LONG passed, failed;
static LONG tol;
static BOOL verbose;

/* --- helpers ---------------------------------------------------------------------- */

static void V4(GLfloat *v, GLfloat a, GLfloat b, GLfloat c, GLfloat d)
{
    v[0] = a;
    v[1] = b;
    v[2] = c;
    v[3] = d;
}

/* every light off and at GL's defaults, the materials and the model too */
static void Reset(void)
{
    GLfloat v[4];
    int i;

    glDisable(GL_COLOR_MATERIAL);
    glDisable(GL_NORMALIZE);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glShadeModel(GL_SMOOTH);
    for (i = 0; i < 8; i++)
    {
        glDisable(GL_LIGHT0 + i);
        V4(v, 0, 0, 0, 1);
        glLightfv(GL_LIGHT0 + i, GL_AMBIENT, v);
        glLightfv(GL_LIGHT0 + i, GL_DIFFUSE, v);
        glLightfv(GL_LIGHT0 + i, GL_SPECULAR, v);
        glLightf(GL_LIGHT0 + i, GL_CONSTANT_ATTENUATION, 1.0f);
        glLightf(GL_LIGHT0 + i, GL_LINEAR_ATTENUATION, 0.0f);
        glLightf(GL_LIGHT0 + i, GL_QUADRATIC_ATTENUATION, 0.0f);
        glLightf(GL_LIGHT0 + i, GL_SPOT_CUTOFF, 180.0f);
        glLightf(GL_LIGHT0 + i, GL_SPOT_EXPONENT, 0.0f);
    }
    V4(v, 0, 0, 0, 1);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, v);
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, v);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, v);
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, v);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, v);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 0.0f);
    glEnable(GL_LIGHTING);
}

/* eye space = window space: x right, y down, z towards the viewer */
static void Ortho(void)
{
    glViewport(0, 0, S, S);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (GLdouble) S, (GLdouble) S, 0.0, -20.0, 20.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

/* the quad over the whole frame, normal n, at z 0, each vertex scaled by k */
static void Quad(GLfloat nx, GLfloat ny, GLfloat nz, GLfloat k)
{
    glBegin(GL_QUADS);
    glNormal3f(nx, ny, nz);
    glVertex3f(0.0f, 0.0f, 0.0f);
    glVertex3f(S * k, 0.0f, 0.0f);
    glVertex3f(S * k, S * k, 0.0f);
    glVertex3f(0.0f, S * k, 0.0f);
    glEnd();
}

/* GL lights each VERTEX: a local light's value at a pixel is what the
 * quad's corners got. So the local cases draw a quad of 3 x 3 pixels around
 * the pixel they read, its corners all one pixel off in x and y. */
static void SmallQuad(GLfloat cx, GLfloat cy)
{
    glBegin(GL_QUADS);
    glNormal3f(0.0f, 0.0f, 1.0f);
    glVertex3f(cx - 1.0f, cy - 1.0f, 0.0f);
    glVertex3f(cx + 1.0f, cy - 1.0f, 0.0f);
    glVertex3f(cx + 1.0f, cy + 1.0f, 0.0f);
    glVertex3f(cx - 1.0f, cy + 1.0f, 0.0f);
    glEnd();
}

static float Sqrt(float v)
{
    float x;
    int i;

    x = v > 1.0f ? v : 1.0f;
    for (i = 0; i < 30; i++)
        x = 0.5f * (x + v / x);
    return x;
}

static void Begin(void)
{
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

static void Show(void)
{
    mglSwitchDisplay();
    BltBitMapRastPort(bm, 0, 0, win->RPort, win->BorderLeft, win->BorderTop, S, S, 0xc0);
}

static LONG Diff(LONG a, LONG b)
{
    return a > b ? a - b : b - a;
}

/* the pixel at (x, y) against r, g, b in [0, 1] */
static void Want(const char *what, LONG x, LONG y, float r, float g, float b)
{
    ULONG px;
    LONG pr, pg, pb, wr, wg, wb;
    BOOL ok;

    px = p96ReadPixel(&rp, (UWORD) x, (UWORD) y);
    pr = (LONG) ((px >> 16) & 0xff);
    pg = (LONG) ((px >> 8) & 0xff);
    pb = (LONG) (px & 0xff);
    wr = (LONG) (r * 255.0f + 0.5f);
    wg = (LONG) (g * 255.0f + 0.5f);
    wb = (LONG) (b * 255.0f + 0.5f);
    if (wr > 255) wr = 255;
    if (wg > 255) wg = 255;
    if (wb > 255) wb = 255;
    ok = (BOOL) (Diff(pr, wr) <= tol && Diff(pg, wg) <= tol && Diff(pb, wb) <= tol);
    if (ok)
        passed++;
    else
        failed++;
    if (!ok || verbose)
        Printf("%s %-44s got %3ld %3ld %3ld  want %3ld %3ld %3ld\n", (LONG) (ok ? "PASS" : "FAIL"),
               (LONG) what, pr, pg, pb, wr, wg, wb);
    else
        Printf("PASS %s\n", (LONG) what);
}

/* --- the cases -------------------------------------------------------------------- */

static void Cases(void)
{
    static GLfloat normals[12] = { 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 };
    static GLfloat corners[12] = { 0, 0, 0, S, 0, 0, S, S, 0, 0, S, 0 };
    static GLfloat tilted[12] = { 0, 0.8660254f, 0.5f, 0, 0.8660254f, 0.5f,
                                  0, 0.8660254f, 0.5f, 0, 0.8660254f, 0.5f };
    static GLushort quad_idx[6] = { 0, 1, 2, 0, 2, 3 };
    GLfloat v[4];
    float h;

    Ortho();

    /* 1. the global ambient times the material's ambient */
    Reset();
    V4(v, 0.5f, 0.25f, 1.0f, 1.0f);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, v);
    V4(v, 0.8f, 0.8f, 0.8f, 1.0f);
    glMaterialfv(GL_FRONT, GL_AMBIENT, v);
    Begin(); Quad(0, 0, 1, 1); Show();
    Want("global ambient x material ambient", S / 2, S / 2, 0.4f, 0.2f, 0.8f);

    /* 2. plus the emission */
    V4(v, 0.3f, 0.1f, 0.0f, 1.0f);
    glMaterialfv(GL_FRONT, GL_EMISSION, v);
    Begin(); Quad(0, 0, 1, 1); Show();
    Want("emission + ambient", S / 2, S / 2, 0.7f, 0.3f, 0.8f);

    /* 3. a directional light along the normal: the material's diffuse */
    Reset();
    V4(v, 1, 1, 1, 1);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, v);
    V4(v, 0, 0, 1, 0);
    glLightfv(GL_LIGHT0, GL_POSITION, v);
    V4(v, 0.6f, 0.4f, 0.2f, 1.0f);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, v);
    glEnable(GL_LIGHT0);
    Begin(); Quad(0, 0, 1, 1); Show();
    Want("directional light, N.L = 1", S / 2, S / 2, 0.6f, 0.4f, 0.2f);

    /* 4. the same light at 60 degrees: N.L = 0.5 */
    V4(v, 0.0f, 0.8660254f, 0.5f, 0.0f);
    glLightfv(GL_LIGHT0, GL_POSITION, v);
    Begin(); Quad(0, 0, 1, 1); Show();
    Want("directional light at 60 degrees, N.L = 0.5", S / 2, S / 2, 0.3f, 0.2f, 0.1f);

    /* 5. from behind: nothing */
    V4(v, 0, 0, -1, 0);
    glLightfv(GL_LIGHT0, GL_POSITION, v);
    Begin(); Quad(0, 0, 1, 1); Show();
    Want("directional light from behind", S / 2, S / 2, 0.0f, 0.0f, 0.0f);

    /* 6. the normal from an array, glDrawArrays */
    V4(v, 0, 0, 1, 0);
    glLightfv(GL_LIGHT0, GL_POSITION, v);
    glVertexPointer(3, GL_FLOAT, 0, corners);
    glNormalPointer(GL_FLOAT, 0, normals);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    Begin(); glDrawArrays(GL_QUADS, 0, 4); Show();
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    Want("normal array (glDrawArrays)", S / 2, S / 2, 0.6f, 0.4f, 0.2f);

    /* 6b. the same from locked arrays, glDrawElements: the normals tilted
     * by 60 degrees (N.L = 0.5), so a stream that lost them would light
     * with the current normal (0,0,1) and give the full diffuse; then twice
     * more under GL_RGL_STATIC_ARRAYS_HINT (the second from the cache) */
    glVertexPointer(3, GL_FLOAT, 0, corners);
    glNormalPointer(GL_FLOAT, 0, tilted);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glLockArrays(0, 4);
    Begin(); glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (GLvoid *) quad_idx); Show();
    glUnlockArrays();
    Want("locked normal array (glDrawElements)", S / 2, S / 2, 0.3f, 0.2f, 0.1f);
    glHint(GL_RGL_STATIC_ARRAYS_HINT, GL_NICEST);
    glLockArrays(0, 4);
    Begin(); glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (GLvoid *) quad_idx); Show();
    glUnlockArrays();
    glLockArrays(0, 4);
    Begin(); glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (GLvoid *) quad_idx); Show();
    glUnlockArrays();
    glHint(GL_RGL_STATIC_ARRAYS_HINT, GL_DONT_CARE);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    Want("locked normal array, static hint, cached", S / 2, S / 2, 0.3f, 0.2f, 0.1f);

    /* 7. specular, light at 60 degrees, shininess 10:
     * H = normalize(L + (0,0,1)) = (0, 0.5, 0.866), N.H^10 = 0.2375 */
    Reset();
    V4(v, 1, 1, 1, 1);
    glLightfv(GL_LIGHT0, GL_SPECULAR, v);
    V4(v, 0.0f, 0.8660254f, 0.5f, 0.0f);
    glLightfv(GL_LIGHT0, GL_POSITION, v);
    V4(v, 1, 1, 1, 1);
    glMaterialfv(GL_FRONT, GL_SPECULAR, v);
    glMaterialf(GL_FRONT, GL_SHININESS, 10.0f);
    glEnable(GL_LIGHT0);
    Begin(); Quad(0, 0, 1, 1); Show();
    h = 0.2375f;
    Want("specular, shininess 10", S / 2, S / 2, h, h, h);

    /* 8. glColorMaterial(GL_FRONT, GL_AMBIENT_AND_DIFFUSE): the vertex colour */
    Reset();
    V4(v, 1, 1, 1, 1);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, v);
    V4(v, 0, 0, 1, 0);
    glLightfv(GL_LIGHT0, GL_POSITION, v);
    glEnable(GL_LIGHT0);
    glColorMaterial(GL_FRONT, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_COLOR_MATERIAL);
    glColor3f(0.2f, 0.9f, 0.4f);
    Begin(); Quad(0, 0, 1, 1); Show();
    Want("colour material, ambient and diffuse", S / 2, S / 2, 0.2f, 0.9f, 0.4f);

    /* 9. two lights add up */
    Reset();
    V4(v, 0.5f, 0.0f, 0.0f, 1.0f);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, v);
    V4(v, 0.0f, 0.5f, 0.0f, 1.0f);
    glLightfv(GL_LIGHT3, GL_DIFFUSE, v);
    V4(v, 0, 0, 1, 0);
    glLightfv(GL_LIGHT0, GL_POSITION, v);
    glLightfv(GL_LIGHT3, GL_POSITION, v);
    V4(v, 1, 1, 1, 1);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, v);
    glEnable(GL_LIGHT0);
    glEnable(GL_LIGHT3);
    Begin(); Quad(0, 0, 1, 1); Show();
    Want("lights 0 and 3 together", S / 2, S / 2, 0.5f, 0.5f, 0.0f);

    /* 10. a local light 20 above the middle, attenuation 1 + 0.025 d: each
     * corner of the small quad is (1, 1, 20) from it -- d = sqrt(402),
     * N.L = 20 / d, the diffuse 0.9 */
    Reset();
    V4(v, 1, 1, 1, 1);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, v);
    V4(v, S / 2.0f, S / 2.0f, 20.0f, 1.0f);
    glLightfv(GL_LIGHT0, GL_POSITION, v);
    glLightf(GL_LIGHT0, GL_LINEAR_ATTENUATION, 0.025f);
    V4(v, 0.9f, 0.9f, 0.9f, 1.0f);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, v);
    glEnable(GL_LIGHT0);
    Begin(); SmallQuad(S / 2.0f, S / 2.0f); Show();
    h = Sqrt(402.0f);
    h = 0.9f * (20.0f / h) / (1.0f + 0.025f * h);
    Want("local light, linear attenuation", S / 2, S / 2, h, h, h);

    /* 11. a spot 10 above the middle, cutoff 30 degrees: lit below it
     * (N.L = 10 / sqrt(102), the corners 8 degrees off its axis), dark 20
     * to the side (63 degrees off) */
    Reset();
    V4(v, 1, 1, 1, 1);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, v);
    V4(v, S / 2.0f, S / 2.0f, 10.0f, 1.0f);
    glLightfv(GL_LIGHT0, GL_POSITION, v);
    V4(v, 0, 0, -1, 0);
    glLightfv(GL_LIGHT0, GL_SPOT_DIRECTION, v);
    glLightf(GL_LIGHT0, GL_SPOT_CUTOFF, 30.0f);
    V4(v, 0.8f, 0.8f, 0.8f, 1.0f);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, v);
    glEnable(GL_LIGHT0);
    Begin(); SmallQuad(S / 2.0f, S / 2.0f); SmallQuad(S / 2.0f + 20.0f, S / 2.0f); Show();
    h = 0.8f * 10.0f / Sqrt(102.0f);
    Want("spot light, inside the cone", S / 2, S / 2, h, h, h);
    Want("spot light, outside the cone", S / 2 + 20, S / 2, 0.0f, 0.0f, 0.0f);

    /* 11b. the spot's exponent 10: the corners' cos^10 of 8 degrees */
    glLightf(GL_LIGHT0, GL_SPOT_EXPONENT, 10.0f);
    Begin(); SmallQuad(S / 2.0f, S / 2.0f); Show();
    h = 10.0f / Sqrt(102.0f);           /* the cosine off the axis = N.L here */
    h = 0.8f * h * h * h * h * h * h * h * h * h * h * h;
    Want("spot light, exponent 10", S / 2, S / 2, h, h, h);

    /* 12. a modelview scaled by 0.5: the normal grows to length 2, GL_NORMALIZE
     * makes it 1 again (diffuse 0.4 x N.L) */
    Reset();
    V4(v, 1, 1, 1, 1);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, v);
    V4(v, 0, 0, 1, 0);
    glLightfv(GL_LIGHT0, GL_POSITION, v);
    V4(v, 0.4f, 0.4f, 0.4f, 1.0f);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, v);
    glEnable(GL_LIGHT0);
    glScalef(0.5f, 0.5f, 0.5f);
    Begin(); Quad(0, 0, 1, 2); Show();
    Want("scaled modelview, no GL_NORMALIZE: N.L = 2", S / 2, S / 2, 0.8f, 0.8f, 0.8f);
    glEnable(GL_NORMALIZE);
    Begin(); Quad(0, 0, 1, 2); Show();
    Want("scaled modelview, GL_NORMALIZE: N.L = 1", S / 2, S / 2, 0.4f, 0.4f, 0.4f);
    glLoadIdentity();

    /* 13. the lit alpha is the material's diffuse alpha: blended over white */
    Reset();
    V4(v, 1, 1, 1, 1);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, v);
    V4(v, 0, 0, 1, 0);
    glLightfv(GL_LIGHT0, GL_POSITION, v);
    V4(v, 0.0f, 0.0f, 0.0f, 0.25f);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, v);
    glEnable(GL_LIGHT0);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    Quad(0, 0, 1, 1);
    Show();
    glDisable(GL_BLEND);
    Want("lit alpha = material diffuse alpha (0.25)", S / 2, S / 2, 0.75f, 0.75f, 0.75f);

    /* 14. lighting off again: the vertex colour passes */
    glDisable(GL_LIGHTING);
    glColor3f(0.1f, 0.5f, 0.9f);
    Begin(); Quad(0, 0, 1, 1); Show();
    Want("lighting off: the vertex colour", S / 2, S / 2, 0.1f, 0.5f, 0.9f);
}

/* --- texgen and texture matrices (texgen.c) ---------------------------------------- */

static void TexQuad(GLfloat x0, GLfloat x1)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(x0, 0.0f, 0.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex3f(x1, 0.0f, 0.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex3f(x1, (GLfloat) S, 0.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(x0, (GLfloat) S, 0.0f);
    glEnd();
}

/* the 2 x 2 texture: red green / blue white, at the four quarters */
static void Quarters(const char *what, float r0, float g0, float b0, float r1, float g1, float b1)
{
    char name[64];
    int i;

    for (i = 0; what[i] != 0 && i < 40; i++)
        name[i] = what[i];
    name[i] = 0;
    strcat(name, ", top left");
    Want(name, S / 4, S / 4, r0, g0, b0);
    name[i] = 0;
    strcat(name, ", top right");
    Want(name, S * 3 / 4, S / 4, r1, g1, b1);
}

static void TexCases(void)
{
    static const UBYTE quads[2 * 2 * 3] = { 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255 };
    UBYTE strip[8 * 3];
    GLfloat p[4];
    GLfloat a;
    int k;

    glDisable(GL_LIGHTING);
    glColor3f(1.0f, 1.0f, 1.0f);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, (void *) quads);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    Ortho();

    /* T1. object linear: s = x / S, t = y / S */
    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR);
    glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR);
    V4(p, 1.0f / S, 0, 0, 0);
    glTexGenfv(GL_S, GL_OBJECT_PLANE, p);
    V4(p, 0, 1.0f / S, 0, 0);
    glTexGenfv(GL_T, GL_OBJECT_PLANE, p);
    glEnable(GL_TEXTURE_GEN_S);
    glEnable(GL_TEXTURE_GEN_T);
    Begin(); TexQuad(0, (GLfloat) S); Show();
    Quarters("texgen object linear", 1, 0, 0, 0, 1, 0);

    /* T2. eye linear: the planes in eye space, the quad moved by the
     * modelview -- the same quarters (object linear would shift by half) */
    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_EYE_LINEAR);
    glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_EYE_LINEAR);
    V4(p, 1.0f / S, 0, 0, 0);
    glTexGenfv(GL_S, GL_EYE_PLANE, p);
    V4(p, 0, 1.0f / S, 0, 0);
    glTexGenfv(GL_T, GL_EYE_PLANE, p);
    glTranslatef(S / 2.0f, 0.0f, 0.0f);
    Begin(); TexQuad(-S / 2.0f, S / 2.0f); Show();
    Quarters("texgen eye linear, quad moved", 1, 0, 0, 0, 1, 0);

    /* T2b. the same move with object linear: s shifted by -0.5 (repeat) */
    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR);
    glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR);
    Begin(); TexQuad(-S / 2.0f, S / 2.0f); Show();
    Quarters("texgen object linear, quad moved", 0, 1, 0, 1, 0, 0);
    glLoadIdentity();
    glDisable(GL_TEXTURE_GEN_S);
    glDisable(GL_TEXTURE_GEN_T);

    /* T3. no texgen: the coordinates as given ... */
    Begin(); TexQuad(0, (GLfloat) S); Show();
    Quarters("texture coordinates as given", 1, 0, 0, 0, 1, 0);
    /* ... and through a texture matrix that halves s */
    glMatrixMode(GL_TEXTURE);
    glScalef(0.5f, 1.0f, 1.0f);
    glMatrixMode(GL_MODELVIEW);
    Begin(); TexQuad(0, (GLfloat) S); Show();
    Quarters("texture matrix, s halved", 1, 0, 0, 1, 0, 0);
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);

    /* T4. a sphere map: an 8 x 1 strip, column k = (32 k, 255 - 32 k, 128);
     * a small quad on the axis at z -5, its normal turned by a about y:
     * s = sin(a) / 2 + 1/2 (GL's formula with the eye on the axis) */
    for (k = 0; k < 8; k++)
    {
        strip[k * 3 + 0] = (UBYTE) (k * 32);
        strip[k * 3 + 1] = (UBYTE) (255 - k * 32);
        strip[k * 3 + 2] = 128;
    }
    glBindTexture(GL_TEXTURE_2D, 2);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 8, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, (void *) strip);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.5, 0.5, -0.5, 0.5, 1.0, 20.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
    glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
    glEnable(GL_TEXTURE_GEN_S);
    glEnable(GL_TEXTURE_GEN_T);
    for (k = 0; k < 3; k++)
    {
        static const float deg[3] = { 10.0f, 45.0f, -45.0f };
        static const int col[3] = { 4, 6, 1 };
        static const char *const what[3] = { "sphere map, normal 10 degrees", "sphere map, normal 45 degrees",
                                             "sphere map, normal -45 degrees" };

        a = deg[k] * 3.14159265f / 180.0f;
        Begin();
        glBegin(GL_QUADS);
        glNormal3f((GLfloat) sin(a), 0.0f, (GLfloat) cos(a));
        glVertex3f(-0.1f, -0.1f, -5.0f);
        glVertex3f(0.1f, -0.1f, -5.0f);
        glVertex3f(0.1f, 0.1f, -5.0f);
        glVertex3f(-0.1f, 0.1f, -5.0f);
        glEnd();
        Show();
        Want(what[k], S / 2, S / 2, col[k] * 32 / 255.0f, (255 - col[k] * 32) / 255.0f, 128 / 255.0f);
    }
    glDisable(GL_TEXTURE_GEN_S);
    glDisable(GL_TEXTURE_GEN_T);
    glDisable(GL_TEXTURE_2D);
    Ortho();
}

/* --- fog and wide lines (fog.c) ------------------------------------------------- */

/* the quad over the frame at eye depth z, in one colour */
static void FlatQuad(GLfloat z)
{
    glBegin(GL_QUADS);
    glNormal3f(0.0f, 0.0f, 1.0f);
    glVertex3f(0.0f, 0.0f, z);
    glVertex3f((GLfloat) S, 0.0f, z);
    glVertex3f((GLfloat) S, (GLfloat) S, z);
    glVertex3f(0.0f, (GLfloat) S, z);
    glEnd();
}

static void FogCases(void)
{
    GLfloat v[4];
    float f;

    Ortho();
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    V4(v, 0.0f, 0.0f, 1.0f, 1.0f);
    glFogfv(GL_FOG_COLOR, v);
    glEnable(GL_FOG);
    glColor3f(1.0f, 0.0f, 0.0f);

    /* F1. linear, start 0 end 10, the quad 5 deep: half fog */
    glFogf(GL_FOG_MODE, (GLfloat) GL_LINEAR);
    glFogf(GL_FOG_START, 0.0f);
    glFogf(GL_FOG_END, 10.0f);
    Begin(); FlatQuad(-5.0f); Show();
    Want("fog linear, half way", S / 2, S / 2, 0.5f, 0.0f, 0.5f);

    /* F2. exp, density 0.1: f = exp(-0.5) */
    glFogf(GL_FOG_MODE, (GLfloat) GL_EXP);
    glFogf(GL_FOG_DENSITY, 0.1f);
    Begin(); FlatQuad(-5.0f); Show();
    f = 0.60653f;
    Want("fog exp, density 0.1, depth 5", S / 2, S / 2, f, 0.0f, 1.0f - f);

    /* F3. exp2: f = exp(-0.25) */
    glFogf(GL_FOG_MODE, (GLfloat) GL_EXP2);
    Begin(); FlatQuad(-5.0f); Show();
    f = 0.77880f;
    Want("fog exp2, density 0.1, depth 5", S / 2, S / 2, f, 0.0f, 1.0f - f);

    /* F4. over a lit surface: green, half fogged */
    glFogf(GL_FOG_MODE, (GLfloat) GL_LINEAR);
    Reset();
    V4(v, 1, 1, 1, 1);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, v);
    V4(v, 0, 0, 1, 0);
    glLightfv(GL_LIGHT0, GL_POSITION, v);
    V4(v, 0, 1, 0, 1);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, v);
    glEnable(GL_LIGHT0);
    Begin(); FlatQuad(-5.0f); Show();
    Want("fog linear over lighting", S / 2, S / 2, 0.0f, 0.5f, 0.5f);
    glDisable(GL_LIGHTING);

    /* F5. off again */
    glDisable(GL_FOG);
    glColor3f(1.0f, 0.0f, 0.0f);
    Begin(); FlatQuad(-5.0f); Show();
    Want("fog off", S / 2, S / 2, 1.0f, 0.0f, 0.0f);

    /* L1. a horizontal line 5 wide: lit 2 above its middle, dark 4 above;
     * 1 wide: dark 2 above */
    glColor3f(1.0f, 1.0f, 1.0f);
    glLineWidth(5.0f);
    Begin();
    glBegin(GL_LINES);
    glVertex3f(10.0f, S / 2.0f + 0.5f, 0.0f);
    glVertex3f(S - 10.0f, S / 2.0f + 0.5f, 0.0f);
    glEnd();
    Show();
    Want("line width 5, 2 off the middle", S / 2, S / 2 - 2, 1.0f, 1.0f, 1.0f);
    Want("line width 5, 4 off the middle", S / 2, S / 2 - 4, 0.0f, 0.0f, 0.0f);
    glLineWidth(1.0f);
    Begin();
    glBegin(GL_LINES);
    glVertex3f(10.0f, S / 2.0f + 0.5f, 0.0f);
    glVertex3f(S - 10.0f, S / 2.0f + 0.5f, 0.0f);
    glEnd();
    Show();
    Want("line width 1, the middle", S / 2, S / 2, 1.0f, 1.0f, 1.0f);
    Want("line width 1, 2 off the middle", S / 2, S / 2 - 2, 0.0f, 0.0f, 0.0f);
}

/* --- true-colour textures (W2) ---------------------------------------------------- */

static void Tex32Cases(void)
{
    static const UBYTE px[2 * 2 * 4] = { 8, 12, 200, 255, 8, 12, 200, 255, 8, 12, 200, 255, 8, 12, 200, 255 };
    LONG keep;

    Ortho();
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 3);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    keep = tol;
    tol = (tol > 6) ? tol : 2;      /* a 16-bit screen cannot tell either */
    /* GL_RGBA8: 8, 12, 200 as given (ARGB4444 would make 0, 0, 204) */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, (void *) px);
    Begin(); TexQuad(0, (GLfloat) S); Show();
    Want("GL_RGBA8 texture, 8 bits a component", S / 2, S / 2, 8 / 255.0f, 12 / 255.0f, 200 / 255.0f);
    /* the generic GL_RGBA: 4 bits a component, as GLQuake's */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, (void *) px);
    Begin(); TexQuad(0, (GLfloat) S); Show();
    Want("GL_RGBA texture, ARGB4444 as before", S / 2, S / 2, 0.0f, 0.0f, 204 / 255.0f);
    tol = keep;
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glDisable(GL_TEXTURE_2D);
}

/* --- glReadPixels and mglWriteShotPPM (lib/rglread.c) ------------------------------- */

static void ReadCases(void)
{
    UBYTE rgb[8 * 3 * 3];           /* 7 pixels a row, rows padded to 24 bytes */
    UBYTE hdr[16];
    BPTR f;
    LONG n;
    int i;
    BOOL ok;

    Ortho();
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_FOG);
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(1.0f, 0.0f, 0.0f);
    glBegin(GL_QUADS);              /* red over the top half (window y 0 .. S/2) */
    glVertex3f(0.0f, 0.0f, 0.0f);
    glVertex3f((GLfloat) S, 0.0f, 0.0f);
    glVertex3f((GLfloat) S, S / 2.0f, 0.0f);
    glVertex3f(0.0f, S / 2.0f, 0.0f);
    glEnd();
    Show();
    /* GL_RGB, 7 wide, 3 high, GL_PACK_ALIGNMENT 4: rows of 21 bytes padded
     * to 24, row 0 at the bottom; from GL row S/2 - 2: blue, blue, red */
    memset(rgb, 0x77, sizeof(rgb));
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(10, S / 2 - 2, 7, 3, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    ok = (BOOL) (rgb[0] < 16 && rgb[2] > 240                     /* row 0: blue */
                 && rgb[24] < 16 && rgb[24 + 2] > 240            /* row 1 (GL S/2 - 1): blue */
                 && rgb[48] > 240 && rgb[48 + 2] < 16            /* row 2 (GL S/2): red */
                 && rgb[21] == 0x77 && rgb[23] == 0x77);        /* padding untouched */
    for (i = 0; i < 7; i++)
        if (rgb[i * 3 + 2] < 240 || rgb[48 + i * 3] < 240)
            ok = FALSE;
    if (ok)
        passed++;
    else
        failed++;
    Printf("%s glReadPixels GL_RGB, rows padded to 4, bottom up: %ld %ld %ld / %ld %ld %ld / %ld %ld %ld\n",
           (LONG) (ok ? "PASS" : "FAIL"), (LONG) rgb[0], (LONG) rgb[1], (LONG) rgb[2], (LONG) rgb[24],
           (LONG) rgb[25], (LONG) rgb[26], (LONG) rgb[48], (LONG) rgb[49], (LONG) rgb[50]);

    /* mglWriteShotPPM: the header, and the first pixel (top left: red) */
    mglWriteShotPPM("RAM:gllight-shot.ppm");
    ok = FALSE;
    f = Open((CONST_STRPTR) "RAM:gllight-shot.ppm", MODE_OLDFILE);
    if (f != 0)
    {
        n = Read(f, hdr, 16);
        ok = (BOOL) (n == 16 && memcmp(hdr, "P6\n128 128\n255\n", 15) == 0 && hdr[15] > 240);
        Close(f);
    }
    if (ok)
        passed++;
    else
        failed++;
    Printf("%s mglWriteShotPPM (RAM:gllight-shot.ppm)\n", (LONG) (ok ? "PASS" : "FAIL"));
}

int main(void)
{
    struct RDArgs *rd;
    LONG args[2];
    LONG depth;
    ULONG id;
    BOOL opened;
    int rc;

    rc = RETURN_FAIL;
    opened = FALSE;
    memset(args, 0, sizeof(args));
    rd = ReadArgs((CONST_STRPTR) TEMPLATE, args, NULL);
    if (rd == NULL)
    {
        PrintFault(IoErr(), (CONST_STRPTR) "GLLight");
        return RETURN_ERROR;
    }
    depth = (args[0] && *(LONG *) args[0] == 16) ? 16 : 32;
    verbose = (BOOL) (args[1] != 0);
    tol = (depth == 16) ? 12 : 6;
    IntuitionBase = (struct IntuitionBase *) OpenLibrary((CONST_STRPTR) "intuition.library", 39);
    GfxBase = (struct GfxBase *) OpenLibrary((CONST_STRPTR) "graphics.library", 39);
    P96Base = OpenLibrary((CONST_STRPTR) "Picasso96API.library", 2);
    if (IntuitionBase == NULL || GfxBase == NULL || P96Base == NULL)
        goto out;
    if (!MiniGLOpen())
    {
        Printf("GLLight: minigl.library 29.1 did not open\n");
        goto out;
    }
    opened = TRUE;
    id = p96BestModeIDTags(P96BIDTAG_NominalWidth, 800, P96BIDTAG_NominalHeight, 600,
                           P96BIDTAG_Depth, (ULONG) depth,
                           P96BIDTAG_FormatsAllowed, (depth == 32) ? RGBFF_B8G8R8A8 : RGBFF_R5G6B5PC,
                           TAG_DONE);
    scr = (id != (ULONG) INVALID_ID)
        ? p96OpenScreenTags(P96SA_DisplayID, id, P96SA_Width, 800, P96SA_Height, 600,
                            P96SA_Depth, (ULONG) depth, P96SA_Title, (ULONG) "GLLight", TAG_DONE)
        : NULL;
    if (scr == NULL)
    {
        Printf("GLLight: no %ld-bit screen\n", depth);
        goto out;
    }
    win = OpenWindowTags(NULL, WA_CustomScreen, (ULONG) scr, WA_Left, 40, WA_Top, 40,
                         WA_InnerWidth, S, WA_InnerHeight, S, WA_Title, (ULONG) "GLLight",
                         WA_DragBar, TRUE, WA_Activate, TRUE, TAG_DONE);
    bm = (win != NULL) ? AllocBitMap(S, S, GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH), BMF_MINPLANES,
                                     scr->RastPort.BitMap) : NULL;
    if (bm == NULL)
    {
        Printf("GLLight: no window or bitmap\n");
        goto out;
    }
    InitRastPort(&rp);
    rp.BitMap = bm;
    if (!mglCreateContextFromBitMap(bm))
    {
        Printf("GLLight: no context (the library log says why)\n");
        goto out;
    }
    Printf("GLLight on a %ld-bit screen, frame %ld x %ld, tolerance %ld\n", depth, (LONG) S, (LONG) S, tol);
    Cases();
    TexCases();
    FogCases();
    Tex32Cases();
    ReadCases();
    mglDeleteContext();
    Printf("GLLight: %ld passed, %ld failed\n", passed, failed);
    rc = (failed != 0) ? RETURN_WARN : RETURN_OK;

out:
    if (bm != NULL)
        FreeBitMap(bm);
    if (win != NULL)
        CloseWindow(win);
    if (scr != NULL)
        p96CloseScreen(scr);
    if (opened)
        MiniGLClose();
    if (P96Base != NULL)
        CloseLibrary(P96Base);
    if (GfxBase != NULL)
        CloseLibrary((struct Library *) GfxBase);
    if (IntuitionBase != NULL)
        CloseLibrary((struct Library *) IntuitionBase);
    FreeArgs(rd);
    return rc;
}
