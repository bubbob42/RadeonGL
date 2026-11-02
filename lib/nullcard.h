/*
 * nullcard.h -- the null card: a Radeon that is not there.
 *
 * Milestone 2 runs the real library (core, emitter, ring writer) in FS-UAE
 * with no Radeon. The null card stands behind the ring's struct R2Hw the
 * way the chip would: CP_RB_WPTR writes hand it work, CP_RB_RPTR and fence
 * reads let it "run". It reads every committed packet and puts it through
 * the packet checker (src/r200/r2check.h), so each frame GLQuake draws is
 * checked as the GPU would read it. "VRAM" is fast RAM: the ring and the
 * texture heap are real memory, the colour and depth buffers only
 * addresses (nothing is ever drawn).
 */

#ifndef RADEONGL_NULLCARD_H
#define RADEONGL_NULLCARD_H

#include <exec/types.h>
#include "r2ring.h"
#include "r2check.h"

struct NullCard
{
    ULONG *ring;
    ULONG  size;            /* dwords */
    ULONG  swap;
    ULONG  wptr;
    ULONG  rptr;
    ULONG  fence_reg;
    ULONG  fence_val;
    ULONG  clock;
    ULONG  packets;
    ULONG  dwords;
    ULONG  bad_align;       /* WPTR not a multiple of 16 */
    ULONG  straddle;        /* a packet across the ring end or past WPTR */
    ULONG  errors;          /* checker errors */
    struct R2ChkError first;
    LONG   first_code;
    struct R2ChkCfg cfg;
    struct R2ChkState st;
    struct R2Hw hw;
    /* recording (the frame dump): every packet read is appended here */
    ULONG *rec;
    ULONG  rec_n;
    ULONG  rec_max;
    ULONG  rec_lost;        /* packets that did not fit */
    ULONG  no_check;        /* consume without the checker (to time the emitter alone) */
};

void NullCard_Init(struct NullCard *n, ULONG *ring, ULONG size, ULONG swap,
                   ULONG fence_reg, const struct R2ChkCfg *cfg);

#endif /* RADEONGL_NULLCARD_H */
