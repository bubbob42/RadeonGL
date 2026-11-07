/*
 * GLModes -- RadeonGL's screen modes (2026-11-06): the list the library
 * offers (mglGetSupportedScreenModes), a context in one of them
 * (mglCreateContextFromID), and the size of a running context changed
 * (mglResizeContext).
 *
 *   GLModes                   the list: id, size, depth, name
 *   GLModes ID=$50051000      the list, then FRAMES frames (60) in that mode
 *   GLModes ID=... RESIZE=640x480,1280x720,...
 *                             after the first frames, each size in turn
 *                             (mglResizeContext), FRAMES frames each, and
 *                             back to the first size at the end
 *
 * Each frame is cleared to a colour of its own, and a white square in the
 * middle shows that the new size is the one drawn. Prints what each step
 * reported. Esc on the GL screen stops it early.
 *
 * GCC only: it links the MiniGL SDK's import library, as GLSmoke does.
 */
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/minigl.h>
#include <stdlib.h>

#define TEMPLATE "ID/K,FRAMES/N,RESIZE/K"

const char ver[] = "$VER: GLModes 0.1 (06.11.2026)";
struct IntuitionBase *IntuitionBase;
static LONG nmodes;

static GLboolean PrintMode(MGLScreenMode *m)
{
    Printf("  $%08lx %4ld x %4ld %2ld bit  %s\n", (ULONG) m->id, (LONG) m->width,
           (LONG) m->height, (LONG) m->bit_depth, (LONG) m->mode_name);
    nmodes++;
    return GL_FALSE;
}

/* FRAMES frames at w x h; FALSE when Esc was pressed */
static BOOL Frames(LONG frames, GLint w, GLint h)
{
    struct Window *win;
    struct IntuiMessage *im;
    LONG f;
    BOOL esc;
    GLfloat s;

    esc = FALSE;
    win = (struct Window *) mglGetWindowHandle();
    if (win != NULL)
        ModifyIDCMP(win, IDCMP_VANILLAKEY);
    s = (GLfloat) ((w < h) ? w : h) / 4.0f;
    for (f = 0; f < frames && !esc; f++)
    {
        glViewport(0, 0, w, h);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, (GLdouble) w, (GLdouble) h, 0.0, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glClearColor((f & 1) ? 0.6f : 0.1f, (f & 2) ? 0.5f : 0.1f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glColor3f(1.0f, 1.0f, 1.0f);
        glBegin(GL_QUADS);
        glVertex2f(w / 2.0f - s, h / 2.0f - s);
        glVertex2f(w / 2.0f + s, h / 2.0f - s);
        glVertex2f(w / 2.0f + s, h / 2.0f + s);
        glVertex2f(w / 2.0f - s, h / 2.0f + s);
        glEnd();
        mglSwitchDisplay();
        win = (struct Window *) mglGetWindowHandle();
        while (win != NULL && win->UserPort != NULL
               && (im = (struct IntuiMessage *) GetMsg(win->UserPort)) != NULL)
        {
            if (im->Class == IDCMP_VANILLAKEY && im->Code == 27)
                esc = TRUE;
            ReplyMsg((struct Message *) im);
        }
        if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
            esc = TRUE;
    }
    return (BOOL) !esc;
}

int main(void)
{
    struct RDArgs *rd;
    LONG args[3];
    LONG frames;
    ULONG id;
    GLint w, h, w0, h0, ret;
    char *p;
    BOOL opened, go;
    int rc;

    rc = RETURN_FAIL;
    opened = FALSE;
    args[0] = args[1] = args[2] = 0;
    rd = ReadArgs((CONST_STRPTR) TEMPLATE, args, NULL);
    if (rd == NULL)
    {
        PrintFault(IoErr(), (CONST_STRPTR) "GLModes");
        return RETURN_ERROR;
    }
    frames = args[1] ? *(LONG *) args[1] : 60;
    IntuitionBase = (struct IntuitionBase *) OpenLibrary((CONST_STRPTR) "intuition.library", 39);
    if (IntuitionBase == NULL)
        goto out;
    if (!MiniGLOpen())
    {
        Printf("GLModes: minigl.library 29.1 did not open\n");
        goto out;
    }
    opened = TRUE;

    nmodes = 0;
    Printf("mglGetSupportedScreenModes:\n");
    ret = mglGetSupportedScreenModes(PrintMode);
    Printf("%ld modes, returned $%08lx\n", nmodes, (ULONG) ret);
    rc = (nmodes > 0) ? RETURN_OK : RETURN_WARN;
    if (args[0] == 0)
        goto out;

    id = strtoul((char *) args[0] + (*(char *) args[0] == '$'), NULL, (*(char *) args[0] == '$') ? 16 : 0);
    w = h = 0;
    if (!mglCreateContextFromID((GLint) id, &w, &h))
    {
        Printf("mglCreateContextFromID($%08lx): no context\n", id);
        rc = RETURN_FAIL;
        goto out;
    }
    Printf("mglCreateContextFromID($%08lx): %ld x %ld\n", id, (LONG) w, (LONG) h);
    w0 = w;
    h0 = h;
    go = Frames(frames, w, h);

    /* RESIZE=WxH,WxH,...: each in turn, then back to the first size */
    p = (char *) args[2];
    while (go && p != NULL && *p != 0)
    {
        w = (GLint) strtol(p, &p, 10);
        if (*p == 'x' || *p == 'X')
            p++;
        h = (GLint) strtol(p, &p, 10);
        if (*p == ',')
            p++;
        ret = mglResizeContext(w, h);
        Printf("mglResizeContext(%ld, %ld): %s\n", (LONG) w, (LONG) h, (LONG) (ret ? "TRUE" : "FALSE"));
        if (!ret)
        {
            rc = RETURN_WARN;
            continue;
        }
        go = Frames(frames, w, h);
    }
    if (go && args[2] != 0 && (w != w0 || h != h0))
    {
        ret = mglResizeContext(w0, h0);
        Printf("mglResizeContext(%ld, %ld) back: %s\n", (LONG) w0, (LONG) h0, (LONG) (ret ? "TRUE" : "FALSE"));
        if (ret)
            go = Frames(frames, w0, h0);
    }
    mglDeleteContext();

out:
    if (opened)
        MiniGLClose();
    if (IntuitionBase != NULL)
        CloseLibrary((struct Library *) IntuitionBase);
    FreeArgs(rd);
    return rc;
}
