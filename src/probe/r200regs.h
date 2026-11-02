/*
 * r200regs.h -- the Radeon registers the probes read.
 *
 * The offsets and bits live in src/r200/r200reg.h, shared with the back
 * end's packet layer (sources and naming are explained there).
 *
 * READ-ONLY use in R200Info. BusBench writes SCRATCH_REG5 (with the value
 * it read from it) and PP_TFACTOR_0 (put back afterwards).
 *
 * Below: 2D registers only the probes read (Radeon.card's: never written),
 * from Linux radeon_reg.h. LockBench samples them inside a P96 lock to see
 * whether P96 programs the 2D engine while a lock is held.
 */

#ifndef PROBE_R200REGS_H
#define PROBE_R200REGS_H

#include "r200reg.h"

#define R_DST_Y_X               0x1438
#define R_DP_BRUSH_FRGD_CLR     0x147c
#define R_DP_SRC_FRGD_CLR       0x15d8
#define R_DP_CNTL               0x16c0
#define R_DP_WRITE_MASK         0x16cc

/* The PCI configuration space as the chip mirrors it in the register
 * aperture (radeon_reg.h: VENDOR_ID 0x0f00, DEVICE_ID 0x0f02); read as
 * dwords. Whether the R200 does is for LockBench to show. */
#define R_PCI_MIRROR            0x0f00

#endif /* PROBE_R200REGS_H */
