/*
 * GLFog -- the fog hang of GLBench 0.2's fill.fog tests, 2026-11-28.
 *
 * GLBench (Hamiller, RadeonGL 0.20, 640 x 480 x 16): fill.fog.linear,
 * fill.fog.exp and fill.fog.exp2 draw untextured immediate-mode quads over
 * a 608 x 288 area at z = -0.5 (glOrtho -1..1, depth test off), many of
 * them a frame, then glFinish and mglSwitchDisplay. Their frames hang the
 * 3D engine (RBBM_STATUS 0x83f96100 at the watchdog: SE, RE, TAM, TDM, PB,
 * RB3D busy), the single 64 x 64 check quad before them does not, and
 * GLQuake's textured, depth-tested linear fog runs clean on the same
 * library. GLFog draws that frame with one thing changed at a time:
 *
 *   off       fog off (the control)
 *   fog       fog GL_EXP, density 1.3863, no texture (GLBench's case)
 *   linear    fog GL_LINEAR 0..1, no texture
 *   tex       fog GL_EXP over a 1 x 1 white texture (MODULATE)
 *   depth     fog GL_EXP, no texture, depth test on (GL_ALWAYS)
 *   small     fog GL_EXP, no texture, 64 x 64 quads
 *
 *   GLFog ONLY/K,FRAMES/N,QUADS/N
 *
 * ONLY: one variant by name (each run of one variant gets a RadeonGLLog of
 * its own). FRAMES: frames (40). QUADS: quads a frame (64). Prints the
 * time a frame and how many frames took longer than 400 ms (the library's
 * watchdog ends a hung frame after 50 vertical blanks), then the colour in
 * the middle of the last frame, read back.
 * GCC only: it links the MiniGL SDK's import library, as GLFill does.
 */

#include <exec/types.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <proto/minigl.h>

static const char ver[] = "$VER: GLFog 0.1 (28.11.2026)";

#define TEMPLATE "ONLY/K,FRAMES/N,QUADS/N"

struct Device *TimerBase = NULL;
static ULONG clk_Freq = 709379;

#define V_FOG    0x01
#define V_LINEAR 0x02
#define V_TEX    0x04
#define V_DEPTH  0x08
#define V_SMALL  0x10

struct Variant
{
    const char *name;
    ULONG flags;
};

static const struct Variant variants[] = {
    { "off",    0 },
    { "fog",    V_FOG },
    { "linear", V_FOG | V_LINEAR },
    { "tex",    V_FOG | V_TEX },
    { "depth",  V_FOG | V_DEPTH },
    { "small",  V_FOG | V_SMALL }
};

#define SW 640
#define SH 480

static GLuint tex_white;

static ULONG Now(void)
{
    struct EClockVal ev;

    ReadEClock(&ev);
    return ev.ev_lo;
}

static ULONG Us(ULONG ticks)
{
    return (ULONG) ((double) ticks * 1000000.0 / (double) clk_Freq + 0.5);
}

static int Lower(int ch)
{
    return (ch >= 'A' && ch <= 'Z') ? ch - 'A' + 'a' : ch;
}

static BOOL SameName(const char *a, const char *b)
{
    while (*a != 0 && Lower((UBYTE) *a) == Lower((UBYTE) *b))
    {
        a++;
        b++;
    }
    return (BOOL) (*a == 0 && *b == 0);
}

/* GLBench's FT_Quad: immediate mode, four glVertex3f, z = -0.5 */
static void Quad(GLfloat x0, GLfloat y0, GLfloat x1, GLfloat y1, BOOL st)
{
    glBegin(GL_QUADS);
    if (st) glTexCoord2f(0.0f, 0.0f);
    glVertex3f(x0, y0, -0.5f);
    if (st) glTexCoord2f(1.0f, 0.0f);
    glVertex3f(x1, y0, -0.5f);
    if (st) glTexCoord2f(1.0f, 1.0f);
    glVertex3f(x1, y1, -0.5f);
    if (st) glTexCoord2f(0.0f, 1.0f);
    glVertex3f(x0, y1, -0.5f);
    glEnd();
}

/* the state as GLBench's FogSet leaves it, plus the variant's change */
static void SetState(ULONG fl)
{
    GLfloat blue[4];

    glViewport(0, 0, SW, SH);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (GLdouble) SW, 0.0, (GLdouble) SH, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glShadeModel(GL_SMOOTH);
    if (fl & V_DEPTH)
    {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_ALWAYS);
    }
    else
        glDisable(GL_DEPTH_TEST);
    if (fl & V_TEX)
    {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, tex_white);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    }
    else
        glDisable(GL_TEXTURE_2D);
    if (fl & V_FOG)
    {
        blue[0] = 0.0f; blue[1] = 0.0f; blue[2] = 1.0f; blue[3] = 1.0f;
        glEnable(GL_FOG);
        glFogfv(GL_FOG_COLOR, blue);
        if (fl & V_LINEAR)
        {
            glFogf(GL_FOG_MODE, (GLfloat) GL_LINEAR);
            glFogf(GL_FOG_START, 0.0f);
            glFogf(GL_FOG_END, 1.0f);
        }
        else
        {
            glFogf(GL_FOG_MODE, (GLfloat) GL_EXP);
            glFogf(GL_FOG_DENSITY, 1.3863f);
        }
    }
    else
        glDisable(GL_FOG);
}

int main(int argc, char **argv)
{
    struct RDArgs *rda;
    struct MsgPort *port;
    struct timerequest *req;
    struct EClockVal ev;
    LONG args[3];
    ULONG frames, nq, f, q, v, t0, t1, us, worst, slow, total;
    GLubyte px[4];
    GLubyte white[4];
    GLfloat w, h;
    BOOL dev, lib, ctx;
    int rc;

    (void) argv;
    if (argc == 0)
        return RETURN_FAIL;     /* the Workbench: no console to report to */
    args[0] = args[1] = args[2] = 0;
    rda = ReadArgs(TEMPLATE, args, NULL);
    if (rda == NULL)
    {
        PrintFault(IoErr(), "GLFog");
        return RETURN_ERROR;
    }
    rc = RETURN_FAIL;
    port = NULL;
    req = NULL;
    dev = lib = ctx = FALSE;
    frames = (args[1] && *(LONG *) args[1] > 0) ? (ULONG) *(LONG *) args[1] : 40;
    nq = (args[2] && *(LONG *) args[2] > 0) ? (ULONG) *(LONG *) args[2] : 64;
    port = CreateMsgPort();
    if (port != NULL)
        req = (struct timerequest *) CreateIORequest(port, sizeof(*req));
    if (req == NULL || OpenDevice((STRPTR) TIMERNAME, UNIT_MICROHZ, (struct IORequest *) req, 0) != 0)
    {
        Printf("GLFog: timer.device did not open\n");
        goto out;
    }
    dev = TRUE;
    TimerBase = req->tr_node.io_Device;
    clk_Freq = ReadEClock(&ev);
    Printf("%s\n", (LONG) (ver + 6));
    if (!MiniGLOpen())
    {
        Printf("GLFog: minigl.library 29.1 did not open\n");
        goto out;
    }
    lib = TRUE;
    mglChoosePixelDepth(16);
    mglChooseNumberOfBuffers(2);
    if (!mglCreateContext(0, 0, SW, SH))
    {
        Printf("GLFog: no context\n");
        goto out;
    }
    ctx = TRUE;
    mglEnableSync(GL_FALSE);
    Printf("GL_RENDERER: %s, %ld frames of %ld quads a variant\n", (LONG) glGetString(GL_RENDERER),
           (LONG) frames, (LONG) nq);
    white[0] = white[1] = white[2] = white[3] = 255;
    glGenTextures(1, &tex_white);
    glBindTexture(GL_TEXTURE_2D, tex_white);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepth(1.0);
    Printf("  variant   ms a frame (mean, worst)   frames over 400 ms   middle pixel\n");
    for (v = 0; v < sizeof(variants) / sizeof(variants[0]); v++)
    {
        if (args[0] && !SameName((const char *) args[0], variants[v].name))
            continue;
        w = (variants[v].flags & V_SMALL) ? 64.0f : (GLfloat) SW;
        h = (variants[v].flags & V_SMALL) ? 64.0f : (GLfloat) SH;
        glDisable(GL_FOG);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        SetState(variants[v].flags);
        worst = slow = total = 0;
        for (f = 0; f < frames; f++)
        {
            t0 = Now();
            for (q = 0; q < nq; q++)
            {
                /* GLBench's colour: (1, 1, (n & 1) * 0.2, 1) */
                glColor4f(1.0f, 1.0f, (q & 1) ? 0.2f : 0.0f, 1.0f);
                Quad(0.0f, 0.0f, w, h, (BOOL) ((variants[v].flags & V_TEX) != 0));
            }
            glFinish();
            mglSwitchDisplay();
            t1 = Now();
            us = Us(t1 - t0);
            total += us;
            if (us > worst)
                worst = us;
            if (us > 400000)
                slow++;
        }
        /* the last frame again, read back before it is shown */
        for (q = 0; q < 4; q++)
        {
            glColor4f(1.0f, 1.0f, 0.0f, 1.0f);
            Quad(0.0f, 0.0f, w, h, (BOOL) ((variants[v].flags & V_TEX) != 0));
        }
        px[0] = px[1] = px[2] = px[3] = 0;
        glReadPixels((GLint) (w / 2), (GLint) (h / 2), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        mglSwitchDisplay();
        Printf("  %-8s  %5ld.%01ld %6ld.%01ld            %3ld                  %ld %ld %ld\n",
               (LONG) variants[v].name, (LONG) (total / frames / 1000),
               (LONG) ((total / frames % 1000) / 100), (LONG) (worst / 1000),
               (LONG) ((worst % 1000) / 100), (LONG) slow, (LONG) px[0], (LONG) px[1], (LONG) px[2]);
        glDisable(GL_FOG);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_DEPTH_TEST);
    }
    Printf("GL error 0x%lx\n", (LONG) glGetError());
    rc = RETURN_OK;

out:
    if (ctx)
    {
        glDeleteTextures(1, &tex_white);
        mglDeleteContext();
    }
    if (lib)
        MiniGLClose();
    if (dev)
        CloseDevice((struct IORequest *) req);
    if (req != NULL)
        DeleteIORequest((struct IORequest *) req);
    if (port != NULL)
        DeleteMsgPort(port);
    FreeArgs(rda);
    return rc;
}
