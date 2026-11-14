/*
 * GLView -- RadeonGL's GL_RGL_bitmap_view on the card (2026-11-14, R4):
 * a board bitmap in the GL screen's format, four coloured quadrants drawn
 * into it with p96RectFill, made a texture with
 * glTexImage2D(GL_RGL_BITMAP_VIEW), drawn 1:1 at the screen's top left with
 * GL_REPLACE and nearest filtering, and read back (glReadPixels): each
 * quadrant's colour must come back. Then the same from a view that starts
 * 32 pixels into the bitmap (its first texel 64 bytes on, 16-bit), and the
 * bitmap redrawn between two frames (the second must show the new colour:
 * no stale texture cache). Refusals must be refused: a view past the
 * bitmap's edge, a fifth view.
 *
 *   GLView DEPTH/N
 *
 * DEPTH 16 (default) or 32. Prints PASS or FAIL per check, rc 0 when all
 * pass. GCC only (the MiniGL SDK's import library, as GLQuake).
 */

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <libraries/Picasso96.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/Picasso96.h>
#include <proto/minigl.h>

#include "../lib/rglview.h"

static const char ver[] = "$VER: GLView 0.1 (14.11.2026)";

struct Library *P96Base = NULL;

#define BW 256
#define BH 128

static ULONG colours[4] = { 0xffff0000UL, 0xff00ff00UL, 0xff0000ffUL, 0xffffff00UL };
static int fails = 0;

static void Check(int ok, const char *what)
{
    Printf("%s %s\n", (LONG) (ok ? "PASS" : "FAIL"), (LONG) what);
    if (!ok)
        fails++;
}

/* the four quadrants of the bitmap, colour i at quadrant i, shifted by s */
static void Fill(struct BitMap *bm, ULONG s)
{
    struct RastPort rp;

    InitRastPort(&rp);
    rp.BitMap = bm;
    p96RectFill(&rp, 0, 0, BW / 2 - 1, BH / 2 - 1, colours[(0 + s) & 3]);
    p96RectFill(&rp, BW / 2, 0, BW - 1, BH / 2 - 1, colours[(1 + s) & 3]);
    p96RectFill(&rp, 0, BH / 2, BW / 2 - 1, BH - 1, colours[(2 + s) & 3]);
    p96RectFill(&rp, BW / 2, BH / 2, BW - 1, BH - 1, colours[(3 + s) & 3]);
}

/* one frame: the texture 1:1 at the top left, w x h pixels */
static void Frame(GLuint tex, LONG w, LONG h, LONG sh)
{
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 640.0, 0.0, (GLdouble) sh, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, (GLfloat) sh);
    glTexCoord2f(1.0f, 0.0f); glVertex2f((GLfloat) w, (GLfloat) sh);
    glTexCoord2f(1.0f, 1.0f); glVertex2f((GLfloat) w, (GLfloat) (sh - h));
    glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, (GLfloat) (sh - h));
    glEnd();
    mglSwitchDisplay();
}

/* the pixel at (x, y) from the top of the screen against colour c (8 bits
 * a channel; 16-bit screens lose the low bits) */
static int Near(LONG x, LONG y, LONG sh, ULONG c)
{
    UBYTE p[4];
    LONG d;
    int i;
    int ok;

    glReadPixels(x, sh - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    ok = 1;
    for (i = 0; i < 3; i++)
    {
        d = (LONG) p[i] - (LONG) ((c >> (16 - 8 * i)) & 255);
        if (d < -12 || d > 12)
            ok = 0;
    }
    if (!ok)
        Printf("   pixel %ld,%ld = %02lx%02lx%02lx, want %06lx\n", x, y, (ULONG) p[0], (ULONG) p[1],
               (ULONG) p[2], c & 0xffffffUL);
    return ok;
}

int main(int argc, char **argv)
{
    struct RDArgs *rda;
    LONG args[1];
    struct Window *win;
    struct BitMap *bm;
    struct RglBitMapView v;
    GLuint tex[6];
    LONG depth, sh;
    int lib, ctx;
    int i;

    (void) argv;
    if (argc == 0)
        return RETURN_FAIL;
    args[0] = 0;
    rda = ReadArgs((CONST_STRPTR) "DEPTH/N", args, NULL);
    if (rda == NULL)
    {
        PrintFault(IoErr(), "GLView");
        return RETURN_ERROR;
    }
    depth = args[0] ? *(LONG *) args[0] : 16;
    lib = ctx = 0;
    bm = NULL;
    Printf("%s\n", (LONG) (ver + 6));
    P96Base = OpenLibrary((CONST_STRPTR) "Picasso96API.library", 2);
    if (P96Base == NULL || !MiniGLOpen())
    {
        Printf("GLView: Picasso96API.library or minigl.library missing\n");
        goto out;
    }
    lib = 1;
    mglChoosePixelDepth((int) depth);
    if (!mglCreateContext(0, 0, 640, 480))
    {
        Printf("GLView: no context\n");
        goto out;
    }
    ctx = 1;
    mglEnableSync(GL_FALSE);
    sh = 480;
    Printf("GL_RENDERER: %s\n", (LONG) glGetString(GL_RENDERER));
    Check(strstr((const char *) glGetString(GL_EXTENSIONS), "GL_RGL_bitmap_view") != NULL,
          "GL_RGL_bitmap_view in GL_EXTENSIONS");
    win = (struct Window *) mglGetWindowHandle();
    if (win == NULL || win->WScreen == NULL)
    {
        Printf("GLView: no window\n");
        goto out;
    }
    /* a board bitmap in the GL screen's format (its friend) */
    bm = p96AllocBitMap(BW, BH, GetBitMapAttr(win->WScreen->RastPort.BitMap, BMA_DEPTH),
                        BMF_MINPLANES | BMF_DISPLAYABLE, win->WScreen->RastPort.BitMap, 0);
    if (bm == NULL)
    {
        Printf("GLView: no bitmap\n");
        goto out;
    }
    Printf("bitmap: %ld x %ld, format %ld, on the board %ld\n", (LONG) BW, (LONG) BH,
           (LONG) p96GetBitMapAttr(bm, P96BMA_RGBFORMAT), (LONG) p96GetBitMapAttr(bm, P96BMA_ISONBOARD));
    Fill(bm, 0);
    glGenTextures(6, tex);

    /* 1. the whole bitmap */
    v.bm = bm; v.x = 0; v.y = 0;
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, BW, BH, 0, GL_RGL_BITMAP_VIEW, GL_UNSIGNED_BYTE, &v);
    Check(glGetError() == GL_NO_ERROR, "a view of the whole bitmap is taken");
    Frame(tex[0], BW, BH, sh);
    Check(Near(BW / 4, BH / 4, sh, colours[0]) && Near(3 * BW / 4, BH / 4, sh, colours[1])
          && Near(BW / 4, 3 * BH / 4, sh, colours[2]) && Near(3 * BW / 4, 3 * BH / 4, sh, colours[3]),
          "the four quadrants drawn from the view");

    /* 2. redrawn between frames: the next frame shows the new colours */
    Fill(bm, 1);
    Frame(tex[0], BW, BH, sh);
    Check(Near(BW / 4, BH / 4, sh, colours[1]) && Near(3 * BW / 4, 3 * BH / 4, sh, colours[0]),
          "the bitmap redrawn: the next frame shows it");

    /* 3. a view 32 pixels in: the right half's quadrants at its left */
    v.x = 32;
    glBindTexture(GL_TEXTURE_2D, tex[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, BW - 32, BH, 0, GL_RGL_BITMAP_VIEW, GL_UNSIGNED_BYTE, &v);
    Check(glGetError() == GL_NO_ERROR, "a view 32 pixels in is taken");
    Frame(tex[1], BW - 32, BH, sh);
    Check(Near(8, 8, sh, colours[1]) && Near(BW / 2 - 32 + 8, 8, sh, colours[2]),
          "the view 32 pixels in starts there");

    /* 4. refusals */
    v.x = 0;
    glBindTexture(GL_TEXTURE_2D, tex[2]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, BW + 32, BH, 0, GL_RGL_BITMAP_VIEW, GL_UNSIGNED_BYTE, &v);
    Check(glGetError() == GL_INVALID_OPERATION, "a view past the bitmap's edge is refused");
    for (i = 2; i < 5; i++)
    {
        glBindTexture(GL_TEXTURE_2D, tex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 64, 64, 0, GL_RGL_BITMAP_VIEW, GL_UNSIGNED_BYTE, &v);
    }
    Check(glGetError() == GL_INVALID_OPERATION, "a fifth view is refused");
    glDeleteTextures(6, tex);
    Check(glGetError() == GL_NO_ERROR, "views deleted");

out:
    if (ctx)
        mglDeleteContext();
    if (bm != NULL)
        p96FreeBitMap(bm);
    if (lib)
        MiniGLClose();
    if (P96Base != NULL)
        CloseLibrary(P96Base);
    FreeArgs(rda);
    Printf("GLView: %s\n", (LONG) (fails ? "FAILED" : "all passed"));
    return fails ? RETURN_ERROR : RETURN_OK;
}
