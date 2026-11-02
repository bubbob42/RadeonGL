/*
 * frame.c -- glClear.
 *
 * Clearing is a draw: one window-space RECT_LIST over the target, with TCL
 * off for that one draw (Mesa r200_blit's vertex state) and the colour,
 * depth test and masks set to what the clear needs. The scissor box applies
 * as GL wants. Every block the clear changed is marked dirty, so the next
 * draw restores the client's state. The R200's own fast clears (HyperZ)
 * are left alone: RV280 has no hierarchical Z (Linux radeon_cp.c).
 */

#include "core.h"
#include "coreint.h"
#include "r200reg.h"

#define CLEAR_BITS (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT \
                    | GL_ACCUM_BUFFER_BIT)

void Core_Clear(struct Core *c, ULONG mask)
{
    struct R2Cs *cs;
    ULONG v[12];
    ULONG rb3d;
    ULONG plane;
    ULONG color;
    float x1;
    float y1;
    float z;

    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    if (mask & ~CLEAR_BITS)
    {
        Core_Error(c, GL_INVALID_VALUE);
        return;
    }
    if (!(mask & (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)))
        return;                     /* no stencil or accumulation buffer */
    if ((mask & GL_DEPTH_BUFFER_BIT) && !c->depth_mask)
        mask &= ~GL_DEPTH_BUFFER_BIT;   /* glDepthMask(FALSE) masks clears too */
    if ((mask & GL_COLOR_BUFFER_BIT) && c->color_mask == 0)
        mask &= ~GL_COLOR_BUFFER_BIT;
    if (mask == 0)
        return;
    Core_Validate(c);               /* scissor, target and the rest up to date */
    cs = c->cs;

    rb3d = c->target.color_format;
    plane = 0;
    if (mask & GL_COLOR_BUFFER_BIT)
    {
        if (c->color_mask != 0xf)
            rb3d |= R_PLANE_MASK_ENABLE;
        plane = (c->target.color_format == R_COLOR_FORMAT_RGB565)
              ? (((c->color_mask & 1) ? 0xf800UL : 0) | ((c->color_mask & 2) ? 0x07e0UL : 0)
                 | ((c->color_mask & 4) ? 0x001fUL : 0))
              : (((c->color_mask & 8) ? 0xff000000UL : 0) | ((c->color_mask & 1) ? 0x00ff0000UL : 0)
                 | ((c->color_mask & 2) ? 0x0000ff00UL : 0) | ((c->color_mask & 4) ? 0x000000ffUL : 0));
    }
    else
        rb3d |= R_PLANE_MASK_ENABLE;    /* depth only: no colour writes at all */
    if (mask & GL_DEPTH_BUFFER_BIT)
        rb3d |= R_Z_ENABLE;

    /* TCL off, window coordinates in (Mesa r200_blit emit_vtx_state) */
    R2E_Reg(cs, R_SE_VAP_CNTL, R_VAP_FORCE_W_TO_ONE | (9UL << R_VAP_VF_MAX_VTX_NUM_SHIFT));
    R2E_Reg(cs, R_SE_VTE_CNTL, R_VTX_XY_FMT | R_VTX_Z_FMT);
    R2E_VertexFormat(cs, R_VTX_Z0 | (R_VTX_PK_RGBA << R_VTX_COLOR_0_SHIFT), 0,
                     R_VTX_Z0 | (R_VTX_PK_RGBA << R_VTX_COLOR_0_SHIFT), 0,
                     R_OUTPUT_XYZW | R_OUTPUT_COLOR_0);
    R2E_Reg(cs, R_PP_CNTL, R_TEX_BLEND_0_ENABLE);
    R2E_Combiner(cs, 0, (R_TXC_ARG_DIFFUSE_COLOR << R_TXC_ARG_C_SHIFT) | R_TXC_OP_MADD,
                 R_TXC_CLAMP_0_1 | R_TXC_OUTPUT_REG_R0,
                 (R_TXA_ARG_DIFFUSE_ALPHA << R_TXC_ARG_C_SHIFT) | R_TXC_OP_MADD,
                 R_TXC_CLAMP_0_1 | R_TXC_OUTPUT_REG_R0);
    R2E_Reg(cs, R_RB3D_CNTL, rb3d);
    R2E_Reg(cs, R_RB3D_PLANEMASK, plane);
    R2E_Reg(cs, R_RB3D_ZSTENCILCNTL, R_DEPTH_FORMAT_16BIT_INT_Z | R_Z_TEST_ALWAYS
                                   | R_STENCIL_TEST_ALWAYS | R_Z_WRITE_ENABLE);
    R2E_Reg(cs, R_SE_CNTL, R_BFACE_SOLID | R_FFACE_SOLID | R_FLAT_SHADE_VTX_LAST
                         | R_DIFFUSE_SHADE_GOURAUD | R_ALPHA_SHADE_GOURAUD
                         | R_VTX_PIX_CENTER_OGL | R_ROUND_MODE_TRUNC | R_ROUND_PREC_8TH_PIX);

    /* three corners of the target; the rect list's fourth is implied */
    x1 = (float) c->target.width;
    y1 = (float) c->target.height;
    z = c->clear_depth;
    color = (Core_Unit255(c->clear_color[3]) << 24) | (Core_Unit255(c->clear_color[2]) << 16)
          | (Core_Unit255(c->clear_color[1]) << 8) | Core_Unit255(c->clear_color[0]);
    v[0] = R2Cs_FloatBits(0.0f); v[1] = R2Cs_FloatBits(y1); v[2] = R2Cs_FloatBits(z); v[3] = color;
    v[4] = R2Cs_FloatBits(x1);   v[5] = R2Cs_FloatBits(y1); v[6] = R2Cs_FloatBits(z); v[7] = color;
    v[8] = R2Cs_FloatBits(x1);   v[9] = R2Cs_FloatBits(0.0f); v[10] = R2Cs_FloatBits(z); v[11] = color;
    R2E_Immd(cs, R_VF_PRIM_RECT_LIST | R_VF_COLOR_ORDER_RGBA, 4, 3, v);

    /* back to the client's state at the next draw */
    c->dirty |= D_VAP | D_VTXFMT | D_PPCNTL | D_COMBINE | D_RB3D | D_PLANEMASK
              | D_ZSTENCIL | D_SECNTL;
    c->hw_units = 0xffffffffUL;
}
