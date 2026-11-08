/*
 * demo.c -- the frame loop of the RadeonGL demonstrators (demo.h).
 *
 *   <Name> FRAMES/N,WIDTH/N,HEIGHT/N,DEPTH/N,SCENESECS/N,SCENEFRAMES/N,
 *          WINDOW/S,LEFT/N,TOP/N
 *
 * FRAMES: frames to draw, 0 (the default) until Esc or Ctrl-C. WIDTH x
 * HEIGHT (640 x 480) at DEPTH 16 or 32 bits (16). With more than one
 * scene (Showcase): the next one after SCENESECS seconds (12), or after
 * SCENEFRAMES frames when that is given; the space bar skips ahead.
 * WINDOW: in a window on the Workbench instead of a screen of its own,
 * WIDTH x HEIGHT (320 x 240) inside, at LEFT, TOP (30, 30): RadeonGL takes
 * mglCreateContext's offx and offy as the window's place
 * (mglChooseWindowMode). Trio (trio.c) starts three programs that way.
 * Started from Workbench: the defaults.
 *
 * At the end: frames, seconds, frames a second, and the first GL error.
 * Return codes: 0 drawn, 5 drawn with a GL error, 20 could not draw
 * (no minigl.library 29.1, no context, a scene that could not start).
 *
 * GCC only: it links the MiniGL SDK's import library (libminigl.a, a GCC
 * archive), as GLSmoke and GLQuake do. C89 otherwise.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <devices/timer.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <proto/intuition.h>
#include <proto/minigl.h>

#include "demo.h"

#define TEMPLATE "FRAMES/N,WIDTH/N,HEIGHT/N,DEPTH/N,SCENESECS/N,SCENEFRAMES/N,WINDOW/S,LEFT/N,TOP/N"

struct Args
{
    LONG *frames;
    LONG *width;
    LONG *height;
    LONG *depth;
    LONG *scenesecs;
    LONG *sceneframes;
    LONG window;
    LONG *left;
    LONG *top;
};

struct IntuitionBase *IntuitionBase = NULL;
struct Device *TimerBase = NULL;

static struct MsgPort *clk_port = NULL;
static struct timerequest *clk_req = NULL;
static ULONG clk_freq = 709379;

/* --- maths ------------------------------------------------------------------- */

float Demo_Sin(float a)
{
    float x;
    float x2;
    LONG k;

    /* to [-0.5, 0.5] turns, then [-pi/2, pi/2] by symmetry. The nearest
     * whole turn: GCC -m68881 compiles a float to LONG cast as a ROUNDING
     * fmove.l or as fintrz + fmove.l, cast by cast (a bare fmove.l made
     * the old x + 0.5 a ceiling: up to 6.9e-3 off), so the cast only
     * comes near and the two loops settle it whichever way it went. */
    x = a * (1.0f / (2.0f * DEMO_PI));
    k = (LONG) x;
    while (x - (float) k > 0.5f)
        k++;
    while (x - (float) k < -0.5f)
        k--;
    x = (x - (float) k) * (2.0f * DEMO_PI);
    if (x > 0.5f * DEMO_PI)
        x = DEMO_PI - x;
    else if (x < -0.5f * DEMO_PI)
        x = -DEMO_PI - x;
    x2 = x * x;
    return x * (1.0f - x2 * (1.0f / 6.0f - x2 * (1.0f / 120.0f - x2 * (1.0f / 5040.0f
                - x2 * (1.0f / 362880.0f)))));
}

float Demo_Cos(float a)
{
    return Demo_Sin(a + 0.5f * DEMO_PI);
}

void Demo_Perspective(float fovy, float aspect, float znear, float zfar)
{
    float h;

    h = znear * Demo_Sin(fovy * (DEMO_PI / 360.0f)) / Demo_Cos(fovy * (DEMO_PI / 360.0f));
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum((GLdouble) (-h * aspect), (GLdouble) (h * aspect), (GLdouble) -h, (GLdouble) h,
              (GLdouble) znear, (GLdouble) zfar);
    glMatrixMode(GL_MODELVIEW);
}

void Demo_Background(float r0, float g0, float b0, float r1, float g1, float b1)
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
    glBegin(GL_QUADS);
    glColor4f(r1, g1, b1, 1.0f);
    glVertex2f(-1.0f, -1.0f);
    glVertex2f(1.0f, -1.0f);
    glColor4f(r0, g0, b0, 1.0f);
    glVertex2f(1.0f, 1.0f);
    glVertex2f(-1.0f, 1.0f);
    glEnd();
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

void Demo_Rotate(float *v, ULONG axis, float deg)
{
    float s;
    float c;
    float a;
    float b;
    ULONG i;
    ULONG j;

    s = Demo_Sin(deg * (DEMO_PI / 180.0f));
    c = Demo_Cos(deg * (DEMO_PI / 180.0f));
    i = (axis + 1) % 3;
    j = (axis + 2) % 3;
    a = v[i];
    b = v[j];
    v[i] = a * c - b * s;
    v[j] = a * s + b * c;
}

ULONG Demo_Rand(ULONG *seed)
{
    *seed = *seed * 1103515245UL + 12345UL;
    return (*seed >> 16) & 0x7fffUL;
}

ULONG Demo_Byte(float v)
{
    float f;
    LONG n;

    if (v <= 0.0f)
        return 0;
    if (v >= 1.0f)
        return 255;
    /* the nearest byte, whether the cast rounds or truncates (Demo_Sin) */
    f = v * 255.0f;
    n = (LONG) f;
    if (f - (float) n >= 0.5f)
        n++;
    else if (f - (float) n < -0.5f)
        n--;
    return (ULONG) n;
}

void Demo_Say(const char *fmt, LONG a, LONG b, LONG c, LONG d)
{
    if (Output() != 0)
        Printf((CONST_STRPTR) fmt, a, b, c, d);
}

/* --- the window's title -------------------------------------------------------- */

/* "<Name> at <left>,<top>": the program's name from its $VER, and the
 * place mglCreateContext was given (it stays while the window is open) */
static char win_title[64];

static void TitleAdd(ULONG *n, const char *s)
{
    while (*s != '\0' && *n < sizeof(win_title) - 1)
        win_title[(*n)++] = *s++;
    win_title[*n] = '\0';
}

static void TitleNum(ULONG *n, LONG v)
{
    char d[12];
    ULONG k;
    ULONG u;

    k = 0;
    u = (v < 0) ? (ULONG) -v : (ULONG) v;
    do
    {
        d[k++] = (char) ('0' + u % 10);
        u /= 10;
    } while (u != 0 && k < sizeof(d) - 1);
    if (v < 0)
        d[k++] = '-';
    while (k > 0 && *n < sizeof(win_title) - 1)
        win_title[(*n)++] = d[--k];
    win_title[*n] = '\0';
}

static const char *Title(LONG left, LONG top)
{
    const char *v;
    ULONG n;

    n = 0;
    for (v = demo_ver + 6; *v != '\0' && *v != ' ' && n < 24; v++)
        win_title[n++] = *v;
    win_title[n] = '\0';
    TitleAdd(&n, " at ");
    TitleNum(&n, left);
    TitleAdd(&n, ",");
    TitleNum(&n, top);
    return win_title;
}

/* --- the clock --------------------------------------------------------------- */

static BOOL ClockOpen(void)
{
    struct EClockVal ev;

    clk_port = CreateMsgPort();
    if (clk_port == NULL)
        return FALSE;
    clk_req = (struct timerequest *) CreateIORequest(clk_port, sizeof(struct timerequest));
    if (clk_req == NULL)
        return FALSE;
    if (OpenDevice((CONST_STRPTR) TIMERNAME, UNIT_MICROHZ, (struct IORequest *) clk_req, 0) != 0)
    {
        DeleteIORequest((struct IORequest *) clk_req);
        clk_req = NULL;
        return FALSE;
    }
    TimerBase = clk_req->tr_node.io_Device;
    clk_freq = ReadEClock(&ev);
    if (clk_freq == 0)
        clk_freq = 709379;
    return TRUE;
}

static void ClockClose(void)
{
    if (clk_req != NULL)
    {
        if (TimerBase != NULL)
            CloseDevice((struct IORequest *) clk_req);
        DeleteIORequest((struct IORequest *) clk_req);
    }
    if (clk_port != NULL)
        DeleteMsgPort(clk_port);
    clk_req = NULL;
    clk_port = NULL;
    TimerBase = NULL;
}

/* Seconds since *since. */
static float Since(const struct EClockVal *since)
{
    struct EClockVal now;
    ULONG lo;
    ULONG hi;

    ReadEClock(&now);
    lo = now.ev_lo - since->ev_lo;
    hi = now.ev_hi - since->ev_hi - ((now.ev_lo < since->ev_lo) ? 1UL : 0UL);
    return ((float) hi * 4294967296.0f + (float) lo) / (float) clk_freq;
}

/* --- input ------------------------------------------------------------------- */

#define KEY_NONE 0
#define KEY_QUIT 1
#define KEY_NEXT 2

/* Esc ends, the space bar skips to the next scene; every message replied. */
static ULONG Keys(struct Window *win)
{
    struct IntuiMessage *im;
    ULONG what;

    what = KEY_NONE;
    if (win == NULL || win->UserPort == NULL)
        return what;
    while ((im = (struct IntuiMessage *) GetMsg(win->UserPort)) != NULL)
    {
        if (im->Class == IDCMP_VANILLAKEY)
        {
            if (im->Code == 27)
                what = KEY_QUIT;
            else if (im->Code == ' ' && what == KEY_NONE)
                what = KEY_NEXT;
        }
        ReplyMsg((struct Message *) im);
    }
    return what;
}

/* --- the loop ---------------------------------------------------------------- */

int main(int argc, char **argv)
{
    struct RDArgs *rda;
    struct Args args;
    struct Window *win;
    struct EClockVal t0;
    struct EClockVal ts;
    const struct Scene *scene;
    ULONG nscenes;
    ULONG si;
    LONG frames;
    LONG w;
    LONG h;
    LONG depth;
    LONG scenesecs;
    LONG sceneframes;
    LONG left;
    LONG top;
    LONG f;
    LONG sf;
    LONG tenths;
    LONG cents;
    ULONG key;
    ULONG err;
    float secs;
    int rc;
    BOOL opened;
    BOOL running;

    (void) argv;
    rda = NULL;
    args.frames = NULL;
    args.width = NULL;
    args.height = NULL;
    args.depth = NULL;
    args.scenesecs = NULL;
    args.sceneframes = NULL;
    args.window = 0;
    args.left = NULL;
    args.top = NULL;
    if (argc != 0)
    {
        rda = ReadArgs((CONST_STRPTR) TEMPLATE, (LONG *) &args, NULL);
        if (rda == NULL)
        {
            PrintFault(IoErr(), (CONST_STRPTR) (demo_ver + 6));
            return RETURN_ERROR;
        }
    }
    frames = (args.frames && *args.frames > 0) ? *args.frames : 0;
    w = (args.width && *args.width >= 64 && *args.width <= 2048) ? *args.width
      : args.window ? 320 : 640;
    h = (args.height && *args.height >= 64 && *args.height <= 2048) ? *args.height
      : args.window ? 240 : 480;
    left = (args.left && *args.left > 0) ? *args.left : 30;
    top = (args.top && *args.top > 0) ? *args.top : 30;
    depth = (args.depth && *args.depth == 32) ? 32 : 16;
    scenesecs = (args.scenesecs && *args.scenesecs > 0) ? *args.scenesecs : 12;
    sceneframes = (args.sceneframes && *args.sceneframes > 0) ? *args.sceneframes : 0;
    for (nscenes = 0; demo_scenes[nscenes] != NULL; nscenes++)
        ;

    rc = RETURN_FAIL;
    opened = FALSE;
    win = NULL;
    scene = NULL;
    f = 0;
    secs = 0.0f;
    err = 0;
    if (args.window)
        Demo_Say("%s: a %ld x %ld window at %ld,", (LONG) (demo_ver + 6), w, h, left);
    if (args.window)
        Demo_Say("%ld\n", top, 0, 0, 0);
    else
        Demo_Say("%s: %ld x %ld x %ld\n", (LONG) (demo_ver + 6), w, h, depth);

    IntuitionBase = (struct IntuitionBase *) OpenLibrary((CONST_STRPTR) "intuition.library", 39);
    if (IntuitionBase == NULL || !ClockOpen())
    {
        Demo_Say("%s: no intuition.library 39 or timer.device\n", (LONG) (demo_ver + 6), 0, 0, 0);
        goto out;
    }
    if (!MiniGLOpen())
    {
        Demo_Say("%s: minigl.library 29.1 did not open\n", (LONG) (demo_ver + 6), 0, 0, 0);
        goto out;
    }
    opened = TRUE;
    if (args.window)
    {
        /* a window of the library's on the default public screen, at
         * offx, offy (RadeonGL; MiniGLV3D opens it at 30,30 and takes them
         * as the viewport's corner) */
        mglChooseWindowMode(GL_TRUE);
        if (!mglCreateContext((int) left, (int) top, (int) w, (int) h))
        {
            Demo_Say("%s: no window context: it needs a 15-, 16- or 32-bit screen on the "
                     "Radeon (see the library's log, RadeonGLLog)\n", (LONG) (demo_ver + 6), 0, 0, 0);
            goto out;
        }
    }
    else
    {
        mglChoosePixelDepth((int) depth);
        if (!mglCreateContext(0, 0, (int) w, (int) h))
        {
            Demo_Say("%s: no context (see the library's log, RadeonGLLog)\n",
                     (LONG) (demo_ver + 6), 0, 0, 0);
            goto out;
        }
    }
    Demo_Say("GL_RENDERER: %s\n", (LONG) glGetString(GL_RENDERER), 0, 0, 0);
    /* the keys of the library's window (it opens it with no IDCMP); a
     * window of its own says which program draws in it */
    win = (struct Window *) mglGetWindowHandle();
    if (win != NULL && args.window)
        SetWindowTitles(win, (CONST_STRPTR) Title(left, top), (CONST_STRPTR) ~0);
    if (win != NULL && !ModifyIDCMP(win, IDCMP_VANILLAKEY))
        win = NULL;
    glViewport(0, 0, (GLsizei) w, (GLsizei) h);

    si = 0;
    scene = demo_scenes[0];
    if (!scene->init())
    {
        Demo_Say("%s: the scene %s could not start\n", (LONG) (demo_ver + 6), (LONG) scene->name,
                 0, 0);
        scene = NULL;
        goto close;
    }
    ReadEClock(&t0);
    ts = t0;
    sf = 0;
    running = TRUE;
    while (running)
    {
        if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
            break;
        key = Keys(win);
        if (key == KEY_QUIT)
            break;
        scene->frame(Since(&ts), w, h);
        mglSwitchDisplay();
        f++;
        sf++;
        if (frames != 0 && f >= frames)
            break;
        if (nscenes > 1 && (key == KEY_NEXT || (sceneframes != 0 ? sf >= sceneframes
                                                 : Since(&ts) >= (float) scenesecs)))
        {
            scene->done();
            si = (si + 1) % nscenes;
            scene = demo_scenes[si];
            if (!scene->init())
            {
                Demo_Say("%s: the scene %s could not start\n", (LONG) (demo_ver + 6),
                         (LONG) scene->name, 0, 0);
                scene = NULL;
                break;
            }
            ReadEClock(&ts);
            sf = 0;
        }
    }
    secs = Since(&t0);
    rc = (scene != NULL) ? RETURN_OK : RETURN_FAIL;

close:
    if (scene != NULL)
        scene->done();
    err = (ULONG) glGetError();
    if (win != NULL)
        ModifyIDCMP(win, 0);
    mglDeleteContext();
    if (rc == RETURN_OK && err != GL_NO_ERROR)
        rc = RETURN_WARN;

out:
    if (opened)
        MiniGLClose();
    ClockClose();
    if (IntuitionBase != NULL)
        CloseLibrary((struct Library *) IntuitionBase);
    if (rda != NULL)
        FreeArgs(rda);
    if (f > 0)
    {
        /* whole hundredths and tenths first: with -m68881 a float to LONG
         * conversion ROUNDS (MEASURED on the A4000D: 14.999 s printed as
         * "14.100"), so the parts are taken from one integer */
        cents = (LONG) (secs * 100.0f);
        tenths = (secs > 0.0f) ? (LONG) ((float) f * 10.0f / secs) : 0;
        Demo_Say("%ld frames in %ld.%02ld s: %ld.", f, cents / 100, cents % 100, tenths / 10);
        Demo_Say("%ld fps; GL error 0x%lx\n", tenths % 10, (LONG) err, 0, 0);
    }
    return rc;
}
