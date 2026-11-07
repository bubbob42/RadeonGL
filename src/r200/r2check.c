/*
 * r2check.c -- see r2check.h.
 */

#include "r2check.h"
#include "r2pm4.h"
#include "r200reg.h"

/* --- The register whitelist -------------------------------------------------
 * Ranges of registers RadeonGL may write, every step bytes from first to
 * last. All of them are in the Linux 6.6 r200 safe list (reg_srcs/r200) or
 * written by Linux's own fence code; the 2D, CP, memory controller and
 * display registers of that list are left out on purpose.
 */

#define RF_DATAPORT 0x01    /* takes ONE_REG_WR bursts */

struct Rule
{
    ULONG first;
    ULONG last;
    ULONG step;
    ULONG flags;
    const char *name;
};

static const struct Rule rules[] =
{
    { 0x0218, 0x0218, 0x04, 0, "CRTC_GUI_TRIG_VLINE" },
    { 0x0318, 0x0318, 0x04, 0, "CRTC2_GUI_TRIG_VLINE" },
    { 0x15e0, 0x15f4, 0x04, 0, "SCRATCH_REGn" },
    { 0x1720, 0x1720, 0x04, 0, "WAIT_UNTIL" },
    { 0x1c14, 0x1c14, 0x04, 0, "PP_MISC" },
    { 0x1c18, 0x1c18, 0x04, 0, "PP_FOG_COLOR" },
    { 0x1c1c, 0x1c1c, 0x04, 0, "RE_SOLID_COLOR" },
    { 0x1c20, 0x1c20, 0x04, 0, "RB3D_BLENDCNTL" },
    { 0x1c24, 0x1c24, 0x04, 0, "RB3D_DEPTHOFFSET" },
    { 0x1c28, 0x1c28, 0x04, 0, "RB3D_DEPTHPITCH" },
    { 0x1c2c, 0x1c2c, 0x04, 0, "RB3D_ZSTENCILCNTL" },
    { 0x1c38, 0x1c38, 0x04, 0, "PP_CNTL" },
    { 0x1c3c, 0x1c3c, 0x04, 0, "RB3D_CNTL" },
    { 0x1c40, 0x1c40, 0x04, 0, "RB3D_COLOROFFSET" },
    { 0x1c44, 0x1c44, 0x04, 0, "RE_WIDTH_HEIGHT" },
    { 0x1c48, 0x1c48, 0x04, 0, "RB3D_COLORPITCH" },
    { 0x1c4c, 0x1c4c, 0x04, 0, "SE_CNTL" },
    { 0x1c50, 0x1c50, 0x04, 0, "RE_CNTL" },
    { 0x1d60, 0x1d60, 0x04, 0, "RB3D_DEPTHXY_OFFSET" },
    { 0x1d7c, 0x1d7c, 0x04, 0, "RB3D_STENCILREFMASK" },
    { 0x1d80, 0x1d80, 0x04, 0, "RB3D_ROPCNTL" },
    { 0x1d84, 0x1d84, 0x04, 0, "RB3D_PLANEMASK" },
    { 0x1d98, 0x1dac, 0x04, 0, "SE_VPORT_n" },
    { 0x1db0, 0x1db4, 0x04, 0, "SE_ZBIAS_n" },
    { 0x1db8, 0x1db8, 0x04, 0, "SE_LINE_WIDTH" },
    { 0x2080, 0x2080, 0x04, 0, "SE_VAP_CNTL" },
    { 0x2088, 0x208c, 0x04, 0, "SE_VTX_FMT_n" },
    { 0x2090, 0x2094, 0x04, 0, "SE_TCL_OUTPUT_VTX_FMT_n" },
    { 0x20b0, 0x20b0, 0x04, 0, "SE_VTE_CNTL" },
    { 0x210c, 0x210c, 0x04, 0, "SE_VF_MAX_VTX_INDX" },
    { 0x2140, 0x2140, 0x04, 0, "SE_VAP_CNTL_STATUS" },
    { 0x2180, 0x2180, 0x04, 0, "SE_VTX_STATE_CNTL" },
    { 0x2200, 0x2200, 0x04, 0, "SE_TCL_VECTOR_INDX_REG" },
    { 0x2204, 0x2204, 0x04, RF_DATAPORT, "SE_TCL_VECTOR_DATA_REG" },
    { 0x2208, 0x2208, 0x04, 0, "SE_TCL_SCALAR_INDX_REG" },
    { 0x220c, 0x220c, 0x04, RF_DATAPORT, "SE_TCL_SCALAR_DATA_REG" },
    { 0x2230, 0x2240, 0x04, 0, "SE_TCL_MATRIX_SEL_n" },
    { 0x2250, 0x2250, 0x04, 0, "SE_TCL_OUTPUT_VTX_COMP_SEL" },
    { 0x2254, 0x2260, 0x04, 0, "SE_TCL_INPUT_VTX_VECTOR_ADDR_n" },
    { 0x2268, 0x226c, 0x04, 0, "SE_TCL_LIGHT_MODEL_CTL_n" },
    { 0x2270, 0x227c, 0x04, 0, "SE_TCL_PER_LIGHT_CTL_n" },
    { 0x2284, 0x2284, 0x04, 0, "SE_TCL_STATE_FLUSH" },
    { 0x22a8, 0x22b4, 0x04, 0, "SE_TCL_TEX_PROC_CTL_n" },
    { 0x22b8, 0x22b8, 0x04, 0, "SE_TC_TEX_CYL_WRAP_CTL" },
    { 0x22c0, 0x22c0, 0x04, 0, "SE_TCL_UCP_VERT_BLEND_CTL" },
    { 0x22c4, 0x22c4, 0x04, 0, "SE_TCL_POINT_SPRITE_CNTL" },
    { 0x2648, 0x2648, 0x04, 0, "RE_POINTSIZE" },
    { 0x26c0, 0x26c0, 0x04, 0, "RE_TOP_LEFT" },
    { 0x26c4, 0x26c4, 0x04, 0, "RE_MISC" },
    { 0x26f0, 0x26f0, 0x04, 0, "RE_AUX_SCISSOR_CNTL" },
    { 0x2c00, 0x2ca0, 0x20, 0, "PP_TXFILTER_n" },
    { 0x2c04, 0x2ca4, 0x20, 0, "PP_TXFORMAT_n" },
    { 0x2c08, 0x2ca8, 0x20, 0, "PP_TXFORMAT_X_n" },
    { 0x2c0c, 0x2cac, 0x20, 0, "PP_TXSIZE_n" },
    { 0x2c10, 0x2cb0, 0x20, 0, "PP_TXPITCH_n" },
    { 0x2c14, 0x2cb4, 0x20, 0, "PP_BORDER_COLOR_n" },
    { 0x2c1c, 0x2cbc, 0x20, 0, "PP_TXMULTI_CTL_n" },
    { 0x2cc4, 0x2cc4, 0x04, 0, "PP_CNTL_X" },
    { 0x2d00, 0x2d78, 0x18, 0, "PP_TXOFFSET_n" },
    { 0x2ee0, 0x2efc, 0x04, 0, "PP_TFACTOR_n" },
    { 0x2f00, 0x2f7c, 0x04, 0, "PP_TXCBLEND/ABLEND_n" },
    { 0x3218, 0x3218, 0x04, 0, "RB3D_BLENDCOLOR" },
    { 0x321c, 0x321c, 0x04, 0, "RB3D_ABLENDCNTL" },
    { 0x3220, 0x3220, 0x04, 0, "RB3D_CBLENDCNTL" },
    { 0x3254, 0x3254, 0x04, 0, "RB3D_ZCACHE_CTLSTAT" },
    { 0x325c, 0x325c, 0x04, 0, "RB3D_DSTCACHE_CTLSTAT" }
};

#define NRULES (sizeof(rules) / sizeof(rules[0]))

/* Scalar memory: Mesa names addresses up to 0x101 (R200_SS_MAT_1_SHININESS)
 * and gives no size. 0x200 is our bound, not the chip's. */
#define SCALAR_LIMIT 0x200

static const struct Rule *FindRule(ULONG reg)
{
    ULONG i;

    for (i = 0; i < NRULES; i++)
    {
        if (reg >= rules[i].first && reg <= rules[i].last
            && (reg - rules[i].first) % rules[i].step == 0)
            return &rules[i];
    }
    return NULL;
}

const char *R2Chk_RegName(ULONG reg)
{
    const struct Rule *rule;

    rule = FindRule(reg);
    return rule != NULL ? rule->name : NULL;
}

const char *R2Chk_ErrorName(LONG code)
{
    static const char *const names[] =
    {
        "ok", "packet runs past the data", "type-1 packet",
        "register not on the whitelist", "ONE_REG_WR to a register that is no data port",
        "type-3 opcode not used", "address outside its memory",
        "misaligned address or pitch", "no vertex format / unknown format",
        "walk does not match the draw packet", "payload size does not match",
        "bad primitive or vertex count", "vertex arrays missing or too short",
        "DRAW_INDX_2 / INDX_BUFFER not paired", "index past the arrays",
        "colour or depth target missing or too small",
        "enabled texture missing or outside texture memory",
        "write outside TCL vector/scalar memory",
        "scratch register other than the fence", "register value never sent"
    };

    if (code < 0 || code > R2CE_VALUE)
        return "unknown";
    return names[code];
}

/* --- Helpers ------------------------------------------------------------------ */

struct Ctx
{
    const struct R2ChkCfg *cfg;
    struct R2ChkState *st;
    struct R2ChkError *err;
    const ULONG *dw;
    ULONG swap;
    ULONG at0;
    ULONG pos;      /* the current packet's header */
};

static ULONG Swap(ULONG v)
{
    return (v >> 24) | ((v >> 8) & 0xff00UL) | ((v << 8) & 0xff0000UL) | (v << 24);
}

static ULONG D(const struct Ctx *c, ULONG i)
{
    ULONG v;

    v = c->dw[i];
    return c->swap ? Swap(v) : v;
}

static LONG Err(struct Ctx *c, LONG code, ULONG what, ULONG value)
{
    c->err->at = c->at0 + c->pos;
    c->err->code = code;
    c->err->what = what;
    c->err->value = value;
    return code;
}

/* len bytes at gpu lie inside one range that may hold kind. */
static BOOL InRange(const struct R2ChkCfg *cfg, ULONG kind, ULONG gpu, ULONG len)
{
    ULONG i;
    ULONG off;

    for (i = 0; i < cfg->nranges && i < R2CHK_MAX_RANGES; i++)
    {
        if ((cfg->range[i].kinds & kind) == 0 || gpu < cfg->range[i].gpu)
            continue;
        off = gpu - cfg->range[i].gpu;
        if (off < cfg->range[i].size && len <= cfg->range[i].size - off)
            return TRUE;
    }
    return FALSE;
}

/* Bytes per pixel of RB3D_CNTL's colour format (Linux r200_packet0_check);
 * 0 for a format we do not render to. */
static ULONG ColorCpp(ULONG rb3d)
{
    switch ((rb3d >> R_COLOR_FORMAT_SHIFT) & 0xf)
    {
        case 3: case 4: case 15:
            return 2;
        case 6:
            return 4;
        case 9:
            return 1;
    }
    return 0;
}

/* Bytes per pixel of RB3D_ZSTENCILCNTL's depth format (Linux). */
static ULONG DepthCpp(ULONG z)
{
    switch (z & 0xf)
    {
        case 0:
            return 2;
        case 2: case 3: case 4: case 5: case 9: case 11:
            return 4;
    }
    return 0;
}

/* Bytes per texel of PP_TXFORMAT's format (Linux); 0 for the YUV,
 * bump-map and compressed formats RadeonGL never uses. */
static ULONG TexCpp(ULONG format)
{
    switch (format & R_TXFORMAT_FORMAT_MASK)
    {
        case 0: case 2: case 8:             /* I8, RGB332, Y8 */
            return 1;
        case 1: case 3: case 4: case 5:     /* AI88, ARGB1555, RGB565, ARGB4444 */
            return 2;
        case 6: case 7: case 22:            /* ARGB8888, RGBA8888, ABGR8888 */
            return 4;
    }
    return 0;
}

ULONG R2Chk_VtxDwords(ULONG fmt0, ULONG fmt1)
{
    ULONG size;
    ULONG i;
    ULONG c;

    /* Linux 6.6 r200.c r200_get_vtx_size_0 + _1 */
    size = 2;
    if (fmt0 & (1UL << 0))  size += 1;      /* Z0 */
    if (fmt0 & (1UL << 1))  size += 1;      /* W0 */
    size += (fmt0 >> 2) & 7;                /* blend weights */
    if (fmt0 & (1UL << 5))  size += 1;      /* PV matrix select */
    if (fmt0 & (1UL << 6))  size += 3;      /* N0 */
    if (fmt0 & (1UL << 7))  size += 1;      /* point size */
    if (fmt0 & (1UL << 8))  size += 1;      /* discrete fog */
    if (fmt0 & (1UL << 9))  size += 1;      /* shininess 0 */
    if (fmt0 & (1UL << 10)) size += 1;      /* shininess 1 */
    for (i = 0; i < 8; i++)
    {
        c = (fmt0 >> (11 + 2 * i)) & 3;
        size += (c == 1) ? 1 : (c == 2) ? 3 : (c == 3) ? 4 : 0;
    }
    if (fmt0 & (1UL << 28)) size += 2;      /* XY1 */
    if (fmt0 & (1UL << 29)) size += 1;      /* Z1 */
    if (fmt0 & (1UL << 30)) size += 1;      /* W1 */
    if (fmt0 & (1UL << 31)) size += 3;      /* N1 */
    for (i = 0; i < 6; i++)
    {
        c = (fmt1 >> (i * 3)) & 7;
        if (c <= 4)
            size += c;
    }
    return size;
}

static BOOL PrimOk(ULONG prim, ULONG n)
{
    switch (prim)
    {
        case R_VF_PRIM_POINTS:          return (BOOL) (n >= 1);
        case R_VF_PRIM_LINES:           return (BOOL) (n >= 2 && (n & 1) == 0);
        case R_VF_PRIM_LINE_STRIP:
        case R_VF_PRIM_LINE_LOOP:       return (BOOL) (n >= 2);
        case R_VF_PRIM_TRIANGLES:
        case R_VF_PRIM_RECT_LIST:       return (BOOL) (n >= 3 && n % 3 == 0);
        case R_VF_PRIM_TRIANGLE_FAN:
        case R_VF_PRIM_TRIANGLE_STRIP:
        case R_VF_PRIM_POLYGON:         return (BOOL) (n >= 3);
        case R_VF_PRIM_QUADS:           return (BOOL) (n >= 4 && (n & 3) == 0);
        case R_VF_PRIM_QUAD_STRIP:      return (BOOL) (n >= 4 && (n & 1) == 0);
    }
    return FALSE;
}

/* --- Register writes --------------------------------------------------------- */

static LONG OnReg(struct Ctx *c, ULONG reg, ULONG val)
{
    struct R2ChkState *st;
    ULONG unit;
    ULONG off;

    st = c->st;
    switch (reg)
    {
        case R_SE_VTX_FMT_0:
            st->vtx_fmt0 = val;
            st->have |= R2CHK_HAVE_FMT0;
            return R2CE_OK;
        case R_SE_VTX_FMT_1:
            /* six texture-coordinate counts of 0..4; codes 5-7 and the
             * bits above are no format RadeonGL sends (Linux counts them
             * as 0: the fetcher might not) */
            if (val & 0xfffc0000UL)
                return Err(c, R2CE_FORMAT, reg, val);
            for (unit = 0; unit < 6; unit++)
                if (((val >> (3 * unit)) & 7) > 4)
                    return Err(c, R2CE_FORMAT, reg, val);
            st->vtx_fmt1 = val;
            st->have |= R2CHK_HAVE_FMT1;
            return R2CE_OK;
        case R_SE_VAP_CNTL:
            st->vap_cntl = val;
            st->have |= R2CHK_HAVE_VAP;
            return R2CE_OK;
        case R_RB3D_CNTL:
            if (val & R_DEPTHXY_OFFSET_ENABLE)
                return Err(c, R2CE_VALUE, reg, val);    /* Linux refuses it too */
            if (val & R_STENCIL_ENABLE)
                return Err(c, R2CE_VALUE, reg, val);    /* no stencil buffer here */
            if (ColorCpp(val) == 0)
                return Err(c, R2CE_FORMAT, reg, val);
            st->rb3d_cntl = val;
            st->have |= R2CHK_HAVE_RB3D;
            return R2CE_OK;
        case R_RB3D_COLOROFFSET:
            if (val & 0xf)
                return Err(c, R2CE_ALIGN, reg, val);
            if (!InRange(c->cfg, R2K_COLOR, val, 1))
                return Err(c, R2CE_ADDR, reg, val);
            st->color_offset = val;
            st->have |= R2CHK_HAVE_COLOR;
            return R2CE_OK;
        case R_RB3D_COLORPITCH:
            st->color_pitch = val;
            st->have |= R2CHK_HAVE_CPITCH;
            return R2CE_OK;
        case R_RB3D_DEPTHOFFSET:
            if (val & 0xf)
                return Err(c, R2CE_ALIGN, reg, val);
            if (!InRange(c->cfg, R2K_DEPTH, val, 1))
                return Err(c, R2CE_ADDR, reg, val);
            st->depth_offset = val;
            st->have |= R2CHK_HAVE_DEPTH;
            return R2CE_OK;
        case R_RB3D_DEPTHPITCH:
            if (val & ~R_DEPTHPITCH_MASK)
                return Err(c, R2CE_VALUE, reg, val);    /* tiling: another footprint */
            st->depth_pitch = val;
            st->have |= R2CHK_HAVE_DPITCH;
            return R2CE_OK;
        case R_RB3D_ZSTENCILCNTL:
            if (DepthCpp(val) == 0)
                return Err(c, R2CE_FORMAT, reg, val);
            st->zstencil = val;
            st->have |= R2CHK_HAVE_ZSTENCIL;
            return R2CE_OK;
        case R_RE_TOP_LEFT:
            st->re_top_left = val;
            return R2CE_OK;
        case R_RE_WIDTH_HEIGHT:
            st->re_bottom_right = val;
            st->have |= R2CHK_HAVE_SCISSOR;
            return R2CE_OK;
        case R_RE_CNTL:
            st->re_cntl = val;
            st->have |= R2CHK_HAVE_RECNTL;
            return R2CE_OK;
        case R_PP_CNTL_X:
            if (val != 0)
                return Err(c, R2CE_VALUE, reg, val);    /* second-pass lookups */
            return R2CE_OK;
        case R_CRTC_GUI_TRIG_VLINE:
        case R_CRTC2_GUI_TRIG_VLINE:
            /* the present's vertical sync (R2E_WaitVline) and nothing else:
             * "wait while inside start..end-1", a window that ends. Every
             * WAIT_CRTC_VLINE needs one of its own (a stale or garbage
             * window could hold the CP for good). */
            if ((val & ~((R_TRIG_VLINE_LINE_MASK << R_TRIG_VLINE_START_SHIFT)
                         | (R_TRIG_VLINE_LINE_MASK << R_TRIG_VLINE_END_SHIFT)
                         | R_TRIG_VLINE_INV | R_TRIG_VLINE_STALL)) != 0
                || !(val & R_TRIG_VLINE_INV)
                || ((val >> R_TRIG_VLINE_START_SHIFT) & R_TRIG_VLINE_LINE_MASK)
                   >= ((val >> R_TRIG_VLINE_END_SHIFT) & R_TRIG_VLINE_LINE_MASK))
                return Err(c, R2CE_VALUE, reg, val);
            st->have |= (reg == R_CRTC_GUI_TRIG_VLINE) ? R2CHK_HAVE_VLINE1 : R2CHK_HAVE_VLINE2;
            return R2CE_OK;
        case R_WAIT_UNTIL:
            if (val & R_WAIT_CRTC_VLINE)
            {
                /* the vertical sync: alone, after its window */
                if ((val & ~(R_WAIT_CRTC_VLINE | R_ENG_DISPLAY_CRTC2)) != 0
                    || !(st->have & ((val & R_ENG_DISPLAY_CRTC2) ? R2CHK_HAVE_VLINE2 : R2CHK_HAVE_VLINE1)))
                    return Err(c, R2CE_VALUE, reg, val);
                st->have &= ~((val & R_ENG_DISPLAY_CRTC2) ? R2CHK_HAVE_VLINE2 : R2CHK_HAVE_VLINE1);
                return R2CE_OK;
            }
            if (val & ~(R_WAIT_2D_IDLE | R_WAIT_3D_IDLE | R_WAIT_2D_IDLECLEAN | R_WAIT_3D_IDLECLEAN))
                return Err(c, R2CE_VALUE, reg, val);    /* no other display waits */
            return R2CE_OK;
        case R_PP_CNTL:
            st->pp_cntl = val;
            st->have |= R2CHK_HAVE_PPCNTL;
            return R2CE_OK;
        case R_SE_VF_MAX_VTX_INDX:
            if (val > 0xffffUL)
                return Err(c, R2CE_ARRAYS, reg, val);   /* 16-bit indices: and no wrap below */
            st->max_vtx_indx = val;
            st->have |= R2CHK_HAVE_MAXINDX;
            return R2CE_OK;
        case R_SE_TCL_VECTOR_INDX_REG:
            if (((val >> R_VEC_INDX_OCTWORD_STRIDE_SHIFT) & 0xfff) != 1)
                return Err(c, R2CE_VALUE, reg, val);    /* we upload contiguous vectors only */
            st->vec_addr = (val & 0xffffUL) * 4;
            st->have |= R2CHK_HAVE_VEC;
            return R2CE_OK;
        case R_SE_TCL_VECTOR_DATA_REG:
            if (!(st->have & R2CHK_HAVE_VEC) || st->vec_addr >= R_VS_MAX * 4)
                return Err(c, R2CE_TCLMEM, reg, st->vec_addr / 4);
            st->vec_addr++;
            return R2CE_OK;
        case R_SE_TCL_SCALAR_INDX_REG:
            if (((val >> R_SCAL_INDX_DWORD_STRIDE_SHIFT) & 0xfff) != 1)
                return Err(c, R2CE_VALUE, reg, val);
            st->scal_addr = val & 0xffffUL;
            st->have |= R2CHK_HAVE_SCAL;
            return R2CE_OK;
        case R_SE_TCL_SCALAR_DATA_REG:
            if (!(st->have & R2CHK_HAVE_SCAL) || st->scal_addr >= SCALAR_LIMIT)
                return Err(c, R2CE_TCLMEM, reg, st->scal_addr);
            st->scal_addr++;
            return R2CE_OK;
        case R_SCRATCH_REG0: case R_SCRATCH_REG1: case R_SCRATCH_REG2:
        case R_SCRATCH_REG3: case R_SCRATCH_REG4: case R_SCRATCH_REG5:
            if (reg != c->cfg->fence_reg)
                return Err(c, R2CE_FENCE, reg, val);
            return R2CE_OK;
    }
    if (reg >= R_PP_TXFILTER_0 && reg < R_PP_TXFILTER_0 + 6 * R_PP_TXUNIT_STRIDE)
    {
        unit = (reg - R_PP_TXFILTER_0) / R_PP_TXUNIT_STRIDE;
        off = (reg - R_PP_TXFILTER_0) % R_PP_TXUNIT_STRIDE;
        if (off == 0x00)
            st->tex[unit].filter = val;
        else if (off == 0x04)
            st->tex[unit].format = val;
        else if (off == 0x08)
        {
            /* depth 1 and plain or projective coordinates: no volume or
             * cube map, whose faces' offsets are not on the whitelist */
            if ((val & 0xfUL) != 0 || (((val >> 16) & 7) != 0 && ((val >> 16) & 7) != 3))
                return Err(c, R2CE_VALUE, reg, val);
            st->tex[unit].format_x = val;
        }
        else if (off == 0x0c)
            st->tex[unit].size = val;
        else if (off == 0x10)
            st->tex[unit].pitch = val;
        return R2CE_OK;
    }
    if (reg >= R_PP_TXOFFSET_0 && reg <= R_PP_TXOFFSET_0 + 5 * R_PP_TXOFFSET_STRIDE)
    {
        unit = (reg - R_PP_TXOFFSET_0) / R_PP_TXOFFSET_STRIDE;
        if (val & 0x1cUL)
            return Err(c, R2CE_VALUE, reg, val);        /* tiled: another footprint */
        if (!InRange(c->cfg, R2K_TEXTURE, val & R_TXO_OFFSET_MASK, 1))
            return Err(c, R2CE_ADDR, reg, val);
        st->tex[unit].offset = val & R_TXO_OFFSET_MASK;
        st->tex[unit].have_offset = 1;
    }
    return R2CE_OK;
}

static LONG Packet0(struct Ctx *c, ULONG h, ULONG n)
{
    const struct Rule *rule;
    ULONG reg;
    ULONG one;
    ULONG count;
    ULONG i;
    ULONG r;
    LONG e;

    if (h & 0x6000UL)
        return Err(c, R2CE_REG, h, 0);  /* register bits the decode above ignores */
    reg = R2_PKT0_REG(h);
    one = R2_PKT0_ONE_REG_WR(h);
    count = R2_PKT_COUNT(h) + 1;
    if (count > n - c->pos - 1)
        return Err(c, R2CE_TRUNC, h, count);
    for (i = 0; i < count; i++)
    {
        r = one ? reg : reg + 4 * i;
        rule = FindRule(r);
        if (rule == NULL)
            return Err(c, R2CE_REG, r, D(c, c->pos + 1 + i));
        if (one && !(rule->flags & RF_DATAPORT))
            return Err(c, R2CE_ONEREG, r, count);
        e = OnReg(c, r, D(c, c->pos + 1 + i));
        if (e != R2CE_OK)
            return e;
    }
    if (one && reg == R_SE_TCL_VECTOR_DATA_REG && (count & 3) != 0)
        return Err(c, R2CE_TCLMEM, reg, count);
    c->st->dw_reg += 1 + count;
    return R2CE_OK;
}

/* --- Draws ------------------------------------------------------------------- */

static LONG CheckTargets(struct Ctx *c)
{
    struct R2ChkState *st;
    ULONG rows;
    ULONG right;
    ULONG pitch;
    ULONG cpp;

    st = c->st;
    if ((st->have & (R2CHK_HAVE_COLOR | R2CHK_HAVE_CPITCH | R2CHK_HAVE_RB3D | R2CHK_HAVE_SCISSOR))
        != (R2CHK_HAVE_COLOR | R2CHK_HAVE_CPITCH | R2CHK_HAVE_RB3D | R2CHK_HAVE_SCISSOR))
        return Err(c, R2CE_TARGET, R_RB3D_COLOROFFSET, st->have);
    /* the box clips only when RE_CNTL says so (Mesa sets it always) */
    if (!(st->have & R2CHK_HAVE_RECNTL) || !(st->re_cntl & R_SCISSOR_ENABLE))
        return Err(c, R2CE_VALUE, R_RE_CNTL, st->re_cntl);
    rows = ((st->re_bottom_right >> 16) & 0xffffUL) + 1;
    right = st->re_bottom_right & 0xffffUL;
    pitch = st->color_pitch & R_COLORPITCH_MASK;
    cpp = ColorCpp(st->rb3d_cntl);
    if (pitch == 0)
        return Err(c, R2CE_ALIGN, R_RB3D_COLORPITCH, st->color_pitch);
    if (right >= pitch)
        return Err(c, R2CE_TARGET, R_RE_WIDTH_HEIGHT, st->re_bottom_right);
    if ((st->color_pitch & R_COLOR_TILE_ENABLE)
        && ((st->color_offset & 0x7ffUL) != 0 || ((pitch * cpp) & 0xffUL) != 0))
        return Err(c, R2CE_ALIGN, R_RB3D_COLOROFFSET, st->color_offset);  /* mirq: 2 KB, pitch 256 B */
    if (!InRange(c->cfg, R2K_COLOR, st->color_offset, pitch * cpp * rows))
        return Err(c, R2CE_TARGET, R_RB3D_COLOROFFSET, pitch * cpp * rows);
    if (st->rb3d_cntl & R_Z_ENABLE)
    {
        if ((st->have & (R2CHK_HAVE_DEPTH | R2CHK_HAVE_DPITCH | R2CHK_HAVE_ZSTENCIL))
            != (R2CHK_HAVE_DEPTH | R2CHK_HAVE_DPITCH | R2CHK_HAVE_ZSTENCIL))
            return Err(c, R2CE_TARGET, R_RB3D_DEPTHOFFSET, st->have);
        pitch = st->depth_pitch & R_DEPTHPITCH_MASK;
        cpp = DepthCpp(st->zstencil);
        if (pitch == 0)
            return Err(c, R2CE_ALIGN, R_RB3D_DEPTHPITCH, st->depth_pitch);
        if (right >= pitch)
            return Err(c, R2CE_TARGET, R_RE_WIDTH_HEIGHT, st->re_bottom_right);
        if (!InRange(c->cfg, R2K_DEPTH, st->depth_offset, pitch * cpp * rows))
            return Err(c, R2CE_TARGET, R_RB3D_DEPTHOFFSET, pitch * cpp * rows);
    }
    return R2CE_OK;
}

static LONG CheckTextures(struct Ctx *c)
{
    const struct R2ChkTex *t;
    ULONG u;
    ULONG cpp;
    ULONG bytes;
    ULONG lw;
    ULONG lh;
    ULONG levels;
    ULONG l;
    ULONG w;
    ULONG h;

    for (u = 0; u < 6; u++)
    {
        if (!(c->st->pp_cntl & (R_TEX_0_ENABLE << u)))
            continue;
        t = &c->st->tex[u];
        if (t->format & R_TXFORMAT_LOOKUP_DISABLE)
            continue;
        if (!t->have_offset)
            return Err(c, R2CE_TEXTURE, R_PP_TXOFFSET_0 + u * R_PP_TXOFFSET_STRIDE, u);
        cpp = TexCpp(t->format);
        if (cpp == 0)
            return Err(c, R2CE_FORMAT, R_PP_TXFORMAT_0 + u * R_PP_TXUNIT_STRIDE, t->format);
        if (t->format & R_TXFORMAT_NON_POWER2)
        {
            /* one level, rows (TXPITCH + 32) bytes apart (Linux, Mesa); the
             * pitch's field only (else (pitch + 32) * h wraps to nothing) */
            if ((t->pitch & ~0x3fe0UL) != 0 || (t->filter & R_MAX_MIP_LEVEL_MASK) != 0)
                return Err(c, R2CE_VALUE, R_PP_TXPITCH_0 + u * R_PP_TXUNIT_STRIDE, t->pitch);
            w = (t->size & 0x7ffUL) + 1;
            h = ((t->size >> 16) & 0x7ffUL) + 1;
            if (((t->pitch + 32) & 31) != 0 || w * cpp > t->pitch + 32)
                return Err(c, R2CE_ALIGN, R_PP_TXPITCH_0 + u * R_PP_TXUNIT_STRIDE, t->pitch);
            bytes = (t->pitch + 32) * h;
        }
        else
        {
            /* every level's rows padded to 32 bytes (Mesa radeon_mipmap_tree) */
            lw = (t->format >> R_TXFORMAT_WIDTH_SHIFT) & 0xf;
            lh = (t->format >> R_TXFORMAT_HEIGHT_SHIFT) & 0xf;
            if (lw > 11 || lh > 11)
                return Err(c, R2CE_TEXTURE, R_PP_TXFORMAT_0 + u * R_PP_TXUNIT_STRIDE, t->format);
            levels = (t->filter & R_MAX_MIP_LEVEL_MASK) >> R_MAX_MIP_LEVEL_SHIFT;
            bytes = 0;
            for (l = 0; l <= levels; l++)
            {
                w = (1UL << lw) >> l;
                h = (1UL << lh) >> l;
                if (w == 0)
                    w = 1;
                if (h == 0)
                    h = 1;
                bytes += ((w * cpp + 31) & ~31UL) * h;
            }
        }
        if (!InRange(c->cfg, R2K_TEXTURE, t->offset, bytes))
            return Err(c, R2CE_TEXTURE, R_PP_TXOFFSET_0 + u * R_PP_TXOFFSET_STRIDE, bytes);
    }
    return R2CE_OK;
}

/* What every draw needs: a vertex set-up, a primitive that takes nverts,
 * a TCL flag that agrees with SE_VAP_CNTL (Mesa sends TCL_OUTPUT_VTX_ENABLE
 * with every TCL draw and never without TCL), targets, textures. */
static LONG CheckDraw(struct Ctx *c, ULONG cntl, ULONG nverts)
{
    struct R2ChkState *st;
    LONG e;

    st = c->st;
    if ((st->have & (R2CHK_HAVE_FMT0 | R2CHK_HAVE_FMT1 | R2CHK_HAVE_VAP | R2CHK_HAVE_PPCNTL))
        != (R2CHK_HAVE_FMT0 | R2CHK_HAVE_FMT1 | R2CHK_HAVE_VAP | R2CHK_HAVE_PPCNTL))
        return Err(c, R2CE_FORMAT, R_SE_VTX_FMT_0, st->have);
    if (!PrimOk(cntl & R_VF_PRIM_MASK, nverts))
        return Err(c, R2CE_PRIM, cntl & R_VF_PRIM_MASK, nverts);
    if (((cntl & R_VF_TCL_OUTPUT_VTX_ENABLE) != 0) != ((st->vap_cntl & R_VAP_TCL_ENABLE) != 0))
        return Err(c, R2CE_VALUE, R_SE_VF_CNTL, cntl);
    if (cntl & R_VF_INDEX_SZ_4)
        return Err(c, R2CE_VALUE, R_SE_VF_CNTL, cntl);
    e = CheckTargets(c);
    if (e != R2CE_OK)
        return e;
    e = CheckTextures(c);
    if (e != R2CE_OK)
        return e;
    st->draws++;
    st->vertices += nverts;
    return R2CE_OK;
}

/* The arrays hold nverts vertices of the current format. */
static LONG CheckArrays(struct Ctx *c, ULONG nverts)
{
    struct R2ChkState *st;
    ULONG i;
    ULONG sum;
    ULONG bytes;

    st = c->st;
    if (st->naos == 0)
        return Err(c, R2CE_ARRAYS, R2_OP_3D_LOAD_VBPNTR, 0);
    sum = 0;
    for (i = 0; i < st->naos; i++)
        sum += st->aos[i].comps;
    if (sum != R2Chk_VtxDwords(st->vtx_fmt0, st->vtx_fmt1))
        return Err(c, R2CE_ARRAYS, R_SE_VTX_FMT_0, sum);
    for (i = 0; i < st->naos; i++)
    {
        /* Linux: esize * (nverts - 1) * 4, plus the last element itself */
        bytes = 4 * (st->aos[i].stride * (nverts - 1) + st->aos[i].comps);
        if (!InRange(c->cfg, R2K_VERTEX, st->aos[i].gpu, bytes))
            return Err(c, R2CE_ARRAYS, st->aos[i].gpu, bytes);
    }
    return R2CE_OK;
}

static LONG IndxBuffer(struct Ctx *c, ULONG count)
{
    struct R2ChkState *st;
    ULONG addr;
    ULONG ndw;
    ULONG nidx;
    ULONG i;
    ULONG v;
    ULONG idx;
    LONG e;

    st = c->st;
    v = 0;
    if (st->pend_indx == 0)
        return Err(c, R2CE_INDX, R2_OP_INDX_BUFFER, 0);
    nidx = st->pend_indx;
    st->pend_indx = 0;
    if (count != 3)
        return Err(c, R2CE_SIZE, R2_OP_INDX_BUFFER, count);
    if (D(c, c->pos + 1) != R2_INDX_BUFFER_CNTL)
        return Err(c, R2CE_INDX, R2_OP_INDX_BUFFER, D(c, c->pos + 1));
    addr = D(c, c->pos + 2);
    ndw = D(c, c->pos + 3);
    if (addr & 3)
        return Err(c, R2CE_ALIGN, R2_OP_INDX_BUFFER, addr);
    if (ndw != (nidx + 1) / 2)
        return Err(c, R2CE_SIZE, R2_OP_INDX_BUFFER, ndw);
    if (!InRange(c->cfg, R2K_INDEX, addr, ndw * 4))
        return Err(c, R2CE_ADDR, addr, ndw * 4);
    e = CheckDraw(c, st->pend_cntl, nidx);
    if (e != R2CE_OK)
        return e;
    if (!(st->have & R2CHK_HAVE_MAXINDX) || st->max_vtx_indx == 0)
        return Err(c, R2CE_ARRAYS, R_SE_VF_MAX_VTX_INDX, st->max_vtx_indx);
    e = CheckArrays(c, st->max_vtx_indx);
    if (e != R2CE_OK)
        return e;
    if (c->cfg->peek == NULL)
    {
        st->index_unchecked++;
        return R2CE_OK;
    }
    for (i = 0; i < nidx; i++)
    {
        if ((i & 1) == 0)
        {
            if (c->cfg->peek(c->cfg->peek_ctx, addr + 2 * i, &v) != 0)
                return Err(c, R2CE_ADDR, addr + 2 * i, 0);
            if (c->swap)
                v = Swap(v);
        }
        idx = (i & 1) ? (v >> 16) : (v & 0xffffUL);      /* first index in the low half */
        if (idx >= st->max_vtx_indx)
            return Err(c, R2CE_INDEX, i, idx);
    }
    return R2CE_OK;
}

static LONG LoadVbpntr(struct Ctx *c, ULONG count)
{
    struct R2ChkState *st;
    ULONG nr;
    ULONG want;
    ULONG i;
    ULONG k;
    ULONG attr;

    st = c->st;
    st->naos = 0;
    nr = D(c, c->pos + 1);
    if (nr == 0 || nr > 12)
        return Err(c, R2CE_ARRAYS, R2_OP_3D_LOAD_VBPNTR, nr);
    want = 1 + (nr >> 1) * 3 + (nr & 1) * 2;
    if (count != want)
        return Err(c, R2CE_SIZE, R2_OP_3D_LOAD_VBPNTR, count);
    k = c->pos + 2;
    for (i = 0; i < nr; i += 2)
    {
        attr = D(c, k);
        if (attr & 0x80008000UL)
            return Err(c, R2CE_ARRAYS, R2_OP_3D_LOAD_VBPNTR, attr);    /* stride is 7 bits */
        st->aos[i].comps = attr & 0xff;
        st->aos[i].stride = (attr >> 8) & 0x7f;
        st->aos[i].gpu = D(c, k + 1);
        if (i + 1 < nr)
        {
            st->aos[i + 1].comps = (attr >> 16) & 0xff;
            st->aos[i + 1].stride = (attr >> 24) & 0x7f;
            st->aos[i + 1].gpu = D(c, k + 2);
            k += 3;
        }
        else
            k += 2;
    }
    for (i = 0; i < nr; i++)
    {
        if (st->aos[i].comps == 0)
            return Err(c, R2CE_ARRAYS, st->aos[i].gpu, 0);
        if (st->aos[i].gpu & 3)
            return Err(c, R2CE_ALIGN, R2_OP_3D_LOAD_VBPNTR, st->aos[i].gpu);
        if (!InRange(c->cfg, R2K_VERTEX, st->aos[i].gpu, 4 * st->aos[i].comps))
            return Err(c, R2CE_ADDR, st->aos[i].gpu, 4 * st->aos[i].comps);
    }
    st->naos = nr;
    return R2CE_OK;
}

static LONG Packet3(struct Ctx *c, ULONG h, ULONG n)
{
    struct R2ChkState *st;
    ULONG op;
    ULONG count;
    ULONG cntl;
    ULONG nv;
    LONG e;

    st = c->st;
    op = R2_PKT3_OPCODE(h);
    count = R2_PKT_COUNT(h) + 1;            /* payload dwords */
    if (count > n - c->pos - 1)
        return Err(c, R2CE_TRUNC, h, count);
    if (st->pend_indx != 0 && op != R2_OP_INDX_BUFFER)
        return Err(c, R2CE_INDX, op, st->pend_at);
    switch (op)
    {
        case R2_OP_NOP:
            st->dw_nop += 1 + count;
            return R2CE_OK;

        case R2_OP_3D_LOAD_VBPNTR:
            st->dw_draw += 1 + count;
            return LoadVbpntr(c, count);

        case R2_OP_3D_DRAW_IMMD_2:
            cntl = D(c, c->pos + 1);
            nv = cntl >> R_VF_VERTEX_NUMBER_SHIFT;
            if ((cntl & R_VF_PRIM_WALK_MASK) != R_VF_PRIM_WALK_RING)
                return Err(c, R2CE_WALK, op, cntl);
            e = CheckDraw(c, cntl, nv);
            if (e != R2CE_OK)
                return e;
            /* Linux r100_cs_track_check walk 3: vtx_size * nverts == count */
            if (count - 1 != nv * R2Chk_VtxDwords(st->vtx_fmt0, st->vtx_fmt1))
                return Err(c, R2CE_SIZE, op, count - 1);
            st->dw_draw += 1 + count;
            return R2CE_OK;

        case R2_OP_3D_DRAW_VBUF_2:
            if (count != 1)
                return Err(c, R2CE_SIZE, op, count);
            cntl = D(c, c->pos + 1);
            nv = cntl >> R_VF_VERTEX_NUMBER_SHIFT;
            if ((cntl & R_VF_PRIM_WALK_MASK) != R_VF_PRIM_WALK_LIST)
                return Err(c, R2CE_WALK, op, cntl);
            e = CheckDraw(c, cntl, nv);
            if (e != R2CE_OK)
                return e;
            st->dw_draw += 2;
            return CheckArrays(c, nv);

        case R2_OP_3D_DRAW_INDX_2:
            if (count != 1)
                return Err(c, R2CE_SIZE, op, count);
            cntl = D(c, c->pos + 1);
            if ((cntl & R_VF_PRIM_WALK_MASK) != R_VF_PRIM_WALK_IND)
                return Err(c, R2CE_WALK, op, cntl);
            nv = cntl >> R_VF_VERTEX_NUMBER_SHIFT;
            if (nv == 0)
                return Err(c, R2CE_PRIM, cntl & R_VF_PRIM_MASK, 0);
            st->pend_indx = nv;
            st->pend_cntl = cntl;
            st->pend_at = c->at0 + c->pos;
            st->dw_draw += 2;
            return R2CE_OK;

        case R2_OP_INDX_BUFFER:
            st->dw_draw += 1 + count;
            return IndxBuffer(c, count);
    }
    return Err(c, R2CE_OPCODE, op, h);
}

void R2Chk_Reset(struct R2ChkState *st)
{
    UBYTE *p;
    ULONG i;

    /* the whole struct, not a field list that rots */
    p = (UBYTE *) st;
    for (i = 0; i < sizeof(*st); i++)
        p[i] = 0;
}

LONG R2Chk_End(struct R2ChkState *st, struct R2ChkError *err)
{
    if (st->pend_indx == 0)
        return R2CE_OK;
    err->code = R2CE_INDX;
    err->at = st->pend_at;
    err->what = R2_OP_3D_DRAW_INDX_2;
    err->value = st->pend_indx;
    st->pend_indx = 0;
    return R2CE_INDX;
}

LONG R2Chk_Run(const struct R2ChkCfg *cfg, struct R2ChkState *st,
               const ULONG *dw, ULONG n, ULONG swap, ULONG at0,
               struct R2ChkError *err)
{
    struct Ctx c;
    ULONG h;
    LONG e;

    c.cfg = cfg;
    c.st = st;
    c.err = err;
    c.dw = dw;
    c.swap = swap;
    c.at0 = at0;
    c.pos = 0;
    err->at = 0;
    err->code = R2CE_OK;
    err->what = 0;
    err->value = 0;
    while (c.pos < n)
    {
        h = D(&c, c.pos);
        st->packets++;
        switch (R2_PKT_TYPE(h))
        {
            case 0:
                if (st->pend_indx != 0)
                    return Err(&c, R2CE_INDX, R2_PKT0_REG(h), st->pend_at);
                e = Packet0(&c, h, n);
                if (e != R2CE_OK)
                    return e;
                c.pos += 2 + R2_PKT_COUNT(h);
                break;
            case 2:
                if (st->pend_indx != 0)
                    return Err(&c, R2CE_INDX, h, st->pend_at);
                st->dw_nop++;
                c.pos++;
                break;
            case 3:
                e = Packet3(&c, h, n);
                if (e != R2CE_OK)
                    return e;
                c.pos += 2 + R2_PKT_COUNT(h);
                break;
            default:
                return Err(&c, R2CE_TYPE, h, 0);
        }
    }
    return R2CE_OK;
}
