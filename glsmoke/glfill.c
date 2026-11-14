/*
 * GLFill -- how fast minigl.library fills big polygons, 2026-11-14.
 *
 * Doom64's level-restart wipe (its session, A4000D, RadeonGL 0.13): per
 * frame a clear, 12 full-screen 640 x 480 quads textured from one
 * 1024 x 512 GL_RGB8 texture (GL_NEAREST, GL_CLAMP, MODULATE with a vertex
 * colour, blended SRC_ALPHA / ONE_MINUS_SRC_ALPHA) and one untextured quad
 * blended ONE / ONE, each quad two triangles from locked arrays through
 * glDrawElements, the viewport and an ortho matrix set again before each
 * quad. About 190 ms a frame: ~22 Mpixel/s, far below what GLQuake fills.
 *
 * GLFill draws that frame, then the same with one thing changed at a time,
 * and prints the time a frame and the pixels a second for each:
 *
 *   doom64    as above
 *   onevp     the viewport and the matrix set once, not per quad
 *   noblend   blending off
 *   notex     no texture (the colour only), blended
 *   plain     no texture, no blending
 *   tex256    a 256 x 256 texture instead of 1024 x 512
 *   rgb565    the 1024 x 512 texture as GL_RGB5 (16 bit on the card)
 *   linear    GL_LINEAR filtering
 *   quarter   quads a quarter of the screen (320 x 240)
 *   clear     the clear and the swap alone
 *   copy      the 1024 x 512 texture made as Doom64 makes it: glTexImage2D
 *             with no pixels, then one glCopyTexSubImage2D(0, 0, 0, 0,
 *             1024, 512) from the frame
 *   full      10 MB of other textures resident in the heap first (Doom64
 *             had its level's textures there, 8-11 MB)
 *   copyfull  both
 *
 *   GLFill FRAMES/N,ONLY/K,W/N,H/N
 *
 * FRAMES: frames per variant (30). ONLY: one variant by name. W, H: the
 * screen (640 x 480). Vertical sync off, two buffers, 16-bit screen.
 * GCC only: it links the MiniGL SDK's import library, as GLQuake does.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <proto/minigl.h>

static const char ver[] = "$VER: GLFill 0.2 (14.11.2026)";

#define TEMPLATE "FRAMES/N,ONLY/K,W/N,H/N"

struct Device *TimerBase = NULL;
static ULONG clk_Freq = 709379;

#define V_ONEVP   0x01
#define V_NOBLEND 0x02
#define V_NOTEX   0x04
#define V_TEX256  0x08
#define V_RGB565  0x10
#define V_LINEAR  0x20
#define V_QUARTER 0x40
#define V_CLEAR   0x80
#define V_COPY    0x100
#define V_FULL    0x200

#define NFULL 10            /* 1 MB textures (512 x 512 RGBA8) for V_FULL */

struct Variant
{
    const char *name;
    ULONG flags;
};

static const struct Variant variants[] = {
    { "doom64",  0 },
    { "onevp",   V_ONEVP },
    { "noblend", V_NOBLEND },
    { "notex",   V_NOTEX },
    { "plain",   V_NOTEX | V_NOBLEND },
    { "tex256",  V_TEX256 },
    { "rgb565",  V_RGB565 },
    { "linear",  V_LINEAR },
    { "quarter", V_QUARTER },
    { "clear",   V_CLEAR },
    { "copy",    V_COPY },
    { "full",    V_FULL },
    { "copyfull", V_COPY | V_FULL }
};

#define NQUADS 12

static GLfloat qpos[4 * 2];
static GLfloat qst[4 * 2];
static GLubyte qcol[4 * 4];
static GLushort qidx[6] = { 0, 1, 2, 0, 2, 3 };
static GLuint tex_big, tex_small, tex_565, tex_copy;
static GLuint tex_full[NFULL];
static LONG sw, sh;

static ULONG Now(void)
{
    struct EClockVal ev;

    ReadEClock(&ev);
    return ev.ev_lo;
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

/* a texture of w x h RGB texels, a pattern so nothing is uniform */
static BOOL MakeTex(GLuint *name, LONG w, LONG h, GLenum internal)
{
    UBYTE *p;
    LONG x, y;
    UBYTE *q;

    p = (UBYTE *) AllocVec((ULONG) (w * h * 3), MEMF_ANY);
    if (p == NULL)
        return FALSE;
    q = p;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
        {
            q[0] = (UBYTE) (x * 7 + y);
            q[1] = (UBYTE) (y * 5 + (x >> 2));
            q[2] = (UBYTE) ((x ^ y) * 3);
            q += 3;
        }
    glGenTextures(1, name);
    glBindTexture(GL_TEXTURE_2D, *name);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint) internal, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, p);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    FreeVec(p);
    return TRUE;
}

/* the screen's projection, as Doom64's GL_SetOrtho + glViewport */
static void Ortho(void)
{
    glViewport(0, 0, sw, sh);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (GLdouble) sw, (GLdouble) sh, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

/* one quad, w x h pixels from the top left, colour (b, b, b, a) */
static void Quad(GLfloat w, GLfloat h, GLfloat s1, GLfloat t1, GLubyte b, GLubyte a)
{
    int i;

    qpos[0] = 0.0f; qpos[1] = 0.0f;
    qpos[2] = w;    qpos[3] = 0.0f;
    qpos[4] = w;    qpos[5] = h;
    qpos[6] = 0.0f; qpos[7] = h;
    qst[0] = 0.0f;  qst[1] = 0.0f;
    qst[2] = s1;    qst[3] = 0.0f;
    qst[4] = s1;    qst[5] = t1;
    qst[6] = 0.0f;  qst[7] = t1;
    for (i = 0; i < 4; i++)
    {
        qcol[i * 4 + 0] = b;
        qcol[i * 4 + 1] = b;
        qcol[i * 4 + 2] = b;
        qcol[i * 4 + 3] = a;
    }
    glLockArraysEXT(0, 4);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, qidx);
    glUnlockArraysEXT();
}

static void Frame(ULONG fl)
{
    GLfloat w, h, s1, t1;
    GLuint t;
    int i;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (fl & V_CLEAR)
        return;
    w = (GLfloat) ((fl & V_QUARTER) ? sw / 2 : sw);
    h = (GLfloat) ((fl & V_QUARTER) ? sh / 2 : sh);
    t = (fl & V_COPY) ? tex_copy : (fl & V_TEX256) ? tex_small : (fl & V_RGB565) ? tex_565 : tex_big;
    /* the screen's part of the texture: as Doom64 copies 640 x 480 into
     * 1024 x 512 */
    s1 = (fl & V_TEX256) ? 1.0f : (GLfloat) sw / 1024.0f;
    t1 = (fl & V_TEX256) ? 1.0f : (GLfloat) sh / 512.0f;
    if (s1 > 1.0f)
        s1 = 1.0f;
    if (t1 > 1.0f)
        t1 = 1.0f;
    if (fl & V_ONEVP)
        Ortho();
    if (fl & V_NOTEX)
        glDisable(GL_TEXTURE_2D);
    else
    {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (fl & V_LINEAR) ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (fl & V_LINEAR) ? GL_LINEAR : GL_NEAREST);
    }
    if (fl & V_NOBLEND)
        glDisable(GL_BLEND);
    else
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    for (i = 0; i < NQUADS; i++)
    {
        if (!(fl & V_ONEVP))
            Ortho();
        /* alphas 1, 1/2, 1/3 ...: Doom64's running average */
        Quad(w, h, s1, t1, (GLubyte) (255 - i * 8), (GLubyte) (255 / (i + 1)));
    }
    /* the untextured additive quad */
    glDisable(GL_TEXTURE_2D);
    if (!(fl & V_NOBLEND))
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
    }
    if (!(fl & V_ONEVP))
        Ortho();
    Quad(w, h, s1, t1, 16, 255);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

int main(int argc, char **argv)
{
    struct RDArgs *rda;
    struct MsgPort *port;
    struct timerequest *req;
    struct EClockVal ev;
    LONG args[4];
    ULONG frames, f, v, t0, ticks, us, quads, px, mpx10;
    BOOL dev, lib, ctx;
    int rc;

    (void) argv;
    if (argc == 0)
        return RETURN_FAIL;     /* the Workbench: no console to report to */
    args[0] = args[1] = args[2] = args[3] = 0;
    rda = ReadArgs(TEMPLATE, args, NULL);
    if (rda == NULL)
    {
        PrintFault(IoErr(), "GLFill");
        return RETURN_ERROR;
    }
    rc = RETURN_FAIL;
    port = NULL;
    req = NULL;
    dev = lib = ctx = FALSE;
    frames = (args[0] && *(LONG *) args[0] > 0) ? (ULONG) *(LONG *) args[0] : 30;
    sw = (args[2] && *(LONG *) args[2] > 0) ? *(LONG *) args[2] : 640;
    sh = (args[3] && *(LONG *) args[3] > 0) ? *(LONG *) args[3] : 480;
    port = CreateMsgPort();
    if (port != NULL)
        req = (struct timerequest *) CreateIORequest(port, sizeof(*req));
    if (req == NULL || OpenDevice((STRPTR) TIMERNAME, UNIT_MICROHZ, (struct IORequest *) req, 0) != 0)
    {
        Printf("GLFill: timer.device did not open\n");
        goto out;
    }
    dev = TRUE;
    TimerBase = req->tr_node.io_Device;
    clk_Freq = ReadEClock(&ev);
    Printf("%s\n", (LONG) (ver + 6));
    if (!MiniGLOpen())
    {
        Printf("GLFill: minigl.library 29.1 did not open\n");
        goto out;
    }
    lib = TRUE;
    mglChoosePixelDepth(16);
    mglChooseNumberOfBuffers(2);
    if (!mglCreateContext(0, 0, (int) sw, (int) sh))
    {
        Printf("GLFill: no context\n");
        goto out;
    }
    ctx = TRUE;
    mglEnableSync(GL_FALSE);
    Printf("GL_RENDERER: %s, screen %ld x %ld, %ld frames a variant\n", (LONG) glGetString(GL_RENDERER),
           sw, sh, (LONG) frames);
    if (!MakeTex(&tex_big, 1024, 512, GL_RGB8) || !MakeTex(&tex_small, 256, 256, GL_RGB8)
        || !MakeTex(&tex_565, 1024, 512, GL_RGB5))
    {
        Printf("GLFill: no memory for the textures\n");
        goto out;
    }
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glShadeModel(GL_SMOOTH);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, qpos);
    glTexCoordPointer(2, GL_FLOAT, 0, qst);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, qcol);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    Ortho();
    Printf("  variant    ms a frame   Mpixel/s (quads' pixels)\n");
    for (v = 0; v < sizeof(variants) / sizeof(variants[0]); v++)
    {
        if (args[1] && !SameName((const char *) args[1], variants[v].name))
            continue;
        if (variants[v].flags & V_FULL)
        {
            ULONG k;

            /* the level's textures: resident, each drawn once */
            for (k = 0; k < NFULL; k++)
                if (!MakeTex(&tex_full[k], 512, 512, GL_RGBA8))
                    break;
            glEnable(GL_TEXTURE_2D);
            for (k = 0; k < NFULL; k++)
            {
                glBindTexture(GL_TEXTURE_2D, tex_full[k]);
                Quad(8.0f, 8.0f, 1.0f, 1.0f, 255, 255);
            }
            mglSwitchDisplay();
        }
        if (variants[v].flags & V_COPY)
        {
            /* Doom64's GL_ScreenToTexture: an empty texture, one copy */
            Frame(0);
            glGenTextures(1, &tex_copy);
            glBindTexture(GL_TEXTURE_2D, tex_copy);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 1024, 512, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
            t0 = Now();
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, 1024, 512);
            glFinish();
            ticks = Now() - t0;
            Printf("  (the copy: %ld ms)\n", (LONG) ((double) ticks * 1000.0 / (double) clk_Freq + 0.5));
        }
        /* two frames to settle (textures to the card), then the timed ones */
        Frame(variants[v].flags);
        mglSwitchDisplay();
        Frame(variants[v].flags);
        mglSwitchDisplay();
        t0 = Now();
        for (f = 0; f < frames; f++)
        {
            Frame(variants[v].flags);
            mglSwitchDisplay();
        }
        glFinish();
        ticks = Now() - t0;
        us = (ULONG) ((double) ticks * 1000000.0 / (double) clk_Freq / (double) frames + 0.5);
        quads = (variants[v].flags & V_CLEAR) ? 0 : NQUADS + 1;
        px = quads * (ULONG) ((variants[v].flags & V_QUARTER) ? (sw / 2) * (sh / 2) : sw * sh);
        mpx10 = us ? (ULONG) ((double) px * 10.0 / (double) us + 0.5) : 0;
        Printf("  %-9s %6ld.%01ld        %4ld.%01ld\n", (LONG) variants[v].name, (LONG) (us / 1000),
               (LONG) ((us % 1000) / 100), (LONG) (mpx10 / 10), (LONG) (mpx10 % 10));
        if (variants[v].flags & V_COPY)
            glDeleteTextures(1, &tex_copy);
        if (variants[v].flags & V_FULL)
            glDeleteTextures(NFULL, tex_full);
    }
    Printf("GL error 0x%lx\n", (LONG) glGetError());
    rc = RETURN_OK;

out:
    if (ctx)
        mglDeleteContext();
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
