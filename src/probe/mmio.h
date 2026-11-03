/*
 * mmio.h -- 32-bit register access to the Radeon through the Mediator.
 *
 * The chip's registers are little-endian. Whether a plain 68k longword
 * read returns them byte-reversed depends on the bridge and on the card's
 * set-up, so it is MEASURED, not assumed (Mmio_DetectSwap): the chip's PCI
 * mirror must show ATI's vendor ID, or CONFIG_MEMSIZE
 * read both ways must give a whole number of megabytes, or, where no PC
 * BIOS set it (it reads 0), CONFIG_APER_0_BASE must equal BAR0's PCI
 * address, or (when the card came from P96 and its PCI address is not
 * known) CONFIG_APER_SIZE must be a power of two. MEASURED on the A4000D (Mediator, Radeon.card 43.579,
 * 2026-11-02): native reads are right, no swap. Every read and write goes
 * through these helpers so the probes share one answer.
 *
 * Reads are volatile and their value is always USED by the caller (SAS/C
 * drops a discarded volatile read; a benchmark loop must sum what it reads).
 */

#ifndef PROBE_MMIO_H
#define PROBE_MMIO_H

#include <exec/types.h>

/* TRUE when a native 68k read must be byte-swapped to give the register
 * value. Set once by Mmio_DetectSwap(); defaults to FALSE (registers read
 * native inside a lock: MEASURED on the A4000D). */
extern BOOL Mmio_Swap;

#define MMIO_RAW(base, off) (*(volatile ULONG *)((UBYTE *)(base) + (off)))

ULONG Mmio_Swap32(ULONG v);

/* Register value (orientation applied). */
ULONG Mmio_Read(APTR base, ULONG off);

/* Write a register value (orientation applied). */
void Mmio_Write(APTR base, ULONG off, ULONG value);

/* How Mmio_DetectSwap decided. */
#define MMIO_UNDECIDED  0
#define MMIO_BY_MEMSIZE 1   /* CONFIG_MEMSIZE: a whole number of megabytes */
#define MMIO_BY_APER    2   /* CONFIG_APER_0_BASE == BAR0's PCI address */
#define MMIO_BY_APERSIZE 3  /* CONFIG_APER_SIZE a power of two one way only */
#define MMIO_BY_PCIMIRROR 4 /* the PCI mirror shows vendor 0x1002 one way only */

/* Decide Mmio_Swap: from the PCI mirror's vendor ID (as lib/card.c), else
 * from CONFIG_MEMSIZE, else from CONFIG_APER_0_BASE against bar0_pci
 * (BAR0's PCI address; 0 when unknown), else from CONFIG_APER_SIZE.
 * Returns MMIO_*; *memsize gets the memory size in bytes when it reads as
 * one, else 0. Undecided leaves the flag at its default (no swap). Per Picasso96 the registers read big-endian (no swap)
 * while a bitmap of the board is locked; this checks it. */
ULONG Mmio_DetectSwap(APTR base, ULONG bar0_pci, ULONG *memsize);

#endif /* PROBE_MMIO_H */
