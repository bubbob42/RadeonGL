/*
 * r2cp.c -- starting and stopping the R200 command processor (r2cp.h).
 *
 * The sequence follows Linux 6.6 r100_cp_init / r100_cp_load_microcode /
 * r100_cp_disable for a ring without write-back.
 */

#include "r2cp.h"
#include "r200reg.h"

#define RB_BLKSZ   9UL      /* 2^9 bytes a fetch block (Linux) */
#define MAX_FETCH  1UL      /* the CP reads 128 bytes at a time (Linux) */
#define CSQ_MODE   0x00004D4DUL

static ULONG Rd(const struct R2Hw *hw, ULONG reg)
{
    return hw->read_reg(hw->hw, reg);
}

static void Wr(const struct R2Hw *hw, ULONG reg, ULONG val)
{
    hw->write_reg(hw->hw, reg, val);
}

void R2Cp_Save(const struct R2Hw *hw, struct R2CpSaved *s)
{
    s->csq_cntl = Rd(hw, R_CP_CSQ_CNTL);
    s->csq_mode = Rd(hw, R_CP_CSQ_MODE);
    s->rb_cntl = Rd(hw, R_CP_RB_CNTL);
    s->rb_base = Rd(hw, R_CP_RB_BASE);
    s->rb_rptr_addr = Rd(hw, R_CP_RB_RPTR_ADDR);
    s->wptr_delay = Rd(hw, R_CP_RB_WPTR_DELAY);
    s->scratch_umsk = Rd(hw, R_SCRATCH_UMSK);
}

BOOL R2Cp_InUse(const struct R2CpSaved *s)
{
    return (BOOL) ((s->csq_cntl >> R_CSQ_MODE_SHIFT) != 0);
}

LONG R2Cp_Reclaim(const struct R2Hw *hw, struct R2CpSaved *s, ULONG timeout_us, ULONG *reset)
{
    s->csq_cntl = R_CSQ_PRIDIS_INDDIS;
    return R2Cp_Stop(hw, s, timeout_us, reset);
}

LONG R2Cp_WaitIdle(const struct R2Hw *hw, ULONG timeout_us)
{
    ULONG start;
    ULONG v;

    start = hw->clock_us(hw->hw);
    for (;;)
    {
        v = Rd(hw, R_RBBM_STATUS);
        if ((v & R_RBBM_FIFOCNT_MASK) == 64 && !(v & R_RBBM_ACTIVE))
            return R2_OK;
        if (hw->clock_us(hw->hw) - start > timeout_us)
            return R2_ETIMEOUT;
    }
}

void R2Cp_LoadMicrocode(const struct R2Hw *hw, const ULONG *ucode)
{
    ULONG i;

    Wr(hw, R_CP_ME_RAM_ADDR, 0);
    for (i = 0; i < R2CP_UCODE_DWORDS; i += 2)
    {
        Wr(hw, R_CP_ME_RAM_DATAH, ucode[i]);
        Wr(hw, R_CP_ME_RAM_DATAL, ucode[i + 1]);
    }
}

ULONG R2Cp_CheckMicrocode(const struct R2Hw *hw, const ULONG *ucode)
{
    ULONG i;
    ULONG bad;
    ULONG h;
    ULONG l;

    bad = 0;
    for (i = 0; i < R2CP_UCODE_WORDS; i++)
    {
        Wr(hw, R_CP_ME_RAM_RADDR, i);
        h = Rd(hw, R_CP_ME_RAM_DATAH);
        l = Rd(hw, R_CP_ME_RAM_DATAL);
        if (h != ucode[2 * i] || l != ucode[2 * i + 1])
            bad++;
    }
    return bad;
}

ULONG R2Cp_RbCntl(ULONG ring_dw, ULONG flags)
{
    ULONG bufsz;

    if (ring_dw < 1024 || (ring_dw & (ring_dw - 1)) != 0)
        return 0;
    /* ring_dw = 2^(bufsz + 1) */
    bufsz = 0;
    while ((2UL << bufsz) < ring_dw)
        bufsz++;
    if (bufsz > R_RB_BUFSZ_MASK)
        return 0;
    return bufsz | (RB_BLKSZ << R_RB_BLKSZ_SHIFT) | (MAX_FETCH << R_MAX_FETCH_SHIFT)
         | ((flags & R2CP_GPU_SWAP) ? R_BUF_SWAP_32BIT : 0);
}

LONG R2Cp_Start(const struct R2Hw *hw, ULONG ring_gpu, ULONG ring_dw, ULONG flags)
{
    ULONG cntl;
    ULONG t;

    cntl = R2Cp_RbCntl(ring_dw, flags);
    if (cntl == 0 || (ring_gpu & 0xfffUL) != 0)
        return R2_EARG;
    Wr(hw, R_CP_RB_CNTL, cntl | R_RB_NO_UPDATE);
    Wr(hw, R_CP_RB_BASE, ring_gpu);
    /* both pointers to 0 */
    Wr(hw, R_CP_RB_CNTL, cntl | R_RB_RPTR_WR_ENA | R_RB_NO_UPDATE);
    Wr(hw, R_CP_RB_RPTR_WR, 0);
    Wr(hw, R_CP_RB_WPTR, 0);
    /* no write-back of the read pointer or the scratch registers */
    Wr(hw, R_CP_RB_RPTR_ADDR, 0);
    Wr(hw, R_SCRATCH_UMSK, 0);
    Wr(hw, R_CP_RB_CNTL, cntl | R_RB_NO_UPDATE);
    /* Linux: udelay(10) before the CSQ starts */
    t = hw->clock_us(hw->hw);
    while (hw->clock_us(hw->hw) - t < 10)
        ;
    Wr(hw, R_CP_RB_WPTR_DELAY, 0);
    Wr(hw, R_CP_CSQ_MODE, CSQ_MODE);
    Wr(hw, R_CP_CSQ_CNTL, R_CSQ_PRIBM_INDBM);
    return R2_OK;
}

LONG R2Cp_Stop(const struct R2Hw *hw, const struct R2CpSaved *s, ULONG timeout_us,
               ULONG *reset)
{
    LONG err;

    *reset = 0;
    Wr(hw, R_CP_CSQ_MODE, 0);
    Wr(hw, R_CP_CSQ_CNTL, R_CSQ_PRIDIS_INDDIS);
    Wr(hw, R_SCRATCH_UMSK, 0);
    err = R2Cp_WaitIdle(hw, timeout_us);
    if (err != R2_OK)
    {
        /* the CP alone; Rd() makes the write reach the chip before the
         * bit is cleared again (Linux reads RBBM_SOFT_RESET back too) */
        Wr(hw, R_RBBM_SOFT_RESET, R_SOFT_RESET_CP);
        (void) Rd(hw, R_RBBM_SOFT_RESET);
        Wr(hw, R_RBBM_SOFT_RESET, 0);
        (void) Rd(hw, R_RBBM_SOFT_RESET);
        *reset = 1;
    }
    /* as found; of CSQ_CNTL only the mode (the rest is queue status) */
    Wr(hw, R_CP_RB_CNTL, s->rb_cntl);
    Wr(hw, R_CP_RB_BASE, s->rb_base);
    Wr(hw, R_CP_RB_RPTR_ADDR, s->rb_rptr_addr);
    Wr(hw, R_CP_RB_WPTR_DELAY, s->wptr_delay);
    Wr(hw, R_SCRATCH_UMSK, s->scratch_umsk);
    Wr(hw, R_CP_CSQ_MODE, s->csq_mode);
    Wr(hw, R_CP_CSQ_CNTL, s->csq_cntl & (0xfUL << R_CSQ_MODE_SHIFT));
    return err;
}
