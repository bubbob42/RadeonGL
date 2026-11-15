/*
 * cardwait.c -- the waits for the Radeon's fence, sleeping (2026-11-15,
 * Herald). Linked late (the Makefile's notes on code layout): card.c calls
 * it from Settle and from the waited present.
 */

#include <stddef.h>
#include <exec/types.h>
#include <exec/ports.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "card.h"

/* RadeonGLWaitSleep: the sleep step in us (default 500; 0: the old spin),
 * read at the card's first wait -- here rather than in card.c's OpenTimer,
 * which sits in front of CardFlush */
static void WaitSetup(struct Card *k)
{
    char flag[12];
    LONG n;
    ULONG i;
    ULONG v;

    k->wait_init = 1;
    k->wait_spin_us = 200;
    k->wait_sleep_us = 500;
    n = GetVar((STRPTR) "RadeonGLWaitSleep", (STRPTR) flag, sizeof(flag), 0);
    if (n > 0)
    {
        for (i = 0, v = 0; i < (ULONG) n && flag[i] >= '0' && flag[i] <= '9'; i++)
            v = v * 10 + (ULONG) (flag[i] - '0');
        if (i > 0)
            k->wait_sleep_us = (v > 20000) ? 20000 : v;
    }
}

/* 2026-11-15 (Herald: a priority-0 task ran only every 115-160 ms while a
 * bitmap context's presents spun for the Radeon): R2Ring_WaitFence, but
 * after wait_spin_us without the fence it sleeps on timer.device between
 * the reads (the card's request -- nothing else of it is out during a
 * flush), so the other tasks of the same priority run meanwhile.
 * wait_sleep_us 0: the plain spin. In this file of its own, linked behind
 * the hot code, and through r2ring.h only: r2ring.o and CardFlush keep their
 * places (as a static in card.c GCC put it ahead of CardFlush: +356 bytes,
 * and GLQuake 0.4% slower in the A/B of 2026-11-15). */
#define WAIT_END_US 600         /* spun before a wait's guessed end */

LONG Card_WaitFenceSleep(struct R2Ring *r, ULONG seq)
{
    struct Card *k;
    struct timerequest *t;
    ULONG start;
    ULONG gone;
    ULONG end;
    ULONG slept;
    LONG err;

    k = (struct Card *) ((UBYTE *) r - offsetof(struct Card, ring));
    /* only with this task's own timer request: SettleOthers settles the
     * card another program left running (card_running), in THIS task --
     * a DoIO on that card's request would wait for a reply that goes to
     * its owner, holding a Picasso96 lock (found 2026-11-15, before it
     * bit). Not ours: spin, as before. The setting is read after this
     * test, so in the task that opened the card (as OpenTimer did) */
    if (k->tport == NULL || k->tport->mp_SigTask != (APTR) FindTask(NULL))
        return R2Ring_WaitFence(r, seq);
    if (!k->wait_init)
        WaitSetup(k);
    if (k->wait_sleep_us == 0)
        return R2Ring_WaitFence(r, seq);
    err = R2Ring_Commit(r);
    if (err != R2_OK)
        return err;
    if (R2Ring_FenceDone(r, seq))
        return R2_OK;
    r->stats.stalls++;
    start = k->hw.clock_us(k->hw.hw);
    /* 2026-11-15 (Herald: 500 us steps are coarse at the end of a ~6 ms
     * wait): the end the last wait had is the guess -- sleep until
     * WAIT_END_US before it, spin the rest; a wait that runs far past the
     * guess sleeps again */
    end = (k->wait_last_us > WAIT_END_US) ? k->wait_last_us - WAIT_END_US : 0;
    slept = 0;
    while (!R2Ring_FenceDone(r, seq))
    {
        gone = k->hw.clock_us(k->hw.hw) - start;
        if (gone > r->timeout_us)
            return R2_ETIMEOUT;
        if (gone >= k->wait_spin_us && (gone < end || gone > end + 2 * WAIT_END_US))
        {
            t = k->treq;
            t->tr_node.io_Command = TR_ADDREQUEST;
            t->tr_time.tv_secs = 0;
            t->tr_time.tv_micro = k->wait_sleep_us;
            DoIO((struct IORequest *) t);
            k->wait_sleeps++;
            slept = 1;
        }
    }
    if (slept)
    {
        gone = k->hw.clock_us(k->hw.hw) - start;
        if (end != 0 && gone >= end)
            k->wait_spun_end++;
        k->wait_last_us = gone;
    }
    return R2_OK;
}
