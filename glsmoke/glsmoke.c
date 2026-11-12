/*
 * GLSmoke -- the first frames through minigl.library on the Radeon
 * (docs/design-card-backend.md, work order step 5).
 *
 * A screen of WIDTH x HEIGHT (640 x 480), cleared every frame, with:
 *   - a textured quad on the left (a 64 x 64 red and white checker, linear
 *     filtering), behind the triangle in depth;
 *   - a Gouraud triangle (red, green, blue corners) spinning on the right,
 *     its tip crossing the quad's edge, so the depth test shows;
 *   - every frame an 8 x 8 patch of the texture rewritten with
 *     glTexSubImage2D (a changing colour, top left of the quad): a texture
 *     the GPU drew a moment ago changes, as GLQuake's lightmaps do, which
 *     exercises the library's texture-hazard flush and its heap lock.
 * FRAMES frames (300), then the frame rate. Esc on the GL screen (or
 * Ctrl-C in the Shell) stops it early. A Shell program: started from the
 * Workbench it quits at once.
 *
 *   GLSmoke FRAMES/N,DEPTH/N,WIDTH/N,HEIGHT/N,NOSUB/S,POLYMODE/K
 *
 * POLYMODE=LINE or POINT (2026-11-11): both shapes drawn with that
 * glPolygonMode, back faces culled (both face the viewer), so the library's
 * polygon-mode way runs every frame (test/auto/GLSmokePolyNull).
 *
 * The library writes its summary to RadeonGLLog (PROGDIR:radeongl.log by
 * default): flushes, presents, lock times, dropped frames, checker.
 *
 * GCC only: it links the MiniGL SDK's import library (libminigl.a, a GCC
 * archive), as GLQuake does. C89 otherwise.
 */

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/minigl.h>

#ifdef RGL_FOR_030
static const char ver[] = "$VER: GLSmoke 0.2 (11.11.2026) for the 68020/68030";
#else
static const char ver[] = "$VER: GLSmoke 0.2 (11.11.2026)";
#endif

#define TEMPLATE "FRAMES/N,DEPTH/N,WIDTH/N,HEIGHT/N,NOSUB/S,POLYMODE/K"

struct Args
{
    LONG *frames;
    LONG *depth;
    LONG *width;
    LONG *height;
    LONG nosub;
    STRPTR polymode;
};

#define TEX 64

struct IntuitionBase *IntuitionBase = NULL;

/* a with b, ASCII letters in either case */
static BOOL SameWord(CONST_STRPTR a, const char *b)
{
    char x;
    char y;

    for (;;)
    {
        x = (char) *a++;
        y = *b++;
        if (x >= 'a' && x <= 'z')
            x = (char) (x - 'a' + 'A');
        if (y >= 'a' && y <= 'z')
            y = (char) (y - 'a' + 'A');
        if (x != y)
            return FALSE;
        if (x == 0)
            return TRUE;
    }
}

/* TRUE when Esc came in the library's window; every message replied */
static BOOL EscPressed(struct Window *win)
{
    struct IntuiMessage *im;
    BOOL esc;

    esc = FALSE;
    if (win == NULL || win->UserPort == NULL)
        return esc;
    while ((im = (struct IntuiMessage *) GetMsg(win->UserPort)) != NULL)
    {
        if (im->Class == IDCMP_VANILLAKEY && im->Code == 27)
            esc = TRUE;
        ReplyMsg((struct Message *) im);
    }
    return esc;
}

static UBYTE tex[TEX * TEX * 3];
static UBYTE patch[8 * 8 * 3];

static void MakeTexture(void)
{
    ULONG x;
    ULONG y;
    UBYTE v;

    for (y = 0; y < TEX; y++)
        for (x = 0; x < TEX; x++)
        {
            v = (UBYTE) ((((x >> 3) ^ (y >> 3)) & 1) ? 255 : 0);
            tex[(y * TEX + x) * 3 + 0] = 255;
            tex[(y * TEX + x) * 3 + 1] = v;
            tex[(y * TEX + x) * 3 + 2] = v;
        }
}

static void Frame(ULONG f, BOOL sub)
{
    ULONG i;

    if (sub)
    {
        for (i = 0; i < 8 * 8; i++)
        {
            patch[i * 3 + 0] = (UBYTE) (f * 5);
            patch[i * 3 + 1] = (UBYTE) (f * 3);
            patch[i * 3 + 2] = (UBYTE) (255 - f * 5);
        }
        glBindTexture(GL_TEXTURE_2D, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, TEX - 8, 8, 8, GL_RGB, GL_UNSIGNED_BYTE, patch);
    }

    glClearColor(0.0f, 0.0f, 0.25f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    /* the textured quad, behind (z = -0.5) */
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 1);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f);
    glVertex3f(-0.9f, -0.6f, -0.5f);
    glTexCoord2f(1.0f, 0.0f);
    glVertex3f(0.1f, -0.6f, -0.5f);
    glTexCoord2f(1.0f, 1.0f);
    glVertex3f(0.1f, 0.6f, -0.5f);
    glTexCoord2f(0.0f, 1.0f);
    glVertex3f(-0.9f, 0.6f, -0.5f);
    glEnd();
    glDisable(GL_TEXTURE_2D);

    /* the spinning triangle, in front (z = 0) */
    glPushMatrix();
    glTranslatef(0.35f, 0.0f, 0.0f);
    glRotatef((float) (f * 3 % 360), 0.0f, 0.0f, 1.0f);
    glBegin(GL_TRIANGLES);
    glColor4f(1.0f, 0.0f, 0.0f, 1.0f);
    glVertex3f(0.0f, 0.55f, 0.0f);
    glColor4f(0.0f, 1.0f, 0.0f, 1.0f);
    glVertex3f(-0.48f, -0.28f, 0.0f);
    glColor4f(0.0f, 0.0f, 1.0f, 1.0f);
    glVertex3f(0.48f, -0.28f, 0.0f);
    glEnd();
    glPopMatrix();

    mglSwitchDisplay();
}

int main(int argc, char **argv)
{
    struct RDArgs *rda;
    struct Args args;
    struct DateStamp d0;
    struct DateStamp d1;
    struct Window *win;
    LONG frames;
    LONG depth;
    LONG w;
    LONG h;
    LONG f;
    LONG ticks;
    int rc;
    BOOL opened;

    (void) argv;
    if (argc == 0)
        return RETURN_FAIL;     /* the Workbench: no console to report to */
    args.frames = NULL;
    args.depth = NULL;
    args.width = NULL;
    args.height = NULL;
    args.nosub = 0;
    args.polymode = NULL;
    rda = ReadArgs(TEMPLATE, (LONG *) &args, NULL);
    if (rda == NULL)
    {
        PrintFault(IoErr(), "GLSmoke");
        return RETURN_ERROR;
    }
    frames = (args.frames && *args.frames > 0) ? *args.frames : 300;
    depth = (args.depth && *args.depth == 32) ? 32 : 16;
    w = (args.width && *args.width >= 64) ? *args.width : 640;
    h = (args.height && *args.height >= 64) ? *args.height : 480;
    Printf("%s: %ld x %ld x %ld, %ld frames%s\n", (LONG) (ver + 6), w, h, depth, frames,
           (LONG) (args.nosub ? " (no texture updates)" : ""));

    rc = RETURN_FAIL;
    opened = FALSE;
    IntuitionBase = (struct IntuitionBase *) OpenLibrary((CONST_STRPTR) "intuition.library", 39);
    if (IntuitionBase == NULL)
    {
        Printf("GLSmoke: intuition.library 39 did not open\n");
        goto out;
    }
    if (!MiniGLOpen())
    {
        Printf("GLSmoke: minigl.library 29.1 did not open\n");
        goto out;
    }
    opened = TRUE;
    mglChoosePixelDepth((int) depth);
    if (!mglCreateContext(0, 0, (int) w, (int) h))
    {
        Printf("GLSmoke: no context (see the library's log, RadeonGLLog)\n");
        goto out;
    }
    Printf("GL_RENDERER: %s\n", (LONG) glGetString(GL_RENDERER));
    /* Esc in the library's window (it opens it with no IDCMP): the Shell
     * that took Ctrl-C is hidden behind the GL screen */
    win = (struct Window *) mglGetWindowHandle();
    if (win != NULL && !ModifyIDCMP(win, IDCMP_VANILLAKEY))
        win = NULL;

    glViewport(0, 0, (GLsizei) w, (GLsizei) h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -0.75, 0.75, -1.0, 1.0);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    MakeTexture();
    glBindTexture(GL_TEXTURE_2D, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, 3, TEX, TEX, 0, GL_RGB, GL_UNSIGNED_BYTE, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    if (args.polymode != NULL)
    {
        if (SameWord(args.polymode, "LINE"))
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        else if (SameWord(args.polymode, "POINT"))
        {
            glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
            glPointSize(3.0f);
        }
        else
            Printf("GLSmoke: POLYMODE is LINE or POINT; drawn filled\n");
        glEnable(GL_CULL_FACE);
        Printf("glPolygonMode %s, back faces culled\n", (LONG) args.polymode);
    }

    DateStamp(&d0);
    for (f = 0; f < frames; f++)
    {
        if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
            break;
        if (EscPressed(win))
            break;
        Frame((ULONG) f, (BOOL) !args.nosub);
    }
    DateStamp(&d1);
    ticks = (d1.ds_Days - d0.ds_Days) * 24L * 60L * 50L * 60L
          + (d1.ds_Minute - d0.ds_Minute) * 50L * 60L + (d1.ds_Tick - d0.ds_Tick);
    if (ticks <= 0)
        ticks = 1;
    Printf("%ld frames in %ld.%02ld s: %ld.%ld fps; GL error 0x%lx\n", f, ticks / 50,
           (ticks % 50) * 2, f * 50L / ticks, (f * 500L / ticks) % 10, (LONG) glGetError());
    rc = RETURN_OK;

    if (win != NULL)
        ModifyIDCMP(win, 0);
    mglDeleteContext();

out:
    if (opened)
        MiniGLClose();
    if (IntuitionBase != NULL)
        CloseLibrary((struct Library *) IntuitionBase);
    FreeArgs(rda);
    return rc;
}
