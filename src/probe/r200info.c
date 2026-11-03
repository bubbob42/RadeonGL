/*
 * R200Info -- read-only report of the Radeon under the Workbench screen:
 * the board as Picasso96 hands it out, how its registers read from the
 * 68k, and the register values the RadeonGL back end's design hinges on
 * (docs/concept-radeongl.md, section 11, probe 1).
 *
 * The card comes from Picasso96 (p96board.h): its register base is
 * P96BMA_BOARDMEMIOBASE, read inside a lock of a small bitmap on the
 * board, and every register is read inside such a lock, which keeps
 * Radeon.card off the chip (Picasso96, 2026-11-02). With
 * openpci.library present its view of the card (identity, BARs, owner) is
 * reported beside it, read-only, the card never claimed.
 *
 * It never writes a register. Run it twice: once with an idle Workbench,
 * once while dragging a window around, to see what Radeon.card changes
 * (SAMPLES=2 INTERVAL=8 does both in one go: drag during the pause, when
 * nothing is locked). SAMPLES is 1 to 100, INTERVAL 0 to 600 seconds;
 * Ctrl-C stops between samples. A Shell program: started from the
 * Workbench (no console) it quits at once.
 *
 *   R200Info LOG/K,SAMPLES/N,INTERVAL/N
 */

#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "p96board.h"
#include "pcicard.h"
#include "mmio.h"
#include "r200probe.h"
#include "plog.h"

static const char ver[] = "$VER: R200Info 0.2 (02.11.2026)";

#define TEMPLATE "LOG/K,SAMPLES/N,INTERVAL/N"
#define MAX_SAMPLES  100
#define MAX_INTERVAL 600

struct Args
{
    STRPTR log;
    LONG *samples;
    LONG *interval;
};

static void ReportCpu(void)
{
    UWORD f;
    CONST_STRPTR cpu;
    CONST_STRPTR fpu;

    /* AttnFlags sets every lower CPU bit too: name the highest only. */
    f = SysBase->AttnFlags;
    cpu = (f & AFF_68060) ? (CONST_STRPTR)"68060" :
          (f & AFF_68040) ? (CONST_STRPTR)"68040" :
          (f & AFF_68030) ? (CONST_STRPTR)"68030" :
          (f & AFF_68020) ? (CONST_STRPTR)"68020" :
          (f & AFF_68010) ? (CONST_STRPTR)"68010" : (CONST_STRPTR)"68000";
    fpu = (f & AFF_FPU40) ? (CONST_STRPTR)"internal" :
          (f & AFF_68882) ? (CONST_STRPTR)"68882" :
          (f & AFF_68881) ? (CONST_STRPTR)"68881" : (CONST_STRPTR)"none";
    Log_Printf("CPU: %s, FPU: %s\n", cpu, fpu);
}

/* Waits secs seconds, a second at a time; TRUE when Ctrl-C came. */
static BOOL Pause(LONG secs)
{
    LONG t;

    for (t = 0; t < secs; t++)
    {
        if (CheckSignal(SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
            return TRUE;
        Delay(50);
    }
    return (BOOL)((CheckSignal(SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) != 0);
}

int main(void)
{
    static struct P96Board board;     /* zero: P96Board_Close is right before Open */
    struct RDArgs *rda;
    struct Args args;
    struct PciCard card;
    struct PciCard pci;
    ULONG *prev;
    LONG samples;
    LONG interval;
    LONG i;
    BOOL havePci;
    int rc;

    if (((struct Process *) FindTask(NULL))->pr_CLI == 0)
        return RETURN_FAIL;     /* the Workbench: no console to report to */
    args.log = NULL;
    args.samples = NULL;
    args.interval = NULL;
    rda = ReadArgs(TEMPLATE, (LONG *)&args, NULL);
    if (rda == NULL)
    {
        PrintFault(IoErr(), "R200Info");
        return RETURN_ERROR;
    }
    samples = args.samples ? *args.samples : 1;
    interval = args.interval ? *args.interval : 5;
    if (samples < 1)
        samples = 1;
    if (samples > MAX_SAMPLES)
        samples = MAX_SAMPLES;
    if (interval < 0)
        interval = 0;
    if (interval > MAX_INTERVAL)
        interval = MAX_INTERVAL;

    if (!Log_Open(args.log))
        Log_Printf("R200Info: cannot write %s, console only\n", args.log);
    Log_Printf("%s -- read-only Radeon register report\n", ver + 6);
    ReportCpu();

    rc = RETURN_FAIL;
    havePci = FALSE;
    prev = NULL;
    if (!P96Board_Open(&board, 4096))
        goto out;
    P96Board_Report(&board);
    if (!P96Board_IsRadeon(&board) || board.memIoBase == NULL)
    {
        Log_Printf("R200Info: the Workbench's board is not a Radeon: no registers read\n");
        goto out;
    }
    R200_CardFromP96(&card, &board);
    /* openpci's view beside it: identity and BAR0's PCI address */
    havePci = PciCard_Open(&pci);
    if (havePci)
    {
        PciCard_Report(&pci);
        Log_Printf("P96's register base 0x%08lx %s openpci's BAR2 0x%08lx; memory base 0x%08lx %s BAR0 0x%08lx\n",
                   (ULONG)card.mmio, card.mmio == pci.mmio ? (CONST_STRPTR)"==" : (CONST_STRPTR)"!=",
                   (ULONG)pci.mmio, (ULONG)card.aper,
                   card.aper == pci.aper ? (CONST_STRPTR)"==" : (CONST_STRPTR)"!=", (ULONG)pci.aper);
        card.vendor = pci.vendor;
        card.device = pci.device;
        card.cfgCommand = pci.cfgCommand;
        card.aperSize = pci.aperSize;
        for (i = 0; i < 6; i++)
            card.cfgBar[i] = pci.cfgBar[i];
    }
    if (!P96Board_Lock(&board))
    {
        Log_Printf("R200Info: the bitmap could not be locked on the board\n");
        goto out;
    }
    R200_ReportEndian(&card);
    P96Board_Unlock(&board);
    prev = NULL;
    if (samples > 1)
    {
        prev = AllocVec(R200_RegisterCount() * sizeof(ULONG), MEMF_ANY | MEMF_CLEAR);
        if (prev == NULL)
            Log_Printf("R200Info: no memory to compare samples: changes are not marked\n");
    }
    for (i = 0; i < samples; i++)
    {
        if (i > 0)
        {
            Log_Printf("\n(sample %ld of %ld in %ld s -- move a window now; Ctrl-C stops)\n",
                       i + 1, samples, interval);
            if (Pause(interval))
            {
                Log_Printf("***Break: no more samples\n");
                break;
            }
        }
        if (!P96Board_Lock(&board))
        {
            Log_Printf("sample %ld: the bitmap is not on the board now\n", i + 1);
            continue;
        }
        R200_DumpRegisters(&card, prev, (BOOL)(i > 0));
        P96Board_Unlock(&board);
    }
    if (P96Board_Lock(&board))
    {
        R200_DecodeRegisters(&card);
        P96Board_Unlock(&board);
    }
    rc = RETURN_OK;

out:
    if (prev != NULL)
        FreeVec(prev);
    if (havePci)
        PciCard_Close(&pci);
    P96Board_Close(&board);
    Log_Printf("\nR200Info: done (rc %ld)\n", (LONG)rc);
    Log_Close();
    FreeArgs(rda);
    return rc;
}
