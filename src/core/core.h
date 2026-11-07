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
#define CORE_STATE_MAX    512     /* dwords the whole state takes at most (MEASURED 182: test_state_max) */
#define CORE_DRAW_DW      64      /* an array draw's packets after the state */
#define CORE_STREAM_BYTES (512UL * 1024)  /* VRAM for locked arrays and indices, refilled every frame */
#define CORE_UCACHE_BYTES (2048UL * 1024)  /* VRAM keeping uploads across frames (array.c) */
#define CORE_UCACHE_MIN   (1024UL * 1024)  /* when the heap has no room for the above */
/* 2 MB since 2026-11-06: GLQuake's static world (GL_RGL_static_arrays) takes
 * 663 KB, 775 KB with its T-junction points kept; with the models and indices
 * of a frame that overflowed 1 MB, and what did not fit went through the
 * stream every frame (MEASURED A4000D: +10 KB a frame, -6% at 320 x 240). */
#define CORE_UQ           1024            /* cache entries, oldest first */
#define CORE_UDIRTY       32              /* staged: ranges for the back end to copy */
#define CORE_USLOTS       16384           /* their lookup table (a power of two): a key
                                           * looks in 4 slots from its own (array.c
                                           * UPROBE); with all 4 taken an insert takes
                                           * the stalest, so a small table loses live
                                           * entries (MEASURED 2026-11-03, one slot a
                                           * key: 256 slots cost misses; at 4096 a new
                                           * hash function cost 22% more misses by which
                                           * entries happened to collide) */

/* One upload the cache holds: its key (two hashes of the packed dwords and
 * their count, a kind folded into the second) and where it is. */
struct CoreUEntry
{
    ULONG h1, h2, ndw;
    ULONG pos;              /* offset in the cache region */
    ULONG bytes;
    ULONG epoch;            /* last drawn in: never evicted within it */
    ULONG slot;
    ULONG ref;              /* hit since the ring last passed it (second chance) */
};

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
    ULONG peak;             /* the most ever handed out at once */
    ULONG fails;            /* allocations refused (no gap large enough) */
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
    ULONG bytes;            /* allocated */
    ULONG lofs[CORE_MAX_LEVELS];    /* byte offset of each level */
    ULONG pitch[CORE_MAX_LEVELS];   /* bytes per row of each level */
    ULONG min_filter, mag_filter, wrap_s, wrap_t;
    ULONG use_epoch;        /* the epoch it was last drawn in (0: never) */
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
/* accepted and stored, no R200 effect (yet): */
#define EN_STENCIL      0x0400UL    /* no stencil buffer: a no-op, but a valid cap */
#define EN_PERSPECTIVE  0x0800UL    /* MGL_PERSPECTIVE_MAPPING: the R200 always is */
#define EN_NORMALIZE    0x1000UL
#define EN_COLOR_MATERIAL 0x2000UL
#define EN_LINE_STIPPLE 0x4000UL
#define EN_POLY_STIPPLE 0x8000UL
#define EN_LOGIC_OP     0x10000UL
#define EN_TEXGEN_S     0x20000UL
#define EN_TEXGEN_T     0x40000UL
#define EN_SMOOTH       0x80000UL   /* point, line, polygon smoothing */
#define EN_LIGHT0       0x100000UL  /* lights 0..7: bits 20..27 */

struct CoreMat
{
    float m[16];            /* column-major, as GL keeps it */
    ULONG ver;              /* new at every change; a push copies it, a pop
                             * brings the saved one back */
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
    ULONG epoch;            /* GPU-idle epoch: bumped whenever the GPU is known idle */
    ULONG rebase_gen;       /* counts Core_Forget: the GPU's state, or what was emitted, is gone */

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
    ULONG colorf_set;       /* cur_colorf came from glColor4f as given (vertex.c) */
    float cur_normal[3];

    /* the open immediate packet (glBegin .. glEnd) */
    ULONG prim;             /* GL mode, or CORE_NO_PRIM */
    ULONG hwprim;           /* R_VF_PRIM_* */
    ULONG units_on;         /* bit n: the vertex carries texture set n */
    ULONG vfast;            /* glVertex may write straight into the packet (vtxfast.h) */
    ULONG vtx_dw;           /* dwords a vertex */
    ULONG *vbase;           /* first vertex of the open packet */
    ULONG *vptr;            /* where the next vertex goes */
    ULONG *vlimit;          /* end of the reserved room (one spare vertex for loops) */
    ULONG nverts;           /* vertices in the open packet */
    ULONG first[10];        /* the primitive's first vertex (fans, polygons, loops) */
    ULONG split_parity;     /* triangle strips: the carried part starts odd */

    /* the vertex format last emitted */
    ULONG hw_fmt0, hw_fmt1, hw_units;

    /* Registers as last emitted, so a block is sent only when a value
     * differs (a bind usually changes TXOFFSET and TXFORMAT alone). ~0 is
     * never a real value: Hw_Forget sets it wherever the GPU's state is
     * lost (init, present, clear, a dropped frame). */
    ULONG hw_tex[CORE_UNITS][7];    /* TXFILTER..TXPITCH, TXMULTI_CTL, TXOFFSET */
    ULONG hw_comb[CORE_UNITS][4];   /* TXCBLEND, TXCBLEND2, TXABLEND, TXABLEND2 */
    ULONG hw_ppcntl;
    ULONG mat_ver;          /* the last matrix version handed out */
    ULONG hw_mv_ver;        /* the modelview and projection versions whose */
    ULONG hw_proj_ver;      /* product is in the TCL's MVP now (~0: none) */

    /* client arrays */
    struct CoreArray va, ca, ta[CORE_UNITS];
    LONG  lock_first, lock_count;   /* glLockArraysEXT, count 0 = none */

    /* the stream block: VRAM the locked arrays and their indices are
     * written to, from 0 again once the GPU is idle */
    ULONG stream_off;       /* heap offset */
    ULONG stream_size;      /* 0: no stream (arrays go the immediate way) */
    ULONG stream_pos;
    ULONG *stream_cpu;      /* where its dwords are written: the heap, or a
                             * back end's staging buffer (Core_SetVram) */
    ULONG stream_direct;    /* written into the heap itself, inside vram_begin/end */
    ULONG stream_gen;       /* counts Core_GpuIdle: the stream restarted at 0 */

    /* Video RAM a back end reaches only for a moment (a P96 bitmap that
     * moves when unlocked, docs/design-card-backend.md): texels are written
     * only between vram_begin, which returns where the heap is now (NULL:
     * out of reach, the upload is lost and counted), and vram_end. NULL
     * hooks: heap.cpu is always valid (the null card, the host tests). */
    ULONG *(*vram_begin)(APTR env);
    void (*vram_end)(APTR env);
    ULONG stat_vram_lost;
    ULONG lk_valid;         /* the locked range is in the stream */
    ULONG lk_gpu;           /* where */
    ULONG lk_units;         /* with these texture sets */
    ULONG no_fast;          /* tests: pack locked arrays the general way only */
    ULONG no_shadow;        /* tests: forget the shadow registers at every validate */
    ULONG lk_color;         /* and these current values (for disabled arrays) */
    float lk_st[CORE_UNITS][2];

    /* statistics for tests and the census */
    ULONG stat_prims, stat_verts, stat_splits, stat_state_emits, stat_idle_waits;
    ULONG stat_lock_uploads, stat_lock_draws, stat_stream_wraps;

    /* The upload cache (array.c): locked ranges and index lists kept in
     * video RAM across frames. With a staged stream (ustaged) its new
     * entries go into a fast-RAM mirror of the region, and the back end
     * copies the dirty ranges at its flush, inside its one lock
     * (Core_UCacheCopied). */
    ULONG ucache_off, ucache_size, ucache_pos, ucache_on;
    ULONG ustaged;
    ULONG *ushadow;                 /* CORE_UCACHE_BYTES of fast RAM, staged only */
    ULONG udirty_off[CORE_UDIRTY], udirty_len[CORE_UDIRTY];  /* bytes in the region */
    ULONG udirty_n, udirty_all;
    struct CoreUEntry uq[CORE_UQ];
    ULONG uq_head, uq_n;
    ULONG ufull_epoch;              /* the epoch in which UAlloc found no room */
    UWORD uslot[CORE_USLOTS];       /* uq index + 1, 0 = empty */
    ULONG no_ucache;                /* tests, RadeonGLUploadCache=0 */
    /* glHint(GL_RGL_STATIC_ARRAYS_HINT): GL_NICEST keys the world format
     * (positions + two texture units, no colour array) by the arrays'
     * addresses instead of their bytes -- the program promises they do not
     * change; GL_FASTEST forgets every such key (a new generation). */
    ULONG static_arrays, static_gen;
    /* glHint(GL_RGL_STATIC_INDICES_HINT): GL_NICEST keys 16-bit index lists
     * by address (and count, lock), unread; GL_DONT_CARE off; GL_FASTEST off
     * and every address key forgotten (the generation shared with arrays) */
    ULONG static_indices;
    ULONG static_tag;               /* glHint(GL_RGL_ARRAYS_TAG_HINT, tag): in the model
                                     * format's static key (a program that writes new
                                     * bytes at an address changes it) */
    /* 2026-11-06, glHint(GL_RGL_DYNAMIC_ARRAYS_HINT): GL_NICEST -- the
     * program says the next draws' vertices are new each time (an animated
     * model's interpolated frame): no key, no hashing, no place in the
     * upload cache, straight into the stream; GL_DONT_CARE or GL_FASTEST off */
    ULONG dynamic_arrays;
    ULONG stat_dyn_v;               /* vertex blocks streamed under that hint */
    ULONG stat_uc_hits, stat_uc_misses, stat_uc_saved, stat_uc_written;
    ULONG stat_uc_smiss_v, stat_uc_smiss_i;  /* misses while the static hints were on */
    ULONG stat_stream_bytes;    /* written into the stream (vertices + indices): bus bytes */
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

/* Room for the whole state and ndw dwords of draw packets, made BEFORE a
 * draw validates or computes an address: a flush -- and a frame the back
 * end drops, which asks for the whole state again (Core_Rebase) -- happens
 * here, never between the state and its draw. A macro: every draw passes
 * here (MEASURED: a call per draw cost GLQuake ~0.4 ms a frame). */
#define CORE_ROOM(c, ndw) ((void) R2_ROOM((c)->cs, CORE_STATE_MAX + (ndw)))

/* glScissor's box is empty while the scissor test is on: draws and clears
 * touch nothing (the R200's box cannot be empty: hwstate.c). */
ULONG Core_ScissorEmpty(struct Core *c);
#define CORE_SCISSOR_EMPTY(c) (((c)->enables & EN_SCISSOR) && Core_ScissorEmpty(c))

/* The host knows the GPU has finished everything emitted so far (a frame's
 * fence passed): every texture may be rewritten without waiting. */
void  Core_GpuIdle(struct Core *c);

/* A back end whose video RAM is reached only inside a lock: the hooks of
 * struct Core's vram_begin/vram_end (env as CoreEnv's), and a fast-RAM
 * staging buffer of CORE_STREAM_BYTES for the locked-array stream, which
 * the back end copies to the stream's place in the heap before the GPU
 * runs. staging NULL: the stream is written into the heap itself, one
 * vram_begin/vram_end around each locked draw's vertices and indices (the
 * GPU must be idle between the back end's flushes). MEASURED on the
 * A4000D: a staged dword is written to fast RAM, read back and written
 * across the bus, and fast RAM there streams at 13-16 MB/s; a lock pair
 * costs 18 us. */
void  Core_SetVram(struct Core *c, ULONG *(*begin)(APTR env), void (*end)(APTR env),
                   ULONG *staging);

/* A staged back end copied the upload cache's dirty ranges (udirty_*, or
 * all of ushadow when udirty_all) to the region in the heap: clear them. */
void  Core_UCacheCopied(struct Core *c);

/* The heap or the render target moved (P96 moved an unlocked bitmap): every
 * texture's GPU address follows the heap, the target takes its new
 * addresses, and the whole state is emitted again. What was emitted before
 * holds the old addresses: the back end drops it. */
void  Core_Rebase(struct Core *c, ULONG heap_gpu, ULONG color_gpu, ULONG depth_gpu);
/* 2026-11-06: a new render target for a resized context (mglResizeContext):
 * size, pitches, format and addresses; the state is emitted again. The heap
 * and what it holds stay. */
void  Core_Retarget(struct Core *c, const struct CoreTarget *t);

/* The GPU's state is gone (a reset of its 3D blocks), or what was emitted
 * never reached it (a dropped frame: Core_Rebase calls it): the whole state
 * is emitted again, and a draw being built sends it ahead of itself. */
void  Core_Forget(struct Core *c);

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

/* The hot path as IEEE single bit patterns (r2cs.h R2_FBITS): a vertex
 * that never passes through an FPU register. On the A4000D's 060 every
 * FPU instruction the old path spent on moving floats around (load, store
 * back for the next call) cost more than the integer work. */
void Core_VertexBits(struct Core *c, ULONG x, ULONG y, ULONG z);
#define CORE_TEXCOORD_BITS(c, u, s, t) \
    (((ULONG *) (void *) (c)->cur_st[u])[0] = (s), ((ULONG *) (void *) (c)->cur_st[u])[1] = (t))
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
void Core_GenTextures(struct Core *c, LONG n, ULONG *names);
ULONG Core_IsTexture(struct Core *c, ULONG name);
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

/* The screen a frame is presented to: a P96 bitmap's VRAM. */
struct CoreScreen
{
    ULONG gpu;              /* GPU address of the bitmap's first pixel (16-byte aligned) */
    ULONG pitch;            /* pixels, a multiple of 8 */
    ULONG format;           /* R_COLOR_FORMAT_RGB565 or _ARGB8888 */
    ULONG width, height;
};

/* Copy the target's colour buffer 1:1 to (x, y) on the screen with the 3D
 * engine (concept 6.7; Mesa r200_blit): flush the colour cache, wait for
 * the 3D engine, draw the back buffer as a texture into the screen. The
 * client's state comes back at the next draw. */
void Core_Present(struct Core *c, const struct CoreScreen *s, LONG x, LONG y);

/* --- Queries (get.c) --------------------------------------------------------------- */

void Core_GetFloatv(struct Core *c, ULONG pname, float *out);
ULONG Core_GetCount(ULONG pname);    /* values a glGet of pname returns */
void Core_GetIntegerv(struct Core *c, ULONG pname, LONG *out);
const char *Core_GetString(struct Core *c, ULONG name);

/* --- Internals shared between the core's files ---------------------------------- */

ULONG Hw_DepthFunc(ULONG glfunc);   /* GL compare function -> R200 0..7, ~0 if invalid */
ULONG Hw_BlendFactor(ULONG glf);    /* GL blend factor -> R_BLEND_GL_*, 0 if invalid */
void  Hw_VertexFormat(struct Core *c, ULONG units);   /* emit when it differs */
void  Hw_Forget(struct Core *c);    /* the GPU's state is unknown: emit everything again */
ULONG Core_UnitsOn(struct Core *c); /* units that texture the next draw */

LONG  Heap_Init(struct CoreHeap *h, ULONG gpu, ULONG *cpu, ULONG size);
ULONG Heap_Alloc(struct Core *c, ULONG size, ULONG align);   /* byte offset, or ~0 */
void  Heap_Free(struct Core *c, ULONG offset);
void  Heap_FreeAll(struct Core *c);
ULONG Heap_Top(const struct Core *c);  /* end of the highest allocation, bytes */

#endif /* CORE_CORE_H */
