/*
 * rglport.c -- the public port "RadeonGL" (rglport.h): how many contexts
 * draw on the Radeon, how many of them are games, and when the last flush
 * went to the card (Herald's request R3, 2026-11-13).
 *
 * Linked behind the hot code. The port lives in the library's data from
 * Back_Init to Back_Expunge; its numbers change under Forbid, so a reader
 * under Forbid sees each one whole.
 */

#include <exec/types.h>
#include <exec/ports.h>
#include <exec/nodes.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/timer.h>
#include <libraries/minigl_dispatch.h>

#include "rgl_dispatch.h"
#include "backend.h"
#include "core.h"
#include "coreint.h"
#include "r2ring.h"
#include "nullcard.h"
#include "card.h"
#include "rglint.h"
#include "rglport.h"

extern struct Device *TimerBase;    /* card.c: set while a Radeon context is open */

static struct RglPort rgl_port;
static BOOL rgl_port_added = FALSE;

/* Once, from Back_Init. A port of that name already there (a second
 * copy of the library?) is left alone and this one not added. */
void Port_Add(void)
{
    struct MsgPort *mp;

    mp = &rgl_port.rp_Port;
    mp->mp_Node.ln_Type = NT_MSGPORT;
    mp->mp_Node.ln_Pri = 0;
    mp->mp_Node.ln_Name = (char *) RGLPORT_NAME;
    mp->mp_Flags = PA_IGNORE;
    mp->mp_SigBit = 0;
    mp->mp_SigTask = NULL;
    /* NewList by hand: the library links no amiga.lib */
    mp->mp_MsgList.lh_Head = (struct Node *) &mp->mp_MsgList.lh_Tail;
    mp->mp_MsgList.lh_Tail = NULL;
    mp->mp_MsgList.lh_TailPred = (struct Node *) &mp->mp_MsgList.lh_Head;
    mp->mp_MsgList.lh_Type = NT_MESSAGE;
    rgl_port.rp_Version = RGLPORT_VERSION;
    rgl_port.rp_Reserved = 0;
    rgl_port.rp_Size = sizeof(rgl_port);
    rgl_port.rp_IdString = Back_IdString;
    rgl_port.rp_Contexts = 0;
    rgl_port.rp_ScreenContexts = 0;
    rgl_port.rp_Flushes = 0;
    rgl_port.rp_LastFlush.ev_hi = 0;
    rgl_port.rp_LastFlush.ev_lo = 0;
    Forbid();
    if (FindPort((STRPTR) RGLPORT_NAME) == NULL)
    {
        AddPort(mp);
        rgl_port_added = TRUE;
    }
    Permit();
}

/* From Back_Expunge (no context is left then) */
void Port_Remove(void)
{
    if (!rgl_port_added)
        return;
    RemPort(&rgl_port.rp_Port);
    rgl_port_added = FALSE;
}

/* After the context list changed: counted anew from it (rgl.c). Under
 * Forbid, or called under it (Back_ClientGone). */
void Port_Count(void)
{
    struct Rgl *r;
    ULONG n;
    ULONG games;

    n = 0;
    games = 0;
    Forbid();
    for (r = rgl_list; r != NULL; r = r->next)
    {
        if (!r->real)
            continue;               /* the null card: nothing on the Radeon */
        n++;
        if (r->rad.kind == CARD_KIND_SCREEN)
            games++;
    }
    rgl_port.rp_Contexts = n;
    rgl_port.rp_ScreenContexts = games;
    Permit();
}

/* Every flush to the Radeon (card.c CardFlush): a count and the time */
void Port_Flushed(void)
{
    struct EClockVal ev;

    if (TimerBase != NULL)
        (void) ReadEClock(&ev);
    else
    {
        ev.ev_hi = 0;
        ev.ev_lo = 0;
    }
    Forbid();
    rgl_port.rp_Flushes++;
    rgl_port.rp_LastFlush = ev;
    Permit();
}
