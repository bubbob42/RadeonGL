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
 *   GLFilt [DEPTH=16|32] [HOLD=seconds] [SLANT]
 *
 * HOLD (2026-11-25, the user: "keep the test open for 10 secs each,
 * otherwise I can't see anything due to the monitor switching"): each
 * picture stays that long, its case in the window title, after a wait of
 * 3 s for the monitor to take the screen. SLANT: the slanted floor only.
 *
 * 0.4 (2026-11-25): a slanted floor, anisotropy 1, 8 and 16 (SlantCases).
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
#include <stdio.h>

#define TEMPLATE "DEPTH/K/N,HOLD/K/N,SLANT/S"
#define S 128

const char ver[] = "$VER: GLFilt 0.5 (25.11.2026)";
struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *P96Base;

static struct Screen *scr;
static struct Window *win;
static struct BitMap *bm;
static struct RastPort rp;
static LONG hold;                   /* HOLD: seconds a picture stays (0: none) */
#define ZOOM 3                      /* HOLD: the picture 3 x (the user: "the window should be larger") */
#define TEXT_W 460                  /* HOLD: the description, under the picture */
static LONG text_h;                 /* its height: four lines of the screen font */
static ULONG *zsrc, *zdst;          /* HOLD: the frame and its zoomed copy (ARGB) */

/* The frame into the window: 1:1, or with HOLD read back (the measured
 * pixels stay the bitmap's), enlarged ZOOM x by pixel repetition and
 * written through Picasso96 */
static void ShowFrame(void)
{
    struct RenderInfo ri;
    LONG x, y, d;
    ULONG v;
    ULONG *o;

    if (hold <= 0 || zsrc == NULL || zdst == NULL)
    {
        BltBitMapRastPort(bm, 0, 0, win->RPort, win->BorderLeft, win->BorderTop, S, S, 0xc0);
        return;
    }
    ri.Memory = zsrc;
    ri.BytesPerRow = S * 4;
    ri.pad = 0;
    ri.RGBFormat = RGBFB_A8R8G8B8;
    p96ReadPixelArray(&ri, 0, 0, &rp, 0, 0, S, S);
    for (y = 0; y < S; y++)
    {
        o = zdst + (ULONG) y * ZOOM * S * ZOOM;
        for (x = 0; x < S; x++)
        {
            v = zsrc[y * S + x];
            for (d = 0; d < ZOOM; d++)
                *o++ = v;
        }
        for (d = 1; d < ZOOM; d++)
            memcpy(zdst + ((ULONG) y * ZOOM + d) * S * ZOOM, zdst + (ULONG) y * ZOOM * S * ZOOM,
                   S * ZOOM * 4);
    }
    ri.Memory = zdst;
    ri.BytesPerRow = S * ZOOM * 4;
    p96WritePixelArray(&ri, 0, 0, win->RPort, win->BorderLeft, win->BorderTop, S * ZOOM, S * ZOOM);
}

/* HOLD: the picture just shown stays, described beside it in the window
 * (the user, 2026-11-25: "i'd need a description blitzed in the corner to
 * see which settings are which") -- up to four lines, the last two a
 * colour legend -- and in the screen's title bar */
static void Hold(const char *l1, const char *l2, const char *l3, const char *l4)
{
    struct DrawInfo *di;
    struct RastPort *w;
    const char *line[4];
    LONG x0, y0, i, fg, bg;

    if (hold <= 0)
        return;
    w = win->RPort;
    di = GetScreenDrawInfo(scr);
    fg = (di != NULL) ? di->dri_Pens[TEXTPEN] : 1;
    bg = (di != NULL) ? di->dri_Pens[BACKGROUNDPEN] : 0;
    x0 = win->BorderLeft + 4;
    y0 = win->BorderTop + S * ZOOM;
    SetAPen(w, (ULONG) bg);
    RectFill(w, x0, y0, x0 + TEXT_W - 1, y0 + text_h - 1);
    SetAPen(w, (ULONG) fg);
    SetBPen(w, (ULONG) bg);
    SetDrMd(w, JAM2);
    line[0] = l1;
    line[1] = l2;
    line[2] = l3;
    line[3] = l4;
    for (i = 0; i < 4; i++)
    {
        if (line[i] == NULL || line[i][0] == '\0')
            continue;
        Move(w, x0, y0 + 4 + w->TxBaseline + i * (w->TxHeight + 6) + (i >= 2 ? 10 : 0));
        Text(w, (STRPTR) line[i], (ULONG) strlen(line[i]));
    }
    if (di != NULL)
        FreeScreenDrawInfo(scr, di);
    SetWindowTitles(win, (UBYTE *) ~0, (UBYTE *) l2);
    Delay(hold * 50);
}

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
            ShowFrame();
            px = p96ReadPixel(&rp, S / 2, S / 2);
            Printf("%-26s  %s  %3ld %3ld %3ld\n", (LONG) fname[f], (LONG) lname[j],
                   (LONG) ((px >> 16) & 0xff), (LONG) ((px >> 8) & 0xff), (LONG) (px & 0xff));
            if (hold > 0)
            {
                static char l2[64];
                static char l3[40];

                sprintf(l2, "%s, anisotropy %ld", fname[f], (long) a);
                sprintf(l3, "level of detail %s", lname[j]);
                Hold("Flat quad: the centre pixel", l2, l3,
                     "levels: red 0, green 1, blue 2+");
            }
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
        ShowFrame();
        Printf("%-26s", (LONG) fname[f]);
        for (x = S / 2 - 2; x < S / 2 + 2; x++)
        {
            px = p96ReadPixel(&rp, (UWORD) x, S / 2);
            Printf("  %3ld %3ld %3ld", (LONG) ((px >> 16) & 0xff), (LONG) ((px >> 8) & 0xff),
                   (LONG) (px & 0xff));
        }
        Printf("\n");
        Hold("Checker on level 0, LOD 0.25", fname[f], "red / blue texels: level 0",
             "green: level 1 blended in");
    }
    glDeleteTextures(1, &tex);
}

/* 2026-11-25 (the user wants anisotropy in GLQuake): a floor in
 * perspective, the texture tiled along it, its levels solid colours (0 red,
 * 1 green, 2 blue, 3 yellow, 4 magenta, 5 cyan, 6 white). One column of
 * the frame from the near edge to the horizon, each pixel as the level it
 * shows -- a digit -- or '+' for a blend of two levels ('.' background).
 * Trilinear blends between levels ('+' runs where the digits change);
 * anisotropy keeps the low levels further off (the digits change later).
 * The question of 2026-11-21: does the R200 blend levels under anisotropy
 * (ANISO_NEAREST_MIP_LINEAR, 14)? The square-on quads above never made it
 * sample anisotropically. */
static const UBYTE slant_rgb[7][3] = {
    { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 }, { 255, 255, 0 },
    { 255, 0, 255 }, { 0, 255, 255 }, { 255, 255, 255 } };

static char SlantClass(ULONG px)
{
    LONG r, g, b, k, d, best, bestd;

    r = (LONG) ((px >> 16) & 0xff);
    g = (LONG) ((px >> 8) & 0xff);
    b = (LONG) (px & 0xff);
    if (r < 24 && g < 24 && b < 24)
        return '.';
    best = 0;
    bestd = 1000;
    for (k = 0; k < 7; k++)
    {
        d = (r > slant_rgb[k][0] ? r - slant_rgb[k][0] : slant_rgb[k][0] - r)
          + (g > slant_rgb[k][1] ? g - slant_rgb[k][1] : slant_rgb[k][1] - g)
          + (b > slant_rgb[k][2] ? b - slant_rgb[k][2] : slant_rgb[k][2] - b);
        if (d < bestd)
        {
            bestd = d;
            best = k;
        }
    }
    return (bestd <= 36) ? (char) ('0' + best) : '+';
}

static void SlantCases(void)
{
    static UBYTE lv[64 * 64 * 3];
    static const GLenum filt[2] = { GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR_MIPMAP_LINEAR };
    static const char *const fname[2] = { "LINEAR_MIPMAP_NEAREST", "LINEAR_MIPMAP_LINEAR" };
    static const LONG an[3] = { 1, 8, 16 };
    char col[S + 1];
    GLuint tex;
    LONG l, sz, i, f, a, y, n, blends;
    ULONG px;

    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    for (l = 0, sz = 64; sz >= 1; l++, sz >>= 1)
    {
        for (i = 0; i < sz * sz; i++)
        {
            lv[i * 3] = slant_rgb[l][0];
            lv[i * 3 + 1] = slant_rgb[l][1];
            lv[i * 3 + 2] = slant_rgb[l][2];
        }
        glTexImage2D(GL_TEXTURE_2D, (GLint) l, GL_RGB8, sz, sz, 0, GL_RGB, GL_UNSIGNED_BYTE, lv);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    /* its own state (SLANT skips Cases, which set it before: the floor
     * came out white, untextured -- the user's photo, 2026-11-25) */
    glViewport(0, 0, S, S);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnable(GL_TEXTURE_2D);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.5, 0.5, -0.5, 0.5, 0.5, 200.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    Printf("slanted floor: column x %ld from the near edge (left) to the horizon;\n"
           "  digit = the level shown, + = a blend of two, . = no floor\n", (LONG) (S / 2));
    for (a = 0; a < 3; a++)
        for (f = 0; f < 2; f++)
        {
            glTexParameteri(GL_TEXTURE_2D, 0x84FE /* GL_TEXTURE_MAX_ANISOTROPY_EXT */, an[a]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filt[f]);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glBegin(GL_QUADS);      /* y -1, z from -0.6 to -200, 2 tiles across, 400 along */
            glTexCoord2f(0.0f, 0.0f);   glVertex3f(-1.0f, -1.0f, -0.6f);
            glTexCoord2f(2.0f, 0.0f);   glVertex3f(1.0f, -1.0f, -0.6f);
            glTexCoord2f(2.0f, 400.0f); glVertex3f(1.0f, -1.0f, -200.0f);
            glTexCoord2f(0.0f, 400.0f); glVertex3f(-1.0f, -1.0f, -200.0f);
            glEnd();
            mglSwitchDisplay();
            ShowFrame();
            n = 0;
            blends = 0;
            for (y = S - 1; y >= S / 2 - 2; y--)
            {
                px = p96ReadPixel(&rp, S / 2, (UWORD) y);
                col[n] = SlantClass(px);
                if (col[n] == '+')
                    blends++;
                n++;
            }
            col[n] = 0;
            Printf("aniso %2ld %-22s %3ld blends  %s\n", an[a], (LONG) fname[f], blends, (LONG) col);
            if (hold > 0)
            {
                static char l2[64];

                sprintf(l2, "anisotropy %ld, %s", (long) an[a], f ? "trilinear" : "bilinear");
                Hold("Slanted floor, level by colour", l2,
                     "red 0  green 1  blue 2  yellow 3",
                     "magenta 4  cyan 5  white 6");
            }
        }
    glDeleteTextures(1, &tex);
}

int main(void)
{
    struct RDArgs *rd;
    LONG args[3];
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
    hold = args[1] ? *(LONG *) args[1] : 0;
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
    /* the pointer's colours (palette 17-19) as the Workbench's: on a
     * Picasso96 screen of our own they were black, and the pointer stayed
     * black back on the Workbench (the user, 2026-11-25) */
    {
        struct Screen *wb;
        ULONG rgb[3];
        ULONG k;

        wb = LockPubScreen(NULL);
        if (wb != NULL)
        {
            for (k = 17; k <= 19; k++)
            {
                GetRGB32(wb->ViewPort.ColorMap, k, 1, rgb);
                SetRGB32(&scr->ViewPort, k, rgb[0], rgb[1], rgb[2]);
            }
            UnlockPubScreen(NULL, wb);
        }
    }
    text_h = 4 * ((scr->Font != NULL ? scr->Font->ta_YSize : 8) + 6) + 20;
    if (hold > 0)
    {
        zsrc = (ULONG *) AllocVec(S * S * 4, MEMF_ANY);
        zdst = (ULONG *) AllocVec(S * ZOOM * S * ZOOM * 4, MEMF_ANY);
    }
    win = OpenWindowTags(NULL, WA_CustomScreen, (ULONG) scr, WA_Left, 40, WA_Top, (hold > 0 ? 20 : 40),
                         WA_InnerWidth, (hold > 0 ? (S * ZOOM > TEXT_W + 8 ? S * ZOOM : TEXT_W + 8) : S),
                         WA_InnerHeight, (hold > 0 ? S * ZOOM + text_h : S), WA_Title, (ULONG) "GLFilt",
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
    if (hold > 0)
    {
        SetWindowTitles(win, (UBYTE *) ~0, (UBYTE *) "GLFilt: the monitor takes the screen");
        Delay(3 * 50);
    }
    if (!args[2])
    {
        Cases();
        CheckerCases();
    }
    SlantCases();
    mglDeleteContext();
    rc = RETURN_OK;

out:
    if (zsrc != NULL)
        FreeVec(zsrc);
    if (zdst != NULL)
        FreeVec(zdst);
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
