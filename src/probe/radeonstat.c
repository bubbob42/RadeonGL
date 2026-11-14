/*
 * RadeonStat -- what the Radeon is doing (2026-11-08). Read-only: nothing
 * is written to the card.
 *
 *   RadeonStat [INTERVAL/N] [COUNT/N] [LOG/K] [GUI/S] [SIMULATE/S]
 *
 * Started from its icon, or with GUI, it opens a window on the Workbench
 * (rstatgui.c). From a Shell it prints a line an interval (default 1 s,
 * until Ctrl-C or COUNT lines):
 *   gpu%   share of samples with the engine busy (RBBM_STATUS GUI_ACTIVE)
 *   3d% 2d% cp%  the 3D pipe, the 2D engine, the command processor busy
 *   ring   the command processor's lag behind the writer, most seen (dwords)
 *   free / largest  card memory Picasso96 has free, and its largest block
 *   screen  the front screen when it changed, with its measured refresh and
 *           pixel clock (from the CRTC's totals)
 * The busy shares come from bursts of status reads, fifty a second, made
 * WITHOUT a Picasso96 lock (rstat.c): Radeon.card idles the engine before
 * it hands out a lock, so inside one the engine always read idle.
 *
 * SIMULATE makes the readings up and touches no card: to see the window
 * where there is no Radeon (FS-UAE).
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <dos/dosextens.h>
#include <intuition/intuitionbase.h>
#include <workbench/startup.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>

#include "eclock.h"
#include "plog.h"
#include "rstat.h"
#include "rstatgui.h"

static const char ver[] = "$VER: RadeonStat 0.2 (08.11.2026)";

#define TEMPLATE "INTERVAL/N,COUNT/N,LOG/K,GUI/S,SIMULATE/S"

struct Args
{
    LONG *interval;
    LONG *count;
    STRPTR log;
    LONG gui;
    LONG simulate;
};

struct IntuitionBase *IntuitionBase = NULL;

/* the startup code's Workbench message (libnix and SAS/C c.o alike); NULL
 * from a Shell. main() takes no arguments: under GCC's -msasregparm a
 * main(argc, argv) would read them from registers the startup code never
 * set. */
extern struct WBStartup *_WBenchMsg;

static int Shell(struct RStat *s, struct Args *args)
{
    struct RStatLoad l;
    struct RStatMode m;
    struct RStatFront f;
    ULONG interval, count, lines, tick, secs, total, fr, lg;
    LONG rc;

    interval = (args->interval && *args->interval > 0) ? (ULONG) *args->interval : 1UL;
    count = (args->count && *args->count > 0) ? (ULONG) *args->count : 0UL;
    if (!Log_Open(args->log))
        Log_Printf("RadeonStat: cannot write %s, console only\n", (ULONG) args->log);
    Log_Printf("%s -- read-only; Ctrl-C ends\n", (ULONG) (ver + 6));
    rc = RStat_Open(s);
    if (rc != RSTAT_OK)
    {
        Log_Printf(rc == RSTAT_NORADEON ? "RadeonStat: the Workbench's board is not a Radeon\n"
                   : rc == RSTAT_NOLOCK ? "RadeonStat: the bitmap could not be locked on the board\n"
                   : "RadeonStat: no Picasso96 board under the Workbench\n");
        return RETURN_FAIL;
    }
    Log_Printf("board %s (%s), %lu KB; registers %s\n", (ULONG) s->board.boardName,
               (ULONG) s->board.chipName, s->board.totalMem / 1024,
               (ULONG) (s->swp ? "byte-swapped" : "as they are"));
    Log_Printf("  secs  gpu%%  3d%%  2d%%  cp%%   ring   free KB  largest KB\n");
    f.scr = NULL;
    lines = 0;
    secs = 0;
    while (count == 0 || lines < count)
    {
        for (tick = 0; tick < RSTAT_TICKS * interval; tick++)
        {
            if (CheckSignal(SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
                goto done;
            Delay(1);
            RStat_Sample(s);
        }
        secs += interval;
        RStat_Take(s, &l);
        /* the front screen, when it changed: its size, refresh, pixel clock */
        if (RStat_Front(&f))
        {
            RStat_Mode(s, &m);
            Log_Printf("  -- front screen \"%s\" %ld x %ld; CRTC totals %lu x %lu, %lu.%lu Hz, "
                       "pixel clock %lu.%lu MHz\n", (ULONG) f.title, f.width, f.height,
                       m.ht, m.vt, m.hz10 / 10, m.hz10 % 10, m.mhz10 / 10, m.mhz10 % 10);
        }
        RStat_Memory(s, &total, &fr, &lg);
        Log_Printf("%6lu  %4lu  %3lu  %3lu  %3lu  %6lu  %8lu  %10lu\n", secs,
                   l.gpu, l.d3, l.d2, l.cp, l.ring, fr / 1024, lg / 1024);
        Log_Flush();
        lines++;
    }
done:
    Log_Printf("RadeonStat: %lu lines\n", lines);
    return RETURN_OK;
}

int main(void)
{
    static struct RStat s;      /* zero: RStat_Close is right before Open */
    struct RDArgs *rda;
    struct Args args;
    struct RStatGuiCfg cfg;
    BOOL wb;
    int rc;

    rda = NULL;
    args.interval = NULL;
    args.count = NULL;
    args.log = NULL;
    args.gui = 0;
    args.simulate = 0;
    wb = (BOOL) (((struct Process *) FindTask(NULL))->pr_CLI == 0);
    if (!wb)
    {
        rda = ReadArgs((CONST_STRPTR) TEMPLATE, (LONG *) &args, NULL);
        if (rda == NULL)
        {
            PrintFault(IoErr(), (CONST_STRPTR) "RadeonStat");
            return RETURN_ERROR;
        }
    }
    rc = RETURN_FAIL;
    IntuitionBase = (struct IntuitionBase *) OpenLibrary((CONST_STRPTR) "intuition.library", 39);
    if (IntuitionBase == NULL || !Clock_Open())
        goto out;
    s.sim = (BOOL) (args.simulate != 0);
    if (wb || args.gui)
    {
        RStatGui_Defaults(&cfg);
        RStatGui_ToolTypes(&cfg, wb ? _WBenchMsg : NULL);
        if (args.interval && *args.interval > 0)
            cfg.interval10 = (ULONG) *args.interval * 10;
        rc = RStatGui_Run(&s, &cfg, ver + 6);
    }
    else
        rc = Shell(&s, &args);

out:
    RStat_Close(&s);
    Log_Close();
    Clock_Close();
    if (IntuitionBase != NULL)
        CloseLibrary((struct Library *) IntuitionBase);
    if (rda != NULL)
        FreeArgs(rda);
    return rc;
}
