/*
 * pcicard.h -- the Radeon as openpci.library sees it: read-only.
 *
 * The probes never claim the card (no pci_obtain_card) and never write to
 * its configuration space. Radeon.card owns it; PciCard_Open only looks.
 * Addresses are 68k addresses (what GetBoardAttrs returns); the raw PCI
 * BAR values from configuration space are kept beside them for the report.
 */

#ifndef PROBE_PCICARD_H
#define PROBE_PCICARD_H

#include <exec/types.h>

struct pci_dev;

struct PciCard
{
    struct pci_dev *dev;

    ULONG vendor;
    ULONG device;
    ULONG revision;
    ULONG devclass;          /* base class (3 = display) */
    ULONG subclass;
    ULONG bus;
    ULONG slot;
    ULONG function;
    ULONG subsysVendor;
    ULONG subsysId;

    APTR  aper;              /* BAR0: frame buffer aperture, 68k address */
    ULONG aperSize;
    APTR  bar1;              /* BAR1: I/O, if the bridge maps it */
    ULONG bar1Size;
    APTR  mmio;              /* BAR2: memory-mapped registers */
    ULONG mmioSize;
    APTR  rom;
    ULONG romSize;

    APTR  owner;             /* struct Node * of the owning driver, or NULL */
    ULONG intLine;
    ULONG intPin;
    ULONG pciToHost;         /* PRM_PCIToHostOffset */

    ULONG cfgCommand;        /* configuration space, raw */
    ULONG cfgStatus;
    ULONG cfgLatency;
    ULONG cfgCacheLine;
    ULONG cfgBar[6];
};

/* Opens openpci.library (Thor's, V40+) and finds the first ATI display
 * device. Prints what went wrong through Log_Printf and returns FALSE if
 * there is no library, no card, or no register aperture. */
BOOL PciCard_Open(struct PciCard *card);
void PciCard_Close(struct PciCard *card);

/* Everything above, as report lines. */
void PciCard_Report(const struct PciCard *card);

/* openpci.library, for the DMA-memory calls in BusBench. */
extern struct Library *OpenPciBase;

#endif /* PROBE_PCICARD_H */
