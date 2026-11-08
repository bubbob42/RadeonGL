/*
 * rglcompat.c -- the hooks for minigl.library 29.1's other calls
 * (2026-11-08; src/core/compat.c does the work): glArrayElement,
 * glMultiDrawArrays, glTexCoord4f(v), the double calls, gluPerspective
 * and gluLookAt, glGetDoublev and glGetPointerv, glTexEnvfv, glDrawBuffer
 * and glReadBuffer, edge flags and colour index, texture residency,
 * priorities and memory, MiniGL's mglProhibit... and mglPrintMatrix calls
 * and mglLockBack.
 *
 * A file of its own, linked after everything else, as rgllight.c is: the
 * 68060's caches care where GLQuake's code sits (rgllight.c's note).
 * GLQuake calls none of these.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <libraries/minigl_dispatch.h>

#include "rgl_dispatch.h"
#include "rgl.h"
#include "rglint.h"
#include "core.h"
#include "r200reg.h"

/* MiniGL's requests that change nothing here (29.1 stores them too) */
static GLboolean prohibit_alpha_fallback;
static GLboolean prohibit_mipmapping;
static GLboolean propose_close_desktop;

/* A context's core, with what only the library knows set in it: a window
 * or bitmap context has one buffer (GL_DOUBLEBUFFER, the default draw and
 * read buffers) */
static void Sync(struct Core *c)
{
    c->single_buffer = (((struct Rgl *) c)->kind != CARD_KIND_SCREEN) ? 1UL : 0UL;
}

static struct Core *Ctx(GLcontext context)
{
    if (context == NULL)
        return NULL;
    Sync((struct Core *) context);      /* a context's pointer is its core's (rglint.h) */
    return (struct Core *) context;
}

/* The calling task's newest context, for calls without a context argument
 * (GLU's): the one the SDK made current last */
static struct Core *TaskCore(void)
{
    struct Rgl *r;
    struct Task *me;

    me = FindTask(NULL);
    Forbid();
    for (r = rgl_list; r != NULL && r->owner != me; r = r->next)
        ;
    Permit();
    return (r != NULL) ? &r->core : NULL;
}

/* --- glGet, for rgl.c's hooks ---------------------------------------------------------- */

void Rglc_GetFloatv(struct Core *c, ULONG pname, float *out)
{
    Sync(c);
    Core_GetFloatvX(c, pname, out);
}

void Rglc_GetIntegerv(struct Core *c, ULONG pname, LONG *out)
{
    Sync(c);
    Core_GetIntegervX(c, pname, out);
}

void Hook_GLGetDoublev(GLcontext context, GLenum pname, GLdouble *params)
{
    struct Core *c;
    float v[16];
    ULONG i;
    ULONG e0;
    ULONG e1;

    if ((c = Ctx(context)) == NULL || params == NULL)
        return;
    for (i = 0; i < 16; i++)
        v[i] = 0.0f;
    /* GL leaves params alone when pname is refused (as glGetBooleanv) */
    e0 = c->error;
    c->error = GL_NO_ERROR;
    Core_GetFloatvX(c, pname, v);
    e1 = c->error;
    c->error = (e0 != GL_NO_ERROR) ? e0 : e1;
    if (e1 != GL_NO_ERROR)
        return;
    for (i = 0; i < Core_GetCountX(pname); i++)
        params[i] = (GLdouble) v[i];
}

void Hook_GLGetPointerv(GLcontext context, GLenum pname, GLvoid **params)
{
    struct Core *c;

    if ((c = Ctx(context)) == NULL)
        return;
    Core_GetPointerv(c, pname, params);
}

/* --- Vertices ------------------------------------------------------------------------------ */

void Hook_GLArrayElement(GLcontext context, GLint i)
{
    struct Core *c;

    if ((c = Ctx(context)) == NULL)
        return;
    Core_ArrayElement(c, i);
}

void Hook_GLMultiDrawArrays(GLcontext context, GLenum mode, const GLint *first, const GLsizei *count,
                            GLsizei primcount)
{
    struct Core *c;

    if ((c = Ctx(context)) == NULL)
        return;
    /* GLint is an int, GLsizei a long: both 32 bits, as LONG */
    Core_MultiDrawArrays(c, mode, (const LONG *) first, (const LONG *) count, primcount);
}

void Hook_GLTexCoord4f(GLcontext context, GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
    struct Core *c;

    if ((c = Ctx(context)) == NULL)
        return;
    Core_TexCoord4f(c, s, t, r, q);
}

void Hook_GLTexCoord4fv(GLcontext context, GLfloat *v)
{
    struct Core *c;

    if ((c = Ctx(context)) == NULL || v == NULL)
        return;
    Core_TexCoord4f(c, v[0], v[1], v[2], v[3]);
}

void Hook_GLEdgeFlag(GLcontext context, GLboolean flag)
{
    struct Core *c;

    if ((c = Ctx(context)) == NULL)
        return;
    Core_EdgeFlag(c, flag);
}

void Hook_GLEdgeFlagv(GLcontext context, const GLboolean *flag)
{
    struct Core *c;

    if ((c = Ctx(context)) == NULL || flag == NULL)
        return;
    Core_EdgeFlag(c, *flag);
}

void Hook_GLEdgeFlagPointer(GLcontext context, GLsizei stride, const GLvoid *pointer)
{
    struct Core *c;

    if ((c = Ctx(context)) == NULL)
        return;
    Core_EdgeFlagPointer(c, stride, pointer);
}

void Hook_GLIndexi(GLcontext context, GLint index)
{
    struct Core *c;

    if ((c = Ctx(context)) == NULL)
        return;
    Core_Index(c, (float) index);
}

void Hook_GLIndexiv(GLcontext context, const GLint *index)
{
    struct Core *c;

    if ((c = Ctx(context)) == NULL || index == NULL)
        return;
    Core_Index(c, (float) *index);
}

void Hook_GLIndexPointer(GLcontext context, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    struct Core *c;

    if ((c = Ctx(context)) == NULL)
        return;
    Core_IndexPointer(c, type, stride, pointer);
}

/* --- Matrices: the double calls, GLU --------------------------------------------------------- */

static void ToFloat16(float *f, const GLdouble *m)
{
    ULONG i;

    for (i = 0; i < 16; i++)
        f[i] = (float) m[i];
}

void Hook_GLLoadMatrixd(GLcontext context, const GLdouble *m)
{
    struct Core *c;
    float f[16];

    if ((c = Ctx(context)) == NULL || m == NULL)
        return;
    ToFloat16(f, m);
    Core_LoadMatrixf(c, f);
}

void Hook_GLMultMatrixd(GLcontext context, const GLdouble *m)
{
    struct Core *c;
    float f[16];

    if ((c = Ctx(context)) == NULL || m == NULL)
        return;
    ToFloat16(f, m);
    Core_MultMatrixf(c, f);
}

void Hook_GLRotated(GLcontext context, GLdouble angle, GLdouble x, GLdouble y, GLdouble z)
{
    struct Core *c;

    if ((c = Ctx(context)) == NULL)
        return;
    Core_Rotatef(c, (float) angle, (float) x, (float) y, (float) z);
}

void Hook_GLScaled(GLcontext context, GLdouble x, GLdouble y, GLdouble z)
{
    struct Core *c;

    if ((c = Ctx(context)) == NULL)
        return;
    Core_Scalef(c, (float) x, (float) y, (float) z);
}

void Hook_GLTranslated(GLcontext context, GLdouble x, GLdouble y, GLdouble z)
{
    struct Core *c;

    if ((c = Ctx(context)) == NULL)
        return;
    Core_Translatef(c, (float) x, (float) y, (float) z);
}

void Hook_GLUPerspective(GLfloat fovy, GLfloat aspect, GLfloat znear, GLfloat zfar)
{
    struct Core *c;

    if ((c = TaskCore()) == NULL)
        return;
    Core_Perspective(c, fovy, aspect, znear, zfar);
}

void Hook_GLULookAt(GLfloat ex, GLfloat ey, GLfloat ez, GLfloat cx, GLfloat cy, GLfloat cz,
                    GLfloat ux, GLfloat uy, GLfloat uz)
{
    struct Core *c;
    float eye[3], centre[3], up[3];

    if ((c = TaskCore()) == NULL)
        return;
    eye[0] = ex;
    eye[1] = ey;
    eye[2] = ez;
    centre[0] = cx;
    centre[1] = cy;
    centre[2] = cz;
    up[0] = ux;
    up[1] = uy;
    up[2] = uz;
    Core_LookAt(c, eye, centre, up);
}

/* MiniGL's debugging prints: 29.1 prints only in a debug build, and a
 * release does nothing; so does RadeonGL */
void Hook_MGLPrintMatrix(GLcontext context, int mode)
{
    (void) context;
    (void) mode;
}

void Hook_MGLPrintMatrixStack(GLcontext context, int mode)
{
    (void) context;
    (void) mode;
}

/* --- Textures and buffers --------------------------------------------------------------------- */

void Hook_GLTexEnvfv(GLcontext context, GLenum target, GLenum pname, const GLfloat *params)
{
    struct Core *c;

    if ((c = Ctx(context)) == NULL)
        return;
    Core_TexEnvfv(c, target, pname, params);
}

GLboolean Hook_GLAreTexturesResident(GLcontext context, GLsizei n, const GLuint *textures,
                                     GLboolean *residences)
{
    struct Core *c;

    (void) residences;          /* left alone: every texture is resident */
    if ((c = Ctx(context)) == NULL)
        return GL_FALSE;
    /* GLuint is an unsigned int: 32 bits, as ULONG */
    return Core_AreTexturesResident(c, n, (const ULONG *) textures) ? GL_TRUE : GL_FALSE;
}

void Hook_GLPrioritizeTextures(GLcontext context, GLsizei n, const GLuint *textures,
                               const GLclampf *priorities)
{
    struct Core *c;

    (void) priorities;
    if ((c = Ctx(context)) == NULL)
        return;
    Core_PrioritizeTextures(c, n, (const ULONG *) textures);
}

void Hook_MGLTexMemStat(GLcontext context, GLint *Current, GLint *Peak)
{
    struct Core *c;
    LONG cur;
    LONG peak;

    if ((c = Ctx(context)) == NULL)
        return;
    Core_TexMemStat(c, &cur, &peak);
    if (Current != NULL)
        *Current = cur;
    if (Peak != NULL)
        *Peak = peak;
}

void Hook_GLDrawBuffer(GLcontext context, GLenum mode)
{
    struct Core *c;

    if ((c = Ctx(context)) == NULL)
        return;
    Core_DrawBuffer(c, mode);
}

void Hook_GLReadBuffer(GLcontext context, GLenum mode)
{
    struct Core *c;

    if ((c = Ctx(context)) == NULL)
        return;
    Core_ReadBuffer(c, mode);
}

/* The back buffer is never handed to the program: refused, as 29.1 does
 * whenever it has no lock it could hand out, with the size filled in */
GLboolean Hook_MGLLockBack(GLcontext context, MGLLockInfo *info)
{
    struct Core *c;

    c = Ctx(context);
    if (info != NULL)
    {
        info->width = (c != NULL) ? c->target.width : 0;
        info->height = (c != NULL) ? c->target.height : 0;
        info->depth = (c != NULL && c->target.color_format == R_COLOR_FORMAT_ARGB8888) ? 32 : 16;
        info->pixel_format = 0;
        info->base_address = NULL;
        info->pitch = 0;
    }
    return GL_FALSE;
}

/* --- MiniGL's requests ---------------------------------------------------------------------- */

void Hook_mglProhibitAlphaFallback(GLboolean flag) { prohibit_alpha_fallback = flag; }
void Hook_mglProhibitMipMapping(GLboolean flag) { prohibit_mipmapping = flag; }
void Hook_mglProposeCloseDesktop(GLboolean closeme) { propose_close_desktop = closeme; }

/* --- glInterleavedArrays, GLU ------------------------------------------------------------ */

void Hook_GLInterleavedArrays(GLcontext context, GLenum format, GLsizei stride, const GLvoid *pointer)
{
    struct Core *c;

    if ((c = Ctx(context)) == NULL)
        return;
    Core_InterleavedArrays(c, format, stride, pointer);
}

GLint Hook_GLUBuild2DMipmaps(GLcontext context, GLenum target, GLint internalFormat, GLsizei width,
                             GLsizei height, GLenum format, GLenum type, const GLvoid *data)
{
    struct Core *c;

    if ((c = Ctx(context)) == NULL)
        return 100902;                      /* GLU_OUT_OF_MEMORY: no context to build in */
    return (GLint) Core_Build2DMipmaps(c, target, internalFormat, width, height, format, type, data);
}

/* GLU's own wording for GL's and GLU's errors; NULL for any other code */
const GLubyte *Hook_GLUErrorString(GLenum errCode)
{
    switch (errCode)
    {
        case GL_NO_ERROR:           return (const GLubyte *) "no error";
        case GL_INVALID_ENUM:       return (const GLubyte *) "invalid enumerant";
        case GL_INVALID_VALUE:      return (const GLubyte *) "invalid value";
        case GL_INVALID_OPERATION:  return (const GLubyte *) "invalid operation";
        case GL_STACK_OVERFLOW:     return (const GLubyte *) "stack overflow";
        case GL_STACK_UNDERFLOW:    return (const GLubyte *) "stack underflow";
        case GL_OUT_OF_MEMORY:      return (const GLubyte *) "out of memory";
        case 100900:                return (const GLubyte *) "invalid enumerant";  /* GLU_INVALID_ENUM */
        case 100901:                return (const GLubyte *) "invalid value";      /* GLU_INVALID_VALUE */
        case 100902:                return (const GLubyte *) "out of memory";      /* GLU_OUT_OF_MEMORY */
    }
    return NULL;
}

/* --- MiniGL's main loop ------------------------------------------------------------------- */
/* As 29.1 runs it (others.c, MGLMainLoop): the input window's messages to
 * the program's handlers -- keys as characters, F1-F10 and the cursor keys
 * as MiniGL's special keys, the mouse in GL's window coordinates (y up from
 * the bottom of the inner area) -- then the idle handler, or a wait for the
 * next message without one; until mglExit. */

void Hook_MGLIdleFunc(GLcontext context, IdleFn i)
{
    if (context != NULL)
        ((struct Rgl *) context)->ml_idle = i;
}

void Hook_MGLKeyFunc(GLcontext context, KeyHandlerFn k)
{
    if (context != NULL)
        ((struct Rgl *) context)->ml_key = k;
}

void Hook_MGLMouseFunc(GLcontext context, MouseHandlerFn m)
{
    if (context != NULL)
        ((struct Rgl *) context)->ml_mouse = m;
}

void Hook_MGLSpecialFunc(GLcontext context, SpecialHandlerFn s)
{
    if (context != NULL)
        ((struct Rgl *) context)->ml_special = s;
}

void Hook_MGLExit(GLcontext context)
{
    if (context != NULL)
        ((struct Rgl *) context)->ml_running = 0;
}

static LONG Special(UWORD code)
{
    if (code >= 0x50 && code <= 0x59)
        return (LONG) MGLKEY_F1 + (code - 0x50);
    switch (code)
    {
        case 0x4c: return (LONG) MGLKEY_CUP;
        case 0x4d: return (LONG) MGLKEY_CDOWN;
        case 0x4e: return (LONG) MGLKEY_CRIGHT;
        case 0x4f: return (LONG) MGLKEY_CLEFT;
    }
    return -1;
}

void Hook_MGLMainLoop(GLcontext context)
{
    struct Rgl *r;
    struct Window *win;
    struct IntuiMessage *im;
    ULONG cls;
    UWORD code, qual;
    WORD mx, my;
    GLbitfield buttons;
    LONG sp, bl, bt, ch;

    r = (struct Rgl *) context;
    if (r == NULL)
        return;
    win = (struct Window *) Hook_MGLGetInputWindowHandle(context);
    if (win == NULL)
        return;                             /* a bitmap context has no window */
    /* the program's own classes stay; held off as 29.1 does: DELTAMOVE (the
     * mouse is reported absolute), the VERIFY classes and REFRESHWINDOW */
    if (!ModifyIDCMP(win, (win->IDCMPFlags & ~(IDCMP_DELTAMOVE | IDCMP_REFRESHWINDOW | IDCMP_SIZEVERIFY
                                                | IDCMP_REQVERIFY | IDCMP_MENUVERIFY))
                          | IDCMP_VANILLAKEY | IDCMP_RAWKEY | IDCMP_MOUSEMOVE | IDCMP_MOUSEBUTTONS))
        return;
    r->ml_running = 1;
    while (r->ml_running)
    {
        while (win->UserPort != NULL && (im = (struct IntuiMessage *) GetMsg(win->UserPort)) != NULL)
        {
            cls = im->Class;
            code = im->Code;
            qual = im->Qualifier;
            mx = im->MouseX;
            my = im->MouseY;
            ReplyMsg((struct Message *) im);
            switch (cls)
            {
                case IDCMP_VANILLAKEY:
                    if (r->ml_key != NULL)
                        r->ml_key((char) code);
                    break;
                case IDCMP_RAWKEY:          /* presses only: the handler has no up/down */
                    sp = (code & 0x80) ? -1 : Special(code);
                    if (r->ml_special != NULL && sp >= 0)
                        r->ml_special((MGLspecial) sp);
                    break;
                case IDCMP_MOUSEBUTTONS:
                case IDCMP_MOUSEMOVE:
                    buttons = 0;
                    if (qual & IEQUALIFIER_LEFTBUTTON)
                        buttons |= MGL_BUTTON_LEFT;
                    if (qual & IEQUALIFIER_RBUTTON)
                        buttons |= MGL_BUTTON_RIGHT;
                    if (qual & IEQUALIFIER_MIDBUTTON)
                        buttons |= MGL_BUTTON_MID;
                    if (r->ml_mouse != NULL)
                    {
                        bl = win->BorderLeft;
                        bt = win->BorderTop;
                        ch = win->Height - bt - win->BorderBottom;
                        r->ml_mouse((GLint) (mx - bl), (GLint) ((ch - 1) - (my - bt)), buttons);
                    }
                    break;
            }
        }
        if (r->ml_idle != NULL)
            r->ml_idle();
        else if (r->ml_running && win->UserPort != NULL)
            WaitPort(win->UserPort);
    }
}

/* --- GLU quadrics -------------------------------------------------------------------------- */
/* The object GLU's calls share (opaque to programs); the drawing is the
 * calling task's context's (the GLU calls have no context argument). */
struct GLUquadricObj_t
{
    struct CoreQuadric q;
    MGLUfuncptr error;          /* GLU_ERROR's callback, or NULL */
};

#define GLU_ERROR_CB      100103
#define GLU_INVALID_ENUM_ 100900

static void QError(GLUquadricObj *o, GLenum code)
{
    if (o->error != NULL)
        ((void (*)(GLenum)) o->error)(code);
}

GLUquadricObj *Hook_GLUNewQuadric(void)
{
    GLUquadricObj *o;

    o = (GLUquadricObj *) AllocVec(sizeof(*o), MEMF_ANY | MEMF_CLEAR);
    if (o != NULL)
    {
        o->q.normals = GLU_SMOOTH_Q;
        o->q.style = GLU_FILL_Q;
        o->q.orientation = GLU_OUTSIDE_Q;
    }
    return o;
}

void Hook_GLUDeleteQuadric(GLUquadricObj *q)
{
    if (q != NULL)
        FreeVec(q);
}

void Hook_GLUQuadricNormals(GLUquadricObj *q, GLenum normals)
{
    if (q == NULL)
        return;
    if (normals == GLU_SMOOTH_Q || normals == GLU_FLAT_Q || normals == GLU_NONE_Q)
        q->q.normals = normals;
    else
        QError(q, GLU_INVALID_ENUM_);
}

void Hook_GLUQuadricTexture(GLUquadricObj *q, GLboolean textureCoords)
{
    if (q != NULL)
        q->q.texture = textureCoords ? 1UL : 0UL;
}

void Hook_GLUQuadricDrawStyle(GLUquadricObj *q, GLenum drawStyle)
{
    if (q == NULL)
        return;
    if (drawStyle == GLU_POINT_Q || drawStyle == GLU_LINE_Q || drawStyle == GLU_FILL_Q
        || drawStyle == GLU_SILHOUETTE_Q)
        q->q.style = drawStyle;
    else
        QError(q, GLU_INVALID_ENUM_);
}

void Hook_GLUQuadricOrientation(GLUquadricObj *q, GLenum orientation)
{
    if (q == NULL)
        return;
    if (orientation == GLU_OUTSIDE_Q || orientation == GLU_INSIDE_Q)
        q->q.orientation = orientation;
    else
        QError(q, GLU_INVALID_ENUM_);
}

void Hook_GLUQuadricCallback(GLUquadricObj *q, GLenum which, MGLUfuncptr fn)
{
    if (q != NULL && which == GLU_ERROR_CB)
        q->error = fn;
}

void Hook_GLUCylinder(GLUquadricObj *q, GLdouble base, GLdouble top, GLdouble height, GLint slices,
                      GLint stacks)
{
    struct Core *c;

    if (q == NULL || (c = TaskCore()) == NULL)
        return;
    Core_Cylinder(c, &q->q, (float) base, (float) top, (float) height, slices, stacks);
}

void Hook_GLUSphere(GLUquadricObj *q, GLdouble radius, GLint slices, GLint stacks)
{
    struct Core *c;

    if (q == NULL || (c = TaskCore()) == NULL)
        return;
    Core_Sphere(c, &q->q, (float) radius, slices, stacks);
}

void Hook_GLUDisk(GLUquadricObj *q, GLdouble inner, GLdouble outer, GLint slices, GLint loops)
{
    struct Core *c;

    if (q == NULL || (c = TaskCore()) == NULL)
        return;
    Core_Disk(c, &q->q, (float) inner, (float) outer, slices, loops);
}

/* --- glBlendEquation, glBlendFuncSeparate (compat.c) ----------------------------------------- */

void Hook_GLBlendEquation(GLcontext context, GLenum mode)
{
    struct Core *c;

    if ((c = Ctx(context)) == NULL)
        return;
    Core_BlendEquation(c, mode);
}

void Hook_GLBlendFuncSeparate(GLcontext context, GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha,
                              GLenum dstAlpha)
{
    struct Core *c;

    if ((c = Ctx(context)) == NULL)
        return;
    Core_BlendFuncSeparate(c, srcRGB, dstRGB, srcAlpha, dstAlpha);
}

/* --- glColorTable (compat.c); GL_EXTENSIONS with 29.1's palette names ------------------------ */

const char Rglc_Extensions[] = CORE_EXTENSIONS RGL_PALETTE_EXTENSIONS;

void Hook_GLColorTable(GLcontext context, GLenum target, GLenum internalformat, GLint width,
                       GLenum format, GLenum type, GLvoid *data)
{
    struct Core *c;

    if ((c = Ctx(context)) == NULL)
        return;
    Core_ColorTable(c, target, internalformat, width, format, type, data);
}

