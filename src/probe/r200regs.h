/*
 * r200regs.h -- ATI Radeon R100/R200 register offsets the probes read.
 *
 * Offsets are byte offsets from the MMIO base (PCI BAR2). The registers are
 * little-endian 32-bit; see mmio.h for how a 68k reads them. Names and
 * values follow the public MIT-licensed drivers: Linux 4.4
 * drivers/gpu/drm/radeon/radeon_drv.h, xf86-video-ati 6.14 radeon_reg.h and
 * Mesa 21.3 r200_reg.h (docs/research/notes-r200.md, section 6). AMD never
 * published R200 documentation; a register not listed in one of those files
 * is not listed here.
 *
 * READ-ONLY use in R200Info. BusBench writes exactly one register,
 * SCRATCH_REG5, and only with the value it read from it.
 */

#ifndef PROBE_R200REGS_H
#define PROBE_R200REGS_H

/* --- PCI configuration mirror and bus ----------------------------------- */
#define R_BUS_CNTL              0x0030
#define   R_BUS_MASTER_DIS        (1UL << 6)
#define R_GEN_INT_CNTL          0x0040
#define R_GEN_INT_STATUS        0x0044
#define R_CRTC_GEN_CNTL         0x0050
#define R_CRTC_EXT_CNTL         0x0054
#define R_CONFIG_CNTL           0x00e0
#define R_CONFIG_XSTRAP         0x00e4
#define R_CONFIG_BONDS          0x00e8
#define R_RBBM_SOFT_RESET       0x00f0
#define R_CONFIG_MEMSIZE        0x00f8
#define R_CONFIG_APER_0_BASE    0x0100
#define R_CONFIG_APER_1_BASE    0x0104
#define R_CONFIG_APER_SIZE      0x0108
#define R_CONFIG_REG_1_BASE     0x010c
#define R_CONFIG_REG_APER_SIZE  0x0110
#define R_HOST_PATH_CNTL        0x0130
#define R_MEM_CNTL              0x0140
#define R_MC_FB_LOCATION        0x0148
#define R_MC_AGP_LOCATION       0x014c
#define R_MC_STATUS             0x0150
#define R_AIC_CNTL              0x01d0

/* --- CRTC (Radeon.card's) ------------------------------------------------- */
#define R_CRTC_H_TOTAL_DISP     0x0200
#define R_CRTC_V_TOTAL_DISP     0x0208
#define R_CRTC_OFFSET           0x0224
#define R_CRTC_OFFSET_CNTL      0x0228
#define R_CRTC_PITCH            0x022c
#define R_DISPLAY_BASE_ADDR     0x023c
#define R_CRTC2_OFFSET          0x0324
#define R_CRTC2_GEN_CNTL        0x03f8
#define R_OV0_SCALE_CNTL        0x0420

/* --- Command processor ---------------------------------------------------- */
#define R_CP_RB_BASE            0x0700
#define R_CP_RB_CNTL            0x0704
#define   R_RB_NO_UPDATE          (1UL << 27)
#define R_CP_RB_RPTR_ADDR       0x070c
#define R_CP_RB_RPTR            0x0710
#define R_CP_RB_WPTR            0x0714
#define R_CP_RB_WPTR_DELAY      0x0718
#define R_CP_IB_BASE            0x0738
#define R_CP_IB_BUFSZ           0x073c
#define R_CP_CSQ_CNTL           0x0740
#define R_CP_CSQ_MODE           0x0744
#define R_SCRATCH_UMSK          0x0770
#define R_SCRATCH_ADDR          0x0774
#define R_CP_ME_RAM_ADDR        0x07d4
#define R_CP_ME_RAM_DATAH       0x07dc
#define R_CP_ME_RAM_DATAL       0x07e0
#define R_CP_CSQ_STAT           0x07f8

/* --- Surfaces (aperture byte swapping) ------------------------------------ */
#define R_SURFACE_CNTL          0x0b00
#define   R_NONSURF_AP0_SWP_16BPP (1UL << 20)
#define   R_NONSURF_AP0_SWP_32BPP (1UL << 21)
#define   R_NONSURF_AP1_SWP_16BPP (1UL << 22)
#define   R_NONSURF_AP1_SWP_32BPP (1UL << 23)
#define R_SURFACE0_LOWER_BOUND  0x0b04
#define R_SURFACE0_UPPER_BOUND  0x0b08
#define R_SURFACE0_INFO         0x0b0c
#define R_SURFACE_STRIDE        0x10      /* SURFACEn = SURFACE0 + n * 0x10 */
#define R_SURFACE_COUNT         8

/* --- Engine status and 2D state ------------------------------------------- */
#define R_RBBM_STATUS           0x0e40
#define   R_RBBM_ACTIVE           (1UL << 31)
#define   R_CP_CMDSTRM_BUSY       (1UL << 16)
#define R_DST_PITCH_OFFSET      0x142c
#define R_DP_GUI_MASTER_CNTL    0x146c
#define R_SCRATCH_REG0          0x15e0
#define R_SCRATCH_REG1          0x15e4
#define R_SCRATCH_REG2          0x15e8
#define R_SCRATCH_REG3          0x15ec
#define R_SCRATCH_REG4          0x15f0
#define R_SCRATCH_REG5          0x15f4
#define R_DP_DATATYPE           0x16c4
#define   R_HOST_BIG_ENDIAN_EN    (1UL << 29)
#define R_WAIT_UNTIL            0x1720
#define R_ISYNC_CNTL            0x1724
#define R_RBBM_GUICNTL          0x172c

/* --- 3D (R200) ------------------------------------------------------------ */
#define R_RB3D_CNTL             0x1c3c
#define R_SE_VAP_CNTL           0x2080
#define R_SE_VAP_CNTL_STATUS    0x2140
#define R_RB3D_ZCACHE_CTLSTAT   0x3254
#define R_RB3D_DSTCACHE_CTLSTAT 0x325c
#define R_RB2D_DSTCACHE_CTLSTAT 0x342c

#endif /* PROBE_R200REGS_H */
