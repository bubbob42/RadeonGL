/*
 * rgldlist.c -- display lists: glNewList, glEndList, glCallList,
 * glGenLists, glDeleteLists, glIsList (minigl.library 29.1's six; 2026-11-08).
 *
 * Recorded as calls, not as vertices. Between glNewList and glEndList the
 * calling task's dispatch table (each task has its own, shell.c) sends the
 * calls GL compiles into a list to the recorders here; glEndList puts the
 * library's own functions back. A program that never makes a list runs
 * exactly the code it ran before. Each recorded call keeps the library
 * function it stands for and its arguments, and glCallList calls them
 * again, so a list does what its calls did: vertices and their attributes,
 * textures bound, matrices, materials and lights, enables, nested lists.
 * (29.1 records the vertices of glBegin/glEnd blocks and runs every other
 * call at once.)
 *
 * Not recorded, as GL says: queries, the list calls themselves (but
 * glCallList), client array state and pointers, pixel storage and
 * readback, texture objects, glFlush/glFinish. Run at once as well, unlike
 * GL (and as in 29.1): glTexImage2D/glTexSubImage2D (no image copies are
 * kept) and the vertex array draws glDrawArrays, glDrawElements and
 * glArrayElement.
 *
 * The vector and double forms are recorded as the float call they amount
 * to (glVertex3fv as glVertex4f, glRotated as glRotatef, glColor4ub as
 * glColor4f), so replay needs few shapes of call. glFrustum, glOrtho,
 * glDepthRange and glClearDepth keep their doubles.
 *
 * Linked last, as rgllight.c and rglcompat.c are (the 68060's caches care
 * where GLQuake's code sits); GLQuake makes no lists.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <libraries/minigl_dispatch.h>
#include <stddef.h>

#include "rgl_dispatch.h"
#include "rgl.h"
#include "rglint.h"
#include "core.h"

#ifndef GL_COLOR_INDEXES
#define GL_COLOR_INDEXES        0x1603
#endif
#ifndef GL_MAX_LIST_NESTING
#define GL_MAX_LIST_NESTING     0x0B31
#endif

#define DL_NESTING  64          /* GL_MAX_LIST_NESTING */

extern const MGLDispatchTable MiniGLDispatchTable;
#define LIB MiniGLDispatchTable /* the library's own functions */

/* --- storage ------------------------------------------------------------------------ */

/* The shapes of call a list replays: U a 32-bit integer (GLenum, GLuint,
 * GLint, GLsizei, GLbitfield), B a GLboolean, F a GLfloat, D a GLdouble, P
 * floats passed by pointer (kept in the list) */
enum
{
    S_V, S_U, S_B, S_UU, S_UB, S_UUU, S_UUUU, S_BBBB,
    S_F, S_FF, S_FFF, S_FFFF, S_UF, S_UFF, S_UUF, S_UP, S_UUP, S_P16,
    S_D, S_DD, S_D6, S_FU, S_FFU, S_CALL, S_QUAD
};

typedef void (*FnV)(GLcontext);
typedef void (*FnU)(GLcontext, GLenum);
typedef void (*FnB)(GLcontext, GLboolean);
typedef void (*FnUU)(GLcontext, GLenum, GLenum);
typedef void (*FnUB)(GLcontext, GLenum, GLboolean);
typedef void (*FnUUU)(GLcontext, GLenum, GLenum, GLenum);
typedef void (*FnUUUU)(GLcontext, GLenum, GLenum, GLenum, GLenum);
typedef void (*FnBBBB)(GLcontext, GLboolean, GLboolean, GLboolean, GLboolean);
typedef void (*FnF)(GLcontext, GLfloat);
typedef void (*FnFF)(GLcontext, GLfloat, GLfloat);
typedef void (*FnFFF)(GLcontext, GLfloat, GLfloat, GLfloat);
typedef void (*FnFFFF)(GLcontext, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (*FnUF)(GLcontext, GLenum, GLfloat);
typedef void (*FnUFF)(GLcontext, GLenum, GLfloat, GLfloat);
typedef void (*FnUUF)(GLcontext, GLenum, GLenum, GLfloat);
typedef void (*FnUP)(GLcontext, GLenum, const GLfloat *);
typedef void (*FnUUP)(GLcontext, GLenum, GLenum, const GLfloat *);
typedef void (*FnP16)(GLcontext, const GLfloat *);
typedef void (*FnD)(GLcontext, GLdouble);
typedef void (*FnDD)(GLcontext, GLdouble, GLdouble);
typedef void (*FnD6)(GLcontext, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble);
typedef void (*FnFU)(GLcontext, GLfloat, GLint);
typedef void (*FnFFU)(GLcontext, GLfloat, GLfloat, GLint);

/* One list: its calls, each [shape << 16 | words] [function] [arguments] */
struct DList
{
    ULONG *op;
    ULONG n, cap;               /* longwords used, allocated */
    ULONG used;                 /* the name is a list (glGenLists or glNewList) */
};

/* A context's lists (struct Rgl dl, made at the first list call) */
struct RglDL
{
    struct DList *list;         /* by name; name 0 is never a list */
    ULONG nlist;
    ULONG compiling;            /* the list being made, 0 when none */
    ULONG mode;                 /* GL_COMPILE or GL_COMPILE_AND_EXECUTE */
    MGLDispatchTable *table;    /* the task table whose entries point here */
    ULONG depth;                /* glCallList nesting while replaying */
    ULONG full;                 /* a call that found no memory: GL_OUT_OF_MEMORY at glEndList */
};

union FBits
{
    float f;
    ULONG u;
};

union DBits
{
    double d;
    ULONG u[2];
};

static ULONG UF(float f)
{
    union FBits b;

    b.f = f;
    return b.u;
}

static float FU(ULONG u)
{
    union FBits b;

    b.u = u;
    return b.f;
}

static struct RglDL *State(GLcontext x, ULONG make)
{
    struct Rgl *r;

    r = (struct Rgl *) x;
    if (r->dl == NULL && make)
        r->dl = (struct RglDL *) AllocVec(sizeof(struct RglDL), MEMF_ANY | MEMF_CLEAR);
    return r->dl;
}

/* Names below need valid: the table grown to cover them */
static BOOL Grow(struct RglDL *dl, ULONG need)
{
    struct DList *p;
    ULONG n;

    if (need <= dl->nlist)
        return TRUE;
    n = dl->nlist ? dl->nlist : 64;
    while (n < need)
        n *= 2;
    p = (struct DList *) AllocVec(n * sizeof(struct DList), MEMF_ANY | MEMF_CLEAR);
    if (p == NULL)
        return FALSE;
    if (dl->list != NULL)
    {
        CopyMem(dl->list, p, dl->nlist * sizeof(struct DList));
        FreeVec(dl->list);
    }
    dl->list = p;
    dl->nlist = n;
    return TRUE;
}

static void Empty(struct DList *L)
{
    if (L->op != NULL)
        FreeVec(L->op);
    L->op = NULL;
    L->n = L->cap = 0;
}

/* Room for a call of nargs argument words in the list being made: its
 * argument words, or NULL (out of memory: noted, the call is lost) */
static ULONG *Put(struct RglDL *dl, ULONG shape, APTR fn, ULONG nargs)
{
    struct DList *L;
    ULONG *p;
    ULONG cap;

    L = &dl->list[dl->compiling];
    if (L->n + nargs + 2 > L->cap)
    {
        cap = L->cap ? L->cap * 2 : 256;
        while (cap < L->n + nargs + 2)
            cap *= 2;
        p = (ULONG *) AllocVec(cap * 4, MEMF_ANY);
        if (p == NULL)
        {
            dl->full = 1;
            return NULL;
        }
        if (L->op != NULL)
        {
            CopyMem(L->op, p, L->n * 4);
            FreeVec(L->op);
        }
        L->op = p;
        L->cap = cap;
    }
    p = L->op + L->n;
    p[0] = (shape << 16) | (nargs + 2);
    p[1] = (ULONG) fn;
    L->n += nargs + 2;
    return p + 2;
}

/* How many floats a ...fv call reads for pname (GL's tables) */
static ULONG Count(GLenum pname)
{
    switch (pname)
    {
        case GL_AMBIENT: case GL_DIFFUSE: case GL_SPECULAR: case GL_POSITION:
        case GL_EMISSION: case GL_AMBIENT_AND_DIFFUSE: case GL_FOG_COLOR:
        case GL_LIGHT_MODEL_AMBIENT: case GL_OBJECT_PLANE: case GL_EYE_PLANE:
        case GL_TEXTURE_ENV_COLOR:
            return 4;
        case GL_SPOT_DIRECTION: case GL_COLOR_INDEXES:
            return 3;
    }
    return 1;
}

/* --- recording: one function a shape ------------------------------------------------------
 * Each returns TRUE when the call is to run now as well (GL_COMPILE_AND_EXECUTE). */

static BOOL Rec(GLcontext x, ULONG shape, APTR fn, ULONG n, const ULONG *a)
{
    struct RglDL *dl;
    ULONG *p;
    ULONG i;

    if (x == NULL || (dl = State(x, 0)) == NULL || dl->compiling == 0)
        return x != NULL;           /* not making a list (another context): just run it */
    p = Put(dl, shape, fn, n);
    if (p != NULL)
        for (i = 0; i < n; i++)
            p[i] = a[i];
    return dl->mode == GL_COMPILE_AND_EXECUTE;
}

static BOOL R0(GLcontext x, ULONG shape, APTR fn)
{
    return Rec(x, shape, fn, 0, NULL);
}

static BOOL R1(GLcontext x, ULONG shape, APTR fn, ULONG a0)
{
    ULONG a[1];

    a[0] = a0;
    return Rec(x, shape, fn, 1, a);
}

static BOOL R2(GLcontext x, ULONG shape, APTR fn, ULONG a0, ULONG a1)
{
    ULONG a[2];

    a[0] = a0;
    a[1] = a1;
    return Rec(x, shape, fn, 2, a);
}

static BOOL R3(GLcontext x, ULONG shape, APTR fn, ULONG a0, ULONG a1, ULONG a2)
{
    ULONG a[3];

    a[0] = a0;
    a[1] = a1;
    a[2] = a2;
    return Rec(x, shape, fn, 3, a);
}

static BOOL R4(GLcontext x, ULONG shape, APTR fn, ULONG a0, ULONG a1, ULONG a2, ULONG a3)
{
    ULONG a[4];

    a[0] = a0;
    a[1] = a1;
    a[2] = a2;
    a[3] = a3;
    return Rec(x, shape, fn, 4, a);
}

/* ...fv with pname: [enum(s)] [count] [count floats] */
static BOOL RVec(GLcontext x, ULONG shape, APTR fn, ULONG e0, ULONG e1, ULONG ne, const GLfloat *v,
                 ULONG nv)
{
    ULONG a[2 + 1 + 16];
    ULONG i, k;

    k = 0;
    a[k++] = e0;
    if (ne == 2)
        a[k++] = e1;
    a[k++] = nv;
    for (i = 0; i < nv; i++)
        a[k++] = v ? UF(v[i]) : 0;
    return Rec(x, shape, fn, k, a);
}

static BOOL RDoubles(GLcontext x, ULONG shape, APTR fn, const GLdouble *d, ULONG nd)
{
    ULONG a[12];
    union DBits b;
    ULONG i;

    for (i = 0; i < nd; i++)
    {
        b.d = d[i];
        a[2 * i] = b.u[0];
        a[2 * i + 1] = b.u[1];
    }
    return Rec(x, shape, fn, 2 * nd, a);
}

/* --- the recorders, by the call they stand in for ------------------------------------- */

#define FN(name) ((APTR) LIB.name)

static void Rec_GLBegin(GLcontext x, GLenum m)          { if (R1(x, S_U, FN(GLBegin), m)) LIB.GLBegin(x, m); }
static void Rec_GLEnd(GLcontext x)                      { if (R0(x, S_V, FN(GLEnd))) LIB.GLEnd(x); }
static void Rec_GLLoadIdentity(GLcontext x)             { if (R0(x, S_V, FN(GLLoadIdentity))) LIB.GLLoadIdentity(x); }
static void Rec_GLPushMatrix(GLcontext x)               { if (R0(x, S_V, FN(GLPushMatrix))) LIB.GLPushMatrix(x); }
static void Rec_GLPopMatrix(GLcontext x)                { if (R0(x, S_V, FN(GLPopMatrix))) LIB.GLPopMatrix(x); }
static void Rec_GLMatrixMode(GLcontext x, GLenum m)     { if (R1(x, S_U, FN(GLMatrixMode), m)) LIB.GLMatrixMode(x, m); }
static void Rec_GLShadeModel(GLcontext x, GLenum m)     { if (R1(x, S_U, FN(GLShadeModel), m)) LIB.GLShadeModel(x, m); }
static void Rec_GLCullFace(GLcontext x, GLenum m)       { if (R1(x, S_U, FN(GLCullFace), m)) LIB.GLCullFace(x, m); }
static void Rec_GLFrontFace(GLcontext x, GLenum m)      { if (R1(x, S_U, FN(GLFrontFace), m)) LIB.GLFrontFace(x, m); }
static void Rec_GLDepthFunc(GLcontext x, GLenum f)      { if (R1(x, S_U, FN(GLDepthFunc), f)) LIB.GLDepthFunc(x, f); }
static void Rec_GLActiveTextureARB(GLcontext x, GLenum u) { if (R1(x, S_U, FN(GLActiveTextureARB), u)) LIB.GLActiveTextureARB(x, u); }
static void Rec_GLBlendEquation(GLcontext x, GLenum m)  { if (R1(x, S_U, FN(GLBlendEquation), m)) LIB.GLBlendEquation(x, m); }
static void Rec_GLClear(GLcontext x, GLbitfield m)      { if (R1(x, S_U, FN(GLClear), m)) LIB.GLClear(x, m); }
static void Rec_GLDepthMask(GLcontext x, GLboolean f)   { if (R1(x, S_B, FN(GLDepthMask), f)) LIB.GLDepthMask(x, f); }
static void Rec_GLBlendFunc(GLcontext x, GLenum s, GLenum d) { if (R2(x, S_UU, FN(GLBlendFunc), s, d)) LIB.GLBlendFunc(x, s, d); }
static void Rec_GLPolygonMode(GLcontext x, GLenum f, GLenum m) { if (R2(x, S_UU, FN(GLPolygonMode), f, m)) LIB.GLPolygonMode(x, f, m); }
static void Rec_GLColorMaterial(GLcontext x, GLenum f, GLenum m) { if (R2(x, S_UU, FN(GLColorMaterial), f, m)) LIB.GLColorMaterial(x, f, m); }
static void Rec_GLHint(GLcontext x, GLenum t, GLenum m) { if (R2(x, S_UU, FN(GLHint), t, m)) LIB.GLHint(x, t, m); }
static void Rec_GLBindTexture(GLcontext x, GLenum t, GLuint n) { if (R2(x, S_UU, FN(GLBindTexture), t, n)) LIB.GLBindTexture(x, t, n); }
static void Rec_MGLSetState(GLcontext x, GLenum c, GLboolean s) { if (R2(x, S_UB, FN(MGLSetState), c, s)) LIB.MGLSetState(x, c, s); }
static void Rec_GLTexGeni(GLcontext x, GLenum c, GLenum m, GLenum p) { if (R3(x, S_UUU, FN(GLTexGeni), c, m, p)) LIB.GLTexGeni(x, c, m, p); }
static void Rec_GLTexEnvi(GLcontext x, GLenum t, GLenum p, GLint v) { if (R3(x, S_UUU, FN(GLTexEnvi), t, p, (ULONG) v)) LIB.GLTexEnvi(x, t, p, v); }
static void Rec_GLTexParameteri(GLcontext x, GLenum t, GLenum p, GLint v) { if (R3(x, S_UUU, FN(GLTexParameteri), t, p, (ULONG) v)) LIB.GLTexParameteri(x, t, p, v); }
static void Rec_GLBlendFuncSeparate(GLcontext x, GLenum a, GLenum b, GLenum c, GLenum d) { if (R4(x, S_UUUU, FN(GLBlendFuncSeparate), a, b, c, d)) LIB.GLBlendFuncSeparate(x, a, b, c, d); }
static void Rec_GLScissor(GLcontext x, GLint a, GLint b, GLsizei w, GLsizei h) { if (R4(x, S_UUUU, FN(GLScissor), (ULONG) a, (ULONG) b, (ULONG) w, (ULONG) h)) LIB.GLScissor(x, a, b, w, h); }
static void Rec_GLViewport(GLcontext x, GLint a, GLint b, GLsizei w, GLsizei h) { if (R4(x, S_UUUU, FN(GLViewport), (ULONG) a, (ULONG) b, (ULONG) w, (ULONG) h)) LIB.GLViewport(x, a, b, w, h); }
static void Rec_GLColorMask(GLcontext x, GLboolean r, GLboolean g, GLboolean b, GLboolean a) { if (R4(x, S_BBBB, FN(GLColorMask), r, g, b, a)) LIB.GLColorMask(x, r, g, b, a); }
static void Rec_GLPointSize(GLcontext x, GLfloat s)     { if (R1(x, S_F, FN(GLPointSize), UF(s))) LIB.GLPointSize(x, s); }
static void Rec_GLLineWidth(GLcontext x, GLfloat w)     { if (R1(x, S_F, FN(GLLineWidth), UF(w))) LIB.GLLineWidth(x, w); }
static void Rec_GLPolygonOffset(GLcontext x, GLfloat f, GLfloat u) { if (R2(x, S_FF, FN(GLPolygonOffset), UF(f), UF(u))) LIB.GLPolygonOffset(x, f, u); }
static void Rec_GLTexCoord2f(GLcontext x, GLfloat s, GLfloat t) { if (R2(x, S_FF, FN(GLTexCoord2f), UF(s), UF(t))) LIB.GLTexCoord2f(x, s, t); }
static void Rec_GLTexCoord2fv(GLcontext x, GLfloat *v)  { Rec_GLTexCoord2f(x, v[0], v[1]); }
static void Rec_GLNormal3f(GLcontext x, GLfloat a, GLfloat b, GLfloat c) { if (R3(x, S_FFF, FN(GLNormal3f), UF(a), UF(b), UF(c))) LIB.GLNormal3f(x, a, b, c); }
static void Rec_GLNormal3fv(GLcontext x, GLfloat *n)    { Rec_GLNormal3f(x, n[0], n[1], n[2]); }
static void Rec_GLScalef(GLcontext x, GLfloat a, GLfloat b, GLfloat c) { if (R3(x, S_FFF, FN(GLScalef), UF(a), UF(b), UF(c))) LIB.GLScalef(x, a, b, c); }
static void Rec_GLScaled(GLcontext x, GLdouble a, GLdouble b, GLdouble c) { Rec_GLScalef(x, (GLfloat) a, (GLfloat) b, (GLfloat) c); }
static void Rec_GLTranslatef(GLcontext x, GLfloat a, GLfloat b, GLfloat c) { if (R3(x, S_FFF, FN(GLTranslatef), UF(a), UF(b), UF(c))) LIB.GLTranslatef(x, a, b, c); }
static void Rec_GLTranslated(GLcontext x, GLdouble a, GLdouble b, GLdouble c) { Rec_GLTranslatef(x, (GLfloat) a, (GLfloat) b, (GLfloat) c); }
static void Rec_GLVertex4f(GLcontext x, GLfloat a, GLfloat b, GLfloat c, GLfloat d) { if (R4(x, S_FFFF, FN(GLVertex4f), UF(a), UF(b), UF(c), UF(d))) LIB.GLVertex4f(x, a, b, c, d); }
static void Rec_GLVertex2fv(GLcontext x, GLfloat *v)    { Rec_GLVertex4f(x, v[0], v[1], 0.0f, 1.0f); }
static void Rec_GLVertex3fv(GLcontext x, GLfloat *v)    { Rec_GLVertex4f(x, v[0], v[1], v[2], 1.0f); }
static void Rec_GLVertex4fv(GLcontext x, GLfloat *v)    { Rec_GLVertex4f(x, v[0], v[1], v[2], v[3]); }
static void Rec_GLColor4f(GLcontext x, GLfloat r, GLfloat g, GLfloat b, GLfloat a) { if (R4(x, S_FFFF, FN(GLColor4f), UF(r), UF(g), UF(b), UF(a))) LIB.GLColor4f(x, r, g, b, a); }
static void Rec_GLColor3fv(GLcontext x, GLfloat *v)     { Rec_GLColor4f(x, v[0], v[1], v[2], 1.0f); }
static void Rec_GLColor4fv(GLcontext x, GLfloat *v)     { Rec_GLColor4f(x, v[0], v[1], v[2], v[3]); }
static void Rec_GLColor4ub(GLcontext x, GLubyte r, GLubyte g, GLubyte b, GLubyte a)
{
    Rec_GLColor4f(x, r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}
static void Rec_GLColor3ubv(GLcontext x, GLubyte *v)    { Rec_GLColor4ub(x, v[0], v[1], v[2], 255); }
static void Rec_GLColor4ubv(GLcontext x, GLubyte *v)    { Rec_GLColor4ub(x, v[0], v[1], v[2], v[3]); }
static void Rec_GLTexCoord4f(GLcontext x, GLfloat s, GLfloat t, GLfloat r, GLfloat q) { if (R4(x, S_FFFF, FN(GLTexCoord4f), UF(s), UF(t), UF(r), UF(q))) LIB.GLTexCoord4f(x, s, t, r, q); }
static void Rec_GLTexCoord4fv(GLcontext x, GLfloat *v)  { Rec_GLTexCoord4f(x, v[0], v[1], v[2], v[3]); }
static void Rec_GLRotatef(GLcontext x, GLfloat a, GLfloat b, GLfloat c, GLfloat d) { if (R4(x, S_FFFF, FN(GLRotatef), UF(a), UF(b), UF(c), UF(d))) LIB.GLRotatef(x, a, b, c, d); }
static void Rec_GLRotated(GLcontext x, GLdouble a, GLdouble b, GLdouble c, GLdouble d)
{
    Rec_GLRotatef(x, (GLfloat) a, (GLfloat) b, (GLfloat) c, (GLfloat) d);
}
static void Rec_GLClearColor(GLcontext x, GLclampf r, GLclampf g, GLclampf b, GLclampf a) { if (R4(x, S_FFFF, FN(GLClearColor), UF(r), UF(g), UF(b), UF(a))) LIB.GLClearColor(x, r, g, b, a); }
static void Rec_GLAlphaFunc(GLcontext x, GLenum f, GLclampf r) { if (R2(x, S_UF, FN(GLAlphaFunc), f, UF(r))) LIB.GLAlphaFunc(x, f, r); }
static void Rec_GLFogf(GLcontext x, GLenum p, GLfloat v) { if (R2(x, S_UF, FN(GLFogf), p, UF(v))) LIB.GLFogf(x, p, v); }
static void Rec_GLLightModelf(GLcontext x, GLenum p, GLfloat v) { if (R2(x, S_UF, FN(GLLightModelf), p, UF(v))) LIB.GLLightModelf(x, p, v); }
static void Rec_GLMultiTexCoord2fARB(GLcontext x, GLenum u, GLfloat s, GLfloat t) { if (R3(x, S_UFF, FN(GLMultiTexCoord2fARB), u, UF(s), UF(t))) LIB.GLMultiTexCoord2fARB(x, u, s, t); }
static void Rec_GLMultiTexCoord2fvARB(GLcontext x, GLenum u, GLfloat *v) { Rec_GLMultiTexCoord2fARB(x, u, v[0], v[1]); }
static void Rec_GLLightf(GLcontext x, GLenum l, GLenum p, GLfloat v) { if (R3(x, S_UUF, FN(GLLightf), l, p, UF(v))) LIB.GLLightf(x, l, p, v); }
static void Rec_GLMaterialf(GLcontext x, GLenum f, GLenum p, GLfloat v) { if (R3(x, S_UUF, FN(GLMaterialf), f, p, UF(v))) LIB.GLMaterialf(x, f, p, v); }
static void Rec_GLFogfv(GLcontext x, GLenum p, GLfloat *v) { if (RVec(x, S_UP, FN(GLFogfv), p, 0, 1, v, Count(p))) LIB.GLFogfv(x, p, v); }
static void Rec_GLLightModelfv(GLcontext x, GLenum p, const GLfloat *v) { if (RVec(x, S_UP, FN(GLLightModelfv), p, 0, 1, v, Count(p))) LIB.GLLightModelfv(x, p, v); }
static void Rec_GLLightfv(GLcontext x, GLenum l, GLenum p, const GLfloat *v) { if (RVec(x, S_UUP, FN(GLLightfv), l, p, 2, v, Count(p))) LIB.GLLightfv(x, l, p, v); }
static void Rec_GLMaterialfv(GLcontext x, GLenum f, GLenum p, const GLfloat *v) { if (RVec(x, S_UUP, FN(GLMaterialfv), f, p, 2, v, Count(p))) LIB.GLMaterialfv(x, f, p, v); }
static void Rec_GLTexGenfv(GLcontext x, GLenum c, GLenum p, const GLfloat *v) { if (RVec(x, S_UUP, FN(GLTexGenfv), c, p, 2, v, Count(p))) LIB.GLTexGenfv(x, c, p, v); }
static void Rec_GLTexEnvfv(GLcontext x, GLenum t, GLenum p, const GLfloat *v) { if (RVec(x, S_UUP, FN(GLTexEnvfv), t, p, 2, v, Count(p))) LIB.GLTexEnvfv(x, t, p, v); }

static BOOL RMatrix(GLcontext x, APTR fn, const GLfloat *m)
{
    ULONG a[16];
    ULONG i;

    for (i = 0; i < 16; i++)
        a[i] = UF(m[i]);
    return Rec(x, S_P16, fn, 16, a);
}

static void Rec_GLLoadMatrixf(GLcontext x, const GLfloat *m) { if (RMatrix(x, FN(GLLoadMatrixf), m)) LIB.GLLoadMatrixf(x, m); }
static void Rec_GLMultMatrixf(GLcontext x, const GLfloat *m) { if (RMatrix(x, FN(GLMultMatrixf), m)) LIB.GLMultMatrixf(x, m); }

static void Rec_GLLoadMatrixd(GLcontext x, const GLdouble *m)
{
    GLfloat f[16];
    ULONG i;

    for (i = 0; i < 16; i++)
        f[i] = (GLfloat) m[i];
    Rec_GLLoadMatrixf(x, f);
}

static void Rec_GLMultMatrixd(GLcontext x, const GLdouble *m)
{
    GLfloat f[16];
    ULONG i;

    for (i = 0; i < 16; i++)
        f[i] = (GLfloat) m[i];
    Rec_GLMultMatrixf(x, f);
}

static void Rec_GLClearDepth(GLcontext x, GLclampd d)
{
    if (RDoubles(x, S_D, FN(GLClearDepth), &d, 1))
        LIB.GLClearDepth(x, d);
}

static void Rec_GLDepthRange(GLcontext x, GLclampd n, GLclampd f)
{
    GLdouble d[2];

    d[0] = n;
    d[1] = f;
    if (RDoubles(x, S_DD, FN(GLDepthRange), d, 2))
        LIB.GLDepthRange(x, n, f);
}

static void Rec_GLFrustum(GLcontext x, GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n,
                          GLdouble f)
{
    GLdouble d[6];

    d[0] = l; d[1] = r; d[2] = b; d[3] = t; d[4] = n; d[5] = f;
    if (RDoubles(x, S_D6, FN(GLFrustum), d, 6))
        LIB.GLFrustum(x, l, r, b, t, n, f);
}

static void Rec_GLOrtho(GLcontext x, GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n,
                        GLdouble f)
{
    GLdouble d[6];

    d[0] = l; d[1] = r; d[2] = b; d[3] = t; d[4] = n; d[5] = f;
    if (RDoubles(x, S_D6, FN(GLOrtho), d, 6))
        LIB.GLOrtho(x, l, r, b, t, n, f);
}

static void Rec_GLRotatefEXT(GLcontext x, GLfloat a, const GLint xyz)
{
    if (R2(x, S_FU, FN(GLRotatefEXT), UF(a), (ULONG) xyz))
        LIB.GLRotatefEXT(x, a, xyz);
}

static void Rec_GLRotatefEXTs(GLcontext x, GLfloat s, GLfloat c, const GLint xyz)
{
    if (R3(x, S_FFU, FN(GLRotatefEXTs), UF(s), UF(c), (ULONG) xyz))
        LIB.GLRotatefEXTs(x, s, c, xyz);
}

/* gluSphere, gluCylinder, gluDisk: no context argument (the task's); the
 * quadric's settings are kept, not the quadric (it may be gone at the call) */
enum { Q_SPHERE, Q_CYLINDER, Q_DISK };

static BOOL RQuad(GLUquadricObj *q, ULONG kind, float a, float b, float c, LONG s1, LONG s2)
{
    ULONG v[10];

    if (q == NULL)
        return FALSE;
    v[0] = kind;
    v[1] = q->q.normals;
    v[2] = q->q.texture;
    v[3] = q->q.style;
    v[4] = q->q.orientation;
    v[5] = UF(a);
    v[6] = UF(b);
    v[7] = UF(c);
    v[8] = (ULONG) s1;
    v[9] = (ULONG) s2;
    return Rec((GLcontext) Rglc_TaskCore(), S_QUAD, NULL, 10, v);
}

static void Rec_GLUSphere(GLUquadricObj *q, GLdouble r, GLint sl, GLint st)
{
    if (RQuad(q, Q_SPHERE, (float) r, 0.0f, 0.0f, sl, st))
        LIB.GLUSphere(q, r, sl, st);
}

static void Rec_GLUCylinder(GLUquadricObj *q, GLdouble b, GLdouble t, GLdouble h, GLint sl, GLint st)
{
    if (RQuad(q, Q_CYLINDER, (float) b, (float) t, (float) h, sl, st))
        LIB.GLUCylinder(q, b, t, h, sl, st);
}

static void Rec_GLUDisk(GLUquadricObj *q, GLdouble in, GLdouble out, GLint sl, GLint lo)
{
    if (RQuad(q, Q_DISK, (float) in, (float) out, 0.0f, sl, lo))
        LIB.GLUDisk(q, in, out, sl, lo);
}

static void Run(GLcontext x, struct RglDL *dl, ULONG name);

static void Rec_GLCallList(GLcontext x, GLuint list)
{
    struct RglDL *dl;

    if (R1(x, S_CALL, NULL, list) && (dl = State(x, 0)) != NULL)
        Run(x, dl, list);
}

/* The entries a list records, and their recorders */
static const struct
{
    UWORD off;
    APTR rec;
} swap[] =
{
#define SW(name) { offsetof(MGLDispatchTable, name), (APTR) Rec_##name }
    SW(GLBegin), SW(GLEnd), SW(GLLoadIdentity), SW(GLPushMatrix), SW(GLPopMatrix),
    SW(GLMatrixMode), SW(GLShadeModel), SW(GLCullFace), SW(GLFrontFace), SW(GLDepthFunc),
    SW(GLActiveTextureARB), SW(GLBlendEquation), SW(GLClear), SW(GLDepthMask), SW(GLBlendFunc),
    SW(GLPolygonMode), SW(GLColorMaterial), SW(GLHint), SW(GLBindTexture), SW(MGLSetState),
    SW(GLTexGeni), SW(GLTexEnvi), SW(GLTexParameteri), SW(GLBlendFuncSeparate), SW(GLScissor),
    SW(GLViewport), SW(GLColorMask), SW(GLPointSize), SW(GLLineWidth), SW(GLPolygonOffset),
    SW(GLTexCoord2f), SW(GLTexCoord2fv), SW(GLNormal3f), SW(GLNormal3fv), SW(GLScalef),
    SW(GLScaled), SW(GLTranslatef), SW(GLTranslated), SW(GLVertex4f), SW(GLVertex2fv),
    SW(GLVertex3fv), SW(GLVertex4fv), SW(GLColor4f), SW(GLColor3fv), SW(GLColor4fv),
    SW(GLColor4ub), SW(GLColor3ubv), SW(GLColor4ubv), SW(GLTexCoord4f), SW(GLTexCoord4fv),
    SW(GLRotatef), SW(GLRotated), SW(GLClearColor), SW(GLAlphaFunc), SW(GLFogf),
    SW(GLLightModelf), SW(GLMultiTexCoord2fARB), SW(GLMultiTexCoord2fvARB), SW(GLLightf),
    SW(GLMaterialf), SW(GLFogfv), SW(GLLightModelfv), SW(GLLightfv), SW(GLMaterialfv),
    SW(GLTexGenfv), SW(GLTexEnvfv), SW(GLLoadMatrixf), SW(GLMultMatrixf), SW(GLLoadMatrixd),
    SW(GLMultMatrixd), SW(GLClearDepth), SW(GLDepthRange), SW(GLFrustum), SW(GLOrtho),
    SW(GLRotatefEXT), SW(GLRotatefEXTs), SW(GLCallList), SW(GLUSphere), SW(GLUCylinder), SW(GLUDisk)
#undef SW
};

static void Swap(MGLDispatchTable *t, BOOL in)
{
    ULONG i;
    APTR *e;

    for (i = 0; i < sizeof(swap) / sizeof(swap[0]); i++)
    {
        e = (APTR *) ((UBYTE *) t + swap[i].off);
        *e = in ? swap[i].rec : *(APTR *) ((UBYTE *) &LIB + swap[i].off);
    }
}

/* --- replay ---------------------------------------------------------------------------- */

static void Run(GLcontext x, struct RglDL *dl, ULONG name)
{
    struct DList *L;
    const ULONG *p;
    const ULONG *a;
    ULONG n, at;
    APTR fn;
    GLfloat v[16];
    union DBits d[6];
    struct CoreQuadric q;
    ULONG i;

    if (name == 0 || name >= dl->nlist || !dl->list[name].used || dl->depth >= DL_NESTING)
        return;
    dl->depth++;
    /* by offset: a list replayed while it is being made (glCallList of
     * itself under GL_COMPILE_AND_EXECUTE) may grow, and move */
    for (at = 0; at < dl->list[name].n; at += n)
    {
        L = &dl->list[name];
        p = L->op + at;
        n = p[0] & 0xffff;
        fn = (APTR) p[1];
        a = p + 2;
        switch (p[0] >> 16)
        {
            case S_V:    ((FnV) fn)(x); break;
            case S_U:    ((FnU) fn)(x, a[0]); break;
            case S_B:    ((FnB) fn)(x, (GLboolean) a[0]); break;
            case S_UU:   ((FnUU) fn)(x, a[0], a[1]); break;
            case S_UB:   ((FnUB) fn)(x, a[0], (GLboolean) a[1]); break;
            case S_UUU:  ((FnUUU) fn)(x, a[0], a[1], a[2]); break;
            case S_UUUU: ((FnUUUU) fn)(x, a[0], a[1], a[2], a[3]); break;
            case S_BBBB: ((FnBBBB) fn)(x, (GLboolean) a[0], (GLboolean) a[1], (GLboolean) a[2],
                                       (GLboolean) a[3]); break;
            case S_F:    ((FnF) fn)(x, FU(a[0])); break;
            case S_FF:   ((FnFF) fn)(x, FU(a[0]), FU(a[1])); break;
            case S_FFF:  ((FnFFF) fn)(x, FU(a[0]), FU(a[1]), FU(a[2])); break;
            case S_FFFF: ((FnFFFF) fn)(x, FU(a[0]), FU(a[1]), FU(a[2]), FU(a[3])); break;
            case S_UF:   ((FnUF) fn)(x, a[0], FU(a[1])); break;
            case S_UFF:  ((FnUFF) fn)(x, a[0], FU(a[1]), FU(a[2])); break;
            case S_UUF:  ((FnUUF) fn)(x, a[0], a[1], FU(a[2])); break;
            case S_UP:
                for (i = 0; i < a[1] && i < 16; i++)
                    v[i] = FU(a[2 + i]);
                ((FnUP) fn)(x, a[0], v);
                break;
            case S_UUP:
                for (i = 0; i < a[2] && i < 16; i++)
                    v[i] = FU(a[3 + i]);
                ((FnUUP) fn)(x, a[0], a[1], v);
                break;
            case S_P16:
                for (i = 0; i < 16; i++)
                    v[i] = FU(a[i]);
                ((FnP16) fn)(x, v);
                break;
            case S_D:
            case S_DD:
            case S_D6:
                for (i = 0; i < 6 && 2 * i + 1 < n - 2; i++)
                {
                    d[i].u[0] = a[2 * i];
                    d[i].u[1] = a[2 * i + 1];
                }
                if ((p[0] >> 16) == S_D)
                    ((FnD) fn)(x, d[0].d);
                else if ((p[0] >> 16) == S_DD)
                    ((FnDD) fn)(x, d[0].d, d[1].d);
                else
                    ((FnD6) fn)(x, d[0].d, d[1].d, d[2].d, d[3].d, d[4].d, d[5].d);
                break;
            case S_FU:   ((FnFU) fn)(x, FU(a[0]), (GLint) a[1]); break;
            case S_FFU:  ((FnFFU) fn)(x, FU(a[0]), FU(a[1]), (GLint) a[2]); break;
            case S_CALL: Run(x, dl, a[0]); break;
            case S_QUAD:
                q.normals = a[1];
                q.texture = a[2];
                q.style = a[3];
                q.orientation = a[4];
                if (a[0] == Q_SPHERE)
                    Core_Sphere((struct Core *) x, &q, FU(a[5]), (LONG) a[8], (LONG) a[9]);
                else if (a[0] == Q_CYLINDER)
                    Core_Cylinder((struct Core *) x, &q, FU(a[5]), FU(a[6]), FU(a[7]), (LONG) a[8],
                                  (LONG) a[9]);
                else
                    Core_Disk((struct Core *) x, &q, FU(a[5]), FU(a[6]), (LONG) a[8], (LONG) a[9]);
                break;
        }
        if (dl->list == NULL || !dl->list[name].used)
            break;                  /* (cannot happen: no recorded call deletes a list) */
    }
    dl->depth--;
}

/* --- the six calls ---------------------------------------------------------------------- */

GLuint Hook_GLGenLists(GLcontext context, GLsizei range)
{
    struct RglDL *dl;
    ULONG first, i;

    if (context == NULL)
        return 0;
    if (range < 0)
    {
        Core_Error((struct Core *) context, GL_INVALID_VALUE);
        return 0;
    }
    if (range == 0 || (dl = State(context, 1)) == NULL)
        return 0;
    /* range CONSECUTIVE free names, the first returned (a program may
     * address base + 1 ... itself) */
    for (first = 1; ; first += i + 1)
    {
        if (!Grow(dl, first + (ULONG) range))
        {
            Core_Error((struct Core *) context, GL_OUT_OF_MEMORY);
            return 0;
        }
        for (i = 0; i < (ULONG) range && !dl->list[first + i].used; i++)
            ;
        if (i == (ULONG) range)
            break;
    }
    for (i = 0; i < (ULONG) range; i++)
        dl->list[first + i].used = 1;
    return first;
}

void Hook_GLDeleteLists(GLcontext context, GLuint list, GLsizei range)
{
    struct RglDL *dl;
    ULONG i, n;

    if (context == NULL)
        return;
    if (range < 0)
    {
        Core_Error((struct Core *) context, GL_INVALID_VALUE);
        return;
    }
    if ((dl = State(context, 0)) == NULL)
        return;
    /* names that are no lists are passed over, as GL says */
    for (i = 0; i < (ULONG) range; i++)
    {
        n = list + i;
        if (n == 0 || n >= dl->nlist || !dl->list[n].used || n == dl->compiling)
            continue;               /* the list being made: kept until glEndList */
        Empty(&dl->list[n]);
        dl->list[n].used = 0;
    }
}

GLboolean Hook_GLIsList(GLcontext context, GLuint list)
{
    struct RglDL *dl;

    if (context == NULL || (dl = State(context, 0)) == NULL)
        return GL_FALSE;
    return (list != 0 && list < dl->nlist && dl->list[list].used) ? GL_TRUE : GL_FALSE;
}

void Hook_GLNewList(GLcontext context, GLuint list, GLenum mode)
{
    struct RglDL *dl;
    MGLDispatchTable *t;

    if (context == NULL)
        return;
    if (mode != GL_COMPILE && mode != GL_COMPILE_AND_EXECUTE)
    {
        Core_Error((struct Core *) context, GL_INVALID_ENUM);
        return;
    }
    if (list == 0)
    {
        Core_Error((struct Core *) context, GL_INVALID_VALUE);
        return;
    }
    dl = State(context, 1);
    if (dl != NULL && dl->compiling != 0)
    {
        Core_Error((struct Core *) context, GL_INVALID_OPERATION);
        return;
    }
    /* the calling task's own table (a task on the shared one cannot record:
     * its entries are every other task's too) */
    t = Shell_TaskTable();
    if (dl == NULL || t == NULL || !Grow(dl, list + 1))
    {
        Core_Error((struct Core *) context, GL_OUT_OF_MEMORY);
        return;
    }
    Empty(&dl->list[list]);         /* a name made again is replaced */
    dl->list[list].used = 1;
    dl->compiling = list;
    dl->mode = mode;
    dl->full = 0;
    dl->table = t;
    Swap(t, TRUE);
}

void Hook_GLEndList(GLcontext context)
{
    struct RglDL *dl;

    if (context == NULL)
        return;
    if ((dl = State(context, 0)) == NULL || dl->compiling == 0)
    {
        Core_Error((struct Core *) context, GL_INVALID_OPERATION);
        return;
    }
    Swap(dl->table, FALSE);
    dl->table = NULL;
    dl->compiling = 0;
    if (dl->full)
        Core_Error((struct Core *) context, GL_OUT_OF_MEMORY);   /* calls were lost */
}

void Hook_GLCallList(GLcontext context, GLuint list)
{
    struct RglDL *dl;

    if (context == NULL || (dl = State(context, 0)) == NULL)
        return;                     /* no lists at all: an undefined name, ignored */
    Run(context, dl, list);
}

/* glGet's list names (rglcompat.c asks first): TRUE when pname is one */
BOOL Rgldl_Get(struct Rgl *r, ULONG pname, LONG *out)
{
    switch (pname)
    {
        case GL_LIST_INDEX:         *out = (r->dl != NULL) ? (LONG) r->dl->compiling : 0; return TRUE;
        case GL_LIST_MODE:          *out = (r->dl != NULL && r->dl->compiling) ? (LONG) r->dl->mode : 0;
                                    return TRUE;
        case GL_MAX_LIST_NESTING:   *out = DL_NESTING; return TRUE;
        case GL_LIST_BASE:          *out = 0; return TRUE;
    }
    return FALSE;
}

/* The context goes (rgl.c FreeRgl): its lists, and the task's table back
 * if a list was being made */
void Rgldl_Free(struct Rgl *r)
{
    struct RglDL *dl;
    ULONG i;

    if ((dl = r->dl) == NULL)
        return;
    if (dl->compiling != 0 && dl->table != NULL)
        Swap(dl->table, FALSE);
    for (i = 0; i < dl->nlist; i++)
        Empty(&dl->list[i]);
    if (dl->list != NULL)
        FreeVec(dl->list);
    FreeVec(dl);
    r->dl = NULL;
}
