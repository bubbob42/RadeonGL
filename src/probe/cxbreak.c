/*
 * CxBreak -- quit the commodities that get in a test's way before it runs:
 * the OS Blanker and DPMSManager by default. On the A4000D a DPMS manager
 * switched the monitor off on idle during the probe runs, which looked like
 * a probe had blanked the display.
 *
 * Commodities started from WBStartup are not CLI processes: Status does not
 * list them and Break cannot reach them. CxBreak finds each named task
 * (case-insensitive) in exec's task lists and sends it the break signal
 * (CTRL-C), which a commodity answers by quitting; then it checks for up to
 * two seconds that the task is gone.
 *
 *   CxBreak NAMES/M,LIST/S
 *
 * LIST prints every task's name first (to find what a commodity is called).
 *
 * rc 0: none of them runs (any more); rc 5: one is still there.
 */

#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>

static const char ver[] = "$VER: CxBreak 0.1 (02.11.2026)";

#define TEMPLATE "NAMES/M,LIST/S"
#define MAXLIST  200      /* task names LIST shows; more are counted, not printed */

static const char *const defaults[] = { "Blanker", "DPMSManager", NULL };

static int Lower(int ch)
{
    return (ch >= 'A' && ch <= 'Z') ? ch - 'A' + 'a' : ch;
}

static BOOL SameName(const char *a, const char *b)
{
    if (a == NULL || b == NULL)
        return FALSE;
    while (*a != 0 && Lower((UBYTE)*a) == Lower((UBYTE)*b))
    {
        a++;
        b++;
    }
    return (BOOL)(*a == 0 && *b == 0);
}

static struct Task *InList(struct List *l, const char *name, struct Task *me)
{
    struct Node *n;

    for (n = l->lh_Head; n->ln_Succ != NULL; n = n->ln_Succ)
        if ((struct Task *)n != me && SameName(n->ln_Name, name))
            return (struct Task *)n;
    return NULL;
}

/* Every task's name, waiting and ready (names copied under Disable, printed
 * after). */
static void List(void)
{
    static char names[MAXLIST][32];
    struct Node *n;
    struct List *l;
    ULONG count;
    ULONG pass;
    ULONG k;

    count = 0;
    Disable();
    for (pass = 0; pass < 2; pass++)
    {
        l = (pass == 0) ? &SysBase->TaskWait : &SysBase->TaskReady;
        for (n = l->lh_Head; n->ln_Succ != NULL && count < MAXLIST; n = n->ln_Succ)
        {
            for (k = 0; k < 31 && n->ln_Name != NULL && n->ln_Name[k] != 0; k++)
                names[count][k] = n->ln_Name[k];
            names[count][k] = 0;
            count++;
        }
    }
    Enable();
    for (k = 0; k < count; k++)
        Printf("  task: %s\n", (LONG)names[k]);
    if (count == MAXLIST)
        Printf("  (the first %ld tasks only)\n", (LONG)MAXLIST);
}

/* The task called name, or NULL; with signal, it gets CTRL-C while the
 * lists are held still (it cannot go away between finding and signalling). */
static BOOL Find(const char *name, BOOL signal)
{
    struct Task *me;
    struct Task *t;

    me = FindTask(NULL);
    Disable();
    t = InList(&SysBase->TaskWait, name, me);
    if (t == NULL)
        t = InList(&SysBase->TaskReady, name, me);
    if (t != NULL && signal)
        Signal(t, SIGBREAKF_CTRL_C);
    Enable();
    return (BOOL)(t != NULL);
}

int main(void)
{
    struct RDArgs *rda;
    STRPTR *names;
    const char *const *list;
    LONG args[2];
    LONG i;
    LONG tries;
    int rc;

    args[0] = 0;
    args[1] = 0;
    rda = ReadArgs(TEMPLATE, args, NULL);
    if (rda == NULL)
    {
        PrintFault(IoErr(), "CxBreak");
        return RETURN_ERROR;
    }
    Printf("%s\n", (LONG)(ver + 6));
    if (args[1])
        List();
    names = (STRPTR *)args[0];
    list = (names != NULL && names[0] != NULL) ? (const char *const *)names : defaults;
    rc = RETURN_OK;
    for (i = 0; list[i] != NULL; i++)
    {
        if (!Find(list[i], TRUE))
        {
            Printf("%s: not running\n", (LONG)list[i]);
            continue;
        }
        for (tries = 0; tries < 20 && Find(list[i], FALSE); tries++)
            Delay(5);
        if (Find(list[i], FALSE))
        {
            Printf("%s: sent CTRL-C, STILL RUNNING\n", (LONG)list[i]);
            rc = RETURN_WARN;
        }
        else
            Printf("%s: sent CTRL-C, quit\n", (LONG)list[i]);
    }
    FreeArgs(rda);
    return rc;
}
