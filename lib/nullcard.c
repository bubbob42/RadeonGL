/*
 * nullcard.c -- see nullcard.h. The same reading of the ring as the host
 * tests' simulated CP (test/host/simcp.c), without the host's printf and
 * shadow copy: here it runs inside minigl.library on the Amiga.
 */

#include "nullcard.h"
#include "r2pm4.h"
#include "r200reg.h"

static ULONG Val(const struct NullCard *n, ULONG i)
{
    ULONG v;

    v = n->ring[i & (n->size - 1)];
    return n->swap ? R2Cs_Swap32(v) : v;
}

/* Read one packet at rptr; 0 when the CP has caught up. */
static ULONG Step(struct NullCard *n)
{
    ULONG h;
    ULONG len;
    ULONG avail;
    ULONG i;
    LONG e;
    struct R2ChkError err;

    if (n->rptr == n->wptr)
        return 0;
    h = Val(n, n->rptr);
    len = (R2_PKT_TYPE(h) == 0 || R2_PKT_TYPE(h) == 3) ? 2 + R2_PKT_COUNT(h) : 1;
    avail = (n->wptr - n->rptr) & (n->size - 1);
    if (len > avail || n->rptr + len > n->size)
    {
        n->straddle++;
        n->rptr = n->wptr;
        return avail;
    }
    e = n->no_check ? R2CE_OK
                    : R2Chk_Run(&n->cfg, &n->st, n->ring + n->rptr, len, n->swap, n->rptr, &err);
    if (e != R2CE_OK)
    {
        if (n->errors == 0)
        {
            n->first = err;
            n->first_code = e;
        }
        n->errors++;
    }
    if (R2_PKT_TYPE(h) == 0 && !R2_PKT0_ONE_REG_WR(h))
        for (i = 0; i <= R2_PKT_COUNT(h); i++)
            if (R2_PKT0_REG(h) + 4 * i == n->fence_reg)
                n->fence_val = Val(n, n->rptr + 1 + i);
    if (n->rec != NULL)
    {
        if (n->rec_n + len <= n->rec_max)
            for (i = 0; i < len; i++)
                n->rec[n->rec_n++] = n->ring[n->rptr + i];
        else
            n->rec_lost++;
    }
    n->rptr = (n->rptr + len) & (n->size - 1);
    n->packets++;
    n->dwords += len;
    return len;
}

static void Drain(struct NullCard *n)
{
    while (Step(n) != 0)
        ;
}

static ULONG ReadReg(APTR hw, ULONG reg)
{
    struct NullCard *n;

    n = (struct NullCard *) hw;
    if (reg == R_CP_RB_RPTR)
    {
        Drain(n);           /* an infinitely fast GPU */
        return n->rptr;
    }
    if (reg == n->fence_reg)
    {
        Drain(n);
        return n->fence_val;
    }
    return 0;
}

static void WriteReg(APTR hw, ULONG reg, ULONG val)
{
    struct NullCard *n;

    n = (struct NullCard *) hw;
    if (reg != R_CP_RB_WPTR)
        return;
    if (val & 15)
        n->bad_align++;
    n->wptr = val & (n->size - 1);
}

static ULONG ReadBack(APTR hw, volatile ULONG *p)
{
    (void) hw;
    return *p;
}

static ULONG Clock(APTR hw)
{
    struct NullCard *n;

    n = (struct NullCard *) hw;
    return ++n->clock;
}

void NullCard_Init(struct NullCard *n, ULONG *ring, ULONG size, ULONG swap,
                   ULONG fence_reg, const struct R2ChkCfg *cfg)
{
    UBYTE *p;
    ULONG i;

    p = (UBYTE *) n;
    for (i = 0; i < sizeof(*n); i++)
        p[i] = 0;
    n->ring = ring;
    n->size = size;
    n->swap = swap;
    n->fence_reg = fence_reg;
    n->cfg = *cfg;
    R2Chk_Reset(&n->st);
    n->hw.read_reg = ReadReg;
    n->hw.write_reg = WriteReg;
    n->hw.read_back = ReadBack;
    n->hw.clock_us = Clock;
    n->hw.hw = n;
}
