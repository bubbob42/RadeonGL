/*
 * simcp.c -- see simcp.h.
 */

#include <stdio.h>
#include "simcp.h"
#include "r2pm4.h"
#include "r200reg.h"

static ULONG Val(const struct SimCp *s, ULONG i)
{
    ULONG v;

    v = s->ring[i & (s->size - 1)];
    return s->swap ? R2Cs_Swap32(v) : v;
}

static void Fail(struct SimCp *s, ULONG *counter, const char *what, ULONG at)
{
    if (s->bad_align + s->bad_order + s->straddle + s->overwritten + s->chk_errors == 0)
        printf("  simcp: %s at ring dword %lu\n", what, (unsigned long) at);
    (*counter)++;
}

/* Read one packet at rptr. Returns its length, 0 when the CP waits. */
static ULONG Step(struct SimCp *s)
{
    ULONG h;
    ULONG len;
    ULONG avail;
    ULONG i;
    LONG e;
    struct R2ChkError err;

    if (s->rptr == s->wptr)
        return 0;
    h = Val(s, s->rptr);
    switch (R2_PKT_TYPE(h))
    {
        case 2:
            len = 1;
            break;
        case 0:
        case 3:
            len = 2 + R2_PKT_COUNT(h);
            break;
        default:
            len = 1;
            break;
    }
    avail = (s->wptr - s->rptr) & (s->size - 1);
    if (len > avail || s->rptr + len > s->size)
    {
        Fail(s, &s->straddle, "packet straddles the ring end or WPTR", s->rptr);
        s->rptr = s->wptr;              /* resynchronise */
        return avail;
    }
    for (i = 0; i < len; i++)
    {
        if (s->ring[s->rptr + i] != s->shadow[s->rptr + i])
        {
            Fail(s, &s->overwritten, "pending work overwritten", s->rptr + i);
            break;
        }
    }
    e = R2Chk_Run(&s->cfg, &s->st, s->ring + s->rptr, len, s->swap, s->rptr, &err);
    if (e != R2CE_OK)
    {
        if (s->chk_errors == 0)
        {
            s->first = err;
            printf("  simcp: checker: %s (at %lu, what 0x%lx, value 0x%lx)\n",
                   R2Chk_ErrorName(e), (unsigned long) err.at,
                   (unsigned long) err.what, (unsigned long) err.value);
        }
        s->chk_errors++;
    }
    /* execute what the test can observe: the fence register */
    if (R2_PKT_TYPE(h) == 0 && !R2_PKT0_ONE_REG_WR(h))
    {
        for (i = 0; i <= R2_PKT_COUNT(h); i++)
            if (R2_PKT0_REG(h) + 4 * i == s->fence_reg)
                s->fence_val = Val(s, s->rptr + 1 + i);
    }
    s->rptr = (s->rptr + len) & (s->size - 1);
    s->packets++;
    s->consumed += len;
    return len;
}

static void Run(struct SimCp *s, ULONG budget)
{
    ULONG done;
    ULONG n;

    done = 0;
    while (done < budget)
    {
        n = Step(s);
        if (n == 0)
            break;
        done += n;
    }
}

static ULONG ReadReg(APTR hw, ULONG reg)
{
    struct SimCp *s;

    s = (struct SimCp *) hw;
    if (reg == R_CP_RB_RPTR)
    {
        Run(s, s->rate);
        return s->rptr;
    }
    if (reg == s->fence_reg)
    {
        Run(s, s->rate);
        return s->fence_val;
    }
    return 0;
}

static void WriteReg(APTR hw, ULONG reg, ULONG val)
{
    struct SimCp *s;
    ULONG i;

    s = (struct SimCp *) hw;
    if (reg != R_CP_RB_WPTR)
        return;
    s->wptr_writes++;
    if (val & 15)
        Fail(s, &s->bad_align, "WPTR not 16-dword aligned", val);
    if (s->last_readback != ((val - 1) & (s->size - 1)))
        Fail(s, &s->bad_order, "WPTR written without reading back the dword below it", val);
    s->last_readback = 0xffffffffUL;
    for (i = s->wptr; i != (val & (s->size - 1)); i = (i + 1) & (s->size - 1))
        s->shadow[i] = s->ring[i];
    s->wptr = val & (s->size - 1);
}

static ULONG ReadBack(APTR hw, volatile ULONG *p)
{
    struct SimCp *s;

    s = (struct SimCp *) hw;
    s->last_readback = (ULONG) (p - s->ring);
    return *p;
}

static ULONG Clock(APTR hw)
{
    struct SimCp *s;

    s = (struct SimCp *) hw;
    s->clock += 10;
    return s->clock;
}

void SimCp_Init(struct SimCp *s, ULONG *ring, ULONG *shadow, ULONG size,
                ULONG swap, ULONG fence_reg, ULONG fence_start, ULONG rate,
                const struct R2ChkCfg *cfg)
{
    ULONG i;

    s->ring = ring;
    s->shadow = shadow;
    s->size = size;
    s->swap = swap;
    s->wptr = 0;
    s->rptr = 0;
    s->fence_reg = fence_reg;
    s->fence_val = fence_start;
    s->rate = rate;
    s->clock = 0;
    s->last_readback = 0xffffffffUL;
    s->consumed = 0;
    s->packets = 0;
    s->wptr_writes = 0;
    s->bad_align = 0;
    s->bad_order = 0;
    s->straddle = 0;
    s->overwritten = 0;
    s->chk_errors = 0;
    s->first.at = 0;
    s->first.code = 0;
    s->first.what = 0;
    s->first.value = 0;
    s->cfg = *cfg;
    R2Chk_Reset(&s->st);
    for (i = 0; i < size; i++)
    {
        /* garbage, as VRAM would hold: the CP must never read it */
        ring[i] = 0x40000000UL | ((ULONG) (i * 2654435761UL) & 0x3fffffffUL);
        shadow[i] = ring[i];
    }
    s->hw.read_reg = ReadReg;
    s->hw.write_reg = WriteReg;
    s->hw.read_back = ReadBack;
    s->hw.clock_us = Clock;
    s->hw.hw = s;
}

void SimCp_Drain(struct SimCp *s)
{
    while (Step(s) != 0)
        ;
}

int SimCp_Clean(const struct SimCp *s)
{
    return s->bad_align == 0 && s->bad_order == 0 && s->straddle == 0
        && s->overwritten == 0 && s->chk_errors == 0;
}
