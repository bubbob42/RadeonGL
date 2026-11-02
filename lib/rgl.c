/*
 * rgl.c -- the RadeonGL back end of minigl.library: the dispatch hooks
 * (SDK types in, the core's types out), the context, and for milestone 2
 * the null card (nullcard.h) in place of a Radeon.
 *
 * One context at a time, owned by the task that made it (concept 7.1).
 * The hooks ignore their context argument: there is only `rgl`.
 *
 * Log: when the context is deleted, a summary goes to the file named by
 * the variable RadeonGLLog (default PROGDIR:radeongl.log): frames, bus
 * dwords, draws, packet-checker errors with the first one decoded, and
 * every dispatch slot called that is not implemented yet.
 *
 * Built WITHOUT -msasregparm: clients call the table with GCC's stack
 * convention. -m68060 -m68881 together (docs/core.md).
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <libraries/minigl_dispatch.h>

#include "rgl_dispatch.h"
#include "backend.h"
#include "core.h"
#include "coreint.h"
#include "r2ring.h"
#include "r2check.h"
#include "r200reg.h"
#include "nullcard.h"

const char Back_IdString[] =
    "$VER: minigl.library 29.1 (02.11.2026) RadeonGL null card (checks, draws nothing), dispatch ABI 5\r\n";

/* The fake GPU address map of the null card. */
#define RING_GPU    0x00100000UL
#define COLOR_GPU   0x01000000UL
#define DEPTH_GPU   0x01800000UL
#define HEAP_GPU    0x02000000UL
#define RING_DW     65536UL         /* 256 KB */
#define FENCE_REG   R_SCRATCH_REG2

struct Rgl
{
    struct Core core;
    struct R2Ring ring;
    struct NullCard card;
    ULONG *ring_mem;
    ULONG *heap_mem;
    ULONG heap_size;
    struct Window *win;
    struct Task *owner;
    ULONG frame;
    ULONG frame_dw_max;
    ULONG last_dwords;
    ULONG unimpl[(DISPATCH_SLOTS + 31) / 32];
    char log[160];
};

GLcontext Rgl_CurrentContext = 0;
static struct Rgl *rgl = NULL;
static ULONG unimpl_early[(DISPATCH_SLOTS + 31) / 32];

/* --- Memory and the core's environment ------------------------------------------ */

static APTR EnvAlloc(APTR env, ULONG size)
{
    (void) env;
    return AllocVec(size, MEMF_ANY | MEMF_CLEAR);
}

static void EnvFree(APTR env, APTR mem, ULONG size)
{
    (void) env;
    (void) size;
    FreeVec(mem);
}

static LONG EnvIdle(APTR env)
{
    return R2Ring_Idle(&((struct Rgl *) env)->ring);
}

void Rgl_Unimpl(ULONG slot)
{
    ULONG *bits;

    bits = (rgl != NULL) ? rgl->unimpl : unimpl_early;
    bits[slot / 32] |= 1UL << (slot % 32);
    if (rgl != NULL)
        Core_Error(&rgl->core, GL_INVALID_OPERATION);
}

/* --- The log ------------------------------------------------------------------------- */

static ULONG StrLen(const char *s)
{
    ULONG n;

    for (n = 0; s[n] != 0; n++)
        ;
    return n;
}

static void Put(BPTR f, const char *s)
{
    Write(f, (APTR) s, (LONG) StrLen(s));
}

static void PutU(BPTR f, ULONG v)
{
    char b[12];
    int i;

    i = 11;
    b[i] = 0;
    do
    {
        b[--i] = (char) ('0' + v % 10);
        v /= 10;
    } while (v != 0 && i > 0);
    Put(f, b + i);
}

static void PutX(BPTR f, ULONG v)
{
    char b[11];
    int i;

    b[0] = '0';
    b[1] = 'x';
    for (i = 0; i < 8; i++)
        b[2 + i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 15];
    b[10] = 0;
    Put(f, b);
}

static void WriteLog(struct Rgl *r)
{
    BPTR f;
    ULONG i;
    const char *name;

    f = Open((STRPTR) r->log, MODE_NEWFILE);
    if (f == 0)
        return;
    Put(f, "RadeonGL null card\n");
    Put(f, "frames: "); PutU(f, r->frame); Put(f, "\n");
    Put(f, "packets: "); PutU(f, r->card.packets); Put(f, "\n");
    Put(f, "dwords: "); PutU(f, r->card.dwords); Put(f, "\n");
    Put(f, "dwords per frame, max: "); PutU(f, r->frame_dw_max); Put(f, "\n");
    Put(f, "draws: "); PutU(f, r->card.st.draws); Put(f, "\n");
    Put(f, "vertices: "); PutU(f, r->card.st.vertices); Put(f, "\n");
    Put(f, "state blocks: "); PutU(f, r->core.stat_state_emits); Put(f, "\n");
    Put(f, "split packets: "); PutU(f, r->core.stat_splits); Put(f, "\n");
    Put(f, "idle waits: "); PutU(f, r->core.stat_idle_waits); Put(f, "\n");
    Put(f, "ring wraps: "); PutU(f, r->ring.stats.wraps); Put(f, "\n");
    Put(f, "texture heap used: "); PutU(f, r->core.heap.used); Put(f, " of ");
    PutU(f, r->heap_size); Put(f, "\n");
    Put(f, "WPTR misaligned: "); PutU(f, r->card.bad_align); Put(f, "\n");
    Put(f, "packets across the ring end: "); PutU(f, r->card.straddle); Put(f, "\n");
    Put(f, "checker errors: "); PutU(f, r->card.errors); Put(f, "\n");
    if (r->card.errors != 0)
    {
        Put(f, "first: "); Put(f, R2Chk_ErrorName(r->card.first_code));
        Put(f, " at ring dword "); PutU(f, r->card.first.at);
        Put(f, ", what "); PutX(f, r->card.first.what);
        name = R2Chk_RegName(r->card.first.what);
        if (name != NULL)
        {
            Put(f, " ("); Put(f, name); Put(f, ")");
        }
        Put(f, ", value "); PutX(f, r->card.first.value); Put(f, "\n");
    }
    Put(f, "GL error pending: "); PutX(f, r->core.error); Put(f, "\n");
    Put(f, "slots called but not implemented:");
    for (i = 0; i < DISPATCH_SLOTS; i++)
        if ((r->unimpl[i / 32] | unimpl_early[i / 32]) & (1UL << (i % 32)))
        {
            Put(f, " ");
            Put(f, Rgl_SlotName[i]);
        }
    Put(f, "\n");
    Close(f);
}

/* --- Context ------------------------------------------------------------------------- */

static void FreeRgl(struct Rgl *r)
{
    Core_Free(&r->core);
    if (r->win != NULL)
    {
        ModifyIDCMP(r->win, 0);
        CloseWindow(r->win);
    }
    if (r->heap_mem != NULL)
        FreeVec(r->heap_mem);
    if (r->ring_mem != NULL)
        FreeVec(r->ring_mem);
    FreeVec(r);
}

void Back_Init(void)
{
}

void *Hook_MGLCreateContext(int offx, int offy, int w, int h)
{
    struct Rgl *r;
    struct R2ChkCfg cfg;
    struct CoreTarget t;
    struct CoreEnv env;
    struct Screen *scr;
    ULONG pitch;
    LONG n;
    ULONG i;

    (void) offx;
    (void) offy;
    if (rgl != NULL || w <= 0 || h <= 0 || w > 2048 || h > 2048)
        return NULL;
    r = (struct Rgl *) AllocVec(sizeof(*r), MEMF_ANY | MEMF_CLEAR);
    if (r == NULL)
        return NULL;
    r->owner = FindTask(NULL);
    r->ring_mem = (ULONG *) AllocVec(RING_DW * 4, MEMF_ANY | MEMF_CLEAR);
    /* the texture heap: as much of 32 MB as there is */
    for (r->heap_size = 32UL << 20; r->heap_size >= (4UL << 20); r->heap_size >>= 1)
    {
        r->heap_mem = (ULONG *) AllocVec(r->heap_size, MEMF_ANY);
        if (r->heap_mem != NULL)
            break;
    }
    if (r->ring_mem == NULL || r->heap_mem == NULL)
    {
        FreeRgl(r);
        return NULL;
    }
    n = GetVar((STRPTR) "RadeonGLLog", (STRPTR) r->log, sizeof(r->log), 0);
    if (n <= 0)
        for (i = 0; i < sizeof("PROGDIR:radeongl.log"); i++)
            r->log[i] = "PROGDIR:radeongl.log"[i];

    /* rows a multiple of 32 bytes: the present reads the back buffer as a
     * texture (R200 TXPITCH, Mesa); 16 pixels at 16 bits */
    pitch = ((ULONG) w + 15) & ~15UL;
    for (i = 0; i < sizeof(cfg); i++)
        ((UBYTE *) &cfg)[i] = 0;
    cfg.range[0].gpu = COLOR_GPU; cfg.range[0].size = pitch * (ULONG) h * 2; cfg.range[0].kinds = R2K_COLOR;
    cfg.range[1].gpu = DEPTH_GPU; cfg.range[1].size = pitch * (ULONG) h * 2; cfg.range[1].kinds = R2K_DEPTH;
    cfg.range[2].gpu = HEAP_GPU;  cfg.range[2].size = r->heap_size;          cfg.range[2].kinds = R2K_TEXTURE;
    cfg.nranges = 3;
    cfg.fence_reg = FENCE_REG;
    NullCard_Init(&r->card, r->ring_mem, RING_DW, 1, FENCE_REG, &cfg);
    if (R2Ring_Init(&r->ring, r->ring_mem, RING_GPU, RING_DW, &r->card.hw, 1, FENCE_REG,
                    1000000) != R2_OK)
    {
        FreeRgl(r);
        return NULL;
    }
    r->ring.kick = 2048;        /* hand work to the "GPU" every 8 KB */

    t.width = (ULONG) w;
    t.height = (ULONG) h;
    t.color_gpu = COLOR_GPU;
    t.color_pitch = pitch;
    t.color_format = R_COLOR_FORMAT_RGB565;
    t.depth_gpu = DEPTH_GPU;
    t.depth_pitch = pitch;
    env.alloc = EnvAlloc;
    env.free = EnvFree;
    env.wait_idle = EnvIdle;
    env.env = r;
    if (Core_Init(&r->core, &r->ring.cs, &env, &t, HEAP_GPU, r->heap_mem, r->heap_size)
        != GL_NO_ERROR)
    {
        FreeRgl(r);
        return NULL;
    }

    /* the client takes its input from this window (mglGetInputWindowHandle) */
    scr = LockPubScreen(NULL);
    if (scr != NULL)
    {
        r->win = OpenWindowTags(NULL,
            WA_PubScreen, (ULONG) scr,
            WA_Left, 20,
            WA_Top, 20,
            WA_InnerWidth, 320UL,
            WA_InnerHeight, 120UL,
            WA_Title, (ULONG) "RadeonGL null card -- checks, draws nothing",
            WA_DragBar, TRUE,
            WA_DepthGadget, TRUE,
            WA_Activate, TRUE,
            WA_RMBTrap, TRUE,
            WA_NoCareRefresh, TRUE,
            TAG_DONE);
        UnlockPubScreen(NULL, scr);
    }
    rgl = r;
    return (void *) r;
}

void Hook_MGLDeleteContext(GLcontext context)
{
    struct Rgl *r;

    (void) context;
    r = rgl;
    if (r == NULL)
        return;
    R2Ring_Idle(&r->ring);
    WriteLog(r);
    rgl = NULL;
    Rgl_CurrentContext = 0;
    FreeRgl(r);
}

/* LibClose: the owner died without mglDeleteContext. No file I/O here. */
void Back_ClientGone(struct Task *task)
{
    struct Rgl *r;

    r = rgl;
    if (r == NULL || r->owner != task)
        return;
    rgl = NULL;
    Rgl_CurrentContext = 0;
    FreeRgl(r);
}

void Hook_MGLSwitchDisplay(GLcontext context)
{
    struct Rgl *r;
    ULONG seq;
    ULONG dw;

    (void) context;
    r = rgl;
    if (r == NULL)
        return;
    /* end of frame: a fence, hand it over, wait (the null card is instant;
     * the Radeon will present here) */
    if (R2Ring_Fence(&r->ring, &seq) == R2_OK)
        R2Ring_WaitFence(&r->ring, seq);
    dw = r->card.dwords - r->last_dwords;
    if (dw > r->frame_dw_max)
        r->frame_dw_max = dw;
    r->last_dwords = r->card.dwords;
    r->frame++;
}

GLboolean Hook_MGLLockDisplay(GLcontext context)
{
    (void) context;
    return GL_TRUE;
}

void Hook_MGLUnlockDisplay(GLcontext context)
{
    (void) context;
}

void *Hook_MGLGetWindowHandle(GLcontext context)
{
    (void) context;
    return (rgl != NULL) ? (void *) rgl->win : NULL;
}

void *Hook_MGLGetInputWindowHandle(GLcontext context)
{
    return Hook_MGLGetWindowHandle(context);
}

GLboolean Hook_MGLResizeContext(GLcontext context, GLsizei width, GLsizei height)
{
    (void) context;
    (void) width;
    (void) height;
    return GL_FALSE;            /* not yet */
}

/* MiniGL's extras: stored or harmless */
void Hook_MGLDrawMultitexBuffer(GLcontext context, GLenum BSrc, GLenum BDst, GLenum TexEnv)
{
    /* the fake-multitexture flush; with real multitexture GLQuake still
     * calls it once a frame. Nothing is buffered here, so nothing to do
     * (MiniGLV3D's side effect on unit 1's texenv is not copied). */
    (void) context;
    (void) BSrc;
    (void) BDst;
    (void) TexEnv;
}

void Hook_MGLMinTriArea(GLcontext context, GLfloat area)
{
    (void) context;
    if (rgl != NULL)
        rgl->core.min_tri_area = area;
}

void Hook_MGLSetZOffset(GLcontext context, GLfloat offset)
{
    (void) context;
    if (rgl != NULL)
        Core_SetZOffset(&rgl->core, offset);
}

void Hook_MGLLockMode(GLcontext context, GLenum lockMode)
{
    (void) context;
    if (rgl != NULL)
        rgl->core.lock_mode = lockMode;
}

void Hook_MGLEnableSync(GLcontext context, GLboolean enable)
{
    (void) context;
    if (rgl != NULL)
        rgl->core.sync = enable;
}

void Hook_MGLClearPointer(GLcontext context)
{
    (void) context;
}

void Hook_MGLSetPointer(GLcontext context)
{
    (void) context;
}

void Hook_mglChooseGuardBand(GLboolean flag) { (void) flag; }
void Hook_mglChooseMtexBufferSize(int size) { (void) size; }
void Hook_mglChooseNumberOfBuffers(int number) { (void) number; }
void Hook_mglChoosePixelDepth(int depth) { (void) depth; }
void Hook_mglChooseTextureBufferSize(int size) { (void) size; }
void Hook_mglChooseVertexBufferSize(int size) { (void) size; }
void Hook_mglChooseWindowMode(GLboolean flag) { (void) flag; }
void Hook_mglChooseZBufferDepth(int bits) { (void) bits; }

/* --- GL --------------------------------------------------------------------------- */

#define C (&rgl->core)
/* a GL call without a context (before mglCreateContext, after
 * mglDeleteContext) does nothing instead of following NULL */
#define GUARD if (rgl == NULL) return

void Hook_GLBegin(GLcontext context, GLenum mode) { (void) context; GUARD; Core_Begin(C, mode); }
void Hook_GLEnd(GLcontext context) { (void) context; GUARD; Core_End(C); }

void Hook_GLVertex4f(GLcontext context, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    (void) context; GUARD;
    if (w == 1.0f)
        Core_Vertex3f(C, x, y, z);
    else
        Core_Vertex4f(C, x, y, z, w);
}

void Hook_GLVertex2fv(GLcontext context, GLfloat *v) { (void) context; GUARD; Core_Vertex3f(C, v[0], v[1], 0.0f); }
void Hook_GLVertex3fv(GLcontext context, GLfloat *v) { (void) context; GUARD; Core_Vertex3f(C, v[0], v[1], v[2]); }
void Hook_GLVertex4fv(GLcontext context, GLfloat *v) { (void) context; GUARD; Core_Vertex4f(C, v[0], v[1], v[2], v[3]); }
void Hook_GLTexCoord2f(GLcontext context, GLfloat s, GLfloat t) { (void) context; GUARD; Core_TexCoord2f(C, s, t); }
void Hook_GLTexCoord2fv(GLcontext context, GLfloat *v) { (void) context; GUARD; Core_TexCoord2f(C, v[0], v[1]); }

void Hook_GLMultiTexCoord2fARB(GLcontext context, GLenum unit, GLfloat s, GLfloat t)
{
    (void) context; GUARD;
    Core_MultiTexCoord2f(C, unit, s, t);
}

void Hook_GLMultiTexCoord2fvARB(GLcontext context, GLenum unit, GLfloat *v)
{
    (void) context; GUARD;
    Core_MultiTexCoord2f(C, unit, v[0], v[1]);
}

void Hook_GLColor4ub(GLcontext context, GLubyte red, GLubyte green, GLubyte blue, GLubyte alhpa)
{
    (void) context; GUARD;
    Core_Color4ub(C, red, green, blue, alhpa);
}

void Hook_GLColor4ubv(GLcontext context, GLubyte *v) { (void) context; GUARD; Core_Color4ub(C, v[0], v[1], v[2], v[3]); }
void Hook_GLColor3ubv(GLcontext context, GLubyte *v) { (void) context; GUARD; Core_Color4ub(C, v[0], v[1], v[2], 255); }

void Hook_GLColor4f(GLcontext context, GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    (void) context; GUARD;
    Core_Color4f(C, red, green, blue, alpha);
}

void Hook_GLColor4fv(GLcontext context, GLfloat *v) { (void) context; GUARD; Core_Color4f(C, v[0], v[1], v[2], v[3]); }
void Hook_GLColor3fv(GLcontext context, GLfloat *v) { (void) context; GUARD; Core_Color4f(C, v[0], v[1], v[2], 1.0f); }
void Hook_GLNormal3f(GLcontext context, GLfloat x, GLfloat y, GLfloat z) { (void) context; GUARD; Core_Normal3f(C, x, y, z); }
void Hook_GLNormal3fv(GLcontext context, GLfloat *n) { (void) context; GUARD; Core_Normal3f(C, n[0], n[1], n[2]); }

void Hook_MGLSetState(GLcontext context, GLenum cap, GLboolean state)
{
    (void) context;
    Core_Enable(C, cap, state);
}

GLboolean Hook_GLIsEnabled(GLcontext context, GLenum cap) { (void) context; return rgl ? (GLboolean) Core_IsEnabled(C, cap) : GL_FALSE; }
void Hook_GLActiveTextureARB(GLcontext context, GLenum unit) { (void) context; GUARD; Core_ActiveTexture(C, unit); }
void Hook_GLClientActiveTextureARB(GLcontext context, GLenum unit) { (void) context; GUARD; Core_ClientActiveTexture(C, unit); }

void Hook_GLTexEnvi(GLcontext context, GLenum target, GLenum pname, GLint param)
{
    (void) context; GUARD;
    Core_TexEnvi(C, target, pname, param);
}

void Hook_GLDrawElements(GLcontext context, GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
    (void) context; GUARD;
    Core_DrawElements(C, mode, count, type, indices);
}

void Hook_GLDrawArrays(GLcontext context, GLenum mode, GLint first, GLsizei count)
{
    (void) context; GUARD;
    Core_DrawArrays(C, mode, first, count);
}

void Hook_GLLockArrays(GLcontext context, GLuint first, GLsizei count)
{
    (void) context; GUARD;
    Core_LockArrays(C, (LONG) first, count);
}

void Hook_GLUnlockArrays(GLcontext context) { (void) context; GUARD; Core_UnlockArrays(C); }
void Hook_GLEnableClientState(GLcontext context, GLenum cap) { (void) context; GUARD; Core_EnableClientState(C, cap, 1); }
void Hook_GLDisableClientState(GLcontext context, GLenum cap) { (void) context; GUARD; Core_EnableClientState(C, cap, 0); }

void Hook_GLVertexPointer(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    (void) context; GUARD;
    Core_VertexPointer(C, size, type, stride, pointer);
}

void Hook_GLTexCoordPointer(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    (void) context; GUARD;
    Core_TexCoordPointer(C, size, type, stride, pointer);
}

void Hook_GLColorPointer(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    (void) context; GUARD;
    Core_ColorPointer(C, size, type, stride, pointer);
}

void Hook_GLRotatefEXT(GLcontext context, GLfloat angle, const GLint xyz)
{
    (void) context; GUARD;
    Core_RotateAxisDeg(C, angle, xyz);
}

void Hook_GLRotatefEXTs(GLcontext context, GLfloat sin_an, GLfloat cos_an, const GLint xyz)
{
    (void) context; GUARD;
    Core_RotateAxis(C, sin_an, cos_an, xyz);
}

void Hook_GLRotatef(GLcontext context, GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    (void) context; GUARD;
    Core_Rotatef(C, angle, x, y, z);
}

void Hook_GLTranslatef(GLcontext context, GLfloat x, GLfloat y, GLfloat z) { (void) context; GUARD; Core_Translatef(C, x, y, z); }
void Hook_GLScalef(GLcontext context, GLfloat x, GLfloat y, GLfloat z) { (void) context; GUARD; Core_Scalef(C, x, y, z); }
void Hook_GLPushMatrix(GLcontext context) { (void) context; GUARD; Core_PushMatrix(C); }
void Hook_GLPopMatrix(GLcontext context) { (void) context; GUARD; Core_PopMatrix(C); }
void Hook_GLMatrixMode(GLcontext context, GLenum mode) { (void) context; GUARD; Core_MatrixMode(C, mode); }
void Hook_GLLoadIdentity(GLcontext context) { (void) context; GUARD; Core_LoadIdentity(C); }
void Hook_GLLoadMatrixf(GLcontext context, const GLfloat *m) { (void) context; GUARD; Core_LoadMatrixf(C, m); }
void Hook_GLMultMatrixf(GLcontext context, const GLfloat *m) { (void) context; GUARD; Core_MultMatrixf(C, m); }

void Hook_GLOrtho(GLcontext context, GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
                  GLdouble zNear, GLdouble zFar)
{
    (void) context; GUARD;
    Core_Ortho(C, left, right, bottom, top, zNear, zFar);
}

void Hook_GLFrustum(GLcontext context, GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
                    GLdouble zNear, GLdouble zFar)
{
    (void) context; GUARD;
    Core_Frustum(C, left, right, bottom, top, zNear, zFar);
}

void Hook_GLShadeModel(GLcontext context, GLenum mode) { (void) context; GUARD; Core_ShadeModel(C, mode); }
void Hook_GLDepthMask(GLcontext context, GLboolean flag) { (void) context; GUARD; Core_DepthMask(C, flag); }
void Hook_GLDepthFunc(GLcontext context, GLenum func) { (void) context; GUARD; Core_DepthFunc(C, func); }
void Hook_GLDepthRange(GLcontext context, GLclampd n, GLclampd f) { (void) context; GUARD; Core_DepthRange(C, n, f); }
void Hook_GLBlendFunc(GLcontext context, GLenum sfactor, GLenum dfactor) { (void) context; GUARD; Core_BlendFunc(C, sfactor, dfactor); }
void Hook_GLAlphaFunc(GLcontext context, GLenum func, GLclampf ref) { (void) context; GUARD; Core_AlphaFunc(C, func, ref); }
void Hook_GLCullFace(GLcontext context, GLenum mode) { (void) context; GUARD; Core_CullFace(C, mode); }
void Hook_GLFrontFace(GLcontext context, GLenum mode) { (void) context; GUARD; Core_FrontFace(C, mode); }

void Hook_GLViewport(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height)
{
    (void) context; GUARD;
    Core_Viewport(C, x, y, width, height);
}

void Hook_GLScissor(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height)
{
    (void) context; GUARD;
    Core_Scissor(C, x, y, width, height);
}

void Hook_GLColorMask(GLcontext context, GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
    (void) context; GUARD;
    Core_ColorMask(C, red, green, blue, alpha);
}

void Hook_GLPolygonOffset(GLcontext context, GLfloat factor, GLfloat units) { (void) context; GUARD; Core_PolygonOffset(C, factor, units); }
void Hook_GLPolygonMode(GLcontext context, GLenum face, GLenum mode) { (void) context; GUARD; Core_PolygonMode(C, face, mode); }
void Hook_GLPointSize(GLcontext context, GLfloat size) { (void) context; GUARD; Core_PointSize(C, size); }
void Hook_GLHint(GLcontext context, GLenum target, GLenum mode) { (void) context; GUARD; Core_Hint(C, target, mode); }

void Hook_GLClearColor(GLcontext context, GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
    (void) context; GUARD;
    Core_ClearColor(C, red, green, blue, alpha);
}

void Hook_GLClearDepth(GLcontext context, GLclampd depth) { (void) context; GUARD; Core_ClearDepth(C, depth); }
void Hook_GLClear(GLcontext context, GLbitfield mask) { (void) context; GUARD; Core_Clear(C, mask); }

void Hook_GLTexImage2D(GLcontext context, GLenum target, GLint level, GLint internalformat,
                       GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type,
                       const GLvoid *pixels)
{
    (void) context; GUARD;
    Core_TexImage2D(C, target, level, internalformat, width, height, border, format, type, pixels);
}

void Hook_GLTexSubImage2D(GLcontext context, GLenum target, GLint level, GLint xoffset, GLint yoffset,
                          GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
    (void) context; GUARD;
    Core_TexSubImage2D(C, target, level, xoffset, yoffset, width, height, format, type, pixels);
}

void Hook_GLTexParameteri(GLcontext context, GLenum target, GLenum pname, GLint param)
{
    (void) context; GUARD;
    Core_TexParameteri(C, target, pname, param);
}

void Hook_GLBindTexture(GLcontext context, GLenum target, GLuint texture) { (void) context; GUARD; Core_BindTexture(C, target, texture); }

void Hook_GLGenTextures(GLcontext context, GLsizei n, GLuint *textures)
{
    (void) context; GUARD;
    Core_GenTextures(C, n, (ULONG *) textures);
}

void Hook_GLDeleteTextures(GLcontext context, GLsizei n, const GLuint *textures)
{
    (void) context; GUARD;
    Core_DeleteTextures(C, n, (const ULONG *) textures);
}

GLboolean Hook_GLIsTexture(GLcontext context, GLuint texture) { (void) context; return rgl ? (GLboolean) Core_IsTexture(C, texture) : GL_FALSE; }
void Hook_GLGetFloatv(GLcontext context, GLenum pname, GLfloat *params) { (void) context; GUARD; Core_GetFloatv(C, pname, params); }

void Hook_GLGetIntegerv(GLcontext context, GLenum pname, GLint *params)
{
    (void) context; GUARD;
    Core_GetIntegerv(C, pname, (LONG *) params);
}

void Hook_GLGetBooleanv(GLcontext context, GLenum pname, GLboolean *params)
{
    float v[16];
    ULONG i;

    (void) context;
    GUARD;
    for (i = 0; i < 16; i++)
        v[i] = 0.0f;
    Core_GetFloatv(C, pname, v);
    params[0] = (v[0] != 0.0f) ? GL_TRUE : GL_FALSE;
    for (i = 1; i < 16 && (pname == GL_MODELVIEW_MATRIX || pname == GL_PROJECTION_MATRIX
                           || pname == GL_TEXTURE_MATRIX || (i < 4 && (pname == GL_VIEWPORT
                           || pname == GL_COLOR_WRITEMASK || pname == GL_SCISSOR_BOX))); i++)
        params[i] = (v[i] != 0.0f) ? GL_TRUE : GL_FALSE;
}

const GLubyte *Hook_GLGetString(GLcontext context, GLenum name)
{
    (void) context;
    if (rgl == NULL)
        return NULL;
    return (const GLubyte *) Core_GetString(C, name);
}

GLenum Hook_GLGetError(GLcontext context) { (void) context; return rgl ? (GLenum) Core_GetError(C) : GL_NO_ERROR; }
void Hook_GLFlush(GLcontext context) { (void) context; GUARD; R2Ring_Commit(&rgl->ring); }
void Hook_GLFinish(GLcontext context) { (void) context; GUARD; R2Ring_Idle(&rgl->ring); }
