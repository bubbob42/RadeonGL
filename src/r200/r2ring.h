/*
 * r2ring.h -- the write side of the CP ring buffer in VRAM.
 *
 * The ring is a power-of-two number of dwords at a 4 KB aligned GPU
 * address, written by the CPU through the aperture. Someone else (the CP
 * set-up: microcode, CP_RB_BASE, CP_RB_CNTL with RB_NO_UPDATE) has started
 * the CP with read and write pointer at 0; from then on this module owns
 * CP_RB_WPTR:
 *
 *  - Packets never straddle the end of the ring: a packet that does not
 *    fit is preceded by filler up to the end (one type-3 NOP whose payload
 *    the CP skips, so the filler costs one bus write; PACKET2 dwords with
 *    R2RING_TYPE2_FILL).
 *  - The write pointer moves in 16-dword steps, padded the same way (Linux
 *    r100 ring align_mask 15; mirq pads to 16 as well).
 *  - Before CP_RB_WPTR is written, the last ring dword is read back
 *    through the aperture: the read pushes the bridge's posted writes
 *    ahead of the pointer update (mirq, measured on the Prometheus).
 *  - Free space comes from CP_RB_RPTR, read over MMIO only when the room
 *    already known runs out (no write-back: nothing is mastered into
 *    Amiga memory).
 *  - Fences: R2E_Fence's sequence number in one scratch register, polled
 *    over MMIO.
 *
 * Every wait is bounded (timeout_us); a timeout is reported, never spun
 * through. The hardware is reached through struct R2Hw so the host tests
 * can drive the same code against a simulated CP.
 */

#ifndef R2RING_H
#define R2RING_H

#include <exec/types.h>
#include "r2cs.h"

struct R2Hw
{
    ULONG (*read_reg)(APTR hw, ULONG reg);
    void  (*write_reg)(APTR hw, ULONG reg, ULONG val);
    /* read one ring dword back through the aperture; the value is unused,
     * the read is what matters */
    ULONG (*read_back)(APTR hw, volatile ULONG *p);
    /* free-running microsecond clock; wraps */
    ULONG (*clock_us)(APTR hw);
    APTR  hw;
};

#define R2RING_ALIGN       16      /* dwords per write-pointer step */
#define R2RING_TYPE2_FILL  0x0001  /* filler and padding as PACKET2 only */

struct R2RingStats
{
    ULONG commits;      /* CP_RB_WPTR writes */
    ULONG rptr_reads;   /* CP_RB_RPTR reads */
    ULONG fence_reads;  /* fence register reads */
    ULONG stalls;       /* times the writer had to wait for the GPU */
    ULONG wraps;        /* filler to the ring end */
    ULONG fill_dw;      /* dwords skipped by wrap filler */
    ULONG pad_dw;       /* dwords of commit padding */
};

struct R2Ring
{
    struct R2Cs cs;          /* first: the emitter writes through it */
    ULONG      *base;        /* CPU address of the ring */
    ULONG       gpu;         /* GPU address of the ring */
    ULONG       size;        /* dwords, a power of two */
    ULONG       committed;   /* dword index last written to CP_RB_WPTR */
    ULONG       rptr;        /* CP_RB_RPTR as last read */
    ULONG       kick;        /* commit when this many dwords wait (0: only on request) */
    ULONG       timeout_us;
    ULONG       flags;
    ULONG       fence_reg;
    ULONG       fence_seq;   /* last fence emitted */
    ULONG       fence_done;  /* last fence value read back */
    const struct R2Hw *hw;
    struct R2RingStats stats;
};

/* Set up the writer for a CP that is running with RPTR = WPTR = 0. size
 * is in dwords (a power of two, at least 1024); gpu must be 4 KB aligned.
 * The fence register's current value becomes the fence baseline (it is
 * never written here). Returns R2_OK or R2_EARG. */
LONG R2Ring_Init(struct R2Ring *r, ULONG *base, ULONG gpu, ULONG size,
                 const struct R2Hw *hw, ULONG swap, ULONG fence_reg,
                 ULONG timeout_us);

/* Hand everything written so far to the CP. */
LONG R2Ring_Commit(struct R2Ring *r);

/* Emit a fence; its sequence number goes to *seq. Not committed yet. */
LONG R2Ring_Fence(struct R2Ring *r, ULONG *seq);

/* TRUE when the GPU has passed fence seq (reads the register only when
 * the last value read does not already say so). */
BOOL R2Ring_FenceDone(struct R2Ring *r, ULONG seq);

/* Commit if needed and wait until fence seq has passed. */
LONG R2Ring_WaitFence(struct R2Ring *r, ULONG seq);

/* Fence, commit and wait: the GPU is idle afterwards. */
LONG R2Ring_Idle(struct R2Ring *r);

#endif /* R2RING_H */
