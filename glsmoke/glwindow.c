/*
 * GLWindow -- RadeonGL's window and bitmap contexts (W1, 2026-11-07):
 *
 *   GLWindow MODE=BITMAP   a bitmap of ours (a friend of the Workbench's),
 *                          mglCreateContextFromBitMap; we blit it into our
 *                          window ourselves after each frame
 *   GLWindow MODE=WINDOW   our window, mglCreateContextFromWindow: the
 *                          library blits into its inner area
 *   GLWindow MODE=OWN      mglChooseWindowMode(GL_TRUE) + mglCreateContext:
 *                          the library's own window on the Workbench
 *   GLWindow               all three, one after the other
 *
 *   W=, H=     the inner size (320 x 240)
 *   FRAMES=    frames of the spinning cube in each (150)
 *   RESIZE     then a second size: the window made larger (WINDOW), the
 *              context made smaller inside the bitmap (BITMAP), each
 *              checked again
 *   NOCHECK    no pixel check (the null card draws nothing)
 *   SYNC       mglEnableSync(GL_TRUE)
 *   SCREEN=16|32  everything on a public screen of ours, 800 x 600 in that
 *              depth, made the default public screen for the run (for
 *              OWN) -- a 24-bit Workbench takes no GL window (the R200
 *              renders 15, 16 and 32 bit only)
 *
 * The check: a frame with a known picture -- blue clear, a red square in
 * the middle nearer than a green rectangle over the left half that is
 * drawn after it -- read back at four points with p96ReadPixel. The red
 * point only stays red when the depth test works. Prints PASS or FAIL for
 * each; rc 0 when everything passed, 5 when a check failed, 20 when a
 * context could not be made. Esc or the close gadget stop early.
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

#define TEMPLATE "MODE/K,W/N,H/N,FRAMES/N,RESIZE/S,NOCHECK/S,SYNC/S,SCREEN/K/N"
#define M_BITMAP 0
#define M_WINDOW 1
#define M_OWN    2

const char ver[] = "$VER: GLWindow 0.1 (07.11.2026)";
struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *P96Base;

static LONG failed;
static LONG passed;
static BOOL nocheck;
static BOOL sync;
static BOOL stop;
static struct Screen *ourscr;       /* SCREEN=: windows go there */

/* --- the pictures ------------------------------------------------------------------ */

static void Ortho(GLint w, GLint h)
{
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (GLdouble) w, (GLdouble) h, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void Quad(GLfloat x0, GLfloat y0, GLfloat x1, GLfloat y1, GLfloat z)
{
    glBegin(GL_QUADS);
    glVertex3f(x0, y0, z);
    glVertex3f(x1, y0, z);
    glVertex3f(x1, y1, z);
    glVertex3f(x0, y1, z);
    glEnd();
}

/* The known picture: blue, a red square in the middle (eye z +0.5: near),
 * then a green rectangle over the left half (eye z -0.5: far) drawn after */
static void TestPicture(GLint w, GLint h)
{
    Ortho(w, h);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glColor3f(1.0f, 0.0f, 0.0f);
    Quad(w / 4.0f, h / 4.0f, w * 3.0f / 4.0f, h * 3.0f / 4.0f, 0.5f);
    glColor3f(0.0f, 1.0f, 0.0f);
    Quad(0.0f, 0.0f, w / 2.0f, (GLfloat) h, -0.5f);
}

/* A cube turning, its faces in six colours, with the depth test */
static void Cube(GLint w, GLint h, LONG f)
{
    static const GLfloat v[8][3] = {
        { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 },
        { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 } };
    static const int face[6][4] = {
        { 0, 1, 2, 3 }, { 4, 7, 6, 5 }, { 0, 4, 5, 1 }, { 3, 2, 6, 7 }, { 0, 3, 7, 4 }, { 1, 5, 6, 2 } };
    static const GLfloat col[6][3] = {
        { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 1, 1, 0 }, { 1, 0, 1 }, { 0, 1, 1 } };
    GLfloat a;
    int i, j;

    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    a = (GLfloat) w / (GLfloat) h;
    glFrustum(-a * 0.5, a * 0.5, -0.5, 0.5, 1.0, 20.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -5.0f);
    glRotatef((GLfloat) (f * 3 % 360), 1.0f, 0.0f, 0.0f);
    glRotatef((GLfloat) (f * 2 % 360), 0.0f, 1.0f, 0.0f);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.15f, 0.15f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBegin(GL_QUADS);
    for (i = 0; i < 6; i++)
    {
        glColor3f(col[i][0], col[i][1], col[i][2]);
        for (j = 0; j < 4; j++)
            glVertex3f(v[face[i][j]][0], v[face[i][j]][1], v[face[i][j]][2]);
    }
    glEnd();
}

/* --- the check --------------------------------------------------------------------- */

/* 'r', 'g', 'b', or '?' for a pixel that is none of them */
static char Hue(ULONG argb)
{
    ULONG r, g, b;

    r = (argb >> 16) & 0xff;
    g = (argb >> 8) & 0xff;
    b = argb & 0xff;
    if (r >= 0x80 && g < 0x40 && b < 0x40)
        return 'r';
    if (g >= 0x80 && r < 0x40 && b < 0x40)
        return 'g';
    if (b >= 0x80 && r < 0x40 && g < 0x40)
        return 'b';
    return '?';
}

/* Four points of TestPicture, read from rp at (ox, oy) + the point */
static void Check(const char *what, struct RastPort *rp, LONG ox, LONG oy, GLint w, GLint h)
{
    static const char want[4] = { 'g', 'r', 'b', 'g' };
    LONG x[4], y[4];
    UBYTE rgba[4];
    ULONG px;
    char got[5];
    int i;
    BOOL ok;

    if (nocheck)
        return;
    x[0] = w / 8;         y[0] = h / 2;        /* the green half, outside the square */
    x[1] = w / 2 - 2;     y[1] = h / 2;        /* the square, under the green: depth */
    x[2] = w * 7 / 8;     y[2] = h / 2;        /* the clear colour */
    x[3] = w * 3 / 8;     y[3] = h / 8;        /* green above the square */
    ok = TRUE;
    for (i = 0; i < 4; i++)
    {
        px = p96ReadPixel(rp, (UWORD) (ox + x[i]), (UWORD) (oy + y[i]));
        got[i] = Hue(px);
        if (got[i] != want[i])
        {
            ok = FALSE;
            Printf("  %s: (%ld,%ld) is $%06lx, want %lc\n", (LONG) what, x[i], y[i], px & 0xffffffUL,
                   (LONG) want[i]);
        }
        /* the same point through glReadPixels (GL counts rows from the bottom) */
        rgba[0] = rgba[1] = rgba[2] = rgba[3] = 0x55;
        glReadPixels(x[i], h - 1 - y[i], 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        px = ((ULONG) rgba[0] << 16) | ((ULONG) rgba[1] << 8) | rgba[2];
        if (Hue(px) != want[i])
        {
            ok = FALSE;
            Printf("  %s: glReadPixels(%ld,%ld) is $%06lx, want %lc\n", (LONG) what, x[i], h - 1 - y[i],
                   px, (LONG) want[i]);
        }
    }
    got[4] = 0;
    Printf("%s %s (%ld x %ld): %s\n", (LONG) (ok ? "PASS" : "FAIL"), (LONG) what, (LONG) w, (LONG) h,
           (LONG) got);
    if (ok)
        passed++;
    else
        failed++;
}

/* --- our window -------------------------------------------------------------------- */

static struct Window *OpenTestWindow(const char *title, LONG w, LONG h)
{
    return OpenWindowTags(NULL,
        ourscr != NULL ? WA_CustomScreen : TAG_IGNORE, (ULONG) ourscr,
        WA_Left, 60,
        WA_Top, 40,
        WA_InnerWidth, (ULONG) w,
        WA_InnerHeight, (ULONG) h,
        WA_Title, (ULONG) title,
        WA_DragBar, TRUE,
        WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE,
        WA_SizeBBottom, TRUE,
        WA_MinWidth, 80,
        WA_MinHeight, 60,
        WA_MaxWidth, 2048,
        WA_MaxHeight, 2048,
        WA_Activate, TRUE,
        WA_SimpleRefresh, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_VANILLAKEY | IDCMP_NEWSIZE | IDCMP_REFRESHWINDOW,
        TAG_DONE);
}

/* Messages of win; *resized TRUE on a new size */
static void Poll(struct Window *win, BOOL *resized)
{
    struct IntuiMessage *im;

    while (win != NULL && win->UserPort != NULL
           && (im = (struct IntuiMessage *) GetMsg(win->UserPort)) != NULL)
    {
        if (im->Class == IDCMP_CLOSEWINDOW || (im->Class == IDCMP_VANILLAKEY && im->Code == 27))
            stop = TRUE;
        else if (im->Class == IDCMP_NEWSIZE && resized != NULL)
            *resized = TRUE;
        else if (im->Class == IDCMP_REFRESHWINDOW)
        {
            BeginRefresh(win);
            EndRefresh(win, TRUE);
        }
        ReplyMsg((struct Message *) im);
    }
    if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
        stop = TRUE;
}

static void InnerSize(struct Window *win, GLint *w, GLint *h)
{
    *w = win->Width - win->BorderLeft - win->BorderRight;
    *h = win->Height - win->BorderTop - win->BorderBottom;
}

static LONG Ticks(const struct DateStamp *t0, const struct DateStamp *t1)
{
    return (t1->ds_Days - t0->ds_Days) * 24 * 60 * 60 * TICKS_PER_SECOND
         + (t1->ds_Minute - t0->ds_Minute) * 60 * TICKS_PER_SECOND + (t1->ds_Tick - t0->ds_Tick);
}

static void Rate(const char *what, LONG frames, const struct DateStamp *t0)
{
    struct DateStamp t1;
    LONG t;

    DateStamp(&t1);
    t = Ticks(t0, &t1);
    Printf("  %s: %ld frames in %ld ticks = %ld.%ld frames a second\n", (LONG) what, frames, t,
           t > 0 ? frames * TICKS_PER_SECOND / t : 0L, t > 0 ? (frames * TICKS_PER_SECOND * 10 / t) % 10 : 0L);
}

/* --- the three ways ------------------------------------------------------------------- */

/* MiniGL renders INTO the program's bitmap (as MiniGLV3D does): what the
 * program drew there stays where GL draws nothing. Our green, then GL's
 * red square without a clear: green around it -- not the blue the last
 * GL frame left on the right */
static void Composite(struct Window *win, struct BitMap *bm, struct RastPort *rp, GLint w, GLint h)
{
    LONG x[3], y[3];
    static const char want[3] = { 'g', 'r', 'g' };
    char got[4];
    ULONG px;
    int i;
    BOOL ok;

    if (nocheck || stop)
        return;
    p96RectFill(rp, 0, 0, (UWORD) (w - 1), (UWORD) (h - 1), 0xff00ff00UL);
    Ortho(w, h);
    glDisable(GL_DEPTH_TEST);
    glColor3f(1.0f, 0.0f, 0.0f);
    Quad(w / 4.0f, h / 4.0f, w * 3.0f / 4.0f, h * 3.0f / 4.0f, 0.0f);
    mglSwitchDisplay();
    BltBitMapRastPort(bm, 0, 0, win->RPort, win->BorderLeft, win->BorderTop, w, h, 0xc0);
    x[0] = w / 8;      y[0] = h / 8;
    x[1] = w / 2;      y[1] = h / 2;
    x[2] = w * 7 / 8;  y[2] = h / 2;
    ok = TRUE;
    for (i = 0; i < 3; i++)
    {
        px = p96ReadPixel(rp, (UWORD) x[i], (UWORD) y[i]);
        got[i] = Hue(px);
        if (got[i] != want[i])
            ok = FALSE;
    }
    got[3] = 0;
    Printf("%s bitmap, GL over the program's own drawing: %s (want grg)\n", (LONG) (ok ? "PASS" : "FAIL"),
           (LONG) got);
    if (ok)
        passed++;
    else
        failed++;
}

/* The program's bitmap: drawn by the library, shown by us */
static BOOL RunBitMap(LONG w, LONG h, LONG frames, BOOL resize)
{
    struct Window *win;
    struct BitMap *bm;
    struct RastPort rp;
    struct DateStamp t0;
    GLint cw, ch;
    LONG f;

    win = OpenTestWindow("GLWindow: mglCreateContextFromBitMap", w, h);
    if (win == NULL)
    {
        Printf("FAIL bitmap: our window did not open\n");
        failed++;
        return FALSE;
    }
    bm = AllocBitMap((ULONG) w, (ULONG) h, GetBitMapAttr(win->WScreen->RastPort.BitMap, BMA_DEPTH),
                     BMF_MINPLANES, win->WScreen->RastPort.BitMap);
    if (bm == NULL)
    {
        Printf("FAIL bitmap: no bitmap\n");
        failed++;
        CloseWindow(win);
        return FALSE;
    }
    Printf("  the Workbench's bitmap: P96 %ld, format %ld, %ld bytes a pixel; ours: P96 %ld, format %ld, "
           "%ld bytes a pixel, %ld x %ld, on the board %ld\n",
           p96GetBitMapAttr(win->WScreen->RastPort.BitMap, P96BMA_ISP96),
           p96GetBitMapAttr(win->WScreen->RastPort.BitMap, P96BMA_RGBFORMAT),
           p96GetBitMapAttr(win->WScreen->RastPort.BitMap, P96BMA_BYTESPERPIXEL),
           p96GetBitMapAttr(bm, P96BMA_ISP96), p96GetBitMapAttr(bm, P96BMA_RGBFORMAT),
           p96GetBitMapAttr(bm, P96BMA_BYTESPERPIXEL), (LONG) GetBitMapAttr(bm, BMA_WIDTH),
           (LONG) GetBitMapAttr(bm, BMA_HEIGHT), p96GetBitMapAttr(bm, P96BMA_ISONBOARD));
    if (!mglCreateContextFromBitMap(bm))
    {
        Printf("FAIL bitmap: mglCreateContextFromBitMap gave no context (the library log says why)\n");
        failed++;
        FreeBitMap(bm);
        CloseWindow(win);
        return FALSE;
    }
    if (sync)
        mglEnableSync(GL_TRUE);
    InitRastPort(&rp);
    rp.BitMap = bm;
    cw = (GLint) w;
    ch = (GLint) h;
    DateStamp(&t0);
    for (f = 0; f < frames && !stop; f++)
    {
        Cube(cw, ch, f);
        mglSwitchDisplay();
        BltBitMapRastPort(bm, 0, 0, win->RPort, win->BorderLeft, win->BorderTop, cw, ch, 0xc0);
        Poll(win, NULL);
    }
    Rate("bitmap", f, &t0);
    TestPicture(cw, ch);
    mglSwitchDisplay();
    BltBitMapRastPort(bm, 0, 0, win->RPort, win->BorderLeft, win->BorderTop, cw, ch, 0xc0);
    Check("bitmap", &rp, 0, 0, cw, ch);
    Composite(win, bm, &rp, cw, ch);
    if (resize && !stop)
    {
        /* smaller inside the same bitmap: the frame in its top left corner */
        cw = (GLint) (w / 2);
        ch = (GLint) (h / 2);
        if (!mglResizeContext(cw, ch))
        {
            Printf("FAIL bitmap: mglResizeContext(%ld, %ld) refused\n", (LONG) cw, (LONG) ch);
            failed++;
        }
        else
        {
            for (f = 0; f < frames / 2 && !stop; f++)
            {
                Cube(cw, ch, f);
                mglSwitchDisplay();
                BltBitMapRastPort(bm, 0, 0, win->RPort, win->BorderLeft, win->BorderTop, w, h, 0xc0);
                Poll(win, NULL);
            }
            TestPicture(cw, ch);
            mglSwitchDisplay();
            BltBitMapRastPort(bm, 0, 0, win->RPort, win->BorderLeft, win->BorderTop, w, h, 0xc0);
            Check("bitmap, resized", &rp, 0, 0, cw, ch);
        }
        if (mglResizeContext((GLint) (w * 2), (GLint) h))
        {
            Printf("FAIL bitmap: mglResizeContext beyond the bitmap was accepted\n");
            failed++;
        }
        else
            Printf("PASS bitmap: mglResizeContext beyond the bitmap refused\n");
    }
    mglDeleteContext();
    FreeBitMap(bm);
    CloseWindow(win);
    return TRUE;
}

/* Our window: drawn and blitted by the library */
static BOOL RunWindow(LONG w, LONG h, LONG frames, BOOL resize)
{
    struct Window *win;
    struct DateStamp t0;
    GLint cw, ch;
    LONG f;
    BOOL resized;

    win = OpenTestWindow("GLWindow: mglCreateContextFromWindow", w, h);
    if (win == NULL)
    {
        Printf("FAIL window: our window did not open\n");
        failed++;
        return FALSE;
    }
    if (!mglCreateContextFromWindow(win))
    {
        Printf("FAIL window: mglCreateContextFromWindow gave no context (the library log says why)\n");
        failed++;
        CloseWindow(win);
        return FALSE;
    }
    if (sync)
        mglEnableSync(GL_TRUE);
    InnerSize(win, &cw, &ch);
    DateStamp(&t0);
    for (f = 0; f < frames && !stop; f++)
    {
        resized = FALSE;
        Poll(win, &resized);
        if (resized)
        {
            InnerSize(win, &cw, &ch);
            if (!mglResizeContext(cw, ch))
                Printf("  window: mglResizeContext(%ld, %ld) refused\n", (LONG) cw, (LONG) ch);
        }
        Cube(cw, ch, f);
        mglSwitchDisplay();
    }
    Rate("window", f, &t0);
    TestPicture(cw, ch);
    mglSwitchDisplay();
    Check("window", win->RPort, win->BorderLeft, win->BorderTop, cw, ch);
    if (resize && !stop)
    {
        ChangeWindowBox(win, win->LeftEdge, win->TopEdge, win->Width + w / 2, win->Height + h / 3);
        resized = FALSE;
        for (f = 0; f < 100 && !resized && !stop; f++)
        {
            WaitTOF();
            Poll(win, &resized);
        }
        InnerSize(win, &cw, &ch);
        if (!mglResizeContext(cw, ch))
        {
            Printf("FAIL window: mglResizeContext(%ld, %ld) refused\n", (LONG) cw, (LONG) ch);
            failed++;
        }
        else
        {
            for (f = 0; f < frames / 2 && !stop; f++)
            {
                Cube(cw, ch, f);
                mglSwitchDisplay();
                Poll(win, NULL);
            }
            TestPicture(cw, ch);
            mglSwitchDisplay();
            Check("window, resized", win->RPort, win->BorderLeft, win->BorderTop, cw, ch);
        }
    }
    mglDeleteContext();
    CloseWindow(win);
    return TRUE;
}

/* The library's own window */
static BOOL RunOwn(LONG w, LONG h, LONG frames)
{
    struct Window *win;
    struct DateStamp t0;
    GLint cw, ch;
    LONG f;

    mglChooseWindowMode(GL_TRUE);
    if (!mglCreateContext(80, 60, (int) w, (int) h))
    {
        Printf("FAIL own window: mglCreateContext in window mode gave no context (the library log says why)\n");
        failed++;
        return FALSE;
    }
    if (sync)
        mglEnableSync(GL_TRUE);
    win = (struct Window *) mglGetWindowHandle();
    if (win == NULL)
    {
        Printf("FAIL own window: mglGetWindowHandle is NULL\n");
        failed++;
        mglDeleteContext();
        return FALSE;
    }
    ModifyIDCMP(win, IDCMP_VANILLAKEY);
    InnerSize(win, &cw, &ch);
    DateStamp(&t0);
    for (f = 0; f < frames && !stop; f++)
    {
        Cube(cw, ch, f);
        mglSwitchDisplay();
        Poll(win, NULL);
    }
    Rate("own window", f, &t0);
    TestPicture(cw, ch);
    mglSwitchDisplay();
    Check("own window", win->RPort, win->BorderLeft, win->BorderTop, cw, ch);
    mglDeleteContext();
    return TRUE;
}

/* SCREEN=16|32: a public screen "GLWindow" on the best Radeon mode for
 * 800 x 600 in that depth, the default public screen until the end */
static char olddefault[MAXPUBSCREENNAME + 1];

static BOOL OpenOurScreen(LONG depth)
{
    ULONG id;

    id = p96BestModeIDTags(P96BIDTAG_NominalWidth, 800, P96BIDTAG_NominalHeight, 600,
                           P96BIDTAG_Depth, (ULONG) depth,
                           P96BIDTAG_FormatsAllowed, (depth == 32) ? RGBFF_B8G8R8A8 : RGBFF_R5G6B5PC,
                           TAG_DONE);
    if (id == (ULONG) INVALID_ID)
        return FALSE;
    ourscr = p96OpenScreenTags(P96SA_DisplayID, id, P96SA_Width, 800, P96SA_Height, 600,
                               P96SA_Depth, (ULONG) depth, P96SA_Title, (ULONG) "GLWindow",
                               P96SA_PubName, (ULONG) "GLWindow", P96SA_AutoScroll, FALSE, TAG_DONE);
    if (ourscr == NULL)
        return FALSE;
    PubScreenStatus(ourscr, 0);
    GetDefaultPubScreen((UBYTE *) olddefault);
    SetDefaultPubScreen((UBYTE *) "GLWindow");
    Printf("  our screen: $%08lx, 800 x 600, %ld bit\n", id, depth);
    return TRUE;
}

static void CloseOurScreen(void)
{
    if (ourscr == NULL)
        return;
    SetDefaultPubScreen(olddefault[0] != 0 ? (UBYTE *) olddefault : NULL);
    while (!p96CloseScreen(ourscr))
        Delay(25);
    ourscr = NULL;
}

int main(void)
{
    struct RDArgs *rd;
    LONG args[8];
    LONG w, h, frames;
    BOOL opened, made;
    const char *mode;
    int rc;

    rc = RETURN_FAIL;
    opened = FALSE;
    made = FALSE;
    memset(args, 0, sizeof(args));
    rd = ReadArgs((CONST_STRPTR) TEMPLATE, args, NULL);
    if (rd == NULL)
    {
        PrintFault(IoErr(), (CONST_STRPTR) "GLWindow");
        return RETURN_ERROR;
    }
    mode = args[0] ? (const char *) args[0] : "ALL";
    w = args[1] ? *(LONG *) args[1] : 320;
    h = args[2] ? *(LONG *) args[2] : 240;
    frames = args[3] ? *(LONG *) args[3] : 150;
    nocheck = (BOOL) (args[5] != 0);
    sync = (BOOL) (args[6] != 0);
    IntuitionBase = (struct IntuitionBase *) OpenLibrary((CONST_STRPTR) "intuition.library", 39);
    GfxBase = (struct GfxBase *) OpenLibrary((CONST_STRPTR) "graphics.library", 39);
    P96Base = OpenLibrary((CONST_STRPTR) "Picasso96API.library", 2);
    if (IntuitionBase == NULL || GfxBase == NULL || P96Base == NULL)
    {
        Printf("GLWindow: needs intuition and graphics V39 and Picasso96API.library\n");
        goto out;
    }
    if (!MiniGLOpen())
    {
        Printf("GLWindow: minigl.library 29.1 did not open\n");
        goto out;
    }
    opened = TRUE;
    if (args[7] != 0 && !OpenOurScreen(*(LONG *) args[7]))
    {
        Printf("GLWindow: our %ld-bit screen did not open\n", *(LONG *) args[7]);
        goto out;
    }
    if ((mode[0] == 'A' || mode[0] == 'a' || mode[0] == 'B' || mode[0] == 'b') && !stop)
        made |= RunBitMap(w, h, frames, (BOOL) (args[4] != 0));
    if ((mode[0] == 'A' || mode[0] == 'a' || mode[0] == 'W' || mode[0] == 'w') && !stop)
        made |= RunWindow(w, h, frames, (BOOL) (args[4] != 0));
    if ((mode[0] == 'A' || mode[0] == 'a' || mode[0] == 'O' || mode[0] == 'o') && !stop)
        made |= RunOwn(w, h, frames);
    Printf("GLWindow: %ld passed, %ld failed%s\n", passed, failed, (LONG) (stop ? " (stopped)" : ""));
    rc = !made ? RETURN_FAIL : (failed != 0 ? RETURN_WARN : RETURN_OK);

out:
    CloseOurScreen();
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
