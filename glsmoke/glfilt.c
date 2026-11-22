/*
 * GLFilt -- what each minification filter does on the card (2026-11-21).
 *
 * A 64 x 64 texture whose levels are solid colours (0 red, 1 green, 2 and
 * up blue) drawn over a 128 x 128 bitmap context with texture coordinates
 * 0..k, so that the level of detail is log2(k / 2) at every pixel. The
 * centre pixel is printed for the six GL minification filters at LOD
 * 0.25, 0.5, 0.75 and 1.25. GLCompat's trilinear cases found
 * GL_LINEAR_MIPMAP_NEAREST blending between levels at LOD 0.25 on the
 * Radeon 9200 although the library sends LINEAR_MIP_NEAREST (host test
 * test_filter_after_upload); this maps every mode as the card does it.
 *
 *   GLFilt [DEPTH=16|32]
 *
 * GCC only: it links the MiniGL SDK's import library, as GLSmoke does.
 */
#include <exec/types.h>
#include <exec/memory.h>
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

#define TEMPLATE "DEPTH/K/N"
#define S 128

const char ver[] = "$VER: GLFilt 0.3 (21.11.2026)";
struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *P96Base;

static struct Screen *scr;
static struct Window *win;
static struct BitMap *bm;
static struct RastPort rp;

static void Quad(float k)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(0.0f, 0.0f, 0.0f);
    glTexCoord2f(k, 0.0f);    glVertex3f((GLfloat) S, 0.0f, 0.0f);
    glTexCoord2f(k, k);       glVertex3f((GLfloat) S, (GLfloat) S, 0.0f);
    glTexCoord2f(0.0f, k);    glVertex3f(0.0f, (GLfloat) S, 0.0f);
    glEnd();
}

static void Cases(void)
{
    static UBYTE lv[64 * 64 * 3];
    static const GLenum filt[6] = { GL_NEAREST, GL_LINEAR, GL_NEAREST_MIPMAP_NEAREST,
                                    GL_NEAREST_MIPMAP_LINEAR, GL_LINEAR_MIPMAP_NEAREST,
                                    GL_LINEAR_MIPMAP_LINEAR };
    static const char *const fname[6] = { "NEAREST", "LINEAR", "NEAREST_MIPMAP_NEAREST",
                                          "NEAREST_MIPMAP_LINEAR", "LINEAR_MIPMAP_NEAREST",
                                          "LINEAR_MIPMAP_LINEAR" };
    static const float kk[4] = { 2.3784f, 2.8284f, 3.3636f, 4.7568f };   /* 2^(LOD + 1) */
    static const char *const lname[4] = { "0.25", "0.50", "0.75", "1.25" };
    GLuint tex;
    LONG l, sz, i, f, j, a;
    ULONG px;

    glViewport(0, 0, S, S);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (GLdouble) S, (GLdouble) S, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    for (l = 0, sz = 64; sz >= 1; l++, sz >>= 1)
    {
        for (i = 0; i < sz * sz; i++)
        {
            lv[i * 3] = (UBYTE) (l == 0 ? 255 : 0);
            lv[i * 3 + 1] = (UBYTE) (l == 1 ? 255 : 0);
            lv[i * 3 + 2] = (UBYTE) (l >= 2 ? 255 : 0);
        }
        glTexImage2D(GL_TEXTURE_2D, (GLint) l, GL_RGB, sz, sz, 0, GL_RGB, GL_UNSIGNED_BYTE, lv);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnable(GL_TEXTURE_2D);
    for (a = 1; a <= 8; a += 7)
    {
    /* anisotropy 1 (plain modes) and 8 (tex13.c's ANISO_* modes) */
    glTexParameteri(GL_TEXTURE_2D, 0x84FE /* GL_TEXTURE_MAX_ANISOTROPY_EXT */, a);
    Printf("filter (anisotropy %ld)       LOD   centre pixel (r g b)  [level 0 red, 1 green, 2+ blue]\n", a);
    for (f = 0; f < 6; f++)
        for (j = 0; j < 4; j++)
        {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filt[f]);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            Quad(kk[j]);
            mglSwitchDisplay();
            BltBitMapRastPort(bm, 0, 0, win->RPort, win->BorderLeft, win->BorderTop, S, S, 0xc0);
            px = p96ReadPixel(&rp, S / 2, S / 2);
            Printf("%-26s  %s  %3ld %3ld %3ld\n", (LONG) fname[f], (LONG) lname[j],
                   (LONG) ((px >> 16) & 0xff), (LONG) ((px >> 8) & 0xff), (LONG) (px & 0xff));
        }
    }
    glDeleteTextures(1, &tex);
}

/* Within a level: level 0 a checker of single red and blue texels, level
 * 1 solid green, the rest black; magnification NEAREST, so that LOD 0.25
 * is minification for every filter. Four pixels of the middle row: pure
 * red or blue = nearest within the level, purple = linear; a green share =
 * blended with level 1. */
static void CheckerCases(void)
{
    static UBYTE lv[64 * 64 * 3];
    static const GLenum filt[4] = { GL_NEAREST_MIPMAP_NEAREST, GL_LINEAR_MIPMAP_NEAREST,
                                    GL_NEAREST_MIPMAP_LINEAR, GL_LINEAR_MIPMAP_LINEAR };
    static const char *const fname[4] = { "NEAREST_MIPMAP_NEAREST", "LINEAR_MIPMAP_NEAREST",
                                          "NEAREST_MIPMAP_LINEAR", "LINEAR_MIPMAP_LINEAR" };
    GLuint tex;
    LONG l, sz, x, y, f;
    ULONG px;

    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    for (l = 0, sz = 64; sz >= 1; l++, sz >>= 1)
    {
        for (y = 0; y < sz; y++)
            for (x = 0; x < sz; x++)
            {
                UBYTE *q = lv + (y * sz + x) * 3;
                q[0] = (UBYTE) (l == 0 && ((x + y) & 1) == 0 ? 255 : 0);
                q[1] = (UBYTE) (l == 1 ? 255 : 0);
                q[2] = (UBYTE) (l == 0 && ((x + y) & 1) == 1 ? 255 : 0);
            }
        glTexImage2D(GL_TEXTURE_2D, (GLint) l, GL_RGB, sz, sz, 0, GL_RGB, GL_UNSIGNED_BYTE, lv);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, 0x84FE /* GL_TEXTURE_MAX_ANISOTROPY_EXT */, 1);
    Printf("checker level 0, LOD 0.25  four pixels (r g b) of the middle row\n");
    for (f = 0; f < 4; f++)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filt[f]);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        Quad(2.3784f);
        mglSwitchDisplay();
        BltBitMapRastPort(bm, 0, 0, win->RPort, win->BorderLeft, win->BorderTop, S, S, 0xc0);
        Printf("%-26s", (LONG) fname[f]);
        for (x = S / 2 - 2; x < S / 2 + 2; x++)
        {
            px = p96ReadPixel(&rp, (UWORD) x, S / 2);
            Printf("  %3ld %3ld %3ld", (LONG) ((px >> 16) & 0xff), (LONG) ((px >> 8) & 0xff),
                   (LONG) (px & 0xff));
        }
        Printf("\n");
    }
    glDeleteTextures(1, &tex);
}

int main(void)
{
    struct RDArgs *rd;
    LONG args[1];
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
        PrintFault(IoErr(), (CONST_STRPTR) "GLFilt");
        return RETURN_ERROR;
    }
    depth = (args[0] && *(LONG *) args[0] == 16) ? 16 : 32;
    IntuitionBase = (struct IntuitionBase *) OpenLibrary((CONST_STRPTR) "intuition.library", 39);
    GfxBase = (struct GfxBase *) OpenLibrary((CONST_STRPTR) "graphics.library", 39);
    P96Base = OpenLibrary((CONST_STRPTR) "Picasso96API.library", 2);
    if (IntuitionBase == NULL || GfxBase == NULL || P96Base == NULL)
        goto out;
    if (!MiniGLOpen())
    {
        Printf("GLFilt: minigl.library 29.1 did not open\n");
        goto out;
    }
    opened = TRUE;
    id = p96BestModeIDTags(P96BIDTAG_NominalWidth, 800, P96BIDTAG_NominalHeight, 600,
                           P96BIDTAG_Depth, (ULONG) depth,
                           P96BIDTAG_FormatsAllowed, (depth == 32) ? RGBFF_B8G8R8A8 : RGBFF_R5G6B5PC,
                           TAG_DONE);
    scr = (id != (ULONG) INVALID_ID)
        ? p96OpenScreenTags(P96SA_DisplayID, id, P96SA_Width, 800, P96SA_Height, 600,
                            P96SA_Depth, (ULONG) depth, P96SA_Title, (ULONG) "GLFilt", TAG_DONE)
        : NULL;
    if (scr == NULL)
    {
        Printf("GLFilt: no %ld-bit screen\n", depth);
        goto out;
    }
    win = OpenWindowTags(NULL, WA_CustomScreen, (ULONG) scr, WA_Left, 40, WA_Top, 40,
                         WA_InnerWidth, S, WA_InnerHeight, S, WA_Title, (ULONG) "GLFilt",
                         WA_DragBar, TRUE, WA_Activate, TRUE, TAG_DONE);
    bm = (win != NULL) ? AllocBitMap(S, S, GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH), BMF_MINPLANES,
                                     scr->RastPort.BitMap) : NULL;
    if (bm == NULL)
    {
        Printf("GLFilt: no window or bitmap\n");
        goto out;
    }
    InitRastPort(&rp);
    rp.BitMap = bm;
    Printf("GLFilt on a %ld-bit screen, frame %ld x %ld\n", depth, (LONG) S, (LONG) S);
    if (!mglCreateContextFromBitMap(bm))
    {
        Printf("GLFilt: no context (the library log says why)\n");
        goto out;
    }
    Cases();
    CheckerCases();
    mglDeleteContext();
    rc = RETURN_OK;

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
