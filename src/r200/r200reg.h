/*
 * r200reg.h -- ATI Radeon R200/RV280 registers and bit fields, as far as
 * RadeonGL uses them.
 *
 * Offsets are byte offsets from the MMIO base (PCI BAR2); the registers are
 * little-endian 32-bit. AMD never published R200 documentation. Every name
 * and value here comes from one of the public MIT-licensed drivers
 * (docs/research/notes-r200.md, sections 1 and 6):
 *
 *   Linux 4.4  drivers/gpu/drm/radeon/radeon_drv.h       (CP, ring, fences)
 *   Linux 6.6  drivers/gpu/drm/radeon/r100d.h, r200.c,   (packets, the
 *              reg_srcs/r200, radeon_reg.h                command checker)
 *   Mesa 21.3  src/mesa/drivers/dri/r200/r200_reg.h      (3D, TCL, PP, RB3D)
 *   xf86-video-ati 6.14 src/radeon_reg.h
 *
 * A register that is in none of them is not here. The names drop the
 * RADEON_/R200_ prefixes for R_ so the probes and the back end share one set.
 * The notices of those headers are in LICENSE, section C.2.
 */

#ifndef R200REG_H
#define R200REG_H

/* --- PCI configuration mirror and bus ------------------------------------ */
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
#define   R_SOFT_RESET_CP         (1UL << 0)
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
#define   R_CRTC_V_DISP_SHIFT     16
#define   R_CRTC_V_DISP_MASK      0x07ffUL  /* displayed lines - 1 */
#define   R_CRTC_EN               (1UL << 25)   /* in CRTC_GEN_CNTL and CRTC2_GEN_CNTL */
/* the vertical-sync wait (xf86-video-ati 6.14.6 radeon_reg.h, RADEONWaitForVLine) */
#define R_CRTC_GUI_TRIG_VLINE   0x0218
#define   R_TRIG_VLINE_START_SHIFT 0
#define   R_TRIG_VLINE_END_SHIFT  16
#define   R_TRIG_VLINE_LINE_MASK  0x07ffUL
#define   R_TRIG_VLINE_INV        (1UL << 15)   /* WAIT_CRTC_VLINE waits while the beam is INSIDE */
#define   R_TRIG_VLINE_STALL      (1UL << 30)
#define R_CRTC2_V_TOTAL_DISP    0x0308
/* the display's memory-request FIFO (Radeon.card's; xf86-video-ati 6.14.6
 * legacy_crtc.c RADEONInitDispBandwidthLegacy): the display asks with high
 * priority once fewer than CRITICAL_POINT octawords are left; 0 = always */
#define R_GRPH_BUFFER_CNTL      0x02f0
#define   R_GRPH_CRITICAL_POINT_SHIFT 16
#define   R_GRPH_CRITICAL_POINT_MASK  (0x7fUL << 16)
#define   R_GRPH_CRITICAL_CNTL    (1UL << 28)
#define   R_GRPH_CRITICAL_AT_SOF  (1UL << 30)
#define   R_GRPH_STOP_CNTL        (1UL << 31)
#define R_CRTC2_GUI_TRIG_VLINE  0x0318
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
#define   R_RB_BUFSZ_MASK         0x3fUL    /* ring = 2^(BUFSZ + 1) dwords */
#define   R_RB_BLKSZ_SHIFT        8
#define   R_BUF_SWAP_32BIT        (2UL << 16)
#define   R_MAX_FETCH_SHIFT       18
#define   R_RB_NO_UPDATE          (1UL << 27)
#define   R_RB_RPTR_WR_ENA        (1UL << 31)
#define R_CP_RB_RPTR_ADDR       0x070c
#define R_CP_RB_RPTR            0x0710
#define R_CP_RB_WPTR            0x0714
#define R_CP_RB_WPTR_DELAY      0x0718
#define R_CP_RB_RPTR_WR         0x071c
#define R_CP_IB_BASE            0x0738
#define R_CP_IB_BUFSZ           0x073c
#define R_CP_CSQ_CNTL           0x0740
#define   R_CSQ_MODE_SHIFT        28        /* 0 = CP off; 4 = PRIBM_INDBM */
#define   R_CSQ_PRIDIS_INDDIS     (0UL << 28)
#define   R_CSQ_PRIBM_INDBM       (4UL << 28)
#define R_CP_CSQ_MODE           0x0744
#define R_SCRATCH_UMSK          0x0770
#define R_SCRATCH_ADDR          0x0774
#define R_CP_ME_RAM_ADDR        0x07d4
#define R_CP_ME_RAM_RADDR       0x07d8
#define R_CP_ME_RAM_DATAH       0x07dc
#define R_CP_ME_RAM_DATAL       0x07e0
#define R_CP_CSQ_STAT           0x07f8

/* --- Surfaces (aperture byte swapping) ------------------------------------ */
#define R_SURFACE_CNTL          0x0b00
#define   R_SURF_TRANSLATION_DIS  (1UL << 8)
#define   R_NONSURF_AP0_SWP_16BPP (1UL << 20)
#define   R_NONSURF_AP0_SWP_32BPP (1UL << 21)
#define   R_NONSURF_AP1_SWP_16BPP (1UL << 22)
#define   R_NONSURF_AP1_SWP_32BPP (1UL << 23)
#define R_SURFACE0_LOWER_BOUND  0x0b04
#define R_SURFACE0_UPPER_BOUND  0x0b08
#define R_SURFACE0_INFO         0x0b0c
#define R_SURFACE_STRIDE        0x10      /* SURFACEn = SURFACE0 + n * 0x10 */
#define R_SURFACE_COUNT         8

/* --- Engine status, 2D state, sync ---------------------------------------- */
#define R_RBBM_STATUS           0x0e40
#define   R_RBBM_FIFOCNT_MASK     0x7fUL    /* free command FIFO slots, 64 = empty */
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
#define   R_WAIT_CRTC_VLINE       (1UL << 3)    /* CRTC_GUI_TRIG_VLINE's condition */
#define   R_ENG_DISPLAY_CRTC2     (1UL << 31)   /* ... of CRTC2 rather than CRTC */
#define   R_WAIT_2D_IDLE          (1UL << 14)
#define   R_WAIT_DMA_GUI_IDLE     (1UL << 9)
#define   R_WAIT_3D_IDLE          (1UL << 15)
#define   R_WAIT_2D_IDLECLEAN     (1UL << 16)
#define   R_WAIT_3D_IDLECLEAN     (1UL << 17)
#define   R_WAIT_HOST_IDLECLEAN   (1UL << 18)
#define R_ISYNC_CNTL            0x1724
#define R_RBBM_GUICNTL          0x172c

/* --- 3D: pixel pipe, render backend, setup engine (Mesa r200_reg.h) ------- */
#define R_PP_MISC               0x1c14
#define   R_REF_ALPHA_MASK        0x000000ffUL
#define   R_ALPHA_TEST_SHIFT      8      /* 0 fail .. 5 greater .. 7 pass */
#define   R_ALPHA_TEST_GREATER    (5UL << 8)
#define   R_ALPHA_TEST_PASS       (7UL << 8)
#define R_PP_FOG_COLOR          0x1c18
#define R_RE_SOLID_COLOR        0x1c1c
#define R_RB3D_BLENDCNTL        0x1c20
#define   R_COMB_FCN_ADD_CLAMP    (0UL << 12)
#define   R_BLEND_GL_ZERO         32UL
#define   R_BLEND_GL_ONE          33UL
#define   R_BLEND_GL_SRC_COLOR    34UL
#define   R_BLEND_GL_ONE_MINUS_SRC_COLOR 35UL
#define   R_BLEND_GL_DST_COLOR    36UL
#define   R_BLEND_GL_ONE_MINUS_DST_COLOR 37UL
#define   R_BLEND_GL_SRC_ALPHA    38UL
#define   R_BLEND_GL_ONE_MINUS_SRC_ALPHA 39UL
#define   R_BLEND_GL_DST_ALPHA    40UL
#define   R_BLEND_GL_ONE_MINUS_DST_ALPHA 41UL
#define   R_SRC_BLEND_SHIFT       16
#define   R_DST_BLEND_SHIFT       24
#define R_RB3D_DEPTHOFFSET      0x1c24
#define R_RB3D_DEPTHPITCH       0x1c28
#define   R_DEPTHPITCH_MASK       0x00001ff8UL  /* pixels */
#define R_RB3D_ZSTENCILCNTL     0x1c2c
#define   R_DEPTH_FORMAT_MASK     0xfUL
#define   R_DEPTH_FORMAT_16BIT_INT_Z 0UL
#define   R_Z_TEST_SHIFT          4      /* 0 never, 1 less, 2 lequal, ... 7 always */
#define   R_Z_TEST_LESS           (1UL << 4)
#define   R_Z_TEST_LEQUAL         (2UL << 4)
#define   R_Z_TEST_GEQUAL         (4UL << 4)
#define   R_Z_TEST_ALWAYS         (7UL << 4)
#define   R_STENCIL_TEST_ALWAYS   (7UL << 12)
#define   R_Z_WRITE_ENABLE        (1UL << 30)
#define R_PP_CNTL               0x1c38
#define   R_TEX_0_ENABLE          0x00000010UL   /* unit n: << n */
#define   R_TEX_ENABLE_MASK       0x000003f0UL
#define   R_TEX_BLEND_0_ENABLE    0x00001000UL   /* stage n: << n */
#define   R_ALPHA_TEST_ENABLE     0x00800000UL
#define R_RB3D_CNTL             0x1c3c
#define   R_ALPHA_BLEND_ENABLE    (1UL << 0)
#define   R_PLANE_MASK_ENABLE     (1UL << 1)
#define   R_DITHER_ENABLE         (1UL << 2)
#define   R_ROUND_ENABLE          (1UL << 3)
#define   R_STENCIL_ENABLE        (1UL << 7)
#define   R_Z_ENABLE              (1UL << 8)
#define   R_DEPTHXY_OFFSET_ENABLE (1UL << 9)
#define   R_COLOR_FORMAT_SHIFT    10
#define   R_COLOR_FORMAT_ARGB1555 (3UL << 10)
#define   R_COLOR_FORMAT_RGB565   (4UL << 10)
#define   R_COLOR_FORMAT_ARGB8888 (6UL << 10)
#define   R_COLOR_FORMAT_RGB8     (9UL << 10)
#define   R_COLOR_FORMAT_ARGB4444 (15UL << 10)
#define R_RB3D_COLOROFFSET      0x1c40
#define   R_COLOROFFSET_MASK      0xfffffff0UL
#define R_RE_WIDTH_HEIGHT       0x1c44   /* (w-1) | (h-1) << 16, inclusive */
#define R_RB3D_COLORPITCH       0x1c48
#define   R_COLORPITCH_MASK       0x00001ff8UL  /* pixels */
#define   R_COLOR_TILE_ENABLE     (1UL << 16)
#define   R_COLOR_MICROTILE_ENABLE (1UL << 17)
#define R_SE_CNTL               0x1c4c
#define   R_FFACE_CULL_CCW        (1UL << 0)
#define   R_BFACE_SOLID           (3UL << 1)
#define   R_FFACE_SOLID           (3UL << 3)
#define   R_FLAT_SHADE_VTX_LAST   (3UL << 6)
#define   R_DIFFUSE_SHADE_GOURAUD (2UL << 8)
#define   R_DIFFUSE_SHADE_FLAT    (1UL << 8)
#define   R_ALPHA_SHADE_FLAT      (1UL << 10)
#define   R_SPECULAR_SHADE_FLAT   (1UL << 12)
#define   R_FOG_SHADE_FLAT        (1UL << 14)
#define   R_DISC_FOG_SHADE_FLAT   (1UL << 24)
#define   R_ZBIAS_ENABLE_TRI      (1UL << 18)
#define   R_ALPHA_SHADE_GOURAUD   (2UL << 10)
#define   R_SPECULAR_SHADE_GOURAUD (2UL << 12)
#define   R_FOG_SHADE_GOURAUD     (2UL << 14)
#define   R_DISC_FOG_SHADE_GOURAUD (2UL << 24)
#define   R_VTX_PIX_CENTER_OGL    (1UL << 27)
#define   R_ROUND_MODE_TRUNC      (0UL << 28)
#define   R_ROUND_MODE_ROUND      (1UL << 28)
#define   R_ROUND_PREC_8TH_PIX    (1UL << 30)
#define   R_ROUND_PREC_4TH_PIX    (2UL << 30)
#define R_RE_CNTL               0x1c50
#define   R_SCISSOR_ENABLE        0x2UL
#define   R_PERSPECTIVE_ENABLE    0x8UL
#define R_RB3D_DEPTHXY_OFFSET   0x1d60
#define R_RB3D_STENCILREFMASK   0x1d7c
#define   R_STENCIL_MASK_SHIFT    16
#define   R_STENCIL_WRITEMASK_SHIFT 24
#define R_RB3D_ROPCNTL          0x1d80
#define   R_ROP_COPY              (12UL << 8)
#define R_RB3D_PLANEMASK        0x1d84
#define R_SE_VPORT_XSCALE       0x1d98   /* XSCALE XOFFSET YSCALE YOFFSET */
#define R_SE_VPORT_XOFFSET      0x1d9c   /* ZSCALE ZOFFSET, six floats in a row */
#define R_SE_VPORT_YSCALE       0x1da0
#define R_SE_VPORT_YOFFSET      0x1da4
#define R_SE_VPORT_ZSCALE       0x1da8
#define R_SE_VPORT_ZOFFSET      0x1dac
#define R_SE_ZBIAS_FACTOR       0x1db0
#define R_SE_ZBIAS_CONSTANT     0x1db4
#define R_SE_LINE_WIDTH         0x1db8

/* --- Vertex assembly and TCL (Mesa r200_reg.h) ----------------------------- */
#define R_SE_VAP_CNTL           0x2080
#define   R_VAP_TCL_ENABLE        0x00000001UL
#define   R_VAP_FORCE_W_TO_ONE    0x00010000UL
#define   R_VAP_VF_MAX_VTX_NUM_SHIFT 18
#define R_SE_VF_CNTL            0x2084   /* also the draw packets' control dword */
#define   R_VF_PRIM_MASK          0x0000000fUL
#define   R_VF_PRIM_POINTS        0x1UL
#define   R_VF_PRIM_LINES         0x2UL
#define   R_VF_PRIM_LINE_STRIP    0x3UL
#define   R_VF_PRIM_TRIANGLES     0x4UL
#define   R_VF_PRIM_TRIANGLE_FAN  0x5UL
#define   R_VF_PRIM_TRIANGLE_STRIP 0x6UL
#define   R_VF_PRIM_RECT_LIST     0x8UL
#define   R_VF_PRIM_LINE_LOOP     0xcUL
#define   R_VF_PRIM_QUADS         0xdUL
#define   R_VF_PRIM_QUAD_STRIP    0xeUL
#define   R_VF_PRIM_POLYGON       0xfUL
#define   R_VF_PRIM_WALK_IND      0x00000010UL
#define   R_VF_PRIM_WALK_LIST     0x00000020UL
#define   R_VF_PRIM_WALK_RING     0x00000030UL
#define   R_VF_PRIM_WALK_MASK     0x00000030UL
#define   R_VF_COLOR_ORDER_RGBA   0x00000040UL
#define   R_VF_TCL_OUTPUT_VTX_ENABLE 0x00000200UL
#define   R_VF_INDEX_SZ_4         0x00000800UL
#define   R_VF_VERTEX_NUMBER_SHIFT 16
#define R_SE_VTX_FMT_0          0x2088
#define   R_VTX_Z0                (1UL << 0)
#define   R_VTX_W0                (1UL << 1)
#define   R_VTX_WEIGHT_COUNT_SHIFT 2
#define   R_VTX_PV_MATRIX_SEL     (1UL << 5)
#define   R_VTX_N0                (1UL << 6)
#define   R_VTX_POINT_SIZE        (1UL << 7)
#define   R_VTX_DISCRETE_FOG      (1UL << 8)
#define   R_VTX_SHININESS_0       (1UL << 9)
#define   R_VTX_SHININESS_1       (1UL << 10)
#define   R_VTX_COLOR_0_SHIFT     11     /* colour n at 11 + 2n: 0 none, 1 packed, 2 RGB, 3 RGBA floats */
#define   R_VTX_PK_RGBA           1UL
#define   R_VTX_FP_RGB            2UL
#define   R_VTX_FP_RGBA           3UL
#define   R_VTX_XY1               (1UL << 28)
#define   R_VTX_Z1                (1UL << 29)
#define   R_VTX_W1                (1UL << 30)
#define   R_VTX_N1                (1UL << 31)
#define R_SE_VTX_FMT_1          0x208c
#define   R_VTX_TEX0_COMP_CNT_SHIFT 0    /* unit n at 3n: 0..4 components */
#define R_SE_TCL_OUTPUT_VTX_FMT_0 0x2090
#define R_SE_TCL_OUTPUT_VTX_FMT_1 0x2094
#define R_SE_VTE_CNTL           0x20b0
#define   R_VPORT_X_SCALE_ENA     0x00000001UL
#define   R_VPORT_X_OFFSET_ENA    0x00000002UL
#define   R_VPORT_Y_SCALE_ENA     0x00000004UL
#define   R_VPORT_Y_OFFSET_ENA    0x00000008UL
#define   R_VPORT_Z_SCALE_ENA     0x00000010UL
#define   R_VPORT_Z_OFFSET_ENA    0x00000020UL
#define   R_VTX_XY_FMT            0x00000100UL
#define   R_VTX_Z_FMT             0x00000200UL
#define   R_VTX_W0_FMT            0x00000400UL
#define R_SE_VF_MAX_VTX_INDX    0x210c
#define R_SE_VF_MIN_VTX_INDX    0x2110
#define R_SE_VAP_CNTL_STATUS    0x2140
#define   R_VC_NO_SWAP            0UL
#define   R_VC_32BIT_SWAP         2UL
#define R_SE_VTX_STATE_CNTL     0x2180
#define   R_VSC_UPDATE_USER_COLOR_0_ENABLE 0x00010000UL
#define R_SE_TCL_VECTOR_INDX_REG 0x2200
#define   R_VEC_INDX_OCTWORD_STRIDE_SHIFT 16
#define R_SE_TCL_VECTOR_DATA_REG 0x2204
#define R_SE_TCL_SCALAR_INDX_REG 0x2208
#define   R_SCAL_INDX_DWORD_STRIDE_SHIFT 16
#define R_SE_TCL_SCALAR_DATA_REG 0x220c
#define R_SE_TCL_MATRIX_SEL_0   0x2230   /* .. _4 at 0x2240 */
#define R_SE_TCL_MATRIX_SEL_1   0x2234
#define R_SE_TCL_MATRIX_SEL_2   0x2238
#define R_SE_TCL_MATRIX_SEL_3   0x223c
#define R_SE_TCL_MATRIX_SEL_4   0x2240
#define R_SE_TCL_OUTPUT_VTX_COMP_SEL 0x2250
#define   R_OUTPUT_XYZW           (1UL << 0)
#define   R_OUTPUT_COLOR_0        (1UL << 8)
#define   R_OUTPUT_COLOR_1        (1UL << 9)
#define   R_OUTPUT_TEX_0          (1UL << 16)    /* unit n: << n */
#define   R_OUTPUT_TEX_MASK       (0x3fUL << 16)
#define R_SE_TCL_INPUT_VTX_VECTOR_ADDR_0 0x2254
#define R_SE_TCL_INPUT_VTX_VECTOR_ADDR_1 0x2258
#define R_SE_TCL_INPUT_VTX_VECTOR_ADDR_2 0x225c
#define R_SE_TCL_INPUT_VTX_VECTOR_ADDR_3 0x2260
#define R_SE_TCL_LIGHT_MODEL_CTL_0 0x2268
#define   R_LIGHTING_ENABLE       (1UL << 0)
#define R_SE_TCL_LIGHT_MODEL_CTL_1 0x226c
#define R_SE_TCL_PER_LIGHT_CTL_0 0x2270  /* .. _3 at 0x227c */
#define R_SE_TCL_STATE_FLUSH    0x2284
#define R_SE_TCL_TEX_PROC_CTL_2 0x22a8
#define R_SE_TCL_TEX_PROC_CTL_3 0x22ac
#define R_SE_TCL_TEX_PROC_CTL_0 0x22b0
#define R_SE_TCL_TEX_PROC_CTL_1 0x22b4
#define R_SE_TC_TEX_CYL_WRAP_CTL 0x22b8
#define R_SE_TCL_UCP_VERT_BLEND_CTL 0x22c0  /* Mesa: ..._CTL; Linux safe list: ..._CNTL */
#define   R_UCP_IN_CLIP_SPACE     (1UL << 0)
#define   R_CULL_FRONT_IS_CCW     (1UL << 28)
#define   R_CULL_FRONT            (1UL << 29)
#define   R_CULL_BACK             (1UL << 30)
#define R_SE_TCL_POINT_SPRITE_CNTL 0x22c4
#define R_RE_POINTSIZE          0x2648   /* size * 16 | max size * 16 << 16 */
#define   R_MAXPOINTSIZE_SHIFT    16
#define R_RE_TOP_LEFT           0x26c0   /* x | y << 16 */
#define R_RE_MISC               0x26c4
#define R_RE_AUX_SCISSOR_CNTL   0x26f0

/* TCL vector memory (SE_TCL_VECTOR_INDX_REG addresses; one vector = 4
 * dwords) and the matrix slots of SE_TCL_MATRIX_SEL_n. */
#define R_VS_MATRIX_0_MV        0x80
#define R_VS_MATRIX_1_INV_MV    0x84
#define R_VS_MATRIX_2_MVP       0x88
#define R_VS_MATRIX_3_TEX0      0x8c
#define R_VS_MATRIX_4_TEX1      0x90
#define R_VS_MAX                0x1c0
#define R_MTX_MV                0
#define R_MTX_IMV               1
#define R_MTX_MVP               2
#define R_MTX_TEX0              3
#define R_MTX_TEX1              4
#define R_MTX_TEX2              5
#define R_MTX_TEX3              6
#define R_MTX_TEX4              7
#define R_MTX_TEX5              8

/* TCL scalar memory (SE_TCL_SCALAR_INDX_REG addresses). */
#define R_SS_VERT_GUARD_CLIP_ADJ_ADDR 0x80  /* + VERT_DISCARD, HORZ_CLIP, HORZ_DISCARD */

/* --- Textures (unit n: TXFILTER..TXMULTI_CTL at + 0x20 * n,
 *     TXOFFSET at + 0x18 * n, combiner stage n at + 0x10 * n) --------------- */
#define R_PP_TXFILTER_0         0x2c00
#define   R_MAG_FILTER_LINEAR     (1UL << 0)
#define   R_MIN_FILTER_LINEAR     (1UL << 1)
#define   R_MIN_FILTER_NEAREST_MIP_NEAREST (2UL << 1)
#define   R_MIN_FILTER_NEAREST_MIP_LINEAR  (3UL << 1)
#define   R_MIN_FILTER_LINEAR_MIP_NEAREST (6UL << 1)
#define   R_MIN_FILTER_LINEAR_MIP_LINEAR  (7UL << 1)
#define   R_MAX_MIP_LEVEL_SHIFT   16
#define   R_MAX_MIP_LEVEL_MASK    (0xfUL << 16)
#define   R_CLAMP_S_CLAMP_LAST    (2UL << 23)
#define   R_CLAMP_T_CLAMP_LAST    (2UL << 27)
#define R_PP_TXFORMAT_0         0x2c04
#define   R_TXFORMAT_FORMAT_MASK  0x1fUL
#define   R_TXFORMAT_I8           0UL
#define   R_TXFORMAT_AI88         1UL
#define   R_TXFORMAT_ARGB1555     3UL
#define   R_TXFORMAT_RGB565       4UL
#define   R_TXFORMAT_ARGB4444     5UL
#define   R_TXFORMAT_ARGB8888     6UL
#define   R_TXFORMAT_ALPHA_IN_MAP (1UL << 6)
#define   R_TXFORMAT_NON_POWER2   (1UL << 7)
#define   R_TXFORMAT_WIDTH_SHIFT  8
#define   R_TXFORMAT_HEIGHT_SHIFT 12
#define   R_TXFORMAT_ST_ROUTE_SHIFT 24
#define   R_TXFORMAT_LOOKUP_DISABLE (1UL << 27)
#define R_PP_TXFORMAT_X_0       0x2c08
#define   R_LOD_BIAS_CORRECTION   0x00600000UL
#define R_PP_TXSIZE_0           0x2c0c   /* NPOT: (w-1) | (h-1) << 16 */
#define R_PP_TXPITCH_0          0x2c10   /* NPOT: pitch in bytes - 32 */
#define R_PP_BORDER_COLOR_0     0x2c14
#define R_PP_TXMULTI_CTL_0      0x2c1c
#define R_PP_TXUNIT_STRIDE      0x20
#define R_PP_CNTL_X             0x2cc4
#define R_PP_TXOFFSET_0         0x2d00
#define   R_TXO_OFFSET_MASK       0xffffffe0UL   /* 32-byte aligned; low bits: swap, tiling */
#define R_PP_TXOFFSET_STRIDE    0x18
#define R_PP_TFACTOR_0          0x2ee0
#define R_PP_TXCBLEND_0         0x2f00
#define R_PP_TXCBLEND2_0        0x2f04
#define R_PP_TXABLEND_0         0x2f08
#define R_PP_TXABLEND2_0        0x2f0c
#define R_PP_TXBLEND_STRIDE     0x10
#define   R_TXC_ARG_A_SHIFT       0      /* colour args: 0 zero, 4 diffuse, 10 R0, 12 R1 */
#define   R_TXC_ARG_B_SHIFT       5
#define   R_TXC_ARG_C_SHIFT       10
#define   R_TXC_ARG_ZERO          0UL
#define   R_TXC_ARG_DIFFUSE_COLOR 4UL
#define   R_TXC_ARG_R0_COLOR      10UL
#define   R_TXC_ARG_R1_COLOR      12UL
#define   R_TXA_ARG_DIFFUSE_ALPHA 4UL
#define   R_TXA_ARG_R0_ALPHA      10UL
#define   R_TXA_ARG_R1_ALPHA      12UL
#define   R_TXC_OP_MADD           (0UL << 28)    /* A * B + C */
#define   R_TXC_COMP_ARG_B        (1UL << 20)    /* B = 1 - B (B = ZERO gives one) */
#define   R_TXA_COMP_ARG_B        (1UL << 20)    /* the same bit in TXABLEND */
#define   R_TXC_CLAMP_0_1         (1UL << 12)    /* in TXCBLEND2 / TXABLEND2 */
#define   R_TXC_OUTPUT_REG_R0     (1UL << 16)
#define   R_TXC_TFACTOR_SEL_SHIFT 0      /* in TXCBLEND2 / TXABLEND2 */
#define R_RB3D_BLENDCOLOR       0x3218
#define R_RB3D_ABLENDCNTL       0x321c
#define R_RB3D_CBLENDCNTL       0x3220

/* --- Caches ----------------------------------------------------------------- */
#define R_RB3D_ZCACHE_CTLSTAT   0x3254
#define   R_RB3D_ZC_FLUSH_ALL     0x5UL
#define R_RB3D_DSTCACHE_CTLSTAT 0x325c
#define   R_RB3D_DC_FLUSH         (3UL << 0)
#define   R_RB3D_DC_FLUSH_ALL     0xfUL
#define R_RB2D_DSTCACHE_CTLSTAT 0x342c

#endif /* R200REG_H */
