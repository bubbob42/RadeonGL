/*
 * RadeonStat -- what the Radeon is doing, a line every second (2026-11-08).
 * Read-only: nothing is written to the card.
 *
 *   RadeonStat [INTERVAL/N] [COUNT/N] [LOG/K]
 *
 * A line an interval (default 1 s, until Ctrl-C or COUNT lines):
 *   gpu%   share of samples with the engine busy (RBBM_STATUS GUI_ACTIVE)
 *   3d% 2d% cp%  the 3D pipe, the 2D engine, the command processor busy
 *   ring   the command processor's lag behind the writer, most seen (dwords)
 *   free / largest  card memory Picasso96 has free, and its largest block
 *   screen  the front screen when it changed, with its measured refresh and
 *           pixel clock (from the CRTC's totals)
 * The busy shares come from bursts of reads, fifty a second, each inside a
 * short Picasso96 lock of a 4 KB friend bitmap (p96board.h: register reads
 * only inside a lock). A lock waits while a program draws, so the samples
 * fall between other programs' frames -- where the Radeon works on what
 * they sent (RadeonGL unlocks before the GPU is done).
 *
 * For watching a GL program, Trio, or the moment a quit blanks the DVI
 * picture: run it in a Shell beside them.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <intuition/intuitionbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/Picasso96.h>
#include <libraries/Picasso96.h>

#include "eclock.h"
#include "mmio.h"
#include "plog.h"
#include "p96board.h"

static const char ver[] = "$VER: RadeonStat 0.1 (08.11.2026)";

#define TEMPLATE "INTERVAL/N,COUNT/N,LOG/K"

#define R_PCI_MIRROR        0x0f00
#define R_RBBM_STATUS       0x0e40
#define   S_GUI_ACTIVE      (1UL << 31)
#define   S_CP_BUSY         (1UL << 16)
#define   S_2D_BUSY         (1UL << 18)
#define   S_3D_BUSY         (0x1fUL << 19)  /* RB3D, SE, RE, TAM, TDM */
#define R_CP_RB_CNTL        0x0704
#define R_CP_RB_RPTR        0x0710
#define R_CP_RB_WPTR        0x0714
#define R_CRTC_H_TOTAL_DISP 0x0200
#define R_CRTC_V_TOTAL_DISP 0x0208
#define R_CRTC_VLINE        0x0210      /* CRTC_VLINE_CRNT_VLINE: current line in bits 16-26 */

#define BURST       64      /* RBBM_STATUS reads a sample burst */
#define TICKS       50      /* bursts a second (one each DOS tick) */

struct Args
{
    LONG *interval;
    LONG *count;
    STRPTR log;
};

struct IntuitionBase *IntuitionBase;

static APTR io;
static BOOL swp;

static ULONG Rd(ULONG off)
{
    ULONG v;

    v = Mmio_Read(io, off);
    return swp ? Mmio_Swap32(v) : v;
}

static ULONG Line(void)
{
    return (Rd(R_CRTC_VLINE) >> 16) & 0x7ffUL;
}

/* The front screen's refresh in 1/10 Hz, from five frames timed by the
 * E clock (about 100 ms inside the lock); 0 when the beam does not move */
static ULONG Refresh(void)
{
    ULONG last, cur, start, frames, ticks, guard;

    last = Line();
    for (guard = 0; guard < 200000UL; guard++)  /* to the top of a frame */
    {
        cur = Line();
        if (cur < last)
            break;
        last = cur;
    }
    if (guard == 200000UL)
        return 0;
    start = Clock_Now();
    frames = 0;
    last = Line();
    for (guard = 0; frames < 5 && guard < 2000000UL; guard++)
    {
        cur = Line();
        if (cur < last)
            frames++;
        last = cur;
    }
    ticks = Clock_Since(start);
    if (frames < 5 || Clock_Us(ticks) == 0)
        return 0;
    return 50000000UL / Clock_Us(ticks);       /* 5 frames: 10 * 5e6 / us */
}

static void Memory(ULONG board, ULONG *fr, ULONG *lg)
{
    struct TagItem t[3];

    *fr = *lg = 0;
    t[0].ti_Tag = P96BD_FreeMemory;         t[0].ti_Data = (ULONG) fr;
    t[1].ti_Tag = P96BD_LargestFreeMemory;  t[1].ti_Data = (ULONG) lg;
    t[2].ti_Tag = TAG_DONE;                 t[2].ti_Data = 0;
    p96GetBoardDataTagList(board, t);
}

int main(void)
{
    static struct P96Board board;   /* zero: P96Board_Close is right before Open */
    struct RDArgs *rda;
    struct Args args;
    struct Screen *front, *shown;
    ULONG interval, count, lines, tick, i, v, mirror;
    ULONG n, gpu, d3, d2, cp, ring, lag, rsize, rptr, wptr;
    ULONG fr, lg, hz, ht, vt;
    ULONG secs;
    int rc;

    if (((struct Process *) FindTask(NULL))->pr_CLI == 0)
        return RETURN_FAIL;
    args.interval = NULL;
    args.count = NULL;
    args.log = NULL;
    rda = ReadArgs((CONST_STRPTR) TEMPLATE, (LONG *) &args, NULL);
    if (rda == NULL)
    {
        PrintFault(IoErr(), (CONST_STRPTR) "RadeonStat");
        return RETURN_ERROR;
    }
    interval = (args.interval && *args.interval > 0) ? (ULONG) *args.interval : 1UL;
    count = (args.count && *args.count > 0) ? (ULONG) *args.count : 0UL;
    rc = RETURN_FAIL;
    IntuitionBase = (struct IntuitionBase *) OpenLibrary((CONST_STRPTR) "intuition.library", 39);
    if (IntuitionBase == NULL || !Clock_Open())
        goto out;
    if (!Log_Open(args.log))
        Log_Printf("RadeonStat: cannot write %s, console only\n", (ULONG) args.log);
    Log_Printf("%s -- read-only; Ctrl-C ends\n", (ULONG) (ver + 6));
    if (!P96Board_Open(&board, 4096))
        goto out;
    if (!P96Board_IsRadeon(&board) || board.memIoBase == NULL)
    {
        Log_Printf("RadeonStat: the Workbench's board is not a Radeon\n");
        goto out;
    }
    io = board.memIoBase;
    if (!P96Board_Lock(&board))
    {
        Log_Printf("RadeonStat: the bitmap could not be locked on the board\n");
        goto out;
    }
    mirror = Mmio_Read(io, R_PCI_MIRROR);
    swp = ((mirror & 0xffffUL) != 0x1002UL && (Mmio_Swap32(mirror) & 0xffffUL) == 0x1002UL);
    P96Board_Unlock(&board);
    Log_Printf("board %s (%s), %lu KB; registers %s\n", (ULONG) board.boardName, (ULONG) board.chipName,
               board.totalMem / 1024, (ULONG) (swp ? "byte-swapped" : "as they are"));
    Log_Printf("  secs  gpu%%  3d%%  2d%%  cp%%   ring   free KB  largest KB\n");
    shown = NULL;
    lines = 0;
    secs = 0;
    rc = RETURN_OK;
    while (count == 0 || lines < count)
    {
        n = gpu = d3 = d2 = cp = ring = 0;
        for (tick = 0; tick < TICKS * interval; tick++)
        {
            if (CheckSignal(SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
                goto done;
            Delay(1);
            if (!P96Board_Lock(&board))
                continue;           /* the bitmap left the board for now */
            for (i = 0; i < BURST; i++)
            {
                v = Rd(R_RBBM_STATUS);
                n++;
                if (v & S_GUI_ACTIVE)
                    gpu++;
                if (v & S_3D_BUSY)
                    d3++;
                if (v & S_2D_BUSY)
                    d2++;
                if (v & S_CP_BUSY)
                    cp++;
            }
            rsize = 2UL << (Rd(R_CP_RB_CNTL) & 0x3fUL);      /* dwords */
            rptr = Rd(R_CP_RB_RPTR);
            wptr = Rd(R_CP_RB_WPTR);
            lag = (rsize >= 2 && rsize <= (1UL << 23)) ? ((wptr - rptr) & (rsize - 1)) : 0;
            if (lag > ring)
                ring = lag;
            P96Board_Unlock(&board);
        }
        secs += interval;
        /* the front screen, when it changed: its size, refresh, pixel clock */
        front = IntuitionBase->FirstScreen;
        if (front != shown)
        {
            shown = front;
            hz = ht = vt = 0;
            if (P96Board_Lock(&board))
            {
                ht = ((Rd(R_CRTC_H_TOTAL_DISP) & 0x3ffUL) + 1) * 8;
                vt = (Rd(R_CRTC_V_TOTAL_DISP) & 0xfffUL) + 1;
                hz = Refresh();
                P96Board_Unlock(&board);
            }
            Log_Printf("  -- front screen \"%s\" %ld x %ld; CRTC totals %lu x %lu, %lu.%lu Hz, "
                       "pixel clock %lu.%lu MHz\n",
                       (ULONG) ((front && front->Title) ? (char *) front->Title : "?"),
                       front ? (LONG) front->Width : 0L, front ? (LONG) front->Height : 0L, ht, vt,
                       hz / 10, hz % 10, (ht * vt / 100UL * hz / 10000UL) / 10,
                       (ht * vt / 100UL * hz / 10000UL) % 10);
        }
        Memory(board.board, &fr, &lg);
        Log_Printf("%6lu  %4lu  %3lu  %3lu  %3lu  %6lu  %8lu  %10lu\n", secs,
                   n ? gpu * 100 / n : 0, n ? d3 * 100 / n : 0, n ? d2 * 100 / n : 0,
                   n ? cp * 100 / n : 0, ring, fr / 1024, lg / 1024);
        Log_Flush();
        lines++;
    }
done:
    Log_Printf("RadeonStat: %lu lines\n", lines);

out:
    P96Board_Close(&board);
    Log_Close();
    Clock_Close();
    if (IntuitionBase != NULL)
        CloseLibrary((struct Library *) IntuitionBase);
    FreeArgs(rda);
    return rc;
}
