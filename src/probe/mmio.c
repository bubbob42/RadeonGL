/*
 * mmio.c -- see mmio.h.
 */

#include "mmio.h"
#include "r200regs.h"

BOOL Mmio_Swap = TRUE;

ULONG Mmio_Swap32(ULONG v)
{
    return ((v >> 24) & 0x000000ffUL)
         | ((v >>  8) & 0x0000ff00UL)
         | ((v <<  8) & 0x00ff0000UL)
         | ((v << 24) & 0xff000000UL);
}

ULONG Mmio_Read(APTR base, ULONG off)
{
    ULONG raw;

    raw = MMIO_RAW(base, off);
    if (Mmio_Swap)
        return Mmio_Swap32(raw);
    return raw;
}

void Mmio_Write(APTR base, ULONG off, ULONG value)
{
    if (Mmio_Swap)
        value = Mmio_Swap32(value);
    MMIO_RAW(base, off) = value;
}

/* A Radeon reports its memory size in bytes: a multiple of one megabyte,
 * at most 512 MB. Exactly one of the two orientations looks like that
 * unless the register reads 0 or garbage. */
static BOOL PlausibleMemSize(ULONG v)
{
    if (v == 0)
        return FALSE;
    if (v & 0x000fffffUL)
        return FALSE;
    if (v > (512UL << 20))
        return FALSE;
    return TRUE;
}

ULONG Mmio_DetectSwap(APTR base, ULONG bar0_pci, ULONG *memsize)
{
    ULONG raw;
    ULONG swapped;

    *memsize = 0;
    raw = MMIO_RAW(base, R_CONFIG_MEMSIZE);
    swapped = Mmio_Swap32(raw);
    if (PlausibleMemSize(swapped) && !PlausibleMemSize(raw))
    {
        Mmio_Swap = TRUE;
        *memsize = swapped;
        return MMIO_BY_MEMSIZE;
    }
    if (PlausibleMemSize(raw) && !PlausibleMemSize(swapped))
    {
        Mmio_Swap = FALSE;
        *memsize = raw;
        return MMIO_BY_MEMSIZE;
    }
    /* No PC BIOS ran (CONFIG_MEMSIZE 0): the aperture base register holds
     * the PCI address BAR0 was given */
    raw = MMIO_RAW(base, R_CONFIG_APER_0_BASE);
    swapped = Mmio_Swap32(raw);
    if (bar0_pci != 0 && raw == bar0_pci && swapped != bar0_pci)
    {
        Mmio_Swap = FALSE;
        return MMIO_BY_APER;
    }
    if (bar0_pci != 0 && swapped == bar0_pci && raw != bar0_pci)
    {
        Mmio_Swap = TRUE;
        return MMIO_BY_APER;
    }
    return MMIO_UNDECIDED;
}
