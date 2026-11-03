/*
 * r2cs.h -- a command stream: where the emitter's dwords go.
 *
 * The emitter (r2emit.h) writes packets through a struct R2Cs and never
 * knows what is behind it: the CP ring in VRAM (r2ring.h), an indirect
 * buffer, a display list, or a plain array in a host test. The owner
 * grants room [ptr, end); when a packet does not fit, the emitter asks the
 * owner through need(), which may wait for the GPU, wrap the ring or
 * fail.
 *
 * Byte order (concept section 6.3): the Radeon reads little-endian dwords
 * and the Mediator does not swap. With swap set, every dword is stored
 * byte-reversed by the CPU (choice a); with swap clear it is stored as the
 * 68k holds it and the GPU must swap (choice b: CP_RB_CNTL BUF_SWAP_32BIT
 * for the ring, SE_VAP_CNTL_STATUS VC_32BIT_SWAP for vertex fetch). Which
 * one holds on the A4000D is measured, not assumed. The packet LAYOUT is
 * the same either way; the checker (r2check.h) reads a stream back in the
 * orientation it was written.
 *
 * Pure C89, no OS calls: builds for the Amiga and for the host tests.
 */

#ifndef R2CS_H
#define R2CS_H

#include <exec/types.h>

/* Results. 0 is success; failures are negative and sticky in cs->error. */
#define R2_OK          0
#define R2_EFULL      -1    /* a fixed buffer has no room left */
#define R2_ETIMEOUT   -2    /* the GPU did not free ring space in time */
#define R2_ETOOBIG    -3    /* more than the owner can ever grant at once */
#define R2_EARG       -4    /* a request the packet format cannot carry */
#define R2_ESTATE     -5    /* an immediate packet is (not) open */

struct R2Cs;

/* Grant at least ndw contiguous dwords at cs->ptr (the owner may move
 * cs->ptr, for instance to the start of the ring). Returns R2_OK and sets
 * cs->end, or a failure. Called only between packets. */
typedef LONG (*R2CsNeed)(struct R2Cs *cs, ULONG ndw);

struct R2Cs
{
    ULONG    *ptr;        /* next dword to write */
    ULONG    *end;        /* end of the room granted */
    ULONG     swap;       /* nonzero: store dwords byte-reversed */
    R2CsNeed  need;       /* NULL: a fixed buffer, no more room ever */
    APTR      owner;      /* the need() function's own data */
    LONG      error;      /* first failure, R2_OK while none */
    ULONG    *immd;       /* an open 3D_DRAW_IMMD_2 packet's header, or NULL */
    ULONG     immd_vtx;   /* its vertex size in dwords */
};

/* A fixed buffer of ndw dwords: an IB, a display list, a test array. */
void R2Cs_InitBuffer(struct R2Cs *cs, ULONG *buf, ULONG ndw, ULONG swap);

/* Room for ndw dwords at the returned pointer (== cs->ptr afterwards), or
 * NULL with cs->error set. The caller writes, then advances cs->ptr. */
ULONG *R2Cs_Room(struct R2Cs *cs, ULONG ndw);

ULONG R2Cs_Swap32(ULONG v);

/* IEEE single bits of a float (the CP takes vertex and matrix floats as
 * their 32-bit patterns). */
ULONG R2Cs_FloatBits(float f);

/* A value as it is stored in this stream. On GCC for the 68k the swap is
 * three register instructions in line (a call per dword was most of a
 * state packet's cost on the 060); elsewhere the function. */
#if defined(__GNUC__) && defined(__mc68000__)
static __inline__ ULONG R2Cs_Out(ULONG swap, ULONG v)
{
    if (swap)
        __asm__("rol.w #8,%0\n\tswap %0\n\trol.w #8,%0" : "+d" (v));
    return v;
}
#define R2_OUT(cs, v)  R2Cs_Out((cs)->swap, (ULONG) (v))
#else
#define R2_OUT(cs, v)  ((cs)->swap ? R2Cs_Swap32((ULONG) (v)) : (ULONG) (v))
#endif

/* R2Cs_Room without the call when the room is there (n evaluated twice). */
#define R2_ROOM(cs, n) (((cs)->error == R2_OK && (ULONG) ((cs)->end - (cs)->ptr) >= (ULONG) (n)) \
                        ? (cs)->ptr : R2Cs_Room((cs), (n)))

/* The hot path (one glVertex: 4 to 8 dwords) without calls.
 *
 * R2_FBITS(f): the bits of a float VARIABLE (an lvalue), read in place.
 * Type punning through a pointer: the GCC builds pass -fno-strict-aliasing
 * (Makefile), SAS/C does not reason about aliasing.
 *
 * R2_SWAPI(v): v byte-reversed, in place (v a ULONG variable). On GCC for
 * the 68k three register instructions; elsewhere the function. */
#define R2_FBITS(f)    (*(const ULONG *) (const void *) &(f))

#if defined(__GNUC__) && defined(__mc68000__)
#define R2_SWAPI(v)    __asm__("rol.w #8,%0\n\tswap %0\n\trol.w #8,%0" : "+d" (v))
#else
#define R2_SWAPI(v)    ((v) = R2Cs_Swap32(v))
#endif

#endif /* R2CS_H */
