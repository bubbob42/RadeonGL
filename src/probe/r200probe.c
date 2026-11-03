/*
 * r200probe.c -- see r200probe.h. Read-only.
 */

#include <exec/types.h>

#include "pcicard.h"
#include "mmio.h"
#include "r200regs.h"
#include "plog.h"
#include "r200probe.h"

struct RegEntry
{
    CONST_STRPTR name;
    ULONG off;
};

/* Order: the questions in concept section 11 first (memory controller,
 * apertures, surfaces, DP_DATATYPE, CP), then the rest. */
static const struct RegEntry reg_Table[] =
{
    { "CONFIG_MEMSIZE",        R_CONFIG_MEMSIZE },
    { "CONFIG_APER_SIZE",      R_CONFIG_APER_SIZE },
    { "CONFIG_APER_0_BASE",    R_CONFIG_APER_0_BASE },
    { "CONFIG_APER_1_BASE",    R_CONFIG_APER_1_BASE },
    { "CONFIG_REG_1_BASE",     R_CONFIG_REG_1_BASE },
    { "CONFIG_REG_APER_SIZE",  R_CONFIG_REG_APER_SIZE },
    { "CONFIG_CNTL",           R_CONFIG_CNTL },
    { "CONFIG_XSTRAP",         R_CONFIG_XSTRAP },
    { "CONFIG_BONDS",          R_CONFIG_BONDS },
    { "MC_FB_LOCATION",        R_MC_FB_LOCATION },
    { "MC_AGP_LOCATION",       R_MC_AGP_LOCATION },
    { "MC_STATUS",             R_MC_STATUS },
    { "MEM_CNTL",              R_MEM_CNTL },
    { "BUS_CNTL",              R_BUS_CNTL },
    { "AIC_CNTL",              R_AIC_CNTL },
    { "HOST_PATH_CNTL",        R_HOST_PATH_CNTL },
    { "GEN_INT_CNTL",          R_GEN_INT_CNTL },
    { "GEN_INT_STATUS",        R_GEN_INT_STATUS },
    { "SURFACE_CNTL",          R_SURFACE_CNTL },
    { "DP_DATATYPE",           R_DP_DATATYPE },
    { "DP_GUI_MASTER_CNTL",    R_DP_GUI_MASTER_CNTL },
    { "DST_PITCH_OFFSET",      R_DST_PITCH_OFFSET },
    { "RBBM_GUICNTL",          R_RBBM_GUICNTL },
    { "RBBM_STATUS",           R_RBBM_STATUS },
    { "RBBM_SOFT_RESET",       R_RBBM_SOFT_RESET },
    { "WAIT_UNTIL",            R_WAIT_UNTIL },
    { "ISYNC_CNTL",            R_ISYNC_CNTL },
    { "CP_CSQ_CNTL",           R_CP_CSQ_CNTL },
    { "CP_CSQ_MODE",           R_CP_CSQ_MODE },
    { "CP_CSQ_STAT",           R_CP_CSQ_STAT },
    { "CP_RB_BASE",            R_CP_RB_BASE },
    { "CP_RB_CNTL",            R_CP_RB_CNTL },
    { "CP_RB_RPTR_ADDR",       R_CP_RB_RPTR_ADDR },
    { "CP_RB_RPTR",            R_CP_RB_RPTR },
    { "CP_RB_WPTR",            R_CP_RB_WPTR },
    { "CP_RB_WPTR_DELAY",      R_CP_RB_WPTR_DELAY },
    { "CP_IB_BASE",            R_CP_IB_BASE },
    { "CP_IB_BUFSZ",           R_CP_IB_BUFSZ },
    { "SCRATCH_UMSK",          R_SCRATCH_UMSK },
    { "SCRATCH_ADDR",          R_SCRATCH_ADDR },
    { "SCRATCH_REG0",          R_SCRATCH_REG0 },
    { "SCRATCH_REG1",          R_SCRATCH_REG1 },
    { "SCRATCH_REG2",          R_SCRATCH_REG2 },
    { "SCRATCH_REG3",          R_SCRATCH_REG3 },
    { "SCRATCH_REG4",          R_SCRATCH_REG4 },
    { "SCRATCH_REG5",          R_SCRATCH_REG5 },
    { "CRTC_GEN_CNTL",         R_CRTC_GEN_CNTL },
    { "CRTC_EXT_CNTL",         R_CRTC_EXT_CNTL },
    { "CRTC_H_TOTAL_DISP",     R_CRTC_H_TOTAL_DISP },
    { "CRTC_V_TOTAL_DISP",     R_CRTC_V_TOTAL_DISP },
    { "CRTC_OFFSET",           R_CRTC_OFFSET },
    { "CRTC_OFFSET_CNTL",      R_CRTC_OFFSET_CNTL },
    { "CRTC_PITCH",            R_CRTC_PITCH },
    { "DISPLAY_BASE_ADDR",     R_DISPLAY_BASE_ADDR },
    { "CRTC2_GEN_CNTL",        R_CRTC2_GEN_CNTL },
    { "CRTC2_OFFSET",          R_CRTC2_OFFSET },
    { "OV0_SCALE_CNTL",        R_OV0_SCALE_CNTL },
    { "RB3D_CNTL",             R_RB3D_CNTL },
    { "SE_VAP_CNTL",           R_SE_VAP_CNTL },
    { "SE_VAP_CNTL_STATUS",    R_SE_VAP_CNTL_STATUS },
    { "RB3D_ZCACHE_CTLSTAT",   R_RB3D_ZCACHE_CTLSTAT },
    { "RB3D_DSTCACHE_CTLSTAT", R_RB3D_DSTCACHE_CTLSTAT },
    { "RB2D_DSTCACHE_CTLSTAT", R_RB2D_DSTCACHE_CTLSTAT }
};

#define REG_TABLE_SIZE (sizeof(reg_Table) / sizeof(reg_Table[0]))

void R200_CardFromP96(struct PciCard *card, const struct P96Board *b)
{
    static struct PciCard zero;

    *card = zero;
    card->mmio = b->memIoBase;
    card->aper = b->memBase;
    card->bar1 = b->ioBase;
}

/* The table above, then SURFACE0..7 LOWER/UPPER/INFO. */
ULONG R200_RegisterCount(void)
{
    return REG_TABLE_SIZE + R_SURFACE_COUNT * 3;
}

ULONG R200_ReportEndian(const struct PciCard *card)
{
    ULONG raw;
    ULONG size;
    ULONG how;
    ULONG bar0;

    Log_Section("Register byte order");
    bar0 = card->cfgBar[0] & 0xfffffff0UL;
    raw = MMIO_RAW(card->mmio, R_PCI_MIRROR);
    how = Mmio_DetectSwap(card->mmio, bar0, &size);
    Log_Printf("PCI mirror (vendor, device) native read 0x%08lx, byte-swapped 0x%08lx\n",
               raw, Mmio_Swap32(raw));
    raw = MMIO_RAW(card->mmio, R_CONFIG_MEMSIZE);
    Log_Printf("CONFIG_MEMSIZE native read 0x%08lx, byte-swapped 0x%08lx\n",
               raw, Mmio_Swap32(raw));
    if (how != MMIO_BY_MEMSIZE && how != MMIO_BY_PCIMIRROR)
    {
        raw = MMIO_RAW(card->mmio, R_CONFIG_APER_0_BASE);
        Log_Printf("  not a whole number of megabytes either way (no PC BIOS set it)\n");
        if (bar0 != 0)
            Log_Printf("CONFIG_APER_0_BASE native read 0x%08lx, byte-swapped 0x%08lx; BAR0 PCI 0x%08lx\n",
                       raw, Mmio_Swap32(raw), bar0);
        else
            Log_Printf("CONFIG_APER_0_BASE native read 0x%08lx (BAR0's PCI address unknown)\n", raw);
    }
    if (how == MMIO_BY_APERSIZE || how == MMIO_UNDECIDED)
    {
        raw = MMIO_RAW(card->mmio, R_CONFIG_APER_SIZE);
        Log_Printf("CONFIG_APER_SIZE native read 0x%08lx, byte-swapped 0x%08lx\n",
                   raw, Mmio_Swap32(raw));
    }
    if (how == MMIO_UNDECIDED)
    {
        Log_Printf("UNDECIDED, assuming registers read as they are (no swap)\n");
        return MMIO_UNDECIDED;
    }
    Log_Printf("verdict: registers read %s (by %s)\n",
               Mmio_Swap ? (CONST_STRPTR)"little-endian (68k swaps every access)"
                         : (CONST_STRPTR)"right as they are (NO swap)",
               how == MMIO_BY_PCIMIRROR ? (CONST_STRPTR)"the PCI mirror's vendor ID"
               : how == MMIO_BY_MEMSIZE ? (CONST_STRPTR)"CONFIG_MEMSIZE"
               : how == MMIO_BY_APER  ? (CONST_STRPTR)"CONFIG_APER_0_BASE"
                                      : (CONST_STRPTR)"CONFIG_APER_SIZE");
    if (size != 0)
        Log_Printf("memory size %lu MB\n", size >> 20);
    return how;
}

BOOL R200_CheckFamily(const struct PciCard *card)
{
    ULONG id;
    ULONG dev;
    BOOL r200;

    id = Mmio_Read(card->mmio, R_PCI_MIRROR);
    dev = id >> 16;
    r200 = (BOOL)((id & 0xffffUL) == 0x1002UL
                  && ((dev >= 0x5148 && dev <= 0x514f)                     /* R200 */
                      || dev == 0x4966 || dev == 0x4967 || dev == 0x4c66 || dev == 0x4c67 /* RV250 */
                      || (dev >= 0x5960 && dev <= 0x5967) || dev == 0x5c61 || dev == 0x5c63)); /* RV280 */
    Log_Printf("chip: vendor 0x%04lx device 0x%04lx: %s\n", id & 0xffffUL, dev,
               r200 ? (CONST_STRPTR)"an R200-family Radeon"
                    : (CONST_STRPTR)"NOT an R200-family Radeon (R200, RV250, RV280)");
    return r200;
}

static void DumpOne(CONST_STRPTR name, ULONG off, ULONG value, ULONG *prev,
                    BOOL compare)
{
    CONST_STRPTR mark;

    mark = (CONST_STRPTR)"";
    if (prev != NULL)
    {
        if (compare && *prev != value)
            mark = (CONST_STRPTR)"   *changed*";
        *prev = value;
    }
    Log_Printf("  %-22s 0x%04lx = 0x%08lx%s\n", name, off, value, mark);
}

void R200_DumpRegisters(const struct PciCard *card, ULONG *prev, BOOL compare)
{
    ULONG i;
    ULONG n;
    ULONG base;
    ULONG *p;
    char name[24];

    Log_Section("Registers");
    p = prev;
    for (i = 0; i < REG_TABLE_SIZE; i++)
    {
        DumpOne(reg_Table[i].name, reg_Table[i].off,
                Mmio_Read(card->mmio, reg_Table[i].off), p, compare);
        if (p != NULL)
            p++;
    }
    for (n = 0; n < R_SURFACE_COUNT; n++)
    {
        base = n * R_SURFACE_STRIDE;
        name[0] = 'S'; name[1] = 'U'; name[2] = 'R'; name[3] = 'F';
        name[4] = (char)('0' + n); name[5] = '_';
        name[6] = 'L'; name[7] = 'O'; name[8] = 'W'; name[9] = '\0';
        DumpOne(name, R_SURFACE0_LOWER_BOUND + base,
                Mmio_Read(card->mmio, R_SURFACE0_LOWER_BOUND + base), p, compare);
        if (p != NULL)
            p++;
        name[6] = 'U'; name[7] = 'P'; name[8] = 'P';
        DumpOne(name, R_SURFACE0_UPPER_BOUND + base,
                Mmio_Read(card->mmio, R_SURFACE0_UPPER_BOUND + base), p, compare);
        if (p != NULL)
            p++;
        name[6] = 'I'; name[7] = 'N'; name[8] = 'F';
        DumpOne(name, R_SURFACE0_INFO + base,
                Mmio_Read(card->mmio, R_SURFACE0_INFO + base), p, compare);
        if (p != NULL)
            p++;
    }
}

static CONST_STRPTR OnOff(ULONG bit)
{
    return bit ? (CONST_STRPTR)"on" : (CONST_STRPTR)"off";
}

/* The surface windows that do something: a tiling mode or a CPU-side byte
 * swap over a VRAM range (Linux radeon_reg.h SURFACEn_INFO; R200 tile mode
 * in bits 18:16). RadeonGL's ring and heap must stay out of a swapping
 * window or follow it. */
static void DecodeSurfaces(const struct PciCard *card)
{
    ULONG n;
    ULONG off;
    ULONG lo;
    ULONG hi;
    ULONG inf;

    for (n = 0; n < R_SURFACE_COUNT; n++)
    {
        off = n * R_SURFACE_STRIDE;
        lo = Mmio_Read(card->mmio, R_SURFACE0_LOWER_BOUND + off);
        hi = Mmio_Read(card->mmio, R_SURFACE0_UPPER_BOUND + off);
        inf = Mmio_Read(card->mmio, R_SURFACE0_INFO + off);
        if ((inf & 0x00f70000UL) == 0)
            continue;               /* no tiling, no swap: no effect */
        Log_Printf("  SURFACE%lu: 0x%08lx - 0x%08lx, tile mode %lu, AP0 swap 16bpp %s 32bpp %s, "
                   "AP1 swap 16bpp %s 32bpp %s (INFO 0x%08lx)\n",
                   n, lo, hi, (inf >> 16) & 7UL,
                   OnOff(inf & R_NONSURF_AP0_SWP_16BPP), OnOff(inf & R_NONSURF_AP0_SWP_32BPP),
                   OnOff(inf & R_NONSURF_AP1_SWP_16BPP), OnOff(inf & R_NONSURF_AP1_SWP_32BPP),
                   inf);
    }
}

void R200_DecodeRegisters(const struct PciCard *card)
{
    ULONG v;
    ULONG lo;
    ULONG hi;
    ULONG aperPci;

    Log_Section("Decoded");

    v = Mmio_Read(card->mmio, R_MC_FB_LOCATION);
    lo = (v & 0xffffUL) << 16;
    hi = (v & 0xffff0000UL) | 0xffffUL;
    Log_Printf("MC_FB_LOCATION: GPU frame buffer 0x%08lx - 0x%08lx\n", lo, hi);
    aperPci = card->cfgBar[0] & 0xfffffff0UL;
    if (aperPci != 0)
        Log_Printf("  PCI BAR0 = 0x%08lx: %s\n", aperPci,
                   (aperPci == lo) ? (CONST_STRPTR)"GPU address == PCI address (flat)"
                                   : (CONST_STRPTR)"GPU address != PCI address (offset needed)");
    v = Mmio_Read(card->mmio, R_MC_AGP_LOCATION);
    Log_Printf("MC_AGP_LOCATION: 0x%08lx - 0x%08lx\n",
               (v & 0xffffUL) << 16, (v & 0xffff0000UL) | 0xffffUL);
    v = Mmio_Read(card->mmio, R_CONFIG_APER_SIZE);
    if (card->aperSize != 0)
        Log_Printf("CONFIG_APER_SIZE: %lu MB; openpci BAR0 window %lu MB\n",
                   v >> 20, card->aperSize >> 20);
    else
        Log_Printf("CONFIG_APER_SIZE: %lu MB (one aperture; the CPU sees two, AP0 and AP1)\n",
                   v >> 20);

    v = Mmio_Read(card->mmio, R_BUS_CNTL);
    Log_Printf("BUS_CNTL: bus master %s (BUS_MASTER_DIS bit 6 = %ld)\n",
               (v & R_BUS_MASTER_DIS) ? (CONST_STRPTR)"DISABLED" : (CONST_STRPTR)"enabled",
               (LONG)((v & R_BUS_MASTER_DIS) ? 1 : 0));
    if (card->vendor != 0)
        Log_Printf("PCI command register 0x%04lx: I/O %s, memory %s, bus master %s\n",
                   card->cfgCommand, OnOff(card->cfgCommand & 1),
                   OnOff(card->cfgCommand & 2), OnOff(card->cfgCommand & 4));

    v = Mmio_Read(card->mmio, R_SURFACE_CNTL);
    Log_Printf("SURFACE_CNTL: AP0 swap 16bpp %s 32bpp %s; AP1 swap 16bpp %s 32bpp %s\n",
               OnOff(v & R_NONSURF_AP0_SWP_16BPP), OnOff(v & R_NONSURF_AP0_SWP_32BPP),
               OnOff(v & R_NONSURF_AP1_SWP_16BPP), OnOff(v & R_NONSURF_AP1_SWP_32BPP));

    Log_Printf("SURFACE_CNTL: surface windows %s\n",
               (v & R_SURF_TRANSLATION_DIS) ? (CONST_STRPTR)"OFF (translation disabled)"
                                            : (CONST_STRPTR)"active");
    DecodeSurfaces(card);

    v = Mmio_Read(card->mmio, R_DP_DATATYPE);
    Log_Printf("DP_DATATYPE: HOST_BIG_ENDIAN_EN %s\n", OnOff(v & R_HOST_BIG_ENDIAN_EN));

    v = Mmio_Read(card->mmio, R_CP_CSQ_CNTL);
    /* CSQ modes (Linux radeon_drv.h): 0 PRIDIS_INDDIS (CP not used),
     * 1 PRIPIO_INDDIS, 2 PRIBM_INDDIS, 3 PRIPIO_INDBM, 4 PRIBM_INDBM
     * (ring + indirect buffers, mirq's setting), 15 PRIPIO_INDPIO. */
    Log_Printf("CP_CSQ_CNTL: mode %lu (0 = CP unused, 2 = ring only, 4 = ring + indirect buffers)\n",
               v >> 28);
    v = Mmio_Read(card->mmio, R_CP_RB_CNTL);
    Log_Printf("CP_RB_CNTL: ring size 2^%lu dwords, NO_UPDATE %s; RB_BASE 0x%08lx RPTR %lu WPTR %lu\n",
               (v & 0x3fUL) + 1, OnOff(v & R_RB_NO_UPDATE),
               Mmio_Read(card->mmio, R_CP_RB_BASE),
               Mmio_Read(card->mmio, R_CP_RB_RPTR),
               Mmio_Read(card->mmio, R_CP_RB_WPTR));

    v = Mmio_Read(card->mmio, R_RBBM_STATUS);
    Log_Printf("RBBM_STATUS: engine %s, CP command stream %s, FIFO slots free %lu\n",
               (v & R_RBBM_ACTIVE) ? (CONST_STRPTR)"ACTIVE" : (CONST_STRPTR)"idle",
               (v & R_CP_CMDSTRM_BUSY) ? (CONST_STRPTR)"busy" : (CONST_STRPTR)"idle",
               v & 0x7fUL);

    v = Mmio_Read(card->mmio, R_GEN_INT_CNTL);
    Log_Printf("GEN_INT_CNTL: 0x%08lx (Radeon.card INTERRUPT=yes shows the VBlank enable here)\n", v);
    Log_Printf("CRTC_OFFSET 0x%08lx, CRTC_PITCH 0x%08lx, DISPLAY_BASE_ADDR 0x%08lx\n",
               Mmio_Read(card->mmio, R_CRTC_OFFSET),
               Mmio_Read(card->mmio, R_CRTC_PITCH),
               Mmio_Read(card->mmio, R_DISPLAY_BASE_ADDR));
}
