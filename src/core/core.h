/*
 * core.h -- RadeonGL's GL core: GL state in GL terms, matrices, immediate
 * mode, client arrays, texture objects, and their R200 encoding.
 *
 * Pure C89 without OS calls (decision D2.1): memory, VRAM and the GPU
 * fence come in through struct CoreEnv, the command stream is an R2Cs
 * (src/r200). So the same code runs in the library and in the host tests,
 * which check every packet it emits with the packet checker.
 *
 * How the parts meet (concept 6.5, docs/d2-review.md):
 *  - GL calls change the state in struct Core and set dirty bits; nothing
 *    is emitted then.
 *  - glBegin (and every array draw) first emits the dirty state as R200
 *    register blocks (hwstate.c), then opens one 3D_DRAW_IMMD_2 packet.
 *  - Every glVertex writes its packed vertex straight into that packet:
 *    xyz floats, one colour dword, a texture coordinate pair per enabled
 *    unit. glEnd closes the packet.
 *  - Textures live in VRAM (vram.c), written a dword at a time in the
 *    GPU's byte order.
 *
 * Byte order of what the core writes into VRAM: every dword is the value
 * the 68k stores, the "68k view" the whole packet layer uses (r2cs.h).
 * Texels are packed so the little-endian GPU reads them right: two 16-bit
 * texels t0, t1 go out as SWAP16(t0) << 16 | SWAP16(t1).
 */

#ifndef CORE_CORE_H
#define CORE_CORE_H

#include <exec/types.h>
#include "glconst.h"
#include "glextra.h"
#include "r2cs.h"
#include "r2emit.h"

/* Limits */
#define CORE_UNITS        2       /* texture units (GLQuake needs 2) */
#define CORE_MAX_TEX      4096    /* texture names 0..4095 */
#define CORE_MAX_LEVELS   12      /* 2048 x 2048 down to 1 x 1 */
#define CORE_MAX_TEXSIZE  2048
#define CORE_MV_DEPTH     32
#define CORE_P_DEPTH      8
#define CORE_T_DEPTH      8
#define CORE_IMMD_VERTS   256     /* vertices reserved per glBegin; more are split */

/* What the core needs from its host (the library or a test). */
struct CoreEnv
{
    APTR (*alloc)(APTR env, ULONG size);            /* zeroed, or NULL */
    void (*free)(APTR env, APTR mem, ULONG size);
    /* Return once the GPU has finished everything emitted so far. Needed
     * before the CPU rewrites VRAM the GPU may still read. */
    LONG (*wait_idle)(APTR env);
    APTR env;
};

/* The render target: colour and depth buffers in VRAM. */
struct CoreTarget
{
    ULONG width, height;
    ULONG color_gpu;
    ULONG color_pitch;      /* pixels, a multiple of 8 */
    ULONG color_format;     /* R_COLOR_FORMAT_RGB565 or _ARGB8888 */
    ULONG depth_gpu;        /* Z16 */
    ULONG depth_pitch;      /* pixels, a multiple of 8 */
};

/* A block of VRAM the core suballocates (textures, arrays). */
struct CoreHeapBlock;

struct CoreHeap
{
    ULONG gpu;              /* GPU address of the first byte */
    ULONG *cpu;             /* CPU address of the same byte */
    ULONG size;             /* bytes */
    ULONG used;             /* bytes handed out */
    struct CoreHeapBlock *blocks;   /* allocated blocks, by address */
};

/* A texture object. */
struct CoreTex
{
    ULONG name;
    ULONG w, h;             /* level 0 */
    ULONG lw, lh;           /* log2 of w and h when both are powers of two */
    ULONG pot;              /* both powers of two */
    ULONG levels;           /* level_mask bit n: level n holds texels */
    ULONG base;             /* GL base format: GL_RGB, GL_RGBA, GL_LUMINANCE ... */
    ULONG hwfmt;            /* R_TXFORMAT_* (| R_TXFORMAT_ALPHA_IN_MAP) */
    ULONG cpp;              /* bytes per texel in VRAM */
    ULONG gpu;              /* VRAM of level 0, 0 = none */
    ULONG *cpu;
    ULONG bytes;            /* allocated */
    ULONG lofs[CORE_MAX_LEVELS];    /* byte offset of each level */
    ULONG pitch[CORE_MAX_LEVELS];   /* bytes per row of each level */
    ULONG min_filter, mag_filter, wrap_s, wrap_t;
    ULONG used_since_idle;  /* drawn with since the last wait_idle */
};

/* Client vertex arrays (glVertexPointer ...). */
struct CoreArray
{
    ULONG enabled;
    ULONG size;             /* components */
    ULONG type;             /* GL_FLOAT, GL_UNSIGNED_BYTE ... */
    ULONG stride;           /* bytes, 0 = tight */
    const UBYTE *ptr;
};

/* Dirty bits: which R200 register blocks must be emitted before the next draw. */
#define D_INIT      0x00000001UL    /* R2E_TclDefaults + R2E_RasterDefaults */
#define D_TARGET    0x00000002UL    /* colour + depth target */
#define D_VIEWPORT  0x00000004UL
#define D_SCISSOR   0x00000008UL
#define D_RB3D      0x00000010UL    /* RB3D_CNTL */
#define D_ZSTENCIL  0x00000020UL
#define D_BLEND     0x00000040UL
#define D_ALPHA     0x00000080UL    /* PP_MISC */
#define D_PPCNTL    0x00000100UL
#define D_SECNTL    0x00000200UL    /* SE_CNTL + UCP_VERT_BLEND_CTL (cull) */
#define D_PLANEMASK 0x00000400UL
#define D_TEX0      0x00000800UL    /* unit n: D_TEX0 << n */
#define D_COMBINE   0x00002000UL
#define D_MATRIX    0x00004000UL    /* MVP upload */
#define D_VTXFMT    0x00008000UL
#define D_ZBIAS     0x00010000UL
#define D_POINTSIZE 0x00020000UL
#define D_VAP       0x00040000UL    /* SE_VAP_CNTL + SE_VTE_CNTL: back to TCL after a clear */
#define D_ALL       0x0007ffffUL

/* Enables (struct Core.enables) */
#define EN_ALPHA_TEST   0x0001UL
#define EN_BLEND        0x0002UL
#define EN_CULL_FACE    0x0004UL
#define EN_DEPTH_TEST   0x0008UL
#define EN_DITHER       0x0010UL
#define EN_FOG          0x0020UL
#define EN_SCISSOR      0x0040UL
#define EN_POLY_OFFSET  0x0080UL    /* GL_POLYGON_OFFSET_FILL */
#define EN_ZOFFSET      0x0100UL    /* MGL_Z_OFFSET */
#define EN_LIGHTING     0x0200UL

struct CoreMat
{
    float m[16];            /* column-major, as GL keeps it */
};

struct Core
{
    struct R2Cs *cs;
    struct CoreEnv env;
    struct CoreTarget target;
    struct CoreHeap heap;
    ULONG dirty;
    ULONG error;            /* first error since glGetError, GL_NO_ERROR = 0 */
    ULONG tex_swap;         /* texel dwords stored byte-reversed (no aperture swap) */

    /* enables and state, in GL terms */
    ULONG enables;
    ULONG tex_enabled[CORE_UNITS];  /* glEnable(GL_TEXTURE_2D) on that unit */
    ULONG blend_src, blend_dst;
    ULONG alpha_func;
    float alpha_ref;
    ULONG depth_func;
    ULONG depth_mask;
    float depth_near, depth_far;
    ULONG cull_mode;        /* GL_FRONT, GL_BACK, GL_FRONT_AND_BACK */
    ULONG front_face;       /* GL_CCW or GL_CW */
    ULONG shade_model;
    ULONG color_mask;       /* bit 0 r, 1 g, 2 b, 3 a */
    float clear_color[4];
    float clear_depth;
    LONG  vp_x, vp_y, vp_w, vp_h;
    LONG  sc_x, sc_y, sc_w, sc_h;
    float poly_factor, poly_units;
    float zoffset;          /* mglSetZOffset: direct depth delta */
    float point_size;
    ULONG polygon_mode;
    ULONG lock_mode;
    ULONG sync;
    float min_tri_area;

    /* texture units */
    ULONG active_unit;      /* glActiveTextureARB */
    ULONG client_unit;      /* glClientActiveTextureARB */
    ULONG bound[CORE_UNITS];        /* texture name per unit */
    ULONG tex_env[CORE_UNITS];      /* GL_MODULATE, GL_REPLACE ... */
    struct CoreTex **tex;           /* CORE_MAX_TEX entries */

    /* matrices */
    ULONG matrix_mode;
    struct CoreMat mv[CORE_MV_DEPTH];
    struct CoreMat proj[CORE_P_DEPTH];
    struct CoreMat texm[CORE_UNITS][CORE_T_DEPTH];
    ULONG mv_top, proj_top, tex_top[CORE_UNITS];

    /* current vertex attributes */
    float cur_st[CORE_UNITS][2];
    ULONG cur_color;        /* packed as the GPU reads it: a << 24 | b << 16 | g << 8 | r */
    float cur_colorf[4];
    float cur_normal[3];

    /* the open immediate packet (glBegin .. glEnd) */
    ULONG prim;             /* GL mode, or CORE_NO_PRIM */
    ULONG hwprim;           /* R_VF_PRIM_* */
    ULONG units_on;         /* bit n: the vertex carries texture set n */
    ULONG vtx_dw;           /* dwords a vertex */
    ULONG *vbase;           /* first vertex of the open packet */
    ULONG *vptr;            /* where the next vertex goes */
    ULONG *vlimit;          /* end of the reserved room (one spare vertex for loops) */
    ULONG nverts;           /* vertices in the open packet */
    ULONG first[10];        /* the primitive's first vertex (fans, polygons, loops) */
    ULONG split_parity;     /* triangle strips: the carried part starts odd */

    /* the vertex format last emitted */
    ULONG hw_fmt0, hw_fmt1, hw_units;

    /* client arrays */
    struct CoreArray va, ca, ta[CORE_UNITS];
    LONG  lock_first, lock_count;   /* glLockArraysEXT, count 0 = none */

    /* statistics for tests and the census */
    ULONG stat_prims, stat_verts, stat_splits, stat_state_emits, stat_idle_waits;
};

#define CORE_NO_PRIM 0xffffffffUL

/* --- Life cycle (core.c) ------------------------------------------------------ */

/* Initialise c for the stream cs, the target t and the texture heap
 * [heap_gpu, heap_gpu + heap_size) mapped at heap_cpu. Returns 0 or a GL
 * error (GL_OUT_OF_MEMORY). Nothing is emitted until the first draw. */
ULONG Core_Init(struct Core *c, struct R2Cs *cs, const struct CoreEnv *env,
                const struct CoreTarget *t, ULONG heap_gpu, ULONG *heap_cpu,
                ULONG heap_size);
void  Core_Free(struct Core *c);
ULONG Core_GetError(struct Core *c);
void  Core_Error(struct Core *c, ULONG err);

/* Emit every dirty register block (before a draw; tests call it too). */
LONG  Core_Validate(struct Core *c);

/* --- State (state.c) ------------------------------------------------------------ */

void Core_Enable(struct Core *c, ULONG cap, ULONG on);
ULONG Core_IsEnabled(struct Core *c, ULONG cap);
void Core_BlendFunc(struct Core *c, ULONG src, ULONG dst);
void Core_AlphaFunc(struct Core *c, ULONG func, float ref);
void Core_DepthFunc(struct Core *c, ULONG func);
void Core_DepthMask(struct Core *c, ULONG flag);
void Core_DepthRange(struct Core *c, double n, double f);
void Core_CullFace(struct Core *c, ULONG mode);
void Core_FrontFace(struct Core *c, ULONG mode);
void Core_ShadeModel(struct Core *c, ULONG mode);
void Core_ColorMask(struct Core *c, ULONG r, ULONG g, ULONG b, ULONG a);
void Core_Viewport(struct Core *c, LONG x, LONG y, LONG w, LONG h);
void Core_Scissor(struct Core *c, LONG x, LONG y, LONG w, LONG h);
void Core_PolygonOffset(struct Core *c, float factor, float units);
void Core_PolygonMode(struct Core *c, ULONG face, ULONG mode);
void Core_PointSize(struct Core *c, float size);
void Core_ClearColor(struct Core *c, float r, float g, float b, float a);
void Core_ClearDepth(struct Core *c, double d);
void Core_Hint(struct Core *c, ULONG target, ULONG mode);
void Core_SetZOffset(struct Core *c, float offset);
void Core_ActiveTexture(struct Core *c, ULONG unit);
void Core_ClientActiveTexture(struct Core *c, ULONG unit);
void Core_TexEnvi(struct Core *c, ULONG target, ULONG pname, LONG param);

/* --- Matrices (matrix.c) ------------------------------------------------------- */

void Core_MatrixMode(struct Core *c, ULONG mode);
void Core_LoadIdentity(struct Core *c);
void Core_LoadMatrixf(struct Core *c, const float *m);
void Core_MultMatrixf(struct Core *c, const float *m);
void Core_PushMatrix(struct Core *c);
void Core_PopMatrix(struct Core *c);
void Core_Translatef(struct Core *c, float x, float y, float z);
void Core_Scalef(struct Core *c, float x, float y, float z);
void Core_Rotatef(struct Core *c, float angle, float x, float y, float z);
void Core_RotateAxis(struct Core *c, float s, float co, LONG axis);   /* glRotatefEXTs */
void Core_RotateAxisDeg(struct Core *c, float angle, LONG axis);     /* glRotatefEXT */
void Core_Ortho(struct Core *c, double l, double r, double b, double t, double n, double f);
void Core_Frustum(struct Core *c, double l, double r, double b, double t, double n, double f);
struct CoreMat *Core_TopMatrix(struct Core *c, ULONG mode);
void Mat_Mul(float *out, const float *a, const float *b);    /* out = a x b, column-major */
float Core_Sin(float deg);    /* degrees, float, no libm */
float Core_Cos(float deg);

/* --- Immediate mode (vertex.c) ------------------------------------------------- */

void Core_Begin(struct Core *c, ULONG mode);
void Core_End(struct Core *c);
void Core_Vertex3f(struct Core *c, float x, float y, float z);
void Core_Vertex4f(struct Core *c, float x, float y, float z, float w);
void Core_TexCoord2f(struct Core *c, float s, float t);
void Core_MultiTexCoord2f(struct Core *c, ULONG unit, float s, float t);
void Core_Color4ub(struct Core *c, ULONG r, ULONG g, ULONG b, ULONG a);
void Core_Color4f(struct Core *c, float r, float g, float b, float a);
void Core_Normal3f(struct Core *c, float x, float y, float z);

/* --- Client arrays (array.c) ---------------------------------------------------- */

void Core_EnableClientState(struct Core *c, ULONG array, ULONG on);
void Core_VertexPointer(struct Core *c, LONG size, ULONG type, LONG stride, const void *p);
void Core_ColorPointer(struct Core *c, LONG size, ULONG type, LONG stride, const void *p);
void Core_TexCoordPointer(struct Core *c, LONG size, ULONG type, LONG stride, const void *p);
void Core_LockArrays(struct Core *c, LONG first, LONG count);
void Core_UnlockArrays(struct Core *c);
void Core_DrawArrays(struct Core *c, ULONG mode, LONG first, LONG count);
void Core_DrawElements(struct Core *c, ULONG mode, LONG count, ULONG type, const void *idx);

/* --- Textures (texture.c) ------------------------------------------------------- */

void Core_BindTexture(struct Core *c, ULONG target, ULONG name);
void Core_DeleteTextures(struct Core *c, LONG n, const ULONG *names);
void Core_TexParameteri(struct Core *c, ULONG target, ULONG pname, LONG param);
void Core_TexImage2D(struct Core *c, ULONG target, LONG level, LONG internal,
                     LONG w, LONG h, LONG border, ULONG format, ULONG type,
                     const void *pixels);
void Core_TexSubImage2D(struct Core *c, ULONG target, LONG level, LONG x, LONG y,
                        LONG w, LONG h, ULONG format, ULONG type, const void *pixels);
struct CoreTex *Core_Texture(struct Core *c, ULONG unit);   /* bound and complete, or NULL */

/* --- Frame (frame.c) --------------------------------------------------------------- */

void Core_Clear(struct Core *c, ULONG mask);

/* --- Queries (get.c) --------------------------------------------------------------- */

void Core_GetFloatv(struct Core *c, ULONG pname, float *out);
void Core_GetIntegerv(struct Core *c, ULONG pname, LONG *out);
const char *Core_GetString(struct Core *c, ULONG name);

/* --- Internals shared between the core's files ---------------------------------- */

ULONG Hw_DepthFunc(ULONG glfunc);   /* GL compare function -> R200 0..7, ~0 if invalid */
ULONG Hw_BlendFactor(ULONG glf);    /* GL blend factor -> R_BLEND_GL_*, 0 if invalid */
void  Hw_VertexFormat(struct Core *c, ULONG units);   /* emit when it differs */
ULONG Core_UnitsOn(struct Core *c); /* units that texture the next draw */

LONG  Heap_Init(struct CoreHeap *h, ULONG gpu, ULONG *cpu, ULONG size);
ULONG Heap_Alloc(struct Core *c, ULONG size, ULONG align);   /* byte offset, or ~0 */
void  Heap_Free(struct Core *c, ULONG offset);
void  Heap_FreeAll(struct Core *c);

#endif /* CORE_CORE_H */
