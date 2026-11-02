/*
 * r200probe.h -- the register report both probes share: register
 * orientation, the register table, and the decoded fields the back end's
 * design hinges on (concept section 11, day-one questions).
 */

#ifndef PROBE_R200PROBE_H
#define PROBE_R200PROBE_H

#include <exec/types.h>

#include "pcicard.h"

/* Decides and reports the register byte order (Mmio_DetectSwap: by
 * CONFIG_MEMSIZE, else by CONFIG_APER_0_BASE against BAR0), sets
 * Mmio_Swap. Returns MMIO_UNDECIDED, MMIO_BY_MEMSIZE or MMIO_BY_APER. */
ULONG R200_ReportEndian(const struct PciCard *card);

/* Number of entries in the register table (for a caller's previous-value
 * array). */
ULONG R200_RegisterCount(void);

/* Prints every table register. prev: an array of R200_RegisterCount()
 * values or NULL; it is filled with this dump's values, and when compare
 * is TRUE a value that differs from what prev held is marked. */
void R200_DumpRegisters(const struct PciCard *card, ULONG *prev, BOOL compare);

/* The fields that matter, decoded: memory controller window, apertures,
 * CP state, surface swapping, DP_DATATYPE, bus master, engine status. */
void R200_DecodeRegisters(const struct PciCard *card);

#endif /* PROBE_R200PROBE_H */
