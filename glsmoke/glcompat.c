/*
 * GLCompat -- minigl.library 29.1's other calls on the card (2026-11-08):
 * the texture environments GL_BLEND (with the environment colour) and
 * RGBA GL_DECAL, gluPerspective and gluLookAt, glArrayElement and
 * glMultiDrawArrays, texture uploads with GL_UNPACK_ALIGNMENT 1 and float
 * texels, glReadPixels in other types, and the glGet answers 29.1 gives.
 * Each picture case draws into a frame of 128 x 128, reads pixels back and
 * compares them with what GL 1.1 says, worked out by hand below.
 *
 *   GLCompat [DEPTH=16|32] [VERBOSE]
 *
 * Like GLLight: a public screen of its own (800 x 600, 32 bit unless
 * DEPTH=16), a window on it, a bitmap context on a bitmap of 128 x 128
 * (pixels read with p96ReadPixel). Prints PASS or FAIL for each case;
 * rc 0 when all passed, 5 when one failed, 20 without a context.
 * Tolerance: 6 of 255 a channel at 32 bit, 12 at 16.
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

#define TEMPLATE "DEPTH/K/N,VERBOSE/S"
#define S 128                   /* the frame: S x S */

const char ver[] = "$VER: GLCompat 0.1 (08.11.2026)";
struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *P96Base;

static struct Screen *scr;
static struct Window *win;
static struct BitMap *bm;
static struct RastPort rp;
static LONG passed, failed;
static LONG tol;
static LONG fbdepth;
static BOOL verbose;

/* --- helpers ---------------------------------------------------------------------- */

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

static void Count(BOOL ok)
{
    if (ok)
        passed++;
    else
        failed++;
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
    ok = (BOOL) (Diff(pr, wr) <= tol && Diff(pg, wg) <= tol && Diff(pb, wb) <= tol);
    Count(ok);
    if (!ok || verbose)
        Printf("%s %-50s got %3ld %3ld %3ld  want %3ld %3ld %3ld\n", (LONG) (ok ? "PASS" : "FAIL"),
               (LONG) what, pr, pg, pb, wr, wg, wb);
    else
        Printf("PASS %s\n", (LONG) what);
}

/* a value the program reads back against what it should be */
static void WantInt(const char *what, LONG got, LONG want)
{
    BOOL ok;

    ok = (BOOL) (got == want);
    Count(ok);
    Printf("%s %-50s got %ld, want %ld\n", (LONG) (ok ? "PASS" : "FAIL"), (LONG) what, got, want);
}

/* eye space = window space: x right, y down */
static void Ortho(void)
{
    glViewport(0, 0, S, S);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (GLdouble) S, (GLdouble) S, 0.0, -20.0, 20.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

/* the frame textured with the 2 x 2 texture bound: (0, 0) top left */
static void TexQuad(void)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(0.0f, 0.0f, 0.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex3f((GLfloat) S, 0.0f, 0.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex3f((GLfloat) S, (GLfloat) S, 0.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(0.0f, (GLfloat) S, 0.0f);
    glEnd();
}

static void Nearest(void)
{
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

/* --- the texture environments ------------------------------------------------------- */

static void EnvCases(void)
{
    /* red green / blue white */
    static const UBYTE quads[2 * 2 * 3] = { 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255 };
    /* red at half alpha, green opaque / blue clear, white opaque */
    static const UBYTE quads4[2 * 2 * 4] = { 255, 0, 0, 128, 0, 255, 0, 255,
                                             0, 0, 255, 0, 255, 255, 255, 255 };
    static const UBYTE grey[2 * 2] = { 128, 128, 128, 128 };
    static const GLfloat yellow[4] = { 1.0f, 1.0f, 0.0f, 1.0f };
    static const GLfloat cyan[4] = { 0.0f, 1.0f, 1.0f, 1.0f };

    Ortho();
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 1);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);  /* the rows here are tight (6 and 2 bytes) */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, (void *) quads);
    Nearest();
    glColor3f(0.2f, 0.4f, 0.6f);

    /* E1. REPLACE and RGB DECAL: the texel */
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    Begin(); TexQuad(); Show();
    Want("REPLACE: the texel (red)", S / 4, S / 4, 1, 0, 0);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
    Begin(); TexQuad(); Show();
    Want("DECAL, RGB texture: the texel (green)", S * 3 / 4, S / 4, 0, 1, 0);

    /* E2. GL_BLEND: C = Cf (1 - Ct) + Cc Ct, Cf (0.2 0.4 0.6), Cc yellow */
    glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, yellow);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
    Begin(); TexQuad(); Show();
    Want("BLEND, red texel: (1, 0.4, 0.6)", S / 4, S / 4, 1.0f, 0.4f, 0.6f);
    Want("BLEND, green texel: (0.2, 1, 0.6)", S * 3 / 4, S / 4, 0.2f, 1.0f, 0.6f);
    Want("BLEND, blue texel: (0.2, 0.4, 0)", S / 4, S * 3 / 4, 0.2f, 0.4f, 0.0f);

    /* E3. GL_BLEND with a luminance texture of 0.5: halfway to yellow */
    glBindTexture(GL_TEXTURE_2D, 2);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 2, 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, (void *) grey);
    Nearest();
    Begin(); TexQuad(); Show();
    Want("BLEND, luminance 0.5: (0.6, 0.7, 0.3)", S / 2, S / 2, 0.6f, 0.7f, 0.3f);

    /* E4. another colour: the TFACTOR goes out again */
    glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, cyan);
    Begin(); TexQuad(); Show();
    Want("BLEND, luminance 0.5, cyan: (0.1, 0.7, 0.8)", S / 2, S / 2, 0.1f, 0.7f, 0.8f);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    /* E5. RGBA DECAL: C = Cf (1 - At) + Ct At */
    glBindTexture(GL_TEXTURE_2D, 3);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, (void *) quads4);
    Nearest();
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
    Begin(); TexQuad(); Show();
    Want("DECAL RGBA, red at alpha 0.5: (0.6, 0.2, 0.3)", S / 4, S / 4, 0.6f, 0.2f, 0.3f);
    Want("DECAL RGBA, green opaque: (0, 1, 0)", S * 3 / 4, S / 4, 0.0f, 1.0f, 0.0f);
    Want("DECAL RGBA, blue clear: the colour", S / 4, S * 3 / 4, 0.2f, 0.4f, 0.6f);

    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glDisable(GL_TEXTURE_2D);
}

/* --- uploads: alignment 1, float texels ------------------------------------------------ */

static void UploadCases(void)
{
    /* 2 x 2 RGB in rows of 6 bytes: red green / blue white */
    static const UBYTE tight[2 * 2 * 3] = { 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255 };
    static const GLfloat fl[2 * 2 * 4] = { 1.0f, 0.5f, 0.0f, 1.0f, 0.0f, 0.25f, 0.75f, 1.0f,
                                          2.0f, -1.0f, 0.5f, 1.0f, 0.1f, 0.2f, 0.3f, 1.0f };

    Ortho();
    glEnable(GL_TEXTURE_2D);
    glColor3f(1.0f, 1.0f, 1.0f);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

    /* U1. GL_UNPACK_ALIGNMENT 1: the second row starts at byte 6 */
    glBindTexture(GL_TEXTURE_2D, 4);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, (void *) tight);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    Nearest();
    Begin(); TexQuad(); Show();
    Want("UNPACK_ALIGNMENT 1, second row: blue", S / 4, S * 3 / 4, 0, 0, 1);
    Want("UNPACK_ALIGNMENT 1, second row: white", S * 3 / 4, S * 3 / 4, 1, 1, 1);

    /* U2. GL_FLOAT texels, clamped to [0, 1] */
    glBindTexture(GL_TEXTURE_2D, 5);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_FLOAT, (void *) fl);
    Nearest();
    Begin(); TexQuad(); Show();
    Want("GL_FLOAT texel (1, 0.5, 0)", S / 4, S / 4, 1.0f, 0.5f, 0.0f);
    Want("GL_FLOAT texel (2, -1, 0.5) clamped", S / 4, S * 3 / 4, 1.0f, 0.0f, 0.5f);

    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glDisable(GL_TEXTURE_2D);
}

/* --- GLU ------------------------------------------------------------------------------- */

/* a quad of 1 x 1 around the origin in the plane z = 0 */
static void UnitQuad(void)
{
    glBegin(GL_QUADS);
    glVertex3f(-0.5f, -0.5f, 0.0f);
    glVertex3f(0.5f, -0.5f, 0.0f);
    glVertex3f(0.5f, 0.5f, 0.0f);
    glVertex3f(-0.5f, 0.5f, 0.0f);
    glEnd();
}

static void GluCases(void)
{
    glViewport(0, 0, S, S);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(90.0f, 1.0f, 1.0f, 10.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    glColor3f(0.0f, 1.0f, 0.0f);

    /* G1. 2 away at 90 degrees: the frame is 4 wide there, the quad a
     * quarter of it (pixels 48 .. 80); identity matrices would give 32 .. 96 */
    Begin(); UnitQuad(); Show();
    Want("gluPerspective + gluLookAt: the quad's middle", S / 2, S / 2, 0, 1, 0);
    Want("gluPerspective + gluLookAt: outside a quarter", 38, S / 2, 0, 0, 0);

    /* G2. looked at from the side the quad is edge-on: nothing */
    glLoadIdentity();
    gluLookAt(2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    Begin(); UnitQuad(); Show();
    Want("gluLookAt from +x: the quad edge-on", S / 2, S / 2, 0, 0, 0);
}

/* --- arrays ------------------------------------------------------------------------------ */

static void ArrayCases(void)
{
    static const GLfloat v[8 * 3] = {
        0, 0, 0,   64, 0, 0,   64, 128, 0,   0, 128, 0,         /* the left half */
        64, 0, 0,  128, 0, 0,  128, 128, 0,  64, 128, 0 };      /* the right half */
    static const UBYTE c[8 * 4] = {
        0, 0, 255, 255,  0, 0, 255, 255,  0, 0, 255, 255,  0, 0, 255, 255,
        255, 0, 255, 255, 255, 0, 255, 255, 255, 0, 255, 255, 255, 0, 255, 255 };
    GLint first[2];
    GLsizei count[2];
    int i;

    Ortho();
    glVertexPointer(3, GL_FLOAT, 0, (void *) v);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, (void *) c);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    /* A1. glArrayElement inside glBegin: the left half, blue */
    Begin();
    glBegin(GL_QUADS);
    for (i = 0; i < 4; i++)
        glArrayElement(i);
    glEnd();
    Show();
    Want("glArrayElement: the left half blue", S / 4, S / 2, 0, 0, 1);
    Want("glArrayElement: the right half untouched", S * 3 / 4, S / 2, 0, 0, 0);
    WantInt("glIsEnabled(GL_VERTEX_ARRAY)", (LONG) glIsEnabled(GL_VERTEX_ARRAY), (LONG) GL_TRUE);

    /* A2. glMultiDrawArrays: both halves in one call */
    first[0] = 0; count[0] = 4;
    first[1] = 4; count[1] = 4;
    Begin(); glMultiDrawArrays(GL_QUADS, first, count, 2); Show();
    Want("glMultiDrawArrays: the left half blue", S / 4, S / 2, 0, 0, 1);
    Want("glMultiDrawArrays: the right half magenta", S * 3 / 4, S / 2, 1, 0, 1);

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
}

/* --- glInterleavedArrays, gluBuild2DMipmaps ------------------------------------------------ */

static void MoreCases(void)
{
    /* GL_C4UB_V3F: four green corners over the frame */
    static const struct { UBYTE c[4]; GLfloat v[3]; } q[4] = {
        { { 0, 255, 0, 255 }, { 0.0f, 0.0f, 0.0f } },
        { { 0, 255, 0, 255 }, { (GLfloat) S, 0.0f, 0.0f } },
        { { 0, 255, 0, 255 }, { (GLfloat) S, (GLfloat) S, 0.0f } },
        { { 0, 255, 0, 255 }, { 0.0f, (GLfloat) S, 0.0f } } };
    /* 4 x 4 RGB: the left half red, the right half blue */
    UBYTE img[4 * 4 * 3];
    int i;

    Ortho();
    glInterleavedArrays(GL_C4UB_V3F, 0, (void *) q);
    Begin(); glDrawArrays(GL_QUADS, 0, 4); Show();
    Want("glInterleavedArrays GL_C4UB_V3F: green", S / 2, S / 2, 0, 1, 0);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);

    for (i = 0; i < 16; i++)
    {
        img[i * 3 + 0] = (i % 4 < 2) ? 255 : 0;
        img[i * 3 + 1] = 0;
        img[i * 3 + 2] = (i % 4 < 2) ? 0 : 255;
    }
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 6);
    WantInt("gluBuild2DMipmaps returns 0",
            (LONG) gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGB8, 4, 4, GL_RGB, GL_UNSIGNED_BYTE, img), 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    Begin();
    glBegin(GL_QUADS);              /* the 4 x 4 texture on 1 x 1 pixels: level 2 */
    glTexCoord2f(0.0f, 0.0f); glVertex3f(60.0f, 60.0f, 0.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex3f(61.0f, 60.0f, 0.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex3f(61.0f, 61.0f, 0.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(60.0f, 61.0f, 0.0f);
    glEnd();
    Show();
    Want("gluBuild2DMipmaps: the 1 x 1 level, purple", 60, 60, 0.5f, 0.0f, 0.5f);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glDisable(GL_TEXTURE_2D);
}

/* --- glCopyTexImage2D, glCopyTexSubImage2D ------------------------------------------------ */

static void CopyCases(void)
{
    Ortho();
    glDisable(GL_TEXTURE_2D);
    Begin();
    glColor3f(1.0f, 0.0f, 0.0f);            /* the left half red, the right half blue */
    glBegin(GL_QUADS);
    glVertex3f(0.0f, 0.0f, 0.0f); glVertex3f(S / 2.0f, 0.0f, 0.0f);
    glVertex3f(S / 2.0f, (GLfloat) S, 0.0f); glVertex3f(0.0f, (GLfloat) S, 0.0f);
    glColor3f(0.0f, 0.0f, 1.0f);
    glVertex3f(S / 2.0f, 0.0f, 0.0f); glVertex3f((GLfloat) S, 0.0f, 0.0f);
    glVertex3f((GLfloat) S, (GLfloat) S, 0.0f); glVertex3f(S / 2.0f, (GLfloat) S, 0.0f);
    glEnd();
    /* no mglSwitchDisplay: the copy must see what is still pending */
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 7);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 0, 0, 4, 4, 0);         /* all red */
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 2, 0, S - 2, 0, 2, 4);        /* its right half blue */
    WantInt("glCopyTexImage2D/glCopyTexSubImage2D: no error", (LONG) glGetError(), 0);
    Nearest();
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glColor3f(1.0f, 1.0f, 1.0f);
    Begin(); TexQuad(); Show();
    Want("copied texture, the left half: red", S / 4, S / 2, 1, 0, 0);
    Want("copied texture, the right half: blue (sub image)", S * 3 / 4, S / 2, 0, 0, 1);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glDisable(GL_TEXTURE_2D);
}

/* --- glBlendEquation, glBlendFuncSeparate ------------------------------------------------ */

/* the whole frame in one colour */
static void Fill(float r, float g, float b, float a)
{
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex3f(0.0f, 0.0f, 0.0f);
    glVertex3f((GLfloat) S, 0.0f, 0.0f);
    glVertex3f((GLfloat) S, (GLfloat) S, 0.0f);
    glVertex3f(0.0f, (GLfloat) S, 0.0f);
    glEnd();
}

/* the destination (0.5, 0.25, 0.75, alpha 0.25) unblended, then the source
 * (0.75, 0.5, 0.125, alpha 0.5) blended over it; the destination drawn with
 * blending off after GL_MIN or GL_FUNC_REVERSE_SUBTRACT checks that those
 * do not linger (Mesa's r200: they spoil unblended drawing) */
static void BlendOver(GLenum eq, GLenum sf, GLenum df)
{
    glDisable(GL_BLEND);
    Fill(0.5f, 0.25f, 0.75f, 0.25f);
    glEnable(GL_BLEND);
    glBlendFunc(sf, df);
    glBlendEquation(eq);
    Fill(0.75f, 0.5f, 0.125f, 0.5f);
    glDisable(GL_BLEND);
    Show();
}

/* the frame's alpha (glReadPixels) against want, both in 1/1000; only a
 * 32-bit frame keeps alpha */
static void WantAlpha(const char *what, LONG want)
{
    GLfloat f[4];
    LONG got;
    BOOL ok;

    if (fbdepth != 32)
        return;
    f[3] = -1.0f;
    glReadPixels(S / 2, S / 2, 1, 1, GL_RGBA, GL_FLOAT, f);
    got = (LONG) (f[3] * 1000.0f + 0.5f);
    ok = (BOOL) (Diff(got, want) <= 10);
    Count(ok);
    Printf("%s %-50s got %ld/1000, want %ld/1000\n", (LONG) (ok ? "PASS" : "FAIL"), (LONG) what, got, want);
}

static void BlendCases(void)
{
    GLint iv[4];

    Ortho();
    glDisable(GL_TEXTURE_2D);
    BlendOver(GL_FUNC_ADD, GL_ONE, GL_ONE);
    Want("GL_FUNC_ADD, ONE ONE: source + destination", S / 2, S / 2, 1.0f, 0.75f, 0.875f);
    WantAlpha("GL_FUNC_ADD, ONE ONE: alpha 0.25 + 0.5", 750);
    BlendOver(GL_FUNC_SUBTRACT, GL_ONE, GL_ONE);
    Want("GL_FUNC_SUBTRACT: source - destination", S / 2, S / 2, 0.25f, 0.25f, 0.0f);
    BlendOver(GL_FUNC_SUBTRACT, GL_ONE, GL_SRC_ALPHA);
    Want("GL_FUNC_SUBTRACT, ONE SRC_ALPHA: s - d / 2", S / 2, S / 2, 0.5f, 0.375f, 0.0f);
    BlendOver(GL_FUNC_REVERSE_SUBTRACT, GL_ONE, GL_ONE);
    Want("GL_FUNC_REVERSE_SUBTRACT: destination - source", S / 2, S / 2, 0.0f, 0.0f, 0.625f);
    WantAlpha("GL_FUNC_REVERSE_SUBTRACT: alpha 0.25 - 0.5 = 0", 0);
    BlendOver(GL_MIN, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    Want("GL_MIN (the factors do not apply)", S / 2, S / 2, 0.5f, 0.25f, 0.125f);
    WantAlpha("GL_MIN: alpha min(0.25, 0.5)", 250);
    BlendOver(GL_MAX, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    Want("GL_MAX (the factors do not apply)", S / 2, S / 2, 0.75f, 0.5f, 0.75f);
    WantAlpha("GL_MAX: alpha max(0.25, 0.5)", 500);
    iv[0] = -1;
    glGetIntegerv(GL_BLEND_EQUATION, iv);
    WantInt("glGetIntegerv GL_BLEND_EQUATION", iv[0], GL_MAX);
    glBlendEquation(GL_FUNC_ADD);

    /* separate alpha factors: the colour from the source, the alpha kept */
    glDisable(GL_BLEND);
    Fill(0.5f, 0.25f, 0.75f, 0.25f);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ZERO, GL_ONE);
    Fill(0.75f, 0.5f, 0.125f, 0.5f);
    glDisable(GL_BLEND);
    Show();
    Want("glBlendFuncSeparate ONE ZERO, ZERO ONE: colour = source", S / 2, S / 2, 0.75f, 0.5f, 0.125f);
    WantAlpha("glBlendFuncSeparate ONE ZERO, ZERO ONE: alpha kept", 250);
    iv[0] = -1;
    glGetIntegerv(GL_BLEND_DST_ALPHA, iv);
    WantInt("glGetIntegerv GL_BLEND_DST_ALPHA", iv[0], GL_ONE);
    /* the other way round: the colour kept, the alpha from the source */
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ONE, GL_ZERO);
    Fill(0.25f, 1.0f, 0.0f, 0.5f);
    glDisable(GL_BLEND);
    Show();
    Want("glBlendFuncSeparate ZERO ONE, ONE ZERO: colour kept", S / 2, S / 2, 0.75f, 0.5f, 0.125f);
    WantAlpha("glBlendFuncSeparate ZERO ONE, ONE ZERO: alpha = source", 500);
    /* a plain glBlendFunc ends the separate factors (BlendOver calls it) */
    BlendOver(GL_FUNC_ADD, GL_ONE, GL_ONE);
    Want("glBlendFunc after glBlendFuncSeparate: ONE ONE again", S / 2, S / 2, 1.0f, 0.75f, 0.875f);
    WantAlpha("glBlendFunc after glBlendFuncSeparate: alpha added", 750);
    iv[0] = -1;
    glGetIntegerv(GL_BLEND_DST_ALPHA, iv);
    WantInt("glGetIntegerv GL_BLEND_DST_ALPHA after glBlendFunc", iv[0], GL_ONE);
    glBlendEquation(GL_ONE);
    WantInt("glBlendEquation(GL_ONE): GL_INVALID_ENUM", (LONG) glGetError(), GL_INVALID_ENUM);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    WantInt("blending: no error left", (LONG) glGetError(), 0);
}

/* --- reading back, queries ------------------------------------------------------------- */

static void QueryCases(void)
{
    GLfloat f[4 * 4 * 2];           /* two rows of four RGBA pixels */
    UWORD us[6];
    GLint iv[4];
    GLint cur, peak;
    MGLLockInfo li;
    int i;
    BOOL ok;

    Ortho();
    glColor3f(1.0f, 0.0f, 0.0f);
    Begin();
    glBegin(GL_QUADS);                      /* red over the whole frame */
    glVertex3f(0.0f, 0.0f, 0.0f);
    glVertex3f((GLfloat) S, 0.0f, 0.0f);
    glVertex3f((GLfloat) S, (GLfloat) S, 0.0f);
    glVertex3f(0.0f, (GLfloat) S, 0.0f);
    glEnd();
    Show();

    /* R1. GL_FLOAT RGBA, two rows of two in rows of four (GL_PACK_ROW_LENGTH) */
    for (i = 0; i < 32; i++)
        f[i] = -9.0f;
    glPixelStorei(GL_PACK_ROW_LENGTH, 4);
    glReadPixels(10, 10, 2, 2, GL_RGBA, GL_FLOAT, f);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    ok = (BOOL) (f[0] > 0.97f && f[1] < 0.03f && f[2] < 0.03f && f[3] > 0.97f
                 && f[4] > 0.97f                            /* pixel 1 of row 0 */
                 && f[8] == -9.0f && f[15] == -9.0f         /* the rest of row 0 untouched */
                 && f[16] > 0.97f && f[20] > 0.97f          /* row 1 starts at pixel 4 */
                 && f[24] == -9.0f);
    Count(ok);
    Printf("%s glReadPixels GL_FLOAT, PACK_ROW_LENGTH 4: %ld/1000 %ld/1000 %ld/1000\n",
           (LONG) (ok ? "PASS" : "FAIL"), (LONG) (f[0] * 1000.0f), (LONG) (f[1] * 1000.0f),
           (LONG) (f[2] * 1000.0f));

    /* R2. GL_UNSIGNED_SHORT RGB */
    for (i = 0; i < 6; i++)
        us[i] = 0x1234;
    glPixelStorei(GL_PACK_ALIGNMENT, 2);
    glReadPixels(20, 20, 2, 1, GL_RGB, GL_UNSIGNED_SHORT, us);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    ok = (BOOL) (us[0] > 0xf000 && us[1] < 0x1000 && us[2] < 0x1000 && us[3] > 0xf000);
    Count(ok);
    Printf("%s glReadPixels GL_UNSIGNED_SHORT: %04lx %04lx %04lx\n", (LONG) (ok ? "PASS" : "FAIL"),
           (LONG) us[0], (LONG) us[1], (LONG) us[2]);

    /* Q. what 29.1 answers */
    glGetIntegerv(GL_DOUBLEBUFFER, iv);
    WantInt("glGet GL_DOUBLEBUFFER (a bitmap context)", iv[0], 0);
    glGetIntegerv(GL_DEPTH_BITS, iv);
    WantInt("glGet GL_DEPTH_BITS", iv[0], 16);
    glGetIntegerv(GL_RGBA_MODE, iv);
    WantInt("glGet GL_RGBA_MODE", iv[0], 1);
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, iv);
    WantInt("glGet GL_MAX_VIEWPORT_DIMS", iv[0], 2048);
    glGetIntegerv(GL_RED_BITS, iv);
    WantInt("glGet GL_RED_BITS >= 5", iv[0] >= 5 ? 1 : 0, 1);
    glDrawBuffer(GL_FRONT);
    WantInt("glDrawBuffer(GL_FRONT): no error", (LONG) glGetError(), 0);
    glDrawBuffer(GL_RIGHT);
    WantInt("glDrawBuffer(GL_RIGHT): GL_INVALID_OPERATION", (LONG) glGetError(),
            (LONG) GL_INVALID_OPERATION);
    cur = peak = -1;
    mglTexMemStat(&cur, &peak);
    WantInt("mglTexMemStat: textures take memory", (cur > 0 && peak >= cur) ? 1 : 0, 1);
    memset(&li, 0x55, sizeof(li));
    WantInt("mglLockBack refuses", (LONG) mglLockBack(&li), (LONG) GL_FALSE);
    WantInt("mglLockBack: no address", li.base_address == NULL ? 1 : 0, 1);
    WantInt("glGetError at the end", (LONG) glGetError(), 0);
}

/* --- mglMainLoop, in a window on the Workbench ------------------------------------------- */

static LONG idles;

static void Idle(void)
{
    glClearColor(0.0f, 0.3f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    mglSwitchDisplay();
    if (++idles == 10)
        mglExit();
}

static void LoopCase(void)
{
    mglChooseWindowMode(GL_TRUE);
    if (!mglCreateContext(40, 300, S, S))
    {
        Count(FALSE);
        Printf("FAIL mglMainLoop: no window context on the Workbench\n");
        return;
    }
    idles = 0;
    mglIdleFunc(Idle);
    mglMainLoop();
    WantInt("mglMainLoop: the idle handler ran until mglExit", idles, 10);
    mglDeleteContext();
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
        PrintFault(IoErr(), (CONST_STRPTR) "GLCompat");
        return RETURN_ERROR;
    }
    depth = (args[0] && *(LONG *) args[0] == 16) ? 16 : 32;
    verbose = (BOOL) (args[1] != 0);
    tol = (depth == 16) ? 12 : 6;
    fbdepth = depth;
    IntuitionBase = (struct IntuitionBase *) OpenLibrary((CONST_STRPTR) "intuition.library", 39);
    GfxBase = (struct GfxBase *) OpenLibrary((CONST_STRPTR) "graphics.library", 39);
    P96Base = OpenLibrary((CONST_STRPTR) "Picasso96API.library", 2);
    if (IntuitionBase == NULL || GfxBase == NULL || P96Base == NULL)
        goto out;
    if (!MiniGLOpen())
    {
        Printf("GLCompat: minigl.library 29.1 did not open\n");
        goto out;
    }
    opened = TRUE;
    id = p96BestModeIDTags(P96BIDTAG_NominalWidth, 800, P96BIDTAG_NominalHeight, 600,
                           P96BIDTAG_Depth, (ULONG) depth,
                           P96BIDTAG_FormatsAllowed, (depth == 32) ? RGBFF_B8G8R8A8 : RGBFF_R5G6B5PC,
                           TAG_DONE);
    scr = (id != (ULONG) INVALID_ID)
        ? p96OpenScreenTags(P96SA_DisplayID, id, P96SA_Width, 800, P96SA_Height, 600,
                            P96SA_Depth, (ULONG) depth, P96SA_Title, (ULONG) "GLCompat", TAG_DONE)
        : NULL;
    if (scr == NULL)
    {
        Printf("GLCompat: no %ld-bit screen\n", depth);
        goto out;
    }
    win = OpenWindowTags(NULL, WA_CustomScreen, (ULONG) scr, WA_Left, 40, WA_Top, 40,
                         WA_InnerWidth, S, WA_InnerHeight, S, WA_Title, (ULONG) "GLCompat",
                         WA_DragBar, TRUE, WA_Activate, TRUE, TAG_DONE);
    bm = (win != NULL) ? AllocBitMap(S, S, GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH), BMF_MINPLANES,
                                     scr->RastPort.BitMap) : NULL;
    if (bm == NULL)
    {
        Printf("GLCompat: no window or bitmap\n");
        goto out;
    }
    InitRastPort(&rp);
    rp.BitMap = bm;
    if (!mglCreateContextFromBitMap(bm))
    {
        Printf("GLCompat: no context (the library log says why)\n");
        goto out;
    }
    Printf("GLCompat on a %ld-bit screen, frame %ld x %ld, tolerance %ld\n", depth, (LONG) S, (LONG) S, tol);
    EnvCases();
    UploadCases();
    GluCases();
    ArrayCases();
    MoreCases();
    CopyCases();
    BlendCases();
    QueryCases();
    mglDeleteContext();
    LoopCase();
    Printf("GLCompat: %ld passed, %ld failed\n", passed, failed);
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
