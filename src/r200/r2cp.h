/*
 * r2cp.h -- starting and stopping the R200 command processor (CP).
 *
 * Radeon.card does not use the CP (MEASURED on the A4000D, 2026-11-02:
 * CP_CSQ_CNTL mode 0, the ring registers at their power-on garbage, and its
 * 2D path leaves them alone; confirmed by its author the same day: it always
 * writes the registers directly), so RadeonGL starts it itself, the way
 * Linux r100_cp_init does for a ring without write-back. Every call here
 * belongs inside a P96 bitmap lock, which keeps Radeon.card off the chip:
 *
 *  1. wait until the engine is idle (RBBM_STATUS: 64 free FIFO slots, not
 *     active);
 *  2. load the microcode through CP_ME_RAM_ADDR / DATAH / DATAL;
 *  3. CP_RB_CNTL (ring size, 512-byte fetch blocks, MAX_FETCH 1,
 *     RB_NO_UPDATE: nothing is mastered into Amiga memory, and bus mastering
 *     is off on the Mediator anyway), CP_RB_BASE, both pointers to 0;
 *  4. CP_CSQ_MODE 0x4D4D, CP_CSQ_CNTL PRIBM_INDBM: the CP runs.
 *
 * Stopping is Linux r100_cp_disable (CSQ off, wait idle) followed by
 * putting back what R2Cp_Save found. If the engine does not go idle, only
 * the CP is soft-reset (RBBM_SOFT_RESET bit 0): the 2D engine is
 * Radeon.card's.
 *
 * Byte order of the ring (concept 6.3): with R2CP_GPU_SWAP the CP swaps
 * each dword it fetches (CP_RB_CNTL BUF_SWAP_32BIT), so the 68k stores
 * native dwords; without it the 68k stores them byte-reversed. Which works
 * through the Mediator is for the CPRing probe to show.
 *
 * Pure C89, no OS calls: every register access goes through struct R2Hw
 * (r2ring.h), so the host tests drive the same code.
 */

#ifndef R2CP_H
#define R2CP_H

#include <exec/types.h>
#include "r2ring.h"

/* The microcode (r2ucode.c, generated from linux-firmware R200_cp.bin):
 * 256 words as (CP_ME_RAM_DATAH, CP_ME_RAM_DATAL) pairs. */
#define R2CP_UCODE_WORDS  256
#define R2CP_UCODE_DWORDS (R2CP_UCODE_WORDS * 2)
extern const ULONG R2Cp_Microcode[R2CP_UCODE_DWORDS];

/* R2Cp_Start flags */
#define R2CP_GPU_SWAP 0x0001    /* BUF_SWAP_32BIT: the CP swaps each ring dword */

/* The CP registers as found, put back by R2Cp_Stop. */
struct R2CpSaved
{
    ULONG csq_cntl;
    ULONG csq_mode;
    ULONG rb_cntl;
    ULONG rb_base;
    ULONG rb_rptr_addr;
    ULONG wptr_delay;
    ULONG scratch_umsk;
};

void R2Cp_Save(const struct R2Hw *hw, struct R2CpSaved *s);

/* TRUE when the CP runs already (CSQ mode not 0). */
BOOL R2Cp_InUse(const struct R2CpSaved *s);

/* A CP found running can only be one RadeonGL left behind when it crashed:
 * Radeon.card never uses the CP (Picasso96, 2026-11-02), and the
 * ring registers held power-on garbage on the A4000D. Stops it as
 * R2Cp_Stop does (CSQ off, a bounded wait for idle, the CP alone
 * soft-reset if the engine stays busy) and records the stopped CSQ in s,
 * so that the R2Cp_Stop at the end leaves it stopped. R2_OK, or
 * R2_ETIMEOUT with *reset set. */
LONG R2Cp_Reclaim(const struct R2Hw *hw, struct R2CpSaved *s, ULONG timeout_us, ULONG *reset);

/* Wait until RBBM_STATUS shows an empty command FIFO and an idle engine.
 * R2_OK or R2_ETIMEOUT. */
LONG R2Cp_WaitIdle(const struct R2Hw *hw, ULONG timeout_us);

/* Write the microcode (the CP must be stopped). */
void R2Cp_LoadMicrocode(const struct R2Hw *hw, const ULONG *ucode);

/* Read the microcode back through CP_ME_RAM_RADDR; returns how many of the
 * 256 words differ. Diagnostic only: whether the read-back port works this
 * way on the R200 is itself something the probe measures. */
ULONG R2Cp_CheckMicrocode(const struct R2Hw *hw, const ULONG *ucode);

/* The CP_RB_CNTL value for a ring of ring_dw dwords (a power of two, 1024 to
 * 2^23), without RB_NO_UPDATE and RB_RPTR_WR_ENA. 0 for a bad size. */
ULONG R2Cp_RbCntl(ULONG ring_dw, ULONG flags);

/* Start the CP on the ring at GPU address ring_gpu (4 KB aligned), both
 * pointers at 0. The microcode must be loaded. R2_OK or R2_EARG. */
LONG R2Cp_Start(const struct R2Hw *hw, ULONG ring_gpu, ULONG ring_dw, ULONG flags);

/* Stop the CP and put back s. R2_OK; R2_ETIMEOUT when the engine stayed
 * busy (the CP was then soft-reset, and *reset is set). */
LONG R2Cp_Stop(const struct R2Hw *hw, const struct R2CpSaved *s, ULONG timeout_us,
               ULONG *reset);

#endif /* R2CP_H */
