/*
 * r2check.h -- the packet checker: reads a command stream the way the CP
 * would and refuses anything RadeonGL has no business sending.
 *
 * A wrong packet does not crash the Radeon politely: the CP or the vertex
 * fetcher hangs, Radeon.card's WaitBlitter then spins on a busy engine and
 * the whole Amiga freezes. So every stream is checked on the host (test
 * suite, simulated CP), and the Amiga debug build checks each batch before
 * it moves CP_RB_WPTR.
 *
 * Model: Linux 6.6 r100_cs_parse / r200_packet0_check / r100_cs_track_check
 * (the kernel's checker for user command streams), narrowed to what
 * RadeonGL emits:
 *
 *  - type-0 writes only to a whitelist of 3D registers, the fence
 *    register and the cache/sync registers -- never the 2D engine (it is
 *    Radeon.card's), the CP's own registers, the memory controller or the
 *    display;
 *  - type-3 packets: NOP, 3D_LOAD_VBPNTR, INDX_BUFFER, 3D_DRAW_VBUF_2,
 *    3D_DRAW_IMMD_2, 3D_DRAW_INDX_2; no indirect buffers;
 *  - every GPU address inside a memory range of the right kind (ring,
 *    vertices, indices, textures, colour, depth), aligned;
 *  - at every draw: a vertex format, a payload that matches it, a
 *    primitive with a vertex count it can take, arrays that hold every
 *    vertex the draw can reach (with a VRAM reader: every index checked),
 *    targets and enabled textures that fit their ranges.
 *
 * The checker keeps the register state it has seen (struct R2ChkState) so
 * a stream can be checked in pieces, batch after batch. A packet must not
 * straddle two pieces. Pure C89, no OS calls, no formatting: errors are
 * codes plus numbers; R2Chk_ErrorName / R2Chk_RegName give the words.
 */

#ifndef R2CHECK_H
#define R2CHECK_H

#include <exec/types.h>

/* Kinds of GPU memory. */
#define R2K_RING     0x01
#define R2K_VERTEX   0x02
#define R2K_INDEX    0x04
#define R2K_TEXTURE  0x08
#define R2K_COLOR    0x10
#define R2K_DEPTH    0x20

struct R2ChkRange
{
    ULONG gpu;      /* first byte */
    ULONG size;     /* bytes */
    ULONG kinds;    /* R2K_* that may live here */
};

#define R2CHK_MAX_RANGES 16

struct R2ChkCfg
{
    struct R2ChkRange range[R2CHK_MAX_RANGES];
    ULONG nranges;
    ULONG fence_reg;        /* the one scratch register a stream may write */
    /* Optional: the dword at GPU address gpu, as the CPU stored it
     * (stream byte order). With it, every index of an indexed draw is
     * read and checked; without it, only the declared bounds are. */
    LONG (*peek)(APTR ctx, ULONG gpu, ULONG *val);
    APTR peek_ctx;
};

/* Errors (R2Chk_Run's result; 0 = clean). */
#define R2CE_OK        0
#define R2CE_TRUNC     1    /* a packet runs past the data */
#define R2CE_TYPE      2    /* a type-1 packet */
#define R2CE_REG       3    /* a register outside the whitelist */
#define R2CE_ONEREG    4    /* ONE_REG_WR to a register that is no data port */
#define R2CE_OPCODE    5    /* a type-3 opcode RadeonGL does not send */
#define R2CE_ADDR      6    /* an address outside every range of its kind */
#define R2CE_ALIGN     7    /* a misaligned address or pitch */
#define R2CE_FORMAT    8    /* no vertex format / VAP set-up, or a format we do not use */
#define R2CE_WALK      9    /* the walk does not match the draw packet */
#define R2CE_SIZE     10    /* payload size does not match the vertices or arrays */
#define R2CE_PRIM     11    /* a primitive type, or a vertex count it cannot take */
#define R2CE_ARRAYS   12    /* arrays missing, too short, or not the vertex size */
#define R2CE_INDX     13    /* DRAW_INDX_2 and INDX_BUFFER not paired, or a bad INDX_BUFFER */
#define R2CE_INDEX    14    /* an index past the arrays */
#define R2CE_TARGET   15    /* a colour or depth target missing or too small */
#define R2CE_TEXTURE  16    /* an enabled texture missing or outside texture memory */
#define R2CE_TCLMEM   17    /* a vector or scalar write outside TCL memory */
#define R2CE_FENCE    18    /* a scratch register other than the fence register */
#define R2CE_VALUE    19    /* a register value RadeonGL never sends */

struct R2ChkError
{
    ULONG at;       /* dword index of the offending packet (at0 + offset) */
    LONG  code;     /* R2CE_* */
    ULONG what;     /* the register, opcode or address concerned */
    ULONG value;    /* the value found */
};

struct R2ChkTex
{
    ULONG filter, format, size, pitch, offset;
    ULONG have_offset;
    ULONG format_x;
};

struct R2ChkAos
{
    ULONG comps, stride, gpu;
};

struct R2ChkState
{
    /* registers seen (have: one bit each, R2CHK_HAVE_*) */
    ULONG have;
    ULONG vtx_fmt0, vtx_fmt1;
    ULONG vap_cntl;
    ULONG rb3d_cntl;
    ULONG color_offset, color_pitch;
    ULONG depth_offset, depth_pitch;
    ULONG zstencil;
    ULONG re_top_left, re_bottom_right;
    ULONG re_cntl;
    ULONG pp_cntl;
    ULONG max_vtx_indx;
    struct R2ChkTex tex[6];
    ULONG naos;
    struct R2ChkAos aos[12];
    ULONG vec_addr;         /* next TCL vector dword (in dwords) */
    ULONG scal_addr;        /* next TCL scalar */
    ULONG pend_indx;        /* index count of a DRAW_INDX_2 awaiting its INDX_BUFFER */
    ULONG pend_cntl;
    ULONG pend_at;
    /* statistics */
    ULONG packets;
    ULONG draws;
    ULONG vertices;         /* vertices drawn (immediate) or indices walked */
    ULONG dw_reg;           /* dwords in register writes */
    ULONG dw_draw;          /* dwords in draw and array packets */
    ULONG dw_nop;           /* filler */
    ULONG index_unchecked;  /* indexed draws whose indices could not be read */
};

#define R2CHK_HAVE_FMT0     0x0001
#define R2CHK_HAVE_FMT1     0x0002
#define R2CHK_HAVE_VAP      0x0004
#define R2CHK_HAVE_RB3D     0x0008
#define R2CHK_HAVE_COLOR    0x0010
#define R2CHK_HAVE_CPITCH   0x0020
#define R2CHK_HAVE_DEPTH    0x0040
#define R2CHK_HAVE_DPITCH   0x0080
#define R2CHK_HAVE_ZSTENCIL 0x0100
#define R2CHK_HAVE_SCISSOR  0x0200
#define R2CHK_HAVE_PPCNTL   0x0400
#define R2CHK_HAVE_MAXINDX  0x0800
#define R2CHK_HAVE_VEC      0x1000
#define R2CHK_HAVE_SCAL     0x2000
#define R2CHK_HAVE_RECNTL   0x4000

void R2Chk_Reset(struct R2ChkState *st);

/* The end of what goes to the CP (a flush): R2CE_INDX when a DRAW_INDX_2
 * still waits for its INDX_BUFFER -- the vertex fetcher would wait for
 * indices that never come. */
LONG R2Chk_End(struct R2ChkState *st, struct R2ChkError *err);

/* Check n dwords (stored in the stream's byte order: swap as the stream's
 * cs->swap). at0 numbers the first one in error reports. Returns R2CE_OK
 * or the first error, described in *err. */
LONG R2Chk_Run(const struct R2ChkCfg *cfg, struct R2ChkState *st,
               const ULONG *dw, ULONG n, ULONG swap, ULONG at0,
               struct R2ChkError *err);

/* The vertex size in dwords of SE_VTX_FMT_0/1 (Linux r200_get_vtx_size_0
 * and _1, written out again here on purpose: the emitter has its own). */
ULONG R2Chk_VtxDwords(ULONG fmt0, ULONG fmt1);

const char *R2Chk_ErrorName(LONG code);

/* The whitelisted register's name ("PP_TXFORMAT_n" for unit registers),
 * or NULL when reg is not on the list. */
const char *R2Chk_RegName(ULONG reg);

#endif /* R2CHECK_H */
