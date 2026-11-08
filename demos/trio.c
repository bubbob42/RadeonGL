/*
 * trio.c -- Trio: three RadeonGL programs at once, each in a window of its
 * own on the Workbench.
 *
 *   Trio SECONDS/N,LEFT/N,TOP/N,WIDTH/N,HEIGHT/N,GAP/N,STEP/N
 *
 * Starts Spinner, Glow and Landscape from its own drawer, each as a
 * program of its own (an asynchronous shell), with
 *
 *   <Name> WINDOW LEFT <x> TOP <y> WIDTH <w> HEIGHT <h>
 *
 * so each opens its own minigl.library context: three dispatch tables,
 * three contexts, one Radeon taking them in turn (RadeonGL 0.9). The
 * windows go where mglCreateContext's offx and offy put them: side by
 * side from LEFT (40), TOP (40), WIDTH x HEIGHT (320 x 240) inside, GAP
 * (30) pixels apart, each STEP (60) pixels lower than the one before.
 *
 * Ctrl-C, or SECONDS when given, ends all three: Trio sends each its
 * Ctrl-C and waits until they have ended (ten seconds at most). Esc in a
 * window ends that program alone; Trio ends when all three have. Their
 * output (and their frame rates at the end) goes to Trio's console.
 *
 * Needs no minigl.library itself. GCC as the other demos (demos.mk); C89.
 */

#include <exec/types.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "demo.h"

#define TEMPLATE "SECONDS/N,LEFT/N,TOP/N,WIDTH/N,HEIGHT/N,GAP/N,STEP/N"
#define NPROGS   3
#define CMDLEN   400

struct Args
{
    LONG *seconds;
    LONG *left;
    LONG *top;
    LONG *width;
    LONG *height;
    LONG *gap;
    LONG *step;
};

static const char ver[] = "$VER: Trio 0.1 (08.11.2026)" DEMO_CPU;

static const char *const progs[NPROGS] = { "Spinner", "Glow", "Landscape" };

/* The shells' names: how Trio finds them again to send Ctrl-C */
static const char *const shells[NPROGS] = { "Trio: Spinner", "Trio: Glow", "Trio: Landscape" };

/* --- the command line ------------------------------------------------------------ */

static void Add(char *buf, ULONG *n, const char *s)
{
    while (*s != '\0' && *n < CMDLEN - 1)
        buf[(*n)++] = *s++;
    buf[*n] = '\0';
}

static void AddNum(char *buf, ULONG *n, LONG v)
{
    char d[12];
    ULONG k;
    ULONG u;

    k = 0;
    u = (v < 0) ? (ULONG) -v : (ULONG) v;
    do
    {
        d[k++] = (char) ('0' + u % 10);
        u /= 10;
    } while (u != 0 && k < sizeof(d) - 1);
    if (v < 0)
        d[k++] = '-';
    while (k > 0 && *n < CMDLEN - 1)
        buf[(*n)++] = d[--k];
    buf[*n] = '\0';
}

/* "<dir>/<prog>" WINDOW LEFT x TOP y WIDTH w HEIGHT h, the program quoted
 * (a drawer name may hold a space) */
static void Command(char *buf, const char *dir, const char *prog, LONG x, LONG y, LONG w, LONG h)
{
    char path[CMDLEN];
    ULONG n;
    ULONG i;

    for (i = 0; dir[i] != '\0' && i < CMDLEN - 1; i++)
        path[i] = dir[i];
    path[i] = '\0';
    (void) AddPart((STRPTR) path, (CONST_STRPTR) prog, CMDLEN);
    n = 0;
    buf[0] = '\0';
    Add(buf, &n, "\"");
    Add(buf, &n, path);
    Add(buf, &n, "\" WINDOW LEFT ");
    AddNum(buf, &n, x);
    Add(buf, &n, " TOP ");
    AddNum(buf, &n, y);
    Add(buf, &n, " WIDTH ");
    AddNum(buf, &n, w);
    Add(buf, &n, " HEIGHT ");
    AddNum(buf, &n, h);
}

/* --- the three ------------------------------------------------------------------ */

/* How many of the three still run; with ctrl_c, each gets a Ctrl-C */
static ULONG Shells(BOOL ctrl_c)
{
    struct Task *t;
    ULONG i;
    ULONG n;

    n = 0;
    Forbid();
    for (i = 0; i < NPROGS; i++)
    {
        t = FindTask((CONST_STRPTR) shells[i]);
        if (t == NULL)
            continue;
        n++;
        if (ctrl_c)
            Signal(t, SIGBREAKF_CTRL_C);
    }
    Permit();
    return n;
}

/* One program, asynchronously: TRUE when its shell started. Its input is
 * NIL:, its output Trio's console (NIL: without one); the shell closes
 * both when it ends. */
static BOOL Start(ULONG i, const char *cmd)
{
    BPTR in;
    BPTR out;
    LONG rc;

    in = Open((CONST_STRPTR) "NIL:", MODE_OLDFILE);
    out = Open((CONST_STRPTR) "CONSOLE:", MODE_NEWFILE);
    if (out == 0)
        out = Open((CONST_STRPTR) "NIL:", MODE_NEWFILE);
    if (in == 0 || out == 0)
    {
        if (in != 0)
            Close(in);
        if (out != 0)
            Close(out);
        return FALSE;
    }
    rc = SystemTags((CONST_STRPTR) cmd,
                    SYS_Input, in,
                    SYS_Output, out,
                    SYS_Asynch, TRUE,
                    NP_StackSize, 32000,
                    NP_Name, (ULONG) shells[i],
                    TAG_DONE);
    if (rc == -1)
    {
        /* the shell did not start: the streams are still ours */
        Close(in);
        Close(out);
        return FALSE;
    }
    return TRUE;
}

int main(int argc, char **argv)
{
    struct RDArgs *rda;
    struct Args args;
    char dir[256];
    char cmd[CMDLEN];
    LONG seconds;
    LONG left;
    LONG top;
    LONG w;
    LONG h;
    LONG gap;
    LONG step;
    LONG x[NPROGS];
    LONG y[NPROGS];
    ULONG started;
    ULONG ticks;
    ULONG stop_at;
    ULONG i;
    BOOL stopping;
    int rc;

    (void) argv;
    if (argc == 0)
        return RETURN_FAIL;             /* from the Trio script, not the Workbench */
    args.seconds = NULL;
    args.left = NULL;
    args.top = NULL;
    args.width = NULL;
    args.height = NULL;
    args.gap = NULL;
    args.step = NULL;
    rda = ReadArgs((CONST_STRPTR) TEMPLATE, (LONG *) &args, NULL);
    if (rda == NULL)
    {
        PrintFault(IoErr(), (CONST_STRPTR) "Trio");
        return RETURN_ERROR;
    }
    seconds = (args.seconds && *args.seconds > 0) ? *args.seconds : 0;
    left = (args.left && *args.left > 0) ? *args.left : 40;
    top = (args.top && *args.top > 0) ? *args.top : 40;
    w = (args.width && *args.width >= 64 && *args.width <= 2048) ? *args.width : 320;
    h = (args.height && *args.height >= 64 && *args.height <= 2048) ? *args.height : 240;
    gap = (args.gap && *args.gap >= 0) ? *args.gap : 30;
    step = (args.step && *args.step >= 0) ? *args.step : 60;
    FreeArgs(rda);

    /* the programs sit in Trio's own drawer */
    if (!NameFromLock(GetProgramDir(), (STRPTR) dir, sizeof(dir)))
    {
        PrintFault(IoErr(), (CONST_STRPTR) "Trio: its drawer");
        return RETURN_FAIL;
    }
    if (Shells(FALSE) != 0)
    {
        Printf((CONST_STRPTR) "Trio: a Trio is already running\n");
        return RETURN_WARN;
    }

    Printf((CONST_STRPTR) "%s\n", (LONG) (ver + 6));
    Printf((CONST_STRPTR) "Three programs, three contexts, one Radeon: each window\n");
    Printf((CONST_STRPTR) "is where its program's mglCreateContext(offx, offy) put it.\n");
    started = 0;
    for (i = 0; i < NPROGS; i++)
    {
        /* side by side; the window's border, about 10 pixels, is in the gap */
        x[i] = left + (LONG) i * (w + gap);
        y[i] = top + (LONG) i * step;
        Command(cmd, dir, progs[i], x[i], y[i], w, h);
        if (Start(i, cmd))
        {
            started++;
            Printf((CONST_STRPTR) "  %-9s at %ld,%ld, %ld x %ld\n", (LONG) progs[i], x[i], y[i], w, h);
        }
        else
            Printf((CONST_STRPTR) "  %-9s did not start\n", (LONG) progs[i], 0, 0, 0, 0);
        Delay(25);                      /* one after the other, half a second apart */
    }
    if (started == 0)
        return RETURN_FAIL;
    if (seconds != 0)
        Printf((CONST_STRPTR) "They end after %ld seconds, or with Ctrl-C here.\n", seconds);
    else
        Printf((CONST_STRPTR) "Ctrl-C here ends all three, Esc in a window that one.\n");

    rc = RETURN_OK;
    stopping = FALSE;
    ticks = 0;
    stop_at = 0;
    for (;;)
    {
        Delay(10);
        ticks += 10;
        if (Shells(FALSE) == 0)
            break;
        if (!stopping && ((CheckSignal(SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) != 0
                          || (seconds != 0 && ticks >= (ULONG) seconds * 50UL)))
        {
            (void) Shells(TRUE);
            stopping = TRUE;
            stop_at = ticks;
        }
        if (stopping && ticks - stop_at >= 500)
        {
            Printf((CONST_STRPTR) "Trio: %ld still running after ten seconds\n", (LONG) Shells(FALSE));
            rc = RETURN_WARN;
            break;
        }
    }
    Delay(25);                          /* their last lines first */
    Printf((CONST_STRPTR) "Trio: done.\n");
    return rc;
}
