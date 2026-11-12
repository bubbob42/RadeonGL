/*
 * frame.c -- glClear and the present.
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
    if (mask == 0 || CORE_SCISSOR_EMPTY(c))
        return;                     /* the scissor box limits clears too */
    CORE_ROOM(c, CORE_DRAW_DW);
    Core_Validate(c);               /* scissor, target and the rest up to date */
    cs = c->cs;

    rb3d = c->target.color_format;
    plane = 0;
    if (mask & GL_COLOR_BUFFER_BIT)
    {
        if (c->color_mask != 0xf)
            rb3d |= R_PLANE_MASK_ENABLE;
        plane = Core_PlaneMask(c->target.color_format, c->color_mask);
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
    Hw_Forget(c);
}

void Core_Present(struct Core *c, const struct CoreScreen *s, LONG x, LONG y)
{
    struct R2Cs *cs;
    struct R2Tex t;
    ULONG v[12];
    ULONG w;
    ULONG h;
    ULONG cpp;
    ULONG row;
    ULONG next;
    ULONG bands;
    float fh;
    float ta, tb;

    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    w = c->target.width;
    h = c->target.height;
    /* the visible part only */
    if (x < 0 || y < 0 || (ULONG) x + w > s->width || (ULONG) y + h > s->height)
    {
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if ((ULONG) x >= s->width || (ULONG) y >= s->height)
            return;
        if ((ULONG) x + w > s->width) w = s->width - (ULONG) x;
        if ((ULONG) y + h > s->height) h = s->height - (ULONG) y;
    }
    Core_Validate(c);
    cs = c->cs;
    cpp = (c->target.color_format == R_COLOR_FORMAT_ARGB8888) ? 4 : 2;

    /* everything drawn so far must be in VRAM before it is read as a texture */
    R2E_Reg(cs, R_RB3D_DSTCACHE_CTLSTAT, R_RB3D_DC_FLUSH_ALL);
    R2E_Reg(cs, R_WAIT_UNTIL, R_WAIT_3D_IDLECLEAN);

    /* vertex state of Mesa's blit: TCL off, window xy, one st pair */
    R2E_Reg(cs, R_SE_VAP_CNTL_STATUS, cs->swap ? R_VC_NO_SWAP : R_VC_32BIT_SWAP);
    R2E_Reg(cs, R_SE_VAP_CNTL, R_VAP_FORCE_W_TO_ONE | (9UL << R_VAP_VF_MAX_VTX_NUM_SHIFT));
    R2E_Reg(cs, R_SE_VTX_STATE_CNTL, 0);
    R2E_Reg(cs, R_SE_VTE_CNTL, 0);
    R2E_VertexFormat(cs, 0, 2UL << R_VTX_TEX0_COMP_CNT_SHIFT, 0, 2UL << R_VTX_TEX0_COMP_CNT_SHIFT,
                     R_OUTPUT_XYZW | R_OUTPUT_TEX_0);
    R2E_Reg(cs, R_SE_CNTL, R_DIFFUSE_SHADE_GOURAUD | R_BFACE_SOLID | R_FFACE_SOLID
                         | R_VTX_PIX_CENTER_OGL | R_ROUND_MODE_ROUND | R_ROUND_PREC_4TH_PIX);

    /* the back buffer as texture 0: NPOT, nearest, REPLACE */
    R2E_Reg(cs, R_PP_CNTL, R_TEX_0_ENABLE | R_TEX_BLEND_0_ENABLE);
    R2E_Combiner(cs, 0, (R_TXC_ARG_R0_COLOR << R_TXC_ARG_C_SHIFT) | R_TXC_OP_MADD,
                 R_TXC_CLAMP_0_1 | R_TXC_OUTPUT_REG_R0,
                 (R_TXA_ARG_R0_ALPHA << R_TXC_ARG_C_SHIFT) | R_TXC_OP_MADD,
                 R_TXC_CLAMP_0_1 | R_TXC_OUTPUT_REG_R0);
    R2E_Reg(cs, R_PP_CNTL_X, 0);
    t.filter = R_CLAMP_S_CLAMP_LAST | R_CLAMP_T_CLAMP_LAST;
    t.format = R_TXFORMAT_NON_POWER2
             | (cpp == 4 ? (R_TXFORMAT_ARGB8888 | R_TXFORMAT_ALPHA_IN_MAP) : R_TXFORMAT_RGB565);
    t.format_x = 0;
    t.size = (c->target.width - 1) | ((c->target.height - 1) << 16);
    t.pitch = c->target.color_pitch * cpp - 32;
    t.multi = 0;
    t.offset = c->target.color_gpu;
    R2E_Texture(cs, 0, &t);
    /* (the back buffer is no texture object: the next frame's first draw
     * into it is ordered after this copy by the ring) */

    /* the screen as the colour target, the rectangle as the scissor */
    R2E_Reg(cs, R_RE_AUX_SCISSOR_CNTL, 0);
    R2E_Reg(cs, R_RE_CNTL, R_SCISSOR_ENABLE);
    R2E_Scissor(cs, (ULONG) x, (ULONG) y, (ULONG) x + w - 1, (ULONG) y + h - 1);
    R2E_Reg(cs, R_RB3D_PLANEMASK, 0xffffffffUL);
    R2E_Reg(cs, R_RB3D_BLENDCNTL, (R_BLEND_GL_ONE << R_SRC_BLEND_SHIFT)
                                | (R_BLEND_GL_ZERO << R_DST_BLEND_SHIFT));
    R2E_Reg(cs, R_RB3D_CNTL, s->format);
    R2E_ColorTarget(cs, s->gpu, s->pitch, 0);

    /* Three corners per band, texture coordinates normalised to the back
     * buffer; the fourth corner is implied. Without vertical sync one band:
     * the whole rectangle. With it, bands from the top down (2026-11-12):
     * with one rectangle the old frame showed in a triangle in the top
     * fifth at 1280 x 720 (Tim, the user) -- the beam overtook the copy
     * somewhere inside the rectangle (two triangles? another walk order?
     * not known); the ring keeps the bands in order, so the copy goes down
     * the screen ahead of the beam whatever happens inside one band. A band at row a is CORE_PRESENT_BAND0
     * + a / CORE_PRESENT_GROW rows high: small at the top, where the copy
     * starts only a little ahead of the beam, longer below, where it is far
     * ahead; at most CORE_PRESENT_BANDS of them (the last takes the rest),
     * so the present fits the back end's reserve. Band edges lie on whole
     * rows and pixel centres on half rows: every row is drawn by one band,
     * from its own source row (t continuous: a band's top is the last
     * band's bottom, the same bits). */
    fh = (float) c->target.height;
    v[0] = R2Cs_FloatBits((float) x);
    v[2] = R2Cs_FloatBits(0.0f);
    v[4] = R2Cs_FloatBits((float) (x + (LONG) w));
    v[6] = R2Cs_FloatBits((float) w / (float) c->target.width);
    v[8] = v[4];
    v[10] = v[6];
    ta = 0.0f;
    bands = 0;
    for (row = 0; row < h; row = next)
    {
        next = h;
        if (c->sync && ++bands < CORE_PRESENT_BANDS)
        {
            next = row + CORE_PRESENT_BAND0 + row / CORE_PRESENT_GROW;
            if (next > h)
                next = h;
        }
        tb = (float) next / fh;
        v[1] = R2Cs_FloatBits((float) (y + (LONG) next));
        v[3] = R2Cs_FloatBits(tb);
        v[5] = v[1];
        v[7] = v[3];
        v[9] = R2Cs_FloatBits((float) (y + (LONG) row));
        v[11] = R2Cs_FloatBits(ta);
        R2E_Immd(cs, R_VF_PRIM_RECT_LIST, 4, 3, v);
        ta = tb;
    }

    /* the next draw goes to the back buffer again, with the client's state */
    c->dirty |= D_INIT;
    Hw_Forget(c);
}
