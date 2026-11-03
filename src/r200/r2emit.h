/*
 * r2emit.h -- R200 command packets: register writes, TCL constants, the
 * draw packets and the fence.
 *
 * Every function writes one complete packet sequence into a struct R2Cs
 * (r2cs.h) or nothing at all, and returns R2_OK or the stream's error.
 * Layouts follow Mesa 21.3 r200 (r200_cmdbuf.c, r200_state_init.c,
 * r200_blit.c) and Linux 6.6 r100.c; the packet checker (r2check.h) is
 * the second, independent reading of the same rules.
 *
 * Values that the GPU reads as data -- vertices, matrices, indices -- go
 * through R2_OUT like every other dword, so they land in VRAM in the
 * stream's byte order. Index pairs are packed (second << 16) | first:
 * the GPU reads the low half of a little-endian dword first (Mesa
 * EMIT_TWO_ELTS).
 */

#ifndef R2EMIT_H
#define R2EMIT_H

#include <exec/types.h>
#include "r2cs.h"

/* --- Plain register writes ------------------------------------------------- */

LONG R2E_Reg(struct R2Cs *cs, ULONG reg, ULONG val);

/* n values to n consecutive registers starting at reg (1 <= n <= 0x4000). */
LONG R2E_Regs(struct R2Cs *cs, ULONG reg, ULONG n, const ULONG *vals);

/* n values, all to the one register reg (ONE_REG_WR). */
LONG R2E_RegOne(struct R2Cs *cs, ULONG reg, ULONG n, const ULONG *vals);

/* Exactly ndw dwords of filler. With skip_ok, two or more are ONE type-3
 * NOP whose payload is skipped and never written; otherwise (and for a
 * single dword) one PACKET2 per dword. */
LONG R2E_Nop(struct R2Cs *cs, ULONG ndw, ULONG skip_ok);

/* --- TCL constant memory --------------------------------------------------- */

/* n dwords (a multiple of 4) into vector memory from vector addr on,
 * after a TCL state flush (Mesa OUT_VEC). */
LONG R2E_Vectors(struct R2Cs *cs, ULONG addr, ULONG n, const ULONG *vals);

/* n dwords into scalar memory from addr on (Mesa OUT_SCL). */
LONG R2E_Scalars(struct R2Cs *cs, ULONG addr, ULONG n, const ULONG *vals);

/* A GL matrix (16 floats, column-major as OpenGL keeps it) into the
 * vector slot vec (R_VS_MATRIX_*): the TCL unit takes it row by row
 * (Mesa upload_matrix). */
LONG R2E_Matrix(struct R2Cs *cs, ULONG vec, const float *m);

/* --- State blocks (values: Mesa r200InitState) ------------------------------ */

/* Vertex assembly and TCL: TCL on, viewport transform on, the default
 * matrix selection and input vector addresses, lighting off, clip space
 * user planes, the guard band. The vertex fetcher swaps (VC_32BIT_SWAP)
 * exactly when the stream does not (cs->swap clear: choice b). */
LONG R2E_TclDefaults(struct R2Cs *cs);

/* Rasteriser, pixel pipe and render backend: GL defaults (depth LESS +
 * write, blend ONE/ZERO, alpha test off, no culling, scissor on,
 * perspective-correct texturing, all planes written), combiner stages 0
 * and 1 passing the diffuse colour. RB3D_CNTL, PP_CNTL and the targets
 * are the state layer's. */
LONG R2E_RasterDefaults(struct R2Cs *cs);

/* GL viewport x, y, w, h (lower-left origin) and depth range zn..zf, for
 * a target fb_height pixels high (the R200 counts rows from the top). */
LONG R2E_Viewport(struct R2Cs *cs, float x, float y, float w, float h,
                  float zn, float zf, ULONG fb_height);

/* Scissor rectangle in target pixels, top-left origin, both corners
 * INCLUSIVE (RE_TOP_LEFT, RE_WIDTH_HEIGHT as Mesa uses them). */
LONG R2E_Scissor(struct R2Cs *cs, ULONG x1, ULONG y1, ULONG x2, ULONG y2);

/* Colour target: GPU address (16-byte aligned; 2 KB when tiled), pitch in
 * pixels (a multiple of 8), pitch flags (R_COLOR_TILE_ENABLE ...). */
LONG R2E_ColorTarget(struct R2Cs *cs, ULONG gpu, ULONG pitch_px, ULONG flags);

/* Depth target: GPU address, pitch in pixels (a multiple of 8). */
LONG R2E_DepthTarget(struct R2Cs *cs, ULONG gpu, ULONG pitch_px);

/* One texture unit's registers, as the state layer computed them. */
struct R2Tex
{
    ULONG filter;     /* PP_TXFILTER */
    ULONG format;     /* PP_TXFORMAT */
    ULONG format_x;   /* PP_TXFORMAT_X */
    ULONG size;       /* PP_TXSIZE (NPOT) */
    ULONG pitch;      /* PP_TXPITCH (NPOT) */
    ULONG multi;      /* PP_TXMULTI_CTL */
    ULONG offset;     /* PP_TXOFFSET: GPU address | swap and tiling bits */
};

LONG R2E_Texture(struct R2Cs *cs, ULONG unit, const struct R2Tex *t);

/* Combiner stage (0..7): TXCBLEND, TXCBLEND2, TXABLEND, TXABLEND2. */
LONG R2E_Combiner(struct R2Cs *cs, ULONG stage, ULONG cblend, ULONG cblend2,
                  ULONG ablend, ULONG ablend2);

/* Input vertex format (SE_VTX_FMT_0/1), TCL output format and component
 * selection. */
LONG R2E_VertexFormat(struct R2Cs *cs, ULONG fmt0, ULONG fmt1,
                      ULONG out0, ULONG out1, ULONG compsel);

/* Dwords one vertex of format fmt0/fmt1 takes (Linux r200_get_vtx_size_0
 * and _1). */
ULONG R2E_VtxDwords(ULONG fmt0, ULONG fmt1);

/* --- Draws -------------------------------------------------------------------
 *
 * cntl is the draw's control dword without the walk and the vertex count:
 * a primitive (R_VF_PRIM_*) | R_VF_COLOR_ORDER_RGBA | R_VF_TCL_OUTPUT_VTX_ENABLE
 * (when TCL is on). The functions add the walk and the count.
 */

/* Most vertices one 3D_DRAW_IMMD_2 packet carries at vtx_dw dwords each. */
#define R2E_IMMD_MAX_VERTS(vtx_dw)  (0x3fffUL / (vtx_dw))

/* Open an immediate packet for up to max_verts vertices of vtx_dw dwords:
 * returns where the first vertex goes. The caller stores vertex dwords
 * there with R2_OUT and hands the end pointer to R2E_ImmdClose. Nothing
 * else may be emitted in between. */
ULONG *R2E_ImmdOpen(struct R2Cs *cs, ULONG vtx_dw, ULONG max_verts);

/* Close it: header and control dword are written now. A packet with no
 * vertex leaves no trace. */
LONG R2E_ImmdClose(struct R2Cs *cs, ULONG *vend, ULONG cntl);

/* The same for a caller that knows its vertex count (no division: a
 * 32-bit DIVU costs 38 cycles on the 060, and glEnd paid two). */
LONG R2E_ImmdCloseN(struct R2Cs *cs, ULONG nverts, ULONG cntl);

/* Open, copy nverts vertices from data (host order) and close. */
LONG R2E_Immd(struct R2Cs *cs, ULONG cntl, ULONG vtx_dw, ULONG nverts,
              const ULONG *data);

/* One vertex array (AOS): components per vertex and stride, both in
 * dwords, and the GPU address of the first vertex. */
struct R2Aos
{
    ULONG comps;
    ULONG stride;
    ULONG gpu;
};

#define R2E_MAX_AOS 12      /* SE_VTX_AOS_ADDR0..11 (Mesa r200_reg.h) */

LONG R2E_LoadVbpntr(struct R2Cs *cs, ULONG n, const struct R2Aos *aos);

/* nverts vertices straight from the arrays. */
LONG R2E_DrawVbuf(struct R2Cs *cs, ULONG cntl, ULONG nverts);

/* nidx 16-bit indices at idx_gpu (4-byte aligned) into arrays that hold
 * array_verts vertices (written to SE_VF_MAX_VTX_INDX first, as Mesa
 * does). */
LONG R2E_DrawIndexed(struct R2Cs *cs, ULONG cntl, ULONG nidx, ULONG idx_gpu,
                     ULONG array_verts);

/* --- Sync ------------------------------------------------------------------- */

/* Flush the colour and depth caches, wait for the 2D and 3D engines to be
 * idle and clean, then write seq to scratch_reg (Linux
 * r100_fence_ring_emit without the HDP flush and the interrupt). */
LONG R2E_Fence(struct R2Cs *cs, ULONG scratch_reg, ULONG seq);

#define R2E_FENCE_DWORDS 8

#endif /* R2EMIT_H */
