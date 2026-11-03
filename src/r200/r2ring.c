/*
 * r2ring.c -- see r2ring.h.
 *
 * Positions are dword indices into the ring. w is where the next dword
 * goes, committed is what the CP was told, rptr is how far the CP has read
 * (as last seen). The CP owns [rptr, committed); [committed, w) is written
 * but not handed over; everything from w up to rptr - 1 is free. One dword
 * always stays free so that a full ring never looks empty (RPTR == WPTR).
 */

#include "r2ring.h"
#include "r2emit.h"
#include "r2pm4.h"
#include "r200reg.h"

#define POS(r)  ((ULONG) ((r)->cs.ptr - (r)->base))

/* The writer is at the very end of the ring: continue at its start. Only
 * between packets. */
static void Normalise(struct R2Ring *r)
{
    if (POS(r) >= r->size)
        r->cs.ptr = r->base;
}

static ULONG Free(const struct R2Ring *r, ULONG w)
{
    return (r->rptr - w - 1) & (r->size - 1);
}

static ULONG Pending(const struct R2Ring *r, ULONG w)
{
    return (w - r->committed) & (r->size - 1);
}

/* Contiguous dwords that may be written at w. When the free space ends at
 * the read pointer, R2RING_ALIGN - 1 of it stay back for the commit
 * padding; when it ends at the ring end, padding never crosses that (the
 * size is a multiple of the step). */
static ULONG Grant(const struct R2Ring *r, ULONG w)
{
    ULONG to_rptr;
    ULONG to_end;

    to_rptr = Free(r, w);
    to_end = r->size - w;
    if (to_end <= to_rptr)
        return to_end;
    if (to_rptr < R2RING_ALIGN - 1)
        return 0;
    return to_rptr - (R2RING_ALIGN - 1);
}

/* Filler of exactly n dwords at p, outside the emitter's room logic. */
static void Fill(struct R2Ring *r, ULONG *p, ULONG n)
{
    struct R2Cs tmp;

    R2Cs_InitBuffer(&tmp, p, n, r->cs.swap);
    R2E_Nop(&tmp, n, (r->flags & R2RING_TYPE2_FILL) == 0);
}

/* CP_RB_RPTR moves from where it was towards what was committed, never
 * past it: anything else (a failed bus read gives 0xffffffff) is no news,
 * not space the CP has not fetched yet (audit 2026-11-03). */
static void ReadRptr(struct R2Ring *r)
{
    ULONG v;

    v = r->hw->read_reg(r->hw->hw, R_CP_RB_RPTR) & (r->size - 1);
    r->stats.rptr_reads++;
    if (((v - r->rptr) & (r->size - 1)) <= ((r->committed - r->rptr) & (r->size - 1)))
        r->rptr = v;
}

/* The writer has to wait for the GPU: read CP_RB_RPTR again, unless the
 * wait has lasted timeout_us already. */
static LONG Progress(struct R2Ring *r, int *stalled, ULONG *start)
{
    if (!*stalled)
    {
        *stalled = 1;
        r->stats.stalls++;
        *start = r->hw->clock_us(r->hw->hw);
    }
    else if ((ULONG) (r->hw->clock_us(r->hw->hw) - *start) > r->timeout_us)
        return R2_ETIMEOUT;
    ReadRptr(r);
    return R2_OK;
}

/* One loop decides everything again after each commit: a commit pads the
 * write position forward, which can turn "fits before the ring end" into
 * "does not" (a wait for room the end could never give would only time
 * out). */
static LONG Need(struct R2Cs *cs, ULONG ndw)
{
    struct R2Ring *r;
    ULONG w;
    ULONG avail;
    ULONG cap;
    ULONG start;
    int stalled;
    LONG err;

    r = (struct R2Ring *) cs->owner;
    if (cs->immd != NULL)
        return R2_ESTATE;
    if (ndw > r->size / 2)
        return R2_ETOOBIG;
    Normalise(r);
    if (r->kick != 0 && Pending(r, POS(r)) >= r->kick)
    {
        err = R2Ring_Commit(r);
        if (err != R2_OK)
            return err;
    }
    stalled = 0;
    start = 0;
    for (;;)
    {
        Normalise(r);
        w = POS(r);
        if (r->size - w < ndw)
        {
            /* not before the end: fill to the end and continue at 0, once
             * the CP has read past everything the filler covers */
            if (Free(r, w) >= r->size - w)
            {
                Fill(r, r->cs.ptr, r->size - w);
                r->stats.wraps++;
                r->stats.fill_dw += r->size - w;
                r->cs.ptr = r->base;
                continue;
            }
        }
        else if (Grant(r, w) >= ndw)
            break;
        if (Pending(r, w) != 0)
        {
            /* the GPU can only free what it has been given */
            err = R2Ring_Commit(r);
            if (err != R2_OK)
                return err;
            continue;
        }
        err = Progress(r, &stalled, &start);
        if (err != R2_OK)
            return err;
    }
    w = POS(r);
    avail = Grant(r, w);
    cap = avail;
    if (r->kick != 0)
    {
        /* hand out only up to the next kick, so Need runs again then */
        cap = Pending(r, w);
        cap = cap < r->kick ? r->kick - cap : 0;
        if (cap < ndw)
            cap = ndw;
        if (cap > avail)
            cap = avail;
    }
    cs->end = r->cs.ptr + cap;
    return R2_OK;
}

LONG R2Ring_Init(struct R2Ring *r, ULONG *base, ULONG gpu, ULONG size,
                 const struct R2Hw *hw, ULONG swap, ULONG fence_reg,
                 ULONG timeout_us)
{
    ULONG v;

    if (size < 1024 || (size & (size - 1)) != 0 || (gpu & 0xfffUL) != 0)
        return R2_EARG;
    R2Cs_InitBuffer(&r->cs, base, 0, swap);
    r->cs.need = Need;
    r->cs.owner = r;
    r->base = base;
    r->gpu = gpu;
    r->size = size;
    r->committed = 0;
    r->rptr = 0;
    r->kick = 0;
    r->timeout_us = timeout_us;
    r->flags = 0;
    r->fence_reg = fence_reg;
    r->hw = hw;
    r->stats.commits = 0;
    r->stats.rptr_reads = 0;
    r->stats.fence_reads = 0;
    r->stats.stalls = 0;
    r->stats.wraps = 0;
    r->stats.fill_dw = 0;
    r->stats.pad_dw = 0;
    v = hw->read_reg(hw->hw, fence_reg);
    r->fence_seq = v;
    r->fence_done = v;
    return R2_OK;
}

LONG R2Ring_Commit(struct R2Ring *r)
{
    ULONG w;
    ULONG pad;
    ULONG last;

    if (r->cs.immd != NULL)
        return R2_ESTATE;
    if (r->cs.error != R2_OK)
        return r->cs.error;
    w = POS(r);
    pad = (R2RING_ALIGN - (w & (R2RING_ALIGN - 1))) & (R2RING_ALIGN - 1);
    if (pad != 0)
    {
        Fill(r, r->cs.ptr, pad);
        r->stats.pad_dw += pad;
        w += pad;
    }
    w &= r->size - 1;
    r->cs.ptr = r->base + w;
    if (r->cs.end < r->cs.ptr)
        r->cs.end = r->cs.ptr;
    if (w == r->committed)
        return R2_OK;
    last = (w - 1) & (r->size - 1);
    r->hw->read_back(r->hw->hw, r->base + last);
    r->hw->write_reg(r->hw->hw, R_CP_RB_WPTR, w);
    r->committed = w;
    r->stats.commits++;
    /* the room granted before may have ended at the old ring end */
    r->cs.end = r->cs.ptr;
    return R2_OK;
}

LONG R2Ring_Fence(struct R2Ring *r, ULONG *seq)
{
    LONG err;

    err = R2E_Fence(&r->cs, r->fence_reg, r->fence_seq + 1);
    if (err != R2_OK)
        return err;
    r->fence_seq++;
    if (seq != NULL)
        *seq = r->fence_seq;
    return R2_OK;
}

BOOL R2Ring_FenceDone(struct R2Ring *r, ULONG seq)
{
    if ((LONG) (r->fence_done - seq) >= 0)
        return TRUE;
    r->fence_done = r->hw->read_reg(r->hw->hw, r->fence_reg);
    r->stats.fence_reads++;
    return (BOOL) ((LONG) (r->fence_done - seq) >= 0);
}

LONG R2Ring_WaitFence(struct R2Ring *r, ULONG seq)
{
    ULONG start;
    LONG err;

    err = R2Ring_Commit(r);
    if (err != R2_OK)
        return err;
    if (R2Ring_FenceDone(r, seq))
        return R2_OK;
    r->stats.stalls++;
    start = r->hw->clock_us(r->hw->hw);
    while (!R2Ring_FenceDone(r, seq))
    {
        if ((ULONG) (r->hw->clock_us(r->hw->hw) - start) > r->timeout_us)
            return R2_ETIMEOUT;
    }
    return R2_OK;
}

LONG R2Ring_Idle(struct R2Ring *r)
{
    ULONG seq;
    LONG err;

    err = R2Ring_Fence(r, &seq);
    if (err != R2_OK)
        return err;
    return R2Ring_WaitFence(r, seq);
}
