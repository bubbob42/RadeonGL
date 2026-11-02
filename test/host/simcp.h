/*
 * simcp.h -- a simulated command processor for the ring tests.
 *
 * It stands behind struct R2Hw (src/r200/r2ring.h) the way the Radeon
 * stands behind MMIO: CP_RB_WPTR writes hand it work, CP_RB_RPTR and
 * fence-register reads let it run. It consumes the ring packet by packet,
 * `rate` dwords per poll, and for every packet it reads:
 *
 *  - runs the packet checker (src/r200/r2check.h) on it;
 *  - refuses a packet that straddles the ring end or reaches past the
 *    committed write pointer;
 *  - compares it with the copy taken when it was committed: a difference
 *    means the CPU wrote over work the CP had not read yet;
 *  - executes the fence register writes.
 *
 * Each CP_RB_WPTR write must be 16-dword aligned and preceded by a read
 * back of the dword just below it.
 */

#ifndef SIMCP_H
#define SIMCP_H

#include <exec/types.h>
#include "r2ring.h"
#include "r2check.h"

struct SimCp
{
    ULONG *ring;
    ULONG  size;
    ULONG  swap;
    ULONG  wptr;
    ULONG  rptr;
    ULONG  fence_reg;
    ULONG  fence_val;
    ULONG  rate;            /* dwords consumed per poll; 0 = a hung GPU */
    ULONG  clock;
    ULONG  last_readback;   /* ring index of the last read back, or ~0 */
    ULONG *shadow;          /* the ring as committed */
    ULONG  consumed;        /* dwords the CP has read */
    ULONG  packets;
    ULONG  wptr_writes;
    /* failures (each counted; the first one described) */
    ULONG  bad_align;       /* WPTR not a multiple of 16 */
    ULONG  bad_order;       /* WPTR written without the read-back first */
    ULONG  straddle;        /* a packet across the ring end or past WPTR */
    ULONG  overwritten;     /* pending work changed before the CP read it */
    ULONG  chk_errors;
    struct R2ChkError first;
    struct R2ChkCfg cfg;
    struct R2ChkState st;
    struct R2Hw hw;
};

void SimCp_Init(struct SimCp *s, ULONG *ring, ULONG *shadow, ULONG size,
                ULONG swap, ULONG fence_reg, ULONG fence_start, ULONG rate,
                const struct R2ChkCfg *cfg);

/* Run until everything committed has been read. */
void SimCp_Drain(struct SimCp *s);

/* Every failure counter is still 0. */
int SimCp_Clean(const struct SimCp *s);

#endif
