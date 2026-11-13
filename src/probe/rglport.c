/*
 * RglPort -- what RadeonGL's public port "RadeonGL" says (lib/rglport.h):
 * contexts on the Radeon, how many are games on a screen of their own,
 * flushes, and how long ago the last one was. Also an example of reading
 * it the way rglport.h asks: under Forbid, copied before Permit.
 *
 *   RglPort WATCH/S,SECONDS/N
 *
 * WATCH: one line a second (SECONDS of them, default 10), with the
 * flushes in that second. Read only; nothing is written.
 *
 * rc 0: the port is there; rc 5: no port (RadeonGL not loaded, or older).
 */

#include <exec/types.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "eclock.h"
#include "rglport.h"

static const char ver[] = "$VER: RglPort 0.1 (13.11.2026)";

#define TEMPLATE "WATCH/S,SECONDS/N"

struct Seen
{
    BOOL there;
    UWORD version;
    ULONG size;
    char id[80];
    ULONG contexts;
    ULONG games;
    ULONG flushes;
    ULONG last_lo;
    ULONG last_hi;
};

static void Copy(char *d, const char *s, ULONG n)
{
    ULONG i;

    for (i = 0; s != NULL && s[i] != 0 && s[i] != '\r' && s[i] != '\n' && i + 1 < n; i++)
        d[i] = s[i];
    d[i] = 0;
}

/* The port read under Forbid, everything copied before Permit */
static void Look(struct Seen *s)
{
    struct RglPort *p;

    s->there = FALSE;
    Forbid();
    p = (struct RglPort *) FindPort((STRPTR) RGLPORT_NAME);
    if (p != NULL && p->rp_Version >= 1)
    {
        s->there = TRUE;
        s->version = p->rp_Version;
        s->size = p->rp_Size;
        Copy(s->id, p->rp_IdString, sizeof(s->id));
        s->contexts = p->rp_Contexts;
        s->games = p->rp_ScreenContexts;
        s->flushes = p->rp_Flushes;
        s->last_lo = p->rp_LastFlush.ev_lo;
        s->last_hi = p->rp_LastFlush.ev_hi;
    }
    Permit();
}

static void Ago(const struct Seen *s)
{
    ULONG ms;

    if (s->last_lo == 0 && s->last_hi == 0)
    {
        Printf("never");
        return;
    }
    ms = Clock_Us(Clock_Since(s->last_lo)) / 1000;
    Printf("%lu ms ago", ms);
}

int main(void)
{
    struct RDArgs *rda;
    LONG args[2];
    struct Seen s;
    ULONG n;
    ULONG i;
    ULONG before;
    BOOL clock;

    args[0] = 0;
    args[1] = 0;
    rda = ReadArgs((STRPTR) TEMPLATE, args, NULL);
    if (rda == NULL)
    {
        PrintFault(IoErr(), (STRPTR) "RglPort");
        return RETURN_FAIL;
    }
    n = (args[1] != 0) ? (ULONG) *(LONG *) args[1] : 10;
    clock = Clock_Open();
    Look(&s);
    if (!s.there)
    {
        Printf("RglPort: no port \"%s\" (RadeonGL is not loaded, or a release without it)\n",
               (ULONG) RGLPORT_NAME);
        if (clock)
            Clock_Close();
        FreeArgs(rda);
        return RETURN_WARN;
    }
    Printf("port \"%s\" version %lu, %lu bytes\n", (ULONG) RGLPORT_NAME, (ULONG) s.version, s.size);
    Printf("library: %s\n", (ULONG) s.id);
    Printf("contexts on the Radeon: %lu, of them games (own screen): %lu\n", s.contexts, s.games);
    Printf("flushes since the library loaded: %lu, the last one ", s.flushes);
    if (clock)
        Ago(&s);
    else
        Printf("(no timer.device)");
    Printf("\n");
    if (args[0] != 0)
    {
        for (i = 0; i < n; i++)
        {
            before = s.flushes;
            Delay(50);
            if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
                break;
            Look(&s);
            if (!s.there)
            {
                Printf("the port went away\n");
                break;
            }
            Printf("contexts %lu, games %lu, flushes in the last second %lu, the last ",
                   s.contexts, s.games, s.flushes - before);
            if (clock)
                Ago(&s);
            Printf("\n");
        }
    }
    if (clock)
        Clock_Close();
    FreeArgs(rda);
    (void) ver;
    return RETURN_OK;
}
