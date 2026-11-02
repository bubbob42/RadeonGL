/*
 * census.c -- see census.h. Counting, the per-frame CSV, and the hooks the
 * generated dispatch table (build/lib/census_dispatch.c) forwards to.
 *
 * No C library: -nostdlib. Numbers are formatted here, files go through
 * dos.library from the CLIENT's process (every hook runs on the client's
 * task), and nothing allocates per call.
 */

#define MINIGL_LIBRARY_BUILD 1
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <libraries/minigl_dispatch.h>

#include "census.h"
#include "census_dispatch.h"

/* --- what a frame counts ------------------------------------------------- */

enum
{
    F_CALLS,
    F_PRIMS, F_IMM_VERTS, F_IMM_BYTES, F_PRIM_ERRORS,
    F_COLOR_OUTSIDE, F_STATE, F_BINDS, F_MATRIX, F_MATRIX_UPLOADS, F_CLEARS,
    F_TEX_UP, F_TEX_UP_T1, F_TEX_UP_T2, F_TEX_UP_TC, F_TEX_UP_SRC,
    F_TEX_SUB, F_TEX_SUB_T1, F_TEX_SUB_T2, F_TEX_SUB_TC, F_TEX_SUB_SRC,
    F_LOCKS, F_LOCK_VERTS, F_LOCK_BYTES,
    F_DE_CALLS, F_DE_IDX, F_DE_UNLOCKED_BYTES,
    F_DA_CALLS, F_DA_VERTS, F_DA_LOCKED_VERTS, F_DA_BYTES,
    F_MODE_PRIMS,                     /* 10 entries: GL_POINTS .. GL_POLYGON */
    F_MODE_VERTS = F_MODE_PRIMS + 10,
    F_MODE_BYTES = F_MODE_VERTS + 10,
    F_COUNT = F_MODE_BYTES + 10
};

static const char *const cen_FName[F_COUNT] =
{
    "calls",
    "prims", "imm_verts", "imm_bytes", "prim_errors",
    "color_outside", "state", "binds", "matrix", "matrix_uploads", "clears",
    "tex_up", "tex_up_t1", "tex_up_t2", "tex_up_tc", "tex_up_src",
    "tex_sub", "tex_sub_t1", "tex_sub_t2", "tex_sub_tc", "tex_sub_src",
    "locks", "lock_verts", "lock_bytes",
    "de_calls", "de_idx", "de_unlocked_bytes",
    "da_calls", "da_verts", "da_locked_verts", "da_bytes",
    "p_points", "p_lines", "p_lineloop", "p_linestrip", "p_tris",
    "p_tristrip", "p_trifan", "p_quads", "p_quadstrip", "p_polygon",
    "v_points", "v_lines", "v_lineloop", "v_linestrip", "v_tris",
    "v_tristrip", "v_trifan", "v_quads", "v_quadstrip", "v_polygon",
    "b_points", "b_lines", "b_lineloop", "b_linestrip", "b_tris",
    "b_tristrip", "b_trifan", "b_quads", "b_quadstrip", "b_polygon"
};

/* Slot categories, decided once from the slot names. */
#define CAT_OTHER   0
#define CAT_STATE   1
#define CAT_MATRIX  2           /* changes the current matrix */
#define CAT_MATRIX0 3           /* matrix call that changes nothing (mode, push) */

static UBYTE cen_Cat[DISPATCH_SLOTS];

/* Packed-vertex attributes of the primitive being built. */
#define A_TC     1
#define A_MTC    2
#define A_COLOR  4
#define A_NORMAL 8
#define A_W      16

#define MAX_TEX_IDS 8192
#define BUF_SIZE    32768
#define BUF_FLUSH   24576

struct CenContext
{
    struct Task   *owner;
    struct Window *win;
    BPTR           csv;
    UBYTE         *buf;
    ULONG          len;
    ULONG          frame;
    char           slotsPath[160];
    UBYTE          texClass[MAX_TEX_IDS];     /* 0 unknown, 1/2 bytes, 3 colour */
};

GLcontext Cen_CurrentContext = 0;

static struct CenContext *cen_Ctx = NULL;
static ULONG cen_Total[DISPATCH_SLOTS];
static ULONG cen_F[F_COUNT];
static ULONG cen_FMax[F_COUNT];

/* Primitive state. */
static BOOL  cen_InPrim = FALSE;
static ULONG cen_PrimMode;
static ULONG cen_PrimVerts;
static ULONG cen_PrimAttr;
static ULONG cen_PrimColors;
static BOOL  cen_MatrixDirty = TRUE;

/* Vertex-array state. */
#define ARR_VERTEX 1
#define ARR_TC0    2
#define ARR_TC1    4
#define ARR_COLOR  8
#define ARR_NORMAL 16
static ULONG cen_Arrays = 0;
static ULONG cen_ClientUnit = 0;
static BOOL  cen_Locked = FALSE;

/* Textures. */
static ULONG cen_NextTex = 1;
static ULONG cen_Unit = 0;
static ULONG cen_Bound[2];

/* GL_EXTENSIONS as PiStorm3D 29.1 reports it (others.c:860). */
static const char cen_DefaultExt[] =
    "GL_MGL_ARB_multitexture GL_EXT_compiled_vertex_array GL_MGL_packed_pixels "
    "GL_EXT_color_table GL_EXT_paletted_texture GL_EXT_shared_texture_palette ";
static char cen_Ext[256];

/* --- small helpers (no C library) ---------------------------------------- */

static ULONG StrLen(const char *s)
{
    ULONG n;

    n = 0;
    while (s[n] != '\0')
        n++;
    return n;
}

static BOOL Prefix(const char *s, const char *p)
{
    while (*p != '\0')
    {
        if (*s != *p)
            return FALSE;
        s++;
        p++;
    }
    return TRUE;
}

static void StrCopy(char *dst, const char *src, ULONG size)
{
    ULONG i;

    for (i = 0; i + 1 < size && src[i] != '\0'; i++)
        dst[i] = src[i];
    if (size > 0)
        dst[i] = '\0';
}

static void ClearLongs(ULONG *p, ULONG n)
{
    ULONG i;

    for (i = 0; i < n; i++)
        p[i] = 0;
}

static void Put(struct CenContext *c, const char *s)
{
    while (*s != '\0' && c->len < BUF_SIZE)
        c->buf[c->len++] = (UBYTE)*s++;
}

static void PutU(struct CenContext *c, ULONG v)
{
    char tmp[12];
    int i;

    i = 11;
    tmp[i] = '\0';
    do
    {
        tmp[--i] = (char)('0' + v % 10);
        v /= 10;
    } while (v != 0 && i > 0);
    Put(c, tmp + i);
}

static void BufFlush(struct CenContext *c)
{
    if (c->csv && c->len > 0)
        Write(c->csv, c->buf, (LONG)c->len);
    c->len = 0;
}

/* A float's bit pattern without touching the FPU (the library builds
 * without one): 1.0f is 0x3f800000. */
static ULONG FloatBits(GLfloat f)
{
    union { GLfloat f; ULONG u; } x;

    x.f = f;
    return x.u;
}

/* --- categories ---------------------------------------------------------- */

static const char *const cen_StateNames[] =
{
    "GLAlphaFunc", "GLBlend", "GLColorMask", "GLCullFace", "GLDepthFunc",
    "GLDepthMask", "GLDepthRange", "GLFog", "GLFrontFace", "GLHint",
    "GLPolygonMode", "GLPolygonOffset", "GLScissor", "GLShadeModel",
    "GLTexEnv", "GLTexParameter", "GLTexGen", "GLViewport", "GLPointSize",
    "GLLineWidth", "GLActiveTextureARB", "MGLSetState", "MGLSetZOffset",
    "GLColorMaterial", "GLLight", "GLMaterial", "GLDrawBuffer", "GLClearColor",
    "GLClearDepth", "GLPixelStorei"
};

static const char *const cen_MatrixNames[] =
{
    "GLLoadIdentity", "GLLoadMatrix", "GLMultMatrix", "GLPopMatrix",
    "GLRotate", "GLTranslate", "GLScale", "GLOrtho", "GLFrustum",
    "GLULookAt", "GLUPerspective"
};

void Cen_Init(void)
{
    ULONG i;
    ULONG k;

    for (i = 0; i < DISPATCH_SLOTS; i++)
    {
        cen_Cat[i] = CAT_OTHER;
        for (k = 0; k < sizeof(cen_StateNames) / sizeof(cen_StateNames[0]); k++)
            if (Prefix(Cen_SlotName[i], cen_StateNames[k]))
                cen_Cat[i] = CAT_STATE;
        for (k = 0; k < sizeof(cen_MatrixNames) / sizeof(cen_MatrixNames[0]); k++)
            if (Prefix(Cen_SlotName[i], cen_MatrixNames[k]))
                cen_Cat[i] = CAT_MATRIX;
        if (Prefix(Cen_SlotName[i], "GLMatrixMode") || Prefix(Cen_SlotName[i], "GLPushMatrix"))
            cen_Cat[i] = CAT_MATRIX0;
    }
}

void Cen_Hit(ULONG slot)
{
    cen_Total[slot]++;
    cen_F[F_CALLS]++;
    switch (cen_Cat[slot])
    {
    case CAT_STATE:
        cen_F[F_STATE]++;
        break;
    case CAT_MATRIX:
        cen_F[F_MATRIX]++;
        cen_MatrixDirty = TRUE;
        break;
    case CAT_MATRIX0:
        cen_F[F_MATRIX]++;
        break;
    default:
        break;
    }
}

/* A draw starts: the TCL matrices go out once after any change. */
static void DrawStarts(void)
{
    if (cen_MatrixDirty)
    {
        cen_F[F_MATRIX_UPLOADS]++;
        cen_MatrixDirty = FALSE;
    }
}

/* Packed size of one vertex: xyz floats, then what the primitive used. */
static ULONG VertexBytes(ULONG attr)
{
    ULONG n;

    n = 12;
    if (attr & A_W)
        n += 4;
    if (attr & A_TC)
        n += 8;
    if (attr & A_MTC)
        n += 8;
    if (attr & A_COLOR)
        n += 4;
    if (attr & A_NORMAL)
        n += 12;
    return n;
}

static ULONG ArrayVertexBytes(void)
{
    ULONG n;

    n = 12;
    if (cen_Arrays & ARR_TC0)
        n += 8;
    if (cen_Arrays & ARR_TC1)
        n += 8;
    if (cen_Arrays & ARR_COLOR)
        n += 4;
    if (cen_Arrays & ARR_NORMAL)
        n += 12;
    return n;
}

/* --- immediate mode ------------------------------------------------------ */

void Hook_GLBegin(GLcontext context, GLenum mode)
{
    (void)context;
    if (cen_InPrim)
        cen_F[F_PRIM_ERRORS]++;
    cen_InPrim = TRUE;
    cen_PrimMode = (mode <= 9) ? mode : 9;
    cen_PrimVerts = 0;
    cen_PrimAttr = 0;
    cen_PrimColors = 0;
    DrawStarts();
}

void Hook_GLEnd(GLcontext context)
{
    ULONG bytes;
    ULONG attr;

    (void)context;
    if (!cen_InPrim)
    {
        cen_F[F_PRIM_ERRORS]++;
        return;
    }
    cen_InPrim = FALSE;
    attr = cen_PrimAttr;
    /* one colour inside the primitive is a constant, not per vertex */
    if (cen_PrimColors >= 2)
        attr |= A_COLOR;
    bytes = cen_PrimVerts * VertexBytes(attr);
    cen_F[F_PRIMS]++;
    cen_F[F_IMM_VERTS] += cen_PrimVerts;
    cen_F[F_IMM_BYTES] += bytes;
    cen_F[F_MODE_PRIMS + cen_PrimMode]++;
    cen_F[F_MODE_VERTS + cen_PrimMode] += cen_PrimVerts;
    cen_F[F_MODE_BYTES + cen_PrimMode] += bytes;
}

static void Vertex(ULONG wbits)
{
    if (!cen_InPrim)
    {
        cen_F[F_PRIM_ERRORS]++;
        return;
    }
    cen_PrimVerts++;
    if (wbits != 0x3f800000UL)
        cen_PrimAttr |= A_W;
}

void Hook_GLVertex2fv(GLcontext context, GLfloat *v)
{
    (void)context;
    (void)v;
    Vertex(0x3f800000UL);
}

void Hook_GLVertex3fv(GLcontext context, GLfloat *v)
{
    (void)context;
    (void)v;
    Vertex(0x3f800000UL);
}

void Hook_GLVertex4f(GLcontext context, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    (void)context;
    (void)x;
    (void)y;
    (void)z;
    Vertex(FloatBits(w));
}

void Hook_GLVertex4fv(GLcontext context, GLfloat *v)
{
    (void)context;
    Vertex(v ? FloatBits(v[3]) : 0x3f800000UL);
}

static void TexCoord(ULONG unit)
{
    if (cen_InPrim)
        cen_PrimAttr |= (unit == 1) ? A_MTC : A_TC;
}

void Hook_GLTexCoord2f(GLcontext context, GLfloat s, GLfloat t)
{
    (void)context;
    (void)s;
    (void)t;
    TexCoord(0);
}

void Hook_GLTexCoord2fv(GLcontext context, GLfloat *v)
{
    (void)context;
    (void)v;
    TexCoord(0);
}

void Hook_GLTexCoord4f(GLcontext context, GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
    (void)context;
    (void)s;
    (void)t;
    (void)r;
    (void)q;
    TexCoord(0);
}

void Hook_GLTexCoord4fv(GLcontext context, GLfloat *v)
{
    (void)context;
    (void)v;
    TexCoord(0);
}

void Hook_GLMultiTexCoord2fARB(GLcontext context, GLenum unit, GLfloat s, GLfloat t)
{
    (void)context;
    (void)s;
    (void)t;
    TexCoord(unit == GL_TEXTURE1_ARB ? 1 : 0);
}

void Hook_GLMultiTexCoord2fvARB(GLcontext context, GLenum unit, GLfloat *v)
{
    (void)context;
    (void)v;
    TexCoord(unit == GL_TEXTURE1_ARB ? 1 : 0);
}

static void Colour(void)
{
    if (cen_InPrim)
        cen_PrimColors++;
    else
        cen_F[F_COLOR_OUTSIDE]++;
}

void Hook_GLColor3fv(GLcontext context, GLfloat *v)
{
    (void)context;
    (void)v;
    Colour();
}

void Hook_GLColor3ubv(GLcontext context, GLubyte *v)
{
    (void)context;
    (void)v;
    Colour();
}

void Hook_GLColor4f(GLcontext context, GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    (void)context;
    (void)red;
    (void)green;
    (void)blue;
    (void)alpha;
    Colour();
}

void Hook_GLColor4fv(GLcontext context, GLfloat *v)
{
    (void)context;
    (void)v;
    Colour();
}

void Hook_GLColor4ub(GLcontext context, GLubyte red, GLubyte green, GLubyte blue, GLubyte alhpa)
{
    (void)context;
    (void)red;
    (void)green;
    (void)blue;
    (void)alhpa;
    Colour();
}

void Hook_GLColor4ubv(GLcontext context, GLubyte *v)
{
    (void)context;
    (void)v;
    Colour();
}

void Hook_GLNormal3f(GLcontext context, GLfloat x, GLfloat y, GLfloat z)
{
    (void)context;
    (void)x;
    (void)y;
    (void)z;
    if (cen_InPrim)
        cen_PrimAttr |= A_NORMAL;
}

void Hook_GLNormal3fv(GLcontext context, GLfloat *n)
{
    (void)context;
    (void)n;
    if (cen_InPrim)
        cen_PrimAttr |= A_NORMAL;
}

/* --- vertex arrays ------------------------------------------------------- */

static ULONG ArrayBit(GLenum cap)
{
    switch (cap)
    {
    case GL_VERTEX_ARRAY:        return ARR_VERTEX;
    case GL_TEXTURE_COORD_ARRAY: return cen_ClientUnit ? ARR_TC1 : ARR_TC0;
    case GL_COLOR_ARRAY:         return ARR_COLOR;
    case GL_NORMAL_ARRAY:        return ARR_NORMAL;
    default:                     return 0;
    }
}

void Hook_GLEnableClientState(GLcontext context, GLenum cap)
{
    (void)context;
    cen_Arrays |= ArrayBit(cap);
}

void Hook_GLDisableClientState(GLcontext context, GLenum cap)
{
    (void)context;
    cen_Arrays &= ~ArrayBit(cap);
}

void Hook_GLClientActiveTextureARB(GLcontext context, GLenum unit)
{
    (void)context;
    cen_ClientUnit = (unit == GL_TEXTURE1_ARB) ? 1 : 0;
}

void Hook_GLArrayElement(GLcontext context, GLint i)
{
    (void)context;
    (void)i;
    if (!cen_InPrim)
    {
        cen_F[F_PRIM_ERRORS]++;
        return;
    }
    cen_PrimVerts++;
    if (cen_Arrays & ARR_TC0)
        cen_PrimAttr |= A_TC;
    if (cen_Arrays & ARR_TC1)
        cen_PrimAttr |= A_MTC;
    if (cen_Arrays & ARR_COLOR)
        cen_PrimColors += 2;
    if (cen_Arrays & ARR_NORMAL)
        cen_PrimAttr |= A_NORMAL;
}

void Hook_GLLockArrays(GLcontext context, GLuint first, GLsizei count)
{
    (void)context;
    (void)first;
    cen_Locked = TRUE;
    cen_F[F_LOCKS]++;
    cen_F[F_LOCK_VERTS] += (ULONG)count;
    cen_F[F_LOCK_BYTES] += (ULONG)count * ArrayVertexBytes();
}

void Hook_GLUnlockArrays(GLcontext context)
{
    (void)context;
    cen_Locked = FALSE;
}

void Hook_GLDrawElements(GLcontext context, GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
    (void)context;
    (void)mode;
    (void)type;
    (void)indices;
    DrawStarts();
    cen_F[F_DE_CALLS]++;
    cen_F[F_DE_IDX] += (ULONG)count;
    if (!cen_Locked)
        cen_F[F_DE_UNLOCKED_BYTES] += (ULONG)count * ArrayVertexBytes();
}

static void DrawArrays(ULONG count)
{
    cen_F[F_DA_CALLS]++;
    cen_F[F_DA_VERTS] += count;
    if (cen_Locked)
        cen_F[F_DA_LOCKED_VERTS] += count;
    else
        cen_F[F_DA_BYTES] += count * ArrayVertexBytes();
}

void Hook_GLDrawArrays(GLcontext context, GLenum mode, GLint first, GLsizei count)
{
    (void)context;
    (void)mode;
    (void)first;
    DrawStarts();
    DrawArrays((ULONG)count);
}

void Hook_GLMultiDrawArrays(GLcontext context, GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount)
{
    GLsizei i;

    (void)context;
    (void)mode;
    (void)first;
    DrawStarts();
    for (i = 0; i < primcount; i++)
        DrawArrays((ULONG)count[i]);
}

/* --- textures ------------------------------------------------------------ */

/* 1 or 2: bytes a texel on the card whatever happens; 3: a colour format
 * whose size the back end chooses (16 or 32 bit), decided in census.py. */
static UBYTE TexClass(GLint internalformat)
{
    switch (internalformat)
    {
    case 1:
    case GL_LUMINANCE:
    case GL_ALPHA:
    case GL_INTENSITY:
    case 0x803C:    /* GL_ALPHA8 */
    case 0x8040:    /* GL_LUMINANCE8 */
    case 0x804B:    /* GL_INTENSITY8 */
        return 1;
    case 2:
    case GL_LUMINANCE_ALPHA:
    case 0x8045:    /* GL_LUMINANCE8_ALPHA8 */
        return 2;
    default:
        return 3;
    }
}

/* Bytes a texel in the client's memory. */
static ULONG SourceBytes(GLenum format, GLenum type)
{
    ULONG comps;

    if (type == MGL_UNSIGNED_SHORT_4_4_4_4 || type == MGL_UNSIGNED_SHORT_5_6_5)
        return 2;
    switch (format)
    {
    case GL_RGBA:            comps = 4; break;
    case GL_RGB:             comps = 3; break;
    case GL_LUMINANCE_ALPHA: comps = 2; break;
    default:                 comps = 1; break;   /* L, A, I, colour index */
    }
    if (type == GL_FLOAT || type == GL_INT || type == GL_UNSIGNED_INT)
        return comps * 4;
    if (type == GL_SHORT || type == GL_UNSIGNED_SHORT)
        return comps * 2;
    return comps;
}

static void CountTexels(ULONG base, UBYTE cls, ULONG texels, ULONG src)
{
    cen_F[base]++;
    if (cls == 1)
        cen_F[base + 1] += texels;
    else if (cls == 2)
        cen_F[base + 2] += texels;
    else
        cen_F[base + 3] += texels;
    cen_F[base + 4] += src;
}

void Hook_GLGenTextures(GLcontext context, GLsizei n, GLuint *textures)
{
    GLsizei i;

    (void)context;
    for (i = 0; i < n; i++)
        textures[i] = cen_NextTex++;
}

GLboolean Hook_GLIsTexture(GLcontext context, GLuint texture)
{
    (void)context;
    return (GLboolean)(texture != 0 && texture < cen_NextTex);
}

void Hook_GLBindTexture(GLcontext context, GLenum target, GLuint texture)
{
    (void)context;
    (void)target;
    cen_F[F_BINDS]++;
    cen_Bound[cen_Unit] = texture;
    if (texture >= cen_NextTex)
        cen_NextTex = texture + 1;
}

void Hook_GLActiveTextureARB(GLcontext context, GLenum unit)
{
    (void)context;
    cen_Unit = (unit == GL_TEXTURE1_ARB) ? 1 : 0;
}

void Hook_GLTexImage2D(GLcontext context, GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
    ULONG texels;
    UBYTE cls;
    GLuint id;

    (void)context;
    (void)target;
    (void)level;
    (void)border;
    (void)pixels;
    texels = (ULONG)width * (ULONG)height;
    cls = TexClass(internalformat);
    id = cen_Bound[cen_Unit];
    if (cen_Ctx != NULL && id < MAX_TEX_IDS)
        cen_Ctx->texClass[id] = cls;
    CountTexels(F_TEX_UP, cls, texels, texels * SourceBytes(format, type));
}

void Hook_GLTexSubImage2D(GLcontext context, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
    ULONG texels;
    UBYTE cls;
    GLuint id;

    (void)context;
    (void)target;
    (void)level;
    (void)xoffset;
    (void)yoffset;
    (void)pixels;
    texels = (ULONG)width * (ULONG)height;
    id = cen_Bound[cen_Unit];
    cls = 3;
    if (cen_Ctx != NULL && id < MAX_TEX_IDS && cen_Ctx->texClass[id] != 0)
        cls = cen_Ctx->texClass[id];
    CountTexels(F_TEX_SUB, cls, texels, texels * SourceBytes(format, type));
}

void Hook_GLClear(GLcontext context, GLbitfield mask)
{
    (void)context;
    (void)mask;
    cen_F[F_CLEARS]++;
}

/* --- queries ------------------------------------------------------------- */

const GLubyte *Hook_GLGetString(GLcontext context, GLenum name)
{
    (void)context;
    switch (name)
    {
    case GL_VENDOR:     return (const GLubyte *)"RadeonGL";
    case GL_RENDERER:   return (const GLubyte *)"RadeonGL census (draws nothing)";
    case GL_VERSION:    return (const GLubyte *)"1.1";
    case GL_EXTENSIONS: return (const GLubyte *)cen_Ext;
    default:            return (const GLubyte *)"";
    }
}

void Hook_GLGetIntegerv(GLcontext context, GLenum pname, GLint *params)
{
    (void)context;
    if (params == NULL)
        return;
    switch (pname)
    {
    case GL_MAX_TEXTURE_SIZE:       params[0] = 2048; break;   /* R200 */
    case GL_MAX_TEXTURE_UNITS_ARB:  params[0] = 2; break;
    default:                        params[0] = 0; break;
    }
}

void Hook_GLGetFloatv(GLcontext context, GLenum pname, GLfloat *params)
{
    ULONG *p;
    int i;

    (void)context;
    if (params == NULL)
        return;
    p = (ULONG *)params;
    if (pname == GL_MODELVIEW_MATRIX || pname == GL_PROJECTION_MATRIX)
    {
        for (i = 0; i < 16; i++)
            p[i] = (i % 5 == 0) ? 0x3f800000UL : 0;     /* identity */
        return;
    }
    p[0] = 0;
}

void Hook_GLGetBooleanv(GLcontext context, GLenum pname, GLboolean *params)
{
    (void)context;
    (void)pname;
    if (params != NULL)
        params[0] = GL_FALSE;
}

GLboolean Hook_GLIsEnabled(GLcontext context, GLenum cap)
{
    (void)context;
    (void)cap;
    return GL_FALSE;
}

/* --- context and frames -------------------------------------------------- */

static void WriteHeader(struct CenContext *c)
{
    ULONG i;

    Put(c, "frame");
    for (i = 0; i < F_COUNT; i++)
    {
        Put(c, ",");
        Put(c, cen_FName[i]);
    }
    Put(c, "\n");
    BufFlush(c);
}

static void WriteFrame(struct CenContext *c)
{
    ULONG i;

    PutU(c, c->frame);
    for (i = 0; i < F_COUNT; i++)
    {
        Put(c, ",");
        PutU(c, cen_F[i]);
        if (cen_F[i] > cen_FMax[i])
            cen_FMax[i] = cen_F[i];
    }
    Put(c, "\n");
    if (c->len >= BUF_FLUSH || (c->frame & 31) == 31)
        BufFlush(c);
}

static void WriteSlots(struct CenContext *c)
{
    BPTR f;
    ULONG i;

    f = Open(c->slotsPath, MODE_NEWFILE);
    if (f == 0)
        return;
    c->len = 0;
    Put(c, "slot,name,calls\n");
    for (i = 0; i < DISPATCH_SLOTS; i++)
    {
        PutU(c, i);
        Put(c, ",");
        Put(c, Cen_SlotName[i]);
        Put(c, ",");
        PutU(c, cen_Total[i]);
        Put(c, "\n");
        if (c->len >= BUF_FLUSH)
        {
            Write(f, c->buf, (LONG)c->len);
            c->len = 0;
        }
    }
    Put(c, "frames,");
    PutU(c, c->frame);
    Put(c, "\n");
    for (i = 0; i < F_COUNT; i++)
    {
        Put(c, "max_");
        Put(c, cen_FName[i]);
        Put(c, ",");
        PutU(c, cen_FMax[i]);
        Put(c, "\n");
    }
    Write(f, c->buf, (LONG)c->len);
    c->len = 0;
    Close(f);
}

static void FreeContext(struct CenContext *c)
{
    if (c->win)
    {
        ModifyIDCMP(c->win, 0);
        CloseWindow(c->win);
    }
    if (c->buf)
        FreeVec(c->buf);
    FreeVec(c);
}

void *Hook_MGLCreateContext(int offx, int offy, int w, int h)
{
    struct CenContext *c;
    struct Screen *scr;
    char prefix[128];
    char path[160];
    LONG n;

    (void)offx;
    (void)offy;
    if (cen_Ctx != NULL)
        return NULL;                 /* one context at a time */
    c = AllocVec(sizeof(*c), MEMF_ANY | MEMF_CLEAR);
    if (c == NULL)
        return NULL;
    c->owner = FindTask(NULL);
    c->buf = AllocVec(BUF_SIZE, MEMF_ANY);
    if (c->buf == NULL)
    {
        FreeContext(c);
        return NULL;
    }

    n = GetVar((STRPTR)"MiniGLCensus", (STRPTR)prefix, sizeof(prefix), 0);
    if (n <= 0)
        StrCopy(prefix, "PROGDIR:minigl-census", sizeof(prefix));
    StrCopy(path, prefix, sizeof(path) - 12);
    StrCopy(path + StrLen(path), ".csv", 8);
    StrCopy(c->slotsPath, prefix, sizeof(c->slotsPath) - 12);
    StrCopy(c->slotsPath + StrLen(c->slotsPath), "-slots.csv", 12);
    c->csv = Open((STRPTR)path, MODE_NEWFILE);

    n = GetVar((STRPTR)"MiniGLCensusExt", (STRPTR)cen_Ext, sizeof(cen_Ext), 0);
    if (n <= 0)
        StrCopy(cen_Ext, cen_DefaultExt, sizeof(cen_Ext));

    /* The client takes its input from this window (mglGetInputWindowHandle). */
    scr = LockPubScreen(NULL);
    if (scr != NULL)
    {
        c->win = OpenWindowTags(NULL,
            WA_PubScreen, (ULONG)scr,
            WA_Left, 20,
            WA_Top, 20,
            WA_InnerWidth, (ULONG)((w > 0 && w < 320) ? w : 320),
            WA_InnerHeight, (ULONG)((h > 0 && h < 120) ? h : 120),
            WA_Title, (ULONG)"minigl census -- draws nothing",
            WA_DragBar, TRUE,
            WA_DepthGadget, TRUE,
            WA_Activate, TRUE,
            WA_RMBTrap, TRUE,
            WA_NoCareRefresh, TRUE,
            TAG_DONE);
        UnlockPubScreen(NULL, scr);
    }

    ClearLongs(cen_Total, DISPATCH_SLOTS);
    ClearLongs(cen_F, F_COUNT);
    ClearLongs(cen_FMax, F_COUNT);
    cen_InPrim = FALSE;
    cen_MatrixDirty = TRUE;
    cen_Arrays = 0;
    cen_Locked = FALSE;
    cen_NextTex = 1;
    cen_Unit = 0;
    cen_Bound[0] = cen_Bound[1] = 0;
    WriteHeader(c);
    cen_Ctx = c;
    return (void *)c;
}

void Hook_MGLDeleteContext(GLcontext context)
{
    struct CenContext *c;

    c = (struct CenContext *)context;
    if (c == NULL || c != cen_Ctx)
        return;
    if (cen_F[F_CALLS] > 0)
        WriteFrame(c);                      /* the unfinished last frame */
    BufFlush(c);
    if (c->csv)
        Close(c->csv);
    c->csv = 0;
    WriteSlots(c);
    cen_Ctx = NULL;
    Cen_CurrentContext = 0;
    FreeContext(c);
}

void Hook_MGLSwitchDisplay(GLcontext context)
{
    struct CenContext *c;

    c = (struct CenContext *)context;
    if (c == NULL || c != cen_Ctx)
        return;
    WriteFrame(c);
    c->frame++;
    ClearLongs(cen_F, F_COUNT);
}

void *Hook_MGLGetWindowHandle(GLcontext context)
{
    struct CenContext *c;

    c = (struct CenContext *)context;
    return (c != NULL) ? (void *)c->win : NULL;
}

void *Hook_MGLGetInputWindowHandle(GLcontext context)
{
    return Hook_MGLGetWindowHandle(context);
}

GLboolean Hook_MGLLockDisplay(GLcontext context)
{
    (void)context;
    return GL_TRUE;
}

GLboolean Hook_MGLResizeContext(GLcontext context, GLsizei width, GLsizei height)
{
    (void)context;
    (void)width;
    (void)height;
    return GL_TRUE;
}

/* LibClose: no writes here (exec calls it under Forbid). Close() and
 * CloseWindow() break the Forbid for a moment, which exec allows; the
 * CSV lines already flushed stay valid. */
void Cen_ClientGone(struct Task *task)
{
    struct CenContext *c;

    c = cen_Ctx;
    if (c == NULL || c->owner != task)
        return;
    if (c->csv)
        Close(c->csv);
    c->csv = 0;
    cen_Ctx = NULL;
    Cen_CurrentContext = 0;
    FreeContext(c);
}
