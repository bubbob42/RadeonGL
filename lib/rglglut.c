/*
 * rglglut.c -- GLUT, as minigl.library 29.1 has it (22 calls; 2026-11-08):
 * one window (or a game-mode screen) a program, its display, idle,
 * keyboard and reshape callbacks, glutGet and glutGameModeGet, and five
 * solid shapes.
 *
 * Over MiniGL's own calls, as in 29.1: glutCreateWindow is
 * mglChooseWindowMode(GL_TRUE) + MGLCreateContext at glutInitWindowPosition
 * (RadeonGL places its window there; GLUT's unset position, -1, means the
 * corner), glutEnterGameMode the same without a window, glutMainLoop
 * MGLMainLoop with trampolines: reshape before the first frame and after
 * the window changed size (the context follows it, mglResizeContext), the
 * display callback only when a redisplay is posted, the idle callback
 * every round; with neither to do, the loop waits for input. The close
 * gadget ends the loop (GLUT ends the program there; glutMainLoop returns,
 * and the program's own exit follows). A keystroke reports the pointer's
 * last place, top-left origin, as GLUT does.
 *
 * Unlike 29.1, the state is the TASK's (the library serves several
 * programs at once, shell.c), and the shapes draw through the task's
 * dispatch table, so a display list being made records them (rgldlist.c).
 * The shapes are GLUT 3.7's, in its order: the last normal a shape sends
 * stays the program's current normal, which programs read (29.1's
 * glutshapes.c says why; the cone keeps freeglut's base disc as there).
 *
 * The display mode: GLUT_INDEX, GLUT_ACCUM, GLUT_STENCIL, GLUT_STEREO and
 * GLUT_LUMINANCE refuse the window, as in 29.1; GLUT_DOUBLE and
 * GLUT_MULTISAMPLE are dropped (a window context draws off screen and shows
 * the finished frame anyway).
 *
 * Linked last, as rgllight.c is; GLQuake calls none of it.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <libraries/minigl_dispatch.h>
#include <mgl/glut.h>

#include "rgl_dispatch.h"
#include "rgl.h"
#include "rglint.h"
#include "core.h"
#include "card.h"
#include "r200reg.h"

extern const MGLDispatchTable MiniGLDispatchTable;
#define LIB MiniGLDispatchTable

#define REFUSED_BITS    (GLUT_INDEX | GLUT_ACCUM | GLUT_STENCIL | GLUT_STEREO | GLUT_LUMINANCE)

struct Glut
{
    struct Glut *next;
    struct Task *task;
    void (*display)(void);
    void (*idle)(void);
    void (*key)(unsigned char key, int x, int y);
    void (*reshape)(int width, int height);
    LONG init_x, init_y, init_w, init_h;
    ULONG mode;
    LONG gm_w, gm_h, gm_bpp, gm_hz;
    GLcontext ctx;              /* glutCreateWindow's or glutEnterGameMode's */
    ULONG game;                 /* ctx is game mode's screen */
    ULONG posted;               /* glutPostRedisplay */
    ULONG first;                /* reshape and draw before the first idle */
    ULONG w, h;                 /* the size reshape last heard */
    LONG mouse_x, mouse_y;      /* the pointer, top-left origin */
    ULONG t0_s, t0_us;          /* glutInit's time */
    GLUquadricObj *quad;        /* the shapes' quadric */
    char title[80];
};

static struct Glut *gluts;      /* one a task (Forbid) */

/* The calling task's GLUT state; made at glutInit or at the first call */
static struct Glut *Me(void)
{
    struct Task *me;
    struct Glut *g;

    me = FindTask(NULL);
    Forbid();
    for (g = gluts; g != NULL && g->task != me; g = g->next)
        ;
    Permit();
    if (g != NULL)
        return g;
    g = (struct Glut *) AllocVec(sizeof(struct Glut), MEMF_ANY | MEMF_CLEAR);
    if (g == NULL)
        return NULL;
    g->task = me;
    g->init_x = g->init_y = -1;
    g->init_w = 640;
    g->init_h = 480;
    g->mode = GLUT_RGB;
    g->gm_w = g->gm_h = g->gm_bpp = g->gm_hz = -1;
    CurrentTime(&g->t0_s, &g->t0_us);
    Forbid();
    g->next = gluts;
    gluts = g;
    Permit();
    return g;
}

/* The task's table, and the slot its gl... calls take the context from */
static const MGLDispatchTable *Table(void)
{
    const MGLDispatchTable *t;

    t = Shell_TaskTable();
    return (t != NULL) ? t : &LIB;
}

static void SetCurrent(GLcontext x)
{
    *Table()->currentContext = x;
}

static void DropContext(struct Glut *g)
{
    if (g->ctx == NULL)
        return;
    if (*Table()->currentContext == g->ctx)
        SetCurrent(NULL);
    Hook_MGLDeleteContext(g->ctx);
    g->ctx = NULL;
    g->game = 0;
}

static void Unlink(struct Glut *g)
{
    struct Glut **pp;

    Forbid();
    for (pp = &gluts; *pp != NULL && *pp != g; pp = &(*pp)->next)
        ;
    if (*pp != NULL)
        *pp = g->next;
    Permit();
    if (g->quad != NULL)
        LIB.GLUDeleteQuadric(g->quad);
    FreeVec(g);
}

/* The task left (shell.c LibClose -> rgl.c Back_ClientGone, under Forbid):
 * its state; its context went with the task's others already */
void Rglg_ClientGone(struct Task *task)
{
    struct Glut **pp;
    struct Glut *g;

    for (pp = &gluts; *pp != NULL; )
    {
        g = *pp;
        if (g->task != task)
        {
            pp = &g->next;
            continue;
        }
        *pp = g->next;
        if (g->quad != NULL)
            LIB.GLUDeleteQuadric(g->quad);
        FreeVec(g);
    }
}

/* --- the window ---------------------------------------------------------------------- */

void Hook_GLUTInit(int *argcp, char **argv)
{
    struct Glut *g;

    (void) argcp;
    (void) argv;
    if ((g = Me()) == NULL)
        return;
    /* a fresh start: a program run again after a crash finds nothing old */
    g->display = NULL;
    g->idle = NULL;
    g->key = NULL;
    g->reshape = NULL;
    g->posted = 0;
    CurrentTime(&g->t0_s, &g->t0_us);
}

void Hook_GLUTInitDisplayMode(unsigned int mode)
{
    struct Glut *g;

    if ((g = Me()) != NULL)
        g->mode = mode;
}

void Hook_GLUTInitWindowSize(int width, int height)
{
    struct Glut *g;

    if ((g = Me()) == NULL)
        return;
    if (width > 0)
        g->init_w = width;
    if (height > 0)
        g->init_h = height;
}

void Hook_GLUTInitWindowPosition(int x, int y)
{
    struct Glut *g;

    if ((g = Me()) == NULL)
        return;
    g->init_x = x;
    g->init_y = y;
}

static int Open(struct Glut *g, ULONG window, LONG w, LONG h)
{
    struct Window *win;

    if (g->ctx != NULL || (g->mode & REFUSED_BITS))
        return 0;                   /* one window a program; a mode it cannot have */
    Hook_mglChooseWindowMode(window ? GL_TRUE : GL_FALSE);
    if (!window && (g->gm_bpp == 16 || g->gm_bpp == 15 || g->gm_bpp == 32 || g->gm_bpp == 24))
        Hook_mglChoosePixelDepth(g->gm_bpp >= 24 ? 32 : 16);
    g->ctx = (GLcontext) Hook_MGLCreateContext(window && g->init_x > 0 ? g->init_x : 0,
                                               window && g->init_y > 0 ? g->init_y : 0, w, h);
    Hook_mglChooseWindowMode(GL_FALSE);
    if (g->ctx == NULL)
        return 0;
    SetCurrent(g->ctx);
    g->game = !window;
    g->first = 1;
    g->w = (ULONG) w;
    g->h = (ULONG) h;
    win = (struct Window *) Hook_MGLGetWindowHandle(g->ctx);
    if (window && win != NULL && g->title[0])
        SetWindowTitles(win, (UBYTE *) g->title, (UBYTE *) ~0);
    return 1;
}

int Hook_GLUTCreateWindow(const char *title)
{
    struct Glut *g;
    ULONG i;

    if ((g = Me()) == NULL)
        return 0;
    for (i = 0; title != NULL && title[i] && i < sizeof(g->title) - 1; i++)
        g->title[i] = title[i];
    g->title[i] = 0;
    return Open(g, 1, g->init_w, g->init_h);
}

void Hook_GLUTDisplayFunc(void (*func)(void))
{
    struct Glut *g;

    if ((g = Me()) != NULL)
        g->display = func;
}

void Hook_GLUTIdleFunc(void (*func)(void))
{
    struct Glut *g;

    if ((g = Me()) != NULL)
        g->idle = func;
}

void Hook_GLUTKeyboardFunc(void (*func)(unsigned char key, int x, int y))
{
    struct Glut *g;

    if ((g = Me()) != NULL)
        g->key = func;
}

void Hook_GLUTReshapeFunc(void (*func)(int width, int height))
{
    struct Glut *g;

    if ((g = Me()) != NULL)
        g->reshape = func;
}

void Hook_GLUTPostRedisplay(void)
{
    struct Glut *g;

    if ((g = Me()) != NULL)
        g->posted = 1;
}

void Hook_GLUTSwapBuffers(void)
{
    struct Glut *g;

    if ((g = Me()) != NULL && g->ctx != NULL)
        Hook_MGLSwitchDisplay(g->ctx);
}

/* --- the loop -------------------------------------------------------------------------- */

static void Reshape(struct Glut *g, ULONG w, ULONG h)
{
    g->w = w;
    g->h = h;
    if (g->reshape != NULL)
        g->reshape((int) w, (int) h);
    else
        Table()->GLViewport(g->ctx, 0, 0, (GLsizei) w, (GLsizei) h);   /* GLUT's default */
    g->posted = 1;
}

static void Idle(void)
{
    struct Glut *g;
    struct Window *win;
    struct Rgl *r;
    LONG w, h;

    if ((g = Me()) == NULL || g->ctx == NULL)
        return;
    r = (struct Rgl *) g->ctx;
    win = (struct Window *) Hook_MGLGetWindowHandle(g->ctx);
    if (g->first)
    {
        g->first = 0;
        Reshape(g, g->w, g->h);
    }
    else if (!g->game && win != NULL)
    {
        /* the window was sized: the context follows, then reshape */
        w = win->Width - win->BorderLeft - win->BorderRight;
        h = win->Height - win->BorderTop - win->BorderBottom;
        if (w > 0 && h > 0 && ((ULONG) w != g->w || (ULONG) h != g->h)
            && Hook_MGLResizeContext(g->ctx, (GLsizei) w, (GLsizei) h))
            Reshape(g, (ULONG) w, (ULONG) h);
    }
    if (g->idle != NULL)
        g->idle();
    if (g->posted && g->display != NULL)
    {
        g->posted = 0;
        g->display();
    }
    else if (g->idle == NULL && r->ml_running && win != NULL && win->UserPort != NULL)
        WaitPort(win->UserPort);    /* nothing to do: wait for input, as GLUT does */
}

static void Key(char key)
{
    struct Glut *g;

    if ((g = Me()) != NULL && g->key != NULL)
        g->key((unsigned char) key, (int) g->mouse_x, (int) g->mouse_y);
}

static void Mouse(GLint x, GLint y, GLbitfield buttons)
{
    struct Glut *g;

    (void) buttons;
    if ((g = Me()) == NULL)
        return;
    g->mouse_x = x;
    g->mouse_y = (LONG) g->h - 1 - y;   /* GL's bottom-left origin to GLUT's top-left */
}

void Hook_GLUTMainLoop(void)
{
    struct Glut *g;
    struct Rgl *r;

    if ((g = Me()) == NULL || g->ctx == NULL)
        return;                     /* no window: nothing to run */
    r = (struct Rgl *) g->ctx;
    r->ml_idle = Idle;
    r->ml_key = Key;
    r->ml_mouse = Mouse;
    r->ml_close = 1;
    Hook_MGLMainLoop(g->ctx);
    DropContext(g);
    Unlink(g);
}

/* --- glutGet ------------------------------------------------------------------------- */

int Hook_GLUTGet(GLenum state)
{
    struct Glut *g;
    struct Window *win;
    struct Rgl *r;
    ULONG s, us, deep;

    if ((g = Me()) == NULL)
        return -1;
    r = (struct Rgl *) g->ctx;
    win = (g->ctx != NULL) ? (struct Window *) Hook_MGLGetWindowHandle(g->ctx) : NULL;
    deep = (r != NULL && r->core.target.color_format == R_COLOR_FORMAT_ARGB8888) ? 1UL : 0UL;
    switch (state)
    {
        case GLUT_INIT_WINDOW_X:        return (int) g->init_x;
        case GLUT_INIT_WINDOW_Y:        return (int) g->init_y;
        case GLUT_INIT_WINDOW_WIDTH:    return (int) g->init_w;
        case GLUT_INIT_WINDOW_HEIGHT:   return (int) g->init_h;
        case GLUT_INIT_DISPLAY_MODE:    return (int) g->mode;
        case GLUT_WINDOW_WIDTH:         return r ? (int) r->core.target.width : 0;
        case GLUT_WINDOW_HEIGHT:        return r ? (int) r->core.target.height : 0;
        case GLUT_WINDOW_X:             return win ? win->LeftEdge + win->BorderLeft : 0;
        case GLUT_WINDOW_Y:             return win ? win->TopEdge + win->BorderTop : 0;
        case GLUT_WINDOW_BUFFER_SIZE:   return r ? (deep ? 32 : 16) : 0;
        case GLUT_WINDOW_RED_SIZE:      return r ? (deep ? 8 : 5) : 0;
        case GLUT_WINDOW_GREEN_SIZE:    return r ? (deep ? 8 : 6) : 0;
        case GLUT_WINDOW_BLUE_SIZE:     return r ? (deep ? 8 : 5) : 0;
        case GLUT_WINDOW_ALPHA_SIZE:    return r ? (deep ? 8 : 0) : 0;
        case GLUT_WINDOW_DEPTH_SIZE:    return r ? 16 : 0;
        case GLUT_WINDOW_RGBA:          return 1;
        case GLUT_WINDOW_DOUBLEBUFFER:  return (r != NULL && r->kind == CARD_KIND_SCREEN) ? 1 : 0;
        case GLUT_WINDOW_STENCIL_SIZE:
        case GLUT_WINDOW_ACCUM_RED_SIZE:
        case GLUT_WINDOW_ACCUM_GREEN_SIZE:
        case GLUT_WINDOW_ACCUM_BLUE_SIZE:
        case GLUT_WINDOW_ACCUM_ALPHA_SIZE:
        case GLUT_WINDOW_NUM_SAMPLES:
        case GLUT_WINDOW_STEREO:
        case GLUT_WINDOW_PARENT:
        case GLUT_WINDOW_NUM_CHILDREN:
        case GLUT_WINDOW_COLORMAP_SIZE:
        case GLUT_MENU_NUM_ITEMS:
        case GLUT_SCREEN_WIDTH_MM:
        case GLUT_SCREEN_HEIGHT_MM:
            return 0;
        case GLUT_WINDOW_CURSOR:        return GLUT_CURSOR_INHERIT;
        case GLUT_WINDOW_FORMAT_ID:     return win ? (int) GetVPModeID(&win->WScreen->ViewPort) : 0;
        case GLUT_SCREEN_WIDTH:         return win ? win->WScreen->Width : 0;
        case GLUT_SCREEN_HEIGHT:        return win ? win->WScreen->Height : 0;
        case GLUT_DISPLAY_MODE_POSSIBLE:
            return (g->mode & REFUSED_BITS) ? 0 : 1;
        case GLUT_ELAPSED_TIME:
            CurrentTime(&s, &us);
            return (int) ((s - g->t0_s) * 1000UL + us / 1000UL) - (int) (g->t0_us / 1000UL);
    }
    return -1;
}

/* --- game mode ----------------------------------------------------------------------- */

static LONG Number(const char **pp)
{
    const char *p;
    LONG v;

    p = *pp;
    if (*p < '0' || *p > '9')
        return -1;
    for (v = 0; *p >= '0' && *p <= '9'; p++)
        v = v * 10 + (*p - '0');
    *pp = p;
    return v;
}

/* "WxH:bpp@hz", every part after the width optional (GLUT's spelling) */
void Hook_GLUTGameModeString(const char *string)
{
    struct Glut *g;
    const char *p;

    if ((g = Me()) == NULL)
        return;
    g->gm_w = g->gm_h = g->gm_bpp = g->gm_hz = -1;
    if ((p = string) == NULL)
        return;
    g->gm_w = Number(&p);
    if (*p == 'x' || *p == 'X')
    {
        p++;
        g->gm_h = Number(&p);
    }
    if (*p == ':')
    {
        p++;
        g->gm_bpp = Number(&p);
    }
    if (*p == '@')
    {
        p++;
        g->gm_hz = Number(&p);
    }
}

int Hook_GLUTEnterGameMode(void)
{
    struct Glut *g;

    if ((g = Me()) == NULL || g->ctx != NULL)
        return 0;                   /* a live window is not turned into a screen (as 29.1) */
    return Open(g, 0, g->gm_w > 0 ? g->gm_w : g->init_w, g->gm_h > 0 ? g->gm_h : g->init_h);
}

void Hook_GLUTLeaveGameMode(void)
{
    struct Glut *g;

    if ((g = Me()) == NULL || g->ctx == NULL || !g->game)
        return;
    if (((struct Rgl *) g->ctx)->ml_running)
        Hook_MGLExit(g->ctx);       /* the loop ends, then glutMainLoop closes it */
    else
        DropContext(g);
}

int Hook_GLUTGameModeGet(GLenum query)
{
    struct Glut *g;
    struct Rgl *r;

    if ((g = Me()) == NULL)
        return -1;
    r = (struct Rgl *) g->ctx;
    switch (query)
    {
        case GLUT_GAME_MODE_ACTIVE:
        case GLUT_GAME_MODE_DISPLAY_CHANGED:
            return g->game ? 1 : 0;
        case GLUT_GAME_MODE_POSSIBLE:
            return (g->gm_w > 0 && g->gm_h > 0) ? 1 : 0;   /* the Radeon opens any of its modes */
        case GLUT_GAME_MODE_WIDTH:
            return (g->game && r) ? (int) r->core.target.width : (int) g->gm_w;
        case GLUT_GAME_MODE_HEIGHT:
            return (g->game && r) ? (int) r->core.target.height : (int) g->gm_h;
        case GLUT_GAME_MODE_PIXEL_DEPTH:
            if (g->game && r)
                return (r->core.target.color_format == R_COLOR_FORMAT_ARGB8888) ? 32 : 16;
            return (int) g->gm_bpp;
        case GLUT_GAME_MODE_REFRESH_RATE:
            return (int) g->gm_hz;
    }
    return -1;
}

/* --- the shapes, through the task's table (a list being made records them) ------------- */

static GLcontext Ready(const MGLDispatchTable **t)
{
    *t = Table();
    return *(*t)->currentContext;
}

static void V3(const MGLDispatchTable *t, GLcontext x, float a, float b, float c)
{
    t->GLVertex4f(x, a, b, c, 1.0f);
}

static GLUquadricObj *Quad(void)
{
    struct Glut *g;

    if ((g = Me()) == NULL)
        return NULL;
    if (g->quad == NULL)
        g->quad = LIB.GLUNewQuadric();
    if (g->quad != NULL)
    {
        /* set at every call: the program may have changed them (GLUT 3.7) */
        LIB.GLUQuadricDrawStyle(g->quad, GLU_FILL);
        LIB.GLUQuadricNormals(g->quad, GLU_SMOOTH);
    }
    return g->quad;
}

void Hook_GLUTSolidCube(GLdouble size)
{
    /* GLUT 3.7's faces, emitted so that (-1, 0, 0) is the normal left over;
     * corner c is (bit0 x, bit1 y, bit2 z), set = +s */
    static const float n[6][3] =
    {
        { 0, 0, -1 }, { 0, 0, 1 }, { 0, -1, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { -1, 0, 0 }
    };
    static const UBYTE f[6][4] =
    {
        { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 1, 3, 7, 5 }, { 2, 6, 7, 3 }, { 0, 4, 6, 2 }
    };
    const MGLDispatchTable *t;
    GLcontext x;
    float s;
    ULONG i, k, c;

    if ((x = Ready(&t)) == NULL)
        return;
    s = (float) size * 0.5f;
    for (i = 0; i < 6; i++)
    {
        t->GLBegin(x, GL_QUADS);
        t->GLNormal3f(x, n[i][0], n[i][1], n[i][2]);
        for (k = 0; k < 4; k++)
        {
            c = f[i][k];
            V3(t, x, (c & 1) ? s : -s, (c & 2) ? s : -s, (c & 4) ? s : -s);
        }
        t->GLEnd(x);
    }
}

void Hook_GLUTSolidSphere(GLdouble radius, GLint slices, GLint stacks)
{
    const MGLDispatchTable *t;
    GLUquadricObj *q;

    if (Ready(&t) == NULL || (q = Quad()) == NULL)
        return;
    t->GLUSphere(q, radius, slices, stacks);
}

void Hook_GLUTSolidCone(GLdouble base, GLdouble height, GLint slices, GLint stacks)
{
    const MGLDispatchTable *t;
    GLcontext x;
    GLUquadricObj *q;
    float r, h, a;
    LONG i, k;

    if ((x = Ready(&t)) == NULL || stacks < 1 || (q = Quad()) == NULL)
        return;                     /* stacks < 1: nothing, as every reference */
    if (slices < 3)
        slices = 3;
    r = (float) base;
    h = (float) height;
    /* the base disc (freeglut's; GLUT 3.7 draws none), facing away from the tip */
    t->GLBegin(x, GL_TRIANGLE_FAN);
    t->GLNormal3f(x, 0.0f, 0.0f, (h < 0.0f) ? 1.0f : -1.0f);
    V3(t, x, 0.0f, 0.0f, 0.0f);
    for (i = 0; i <= slices; i++)
    {
        k = (h < 0.0f) ? (slices - i) : i;
        a = 360.0f * (float) k / (float) slices;
        V3(t, x, r * Core_Sin(a), r * Core_Cos(a), 0.0f);   /* GLU's axes: x sin, y cos */
    }
    t->GLEnd(x);
    t->GLUCylinder(q, base, 0.0, height, slices, stacks);
}

void Hook_GLUTSolidTorus(GLdouble innerRadius, GLdouble outerRadius, GLint sides, GLint rings)
{
    /* GLUT 3.7's doughnut: innerRadius the tube's, outerRadius the distance
     * to its centre; y negated, the theta1 pair first, both loops closed by
     * modulo (the leftover normal is GLUT's) */
    const MGLDispatchTable *t;
    GLcontext x;
    float tube, dist, t0, t1, ct0, st0, ct1, st1, p, cp, sp, rr;
    LONG i, j;

    if ((x = Ready(&t)) == NULL)
        return;
    if (sides < 3)
        sides = 3;
    if (rings < 3)
        rings = 3;
    tube = (float) innerRadius;
    dist = (float) outerRadius;
    for (j = 0; j < rings; j++)
    {
        t0 = 360.0f * (float) j / (float) rings;
        t1 = 360.0f * (float) ((j + 1) % rings) / (float) rings;
        ct0 = Core_Cos(t0);
        st0 = Core_Sin(t0);
        ct1 = Core_Cos(t1);
        st1 = Core_Sin(t1);
        t->GLBegin(x, GL_QUAD_STRIP);
        for (i = 0; i <= sides; i++)
        {
            p = 360.0f * (float) (i % sides) / (float) sides;
            cp = Core_Cos(p);
            sp = Core_Sin(p);
            rr = dist + tube * cp;
            t->GLNormal3f(x, ct1 * cp, -st1 * cp, sp);
            V3(t, x, ct1 * rr, -st1 * rr, tube * sp);
            t->GLNormal3f(x, ct0 * cp, -st0 * cp, sp);
            V3(t, x, ct0 * rr, -st0 * rr, tube * sp);
        }
        t->GLEnd(x);
    }
}

void Hook_GLUTSolidDodecahedron(void)
{
    /* GLUT 3.7's vertices (circumradius sqrt 3) and faces, in its order: the
     * normal left over is (-0.525731, 0.850651, 0). Each face's normal from
     * its first three corners, turned outward if a row winds the other way. */
    static const BYTE face[12][5] =
    {
        { 0, 1, 9, 16, 5 }, { 1, 0, 3, 18, 7 }, { 1, 7, 11, 10, 9 }, { 11, 7, 18, 19, 6 },
        { 8, 17, 16, 9, 10 }, { 2, 14, 15, 6, 19 }, { 2, 13, 12, 4, 14 }, { 2, 19, 18, 3, 13 },
        { 3, 0, 5, 12, 13 }, { 6, 15, 8, 10, 11 }, { 4, 17, 8, 15, 14 }, { 4, 12, 5, 16, 17 }
    };
    /* GLUT's vertex order; p = phi, q = 1/phi */
#define P 1.618034f
#define Q 0.618034f
    static const float v[20][3] =
    {
        { -Q, 0, P }, { Q, 0, P }, { -1, -1, -1 }, { -1, -1, 1 }, { -1, 1, -1 }, { -1, 1, 1 },
        { 1, -1, -1 }, { 1, -1, 1 }, { 1, 1, -1 }, { 1, 1, 1 }, { P, Q, 0 }, { P, -Q, 0 },
        { -P, Q, 0 }, { -P, -Q, 0 }, { -Q, 0, -P }, { Q, 0, -P }, { 0, P, Q }, { 0, P, -Q },
        { 0, -P, Q }, { 0, -P, -Q }
    };
#undef P
#undef Q
    const MGLDispatchTable *t;
    GLcontext x;
    const float *a, *b, *c;
    float u0, u1, u2, w0, w1, w2, nx, ny, nz, cx, cy, cz, len;
    LONG i, k;
    BOOL back;

    if ((x = Ready(&t)) == NULL)
        return;
    for (i = 0; i < 12; i++)
    {
        a = v[face[i][0]];
        b = v[face[i][1]];
        c = v[face[i][2]];
        u0 = b[0] - a[0]; u1 = b[1] - a[1]; u2 = b[2] - a[2];
        w0 = c[0] - a[0]; w1 = c[1] - a[1]; w2 = c[2] - a[2];
        nx = u1 * w2 - u2 * w1;
        ny = u2 * w0 - u0 * w2;
        nz = u0 * w1 - u1 * w0;
        cx = cy = cz = 0.0f;
        for (k = 0; k < 5; k++)
        {
            cx += v[face[i][k]][0];
            cy += v[face[i][k]][1];
            cz += v[face[i][k]][2];
        }
        back = (nx * cx + ny * cy + nz * cz) < 0.0f;
        if (back)
        {
            nx = -nx;
            ny = -ny;
            nz = -nz;
        }
        len = Core_Sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 0.0f)
        {
            nx /= len;
            ny /= len;
            nz /= len;
        }
        t->GLBegin(x, GL_TRIANGLE_FAN);
        t->GLNormal3f(x, nx, ny, nz);
        for (k = 0; k < 5; k++)
        {
            a = v[face[i][back ? 4 - k : k]];
            V3(t, x, a[0], a[1], a[2]);
        }
        t->GLEnd(x);
    }
}
