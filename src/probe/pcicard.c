/*
 * pcicard.c -- see pcicard.h. Read-only: FindBoardA + GetBoardAttrsA +
 * configuration-space reads; never pci_obtain_card, never a config write.
 */

#include <exec/types.h>
#include <exec/nodes.h>
#include <exec/libraries.h>
#include <utility/tagitem.h>
#include <libraries/openpci.h>
#include <libraries/pcitags.h>
#include <proto/exec.h>
#include <proto/openpci.h>

#include "pcicard.h"
#include "plog.h"

struct Library *OpenPciBase = NULL;

#define PCI_VENDOR_ATI      0x1002UL
#define PCI_CLASS_DISPLAY   3UL
#define OPENPCI_MIN_VERSION 40      /* Thor's library: FindBoardA, GetBoardAttrsA, DMA memory */

#define PCI_CFG_COMMAND     0x04
#define PCI_CFG_STATUS      0x06
#define PCI_CFG_CACHELINE   0x0c
#define PCI_CFG_LATENCY     0x0d
#define PCI_CFG_BAR0        0x10

/* One attribute; the tag's data is the address of the variable to fill. */
static ULONG Attr(struct pci_dev *dev, ULONG tag)
{
    struct TagItem tags[2];
    ULONG value;

    value = 0;
    tags[0].ti_Tag = tag;
    tags[0].ti_Data = (ULONG)&value;
    tags[1].ti_Tag = TAG_DONE;
    tags[1].ti_Data = 0;
    GetBoardAttrsA(dev, tags);
    return value;
}

BOOL PciCard_Open(struct PciCard *card)
{
    static const struct PciCard zero;
    struct TagItem find[3];
    struct pci_dev *dev;
    int i;

    *card = zero;
    OpenPciBase = OpenLibrary("openpci.library", OPENPCI_MIN_VERSION);
    if (OpenPciBase == NULL)
    {
        Log_Printf("openpci.library V%ld not found (Thomas Richter's, Aminet driver/other/openpci68k)\n",
                   (LONG)OPENPCI_MIN_VERSION);
        return FALSE;
    }

    find[0].ti_Tag = PRM_Vendor;
    find[0].ti_Data = PCI_VENDOR_ATI;
    find[1].ti_Tag = PRM_Class;
    find[1].ti_Data = PCI_CLASS_DISPLAY;
    find[2].ti_Tag = TAG_DONE;
    find[2].ti_Data = 0;
    dev = FindBoardA(NULL, find);
    if (dev == NULL)
    {
        Log_Printf("openpci %ld.%ld: no ATI display device on the PCI bus\n",
                   (LONG)OpenPciBase->lib_Version, (LONG)OpenPciBase->lib_Revision);
        PciCard_Close(card);
        return FALSE;
    }

    card->dev = dev;
    card->vendor = Attr(dev, PRM_Vendor);
    card->device = Attr(dev, PRM_Device);
    card->revision = Attr(dev, PRM_Revision);
    card->devclass = Attr(dev, PRM_Class);
    card->subclass = Attr(dev, PRM_SubClass);
    card->bus = Attr(dev, PRM_BusNumber);
    card->slot = Attr(dev, PRM_SlotNumber);
    card->function = Attr(dev, PRM_FunctionNumber);
    card->subsysVendor = Attr(dev, PRM_SubsysVendor);
    card->subsysId = Attr(dev, PRM_SubsysID);
    card->aper = (APTR)Attr(dev, PRM_MemoryAddr0);
    card->aperSize = Attr(dev, PRM_MemorySize0);
    card->bar1 = (APTR)Attr(dev, PRM_MemoryAddr1);
    card->bar1Size = Attr(dev, PRM_MemorySize1);
    card->mmio = (APTR)Attr(dev, PRM_MemoryAddr2);
    card->mmioSize = Attr(dev, PRM_MemorySize2);
    card->rom = (APTR)Attr(dev, PRM_ROM_Address);
    card->romSize = Attr(dev, PRM_ROM_Size);
    card->owner = (APTR)Attr(dev, PRM_BoardOwner);
    card->intLine = Attr(dev, PRM_InterruptLine);
    card->intPin = Attr(dev, PRM_InterruptPin);
    card->pciToHost = Attr(dev, PRM_PCIToHostOffset);

    card->cfgCommand = pci_read_config_word(PCI_CFG_COMMAND, dev);
    card->cfgStatus = pci_read_config_word(PCI_CFG_STATUS, dev);
    card->cfgCacheLine = pci_read_config_byte(PCI_CFG_CACHELINE, dev);
    card->cfgLatency = pci_read_config_byte(PCI_CFG_LATENCY, dev);
    for (i = 0; i < 6; i++)
        card->cfgBar[i] = pci_read_config_long((UBYTE)(PCI_CFG_BAR0 + i * 4), dev);

    /* The Radeon's registers are BAR2, 64 KB. Without them nothing here works. */
    if (card->mmio == NULL || card->mmioSize < 0x4000UL)
    {
        Log_Printf("the card's register aperture (BAR2) is not mapped: 0x%08lx, %lu bytes\n",
                   (ULONG)card->mmio, card->mmioSize);
        PciCard_Close(card);
        return FALSE;
    }
    return TRUE;
}

void PciCard_Close(struct PciCard *card)
{
    card->dev = NULL;
    if (OpenPciBase)
    {
        CloseLibrary(OpenPciBase);
        OpenPciBase = NULL;
    }
}

static CONST_STRPTR OwnerName(const struct PciCard *card)
{
    struct Node *node;

    if (card->owner == NULL)
        return (CONST_STRPTR)"nobody (free)";
    if (card->owner == (APTR)OpenPciBase)
        return (CONST_STRPTR)"openpci.library itself (pci_obtain_card)";
    node = (struct Node *)card->owner;
    if (node->ln_Name != NULL)
        return (CONST_STRPTR)node->ln_Name;
    return (CONST_STRPTR)"(unnamed node)";
}

void PciCard_Report(const struct PciCard *card)
{
    int i;

    Log_Section("The card through openpci");
    Log_Printf("openpci.library %ld.%ld\n",
               (LONG)OpenPciBase->lib_Version, (LONG)OpenPciBase->lib_Revision);
    Log_Printf("PCI %04lx:%04lx rev %02lx, class %02lx.%02lx, bus %lu slot %lu function %lu, subsystem %04lx:%04lx\n",
               card->vendor, card->device, card->revision, card->devclass, card->subclass,
               card->bus, card->slot, card->function, card->subsysVendor, card->subsysId);
    Log_Printf("owner: %s (node 0x%08lx)\n", OwnerName(card), (ULONG)card->owner);
    Log_Printf("BAR0 frame buffer: 68k 0x%08lx, %lu MB   (PCI 0x%08lx)\n",
               (ULONG)card->aper, card->aperSize >> 20, card->cfgBar[0]);
    Log_Printf("BAR1 I/O:          68k 0x%08lx, %lu bytes (PCI 0x%08lx)\n",
               (ULONG)card->bar1, card->bar1Size, card->cfgBar[1]);
    Log_Printf("BAR2 registers:    68k 0x%08lx, %lu KB   (PCI 0x%08lx)\n",
               (ULONG)card->mmio, card->mmioSize >> 10, card->cfgBar[2]);
    Log_Printf("ROM:               68k 0x%08lx, %lu KB\n", (ULONG)card->rom, card->romSize >> 10);
    for (i = 3; i < 6; i++)
        if (card->cfgBar[i] != 0)
            Log_Printf("BAR%ld raw: 0x%08lx\n", (LONG)i, card->cfgBar[i]);
    Log_Printf("PCI-to-host offset 0x%08lx; interrupt line %lu, pin %lu\n",
               card->pciToHost, card->intLine, card->intPin);
    Log_Printf("config: command 0x%04lx status 0x%04lx latency %lu cache line %lu\n",
               card->cfgCommand, card->cfgStatus, card->cfgLatency, card->cfgCacheLine);
}
