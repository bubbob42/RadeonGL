/*
 * mmio.h -- 32-bit register access to the Radeon through the Mediator.
 *
 * The bridge does not swap bytes and the chip's registers are little-endian,
 * so a plain 68k longword read returns the register byte-reversed. Whether
 * that holds on the running machine is MEASURED, not assumed: R200Info
 * reads CONFIG_MEMSIZE both ways and keeps the orientation that gives a
 * whole number of megabytes (Mmio_Swap). Every read and write goes through
 * these helpers so the probes share one answer.
 *
 * Reads are volatile and their value is always USED by the caller (SAS/C
 * drops a discarded volatile read; a benchmark loop must sum what it reads).
 */

#ifndef PROBE_MMIO_H
#define PROBE_MMIO_H

#include <exec/types.h>

/* TRUE when a native 68k read must be byte-swapped to give the register
 * value. Set once by Mmio_DetectSwap(); defaults to TRUE (little-endian
 * registers behind a non-swapping bridge). */
extern BOOL Mmio_Swap;

#define MMIO_RAW(base, off) (*(volatile ULONG *)((UBYTE *)(base) + (off)))

ULONG Mmio_Swap32(ULONG v);

/* Register value (orientation applied). */
ULONG Mmio_Read(APTR base, ULONG off);

/* Write a register value (orientation applied). */
void Mmio_Write(APTR base, ULONG off, ULONG value);

/* Decide Mmio_Swap from CONFIG_MEMSIZE. Returns the memory size in bytes
 * in the chosen orientation, or 0 when neither reading is plausible (the
 * flag is then left at its default). */
ULONG Mmio_DetectSwap(APTR base);

#endif /* PROBE_MMIO_H */
