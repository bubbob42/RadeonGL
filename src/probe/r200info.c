/*
 * R200Info -- read-only report of a Radeon behind openpci.library:
 * how openpci maps it, who owns it, how its registers read from the 68k,
 * and the register values the RadeonGL back end's design hinges on
 * (docs/concept-radeongl.md, section 11, probe 1).
 *
 * It never writes a register and never claims the card. Run it twice:
 * once with an idle Workbench, once while dragging a window around, to
 * see what Radeon.card changes (SAMPLES=2 INTERVAL=8 does both in one go:
 * drag during the pause).
 *
 *   R200Info LOG/K,SAMPLES/N,INTERVAL/N
 */

#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "pcicard.h"
#include "mmio.h"
#include "r200probe.h"
#include "plog.h"

static const char ver[] = "$VER: R200Info 0.1 (02.11.2026)";

#define TEMPLATE "LOG/K,SAMPLES/N,INTERVAL/N"

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

int main(void)
{
    struct RDArgs *rda;
    struct Args args;
    struct PciCard card;
    ULONG *prev;
    LONG samples;
    LONG interval;
    LONG i;
    int rc;

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

    if (!Log_Open(args.log))
        Log_Printf("R200Info: cannot write %s, console only\n", args.log);
    Log_Printf("%s -- read-only Radeon register report\n", ver + 6);
    ReportCpu();

    rc = RETURN_OK;
    if (!PciCard_Open(&card))
    {
        rc = RETURN_FAIL;
    }
    else
    {
        PciCard_Report(&card);
        R200_ReportEndian(&card);
        prev = AllocVec(R200_RegisterCount() * sizeof(ULONG), MEMF_ANY | MEMF_CLEAR);
        for (i = 0; i < samples; i++)
        {
            if (i > 0)
            {
                Log_Printf("\n(sample %ld of %ld in %ld s -- move a window now)\n",
                           i + 1, samples, interval);
                Delay(interval * 50);
            }
            R200_DumpRegisters(&card, prev, (BOOL)(i > 0));
        }
        R200_DecodeRegisters(&card);
        FreeVec(prev);
        PciCard_Close(&card);
    }
    Log_Printf("\nR200Info: done (rc %ld)\n", (LONG)rc);
    Log_Close();
    FreeArgs(rda);
    return rc;
}
