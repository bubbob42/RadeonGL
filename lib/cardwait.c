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
#include "core.h"
#include "coreint.h"
#include "r2emit.h"
#include "r200reg.h"

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
    ULONG quiet;
    ULONG now;
    ULONG gone;
    ULONG end;
    ULONG slept;
    ULONG slow;
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
    /* 2026-11-28 (GLBench 0.2's fog tests: frames of more than 0.5 s of
     * GPU work were given up as hung, the engine reset mid-draw): the
     * timeout counts from the CP's last read of the ring, not from the
     * start -- RPTR read now (the baseline) and when timeout_us has run
     * out; a CP still reading starts the next stretch (r2ring.h) */
    (void) R2Ring_Moved(r);
    slow = 0;
    start = k->hw.clock_us(k->hw.hw);
    quiet = start;
    /* 2026-11-15 (Herald: 500 us steps are coarse at the end of a ~6 ms
     * wait): the end the last wait had is the guess -- sleep until
     * WAIT_END_US before it, spin the rest; a wait that runs far past the
     * guess sleeps again */
    end = (k->wait_last_us > WAIT_END_US) ? k->wait_last_us - WAIT_END_US : 0;
    slept = 0;
    while (!R2Ring_FenceDone(r, seq))
    {
        now = k->hw.clock_us(k->hw.hw);
        gone = now - start;
        if (now - quiet > r->timeout_us)
        {
            if (!R2Ring_Moved(r))
                return R2_ETIMEOUT;
            quiet = now;
            if (!slow)
            {
                slow = 1;
                k->wait_slow++;
            }
        }
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

/* 2026-11-28 (GLBench 0.2's fog tests on Hamiller: frames of more than a
 * second of GPU work were reset mid-draw): the vertical-blank server's
 * entry since 0.21. A kicked frame's fence comes only at its end, so the
 * CP still reading the ring counts as progress too: the watchdog's count
 * (card.c Card_WdCheck, wd_ticks) starts again whenever CP_RB_RPTR has
 * moved since the last vblank -- a hung engine stops it within a FIFO's
 * worth of commands. Only a move towards what was committed is one, as in
 * r2ring.c's ReadRptr (a failed bus read gives 0xffffffff); a new frame's
 * baseline is the ring's own last read. Here, behind the hot code, and
 * through a stub of its own: card.o keeps 0.20's bytes (its old stub
 * stays, unused) and the core behind it its places. Interrupt context:
 * register reads only, the conditions as Card_WdCheck's. */
ULONG Card_WdEntry(struct Card *k)
{
    ULONG m;
    ULONG p;
    ULONG f;

    if (k->pending && !k->wd_hold && !k->wd_fired && k->lost == 0)
    {
        if (k->wd_frame != k->wd_seq)
        {
            k->wd_frame = k->wd_seq;
            k->wd_rptr = k->ring.rptr;
            k->wd_quiet = 0;
        }
        f = k->hw.read_reg(k->hw.hw, k->ring.fence_reg);
        if ((LONG) (f - k->wd_seq) < 0)
        {
            if (++k->wd_quiet > k->wd_quiet_max)
                k->wd_quiet_max = k->wd_quiet;
            m = k->ring.size - 1;
            p = k->hw.read_reg(k->hw.hw, R_CP_RB_RPTR) & m;
            if (p != k->wd_rptr && ((p - k->wd_rptr) & m) <= ((k->ring.committed - k->wd_rptr) & m))
            {
                k->wd_rptr = p;
                k->wd_ticks = 0;    /* Card_WdCheck counts from 1 again */
            }
        }
    }
    return Card_WdCheck(k);
}

/* exec's server entry, as card.c's Card_WdStub: A1 = is_Data; GCC's
 * callee-saved registers kept, D0 = 0 with Z set (the chain goes on) */
__asm__(
    "\t.text\n"
    "\t.align\t2\n"
    "\t.globl\t_Card_WdEntryStub\n"
    "_Card_WdEntryStub:\n"
    "\tmovem.l d2-d7/a2-a6,-(sp)\n"
    "\tmove.l a1,-(sp)\n"
    "\tjsr _Card_WdEntry\n"
    "\taddq.l #4,sp\n"
    "\tmovem.l (sp)+,d2-d7/a2-a6\n"
    "\tmoveq #0,d0\n"
    "\trts\n");

/* a decimal environment variable, def when unset or not a number */
static ULONG EnvNum(const char *name, ULONG def)
{
    char v[16];
    ULONG n;
    ULONG i;

    if (GetVar((STRPTR) name, (STRPTR) v, sizeof(v), 0) <= 0)
        return def;
    for (i = 0, n = 0; v[i] >= '0' && v[i] <= '9'; i++)
        n = n * 10 + (ULONG) (v[i] - '0');
    return i > 0 ? n : def;
}

/* 2026-11-25: the switches of the R200 hang's fix and of the watchdog
 * (docs/audit-2026-11-24-r200-hang.md), read at the context's open in the
 * opening task (rgl.c CreateContext; k NULL without a card). Here, behind
 * the hot code: in rgl.c they moved its hooks by 1112 bytes.
 * RadeonGLPreWait=0: the old wait inside the P96 lock (no watchdog then);
 * RadeonGLSerial=1 a line on the serial port for every hang, 2 also a
 * heartbeat; RadeonGLWatchdog=N vblanks without fence progress before the
 * vertical-blank server resets a hung engine (default 50 = 1 s on PAL, 0
 * off); RadeonGLTestWatchdog=1 (test build) lets it fire on an idle
 * engine; RadeonGLPpWait, RadeonGLPpWaitNoClean, RadeonGLPpWaitBlend and
 * RadeonGLBlendWait: the fix (r2emit.h). With RGL_PROBES also the night's
 * probes (r2emit.h, texgen.c): RadeonGLIsync=1 the 2D/3D interlock,
 * RadeonGLDcLine, RadeonGLTclFlush, RadeonGLTamDebug, RadeonGLTexgenFix,
 * RadeonGLTclWait, RadeonGLMiscWait, RadeonGLNoDedup, RadeonGLTexFlush,
 * RadeonGLTc4, RadeonGLSepBlend. */
void Card_ReadSwitches(struct Card *k)
{
    char flag[4];

    if (k != NULL)
    {
        /* RadeonGLPreWait=0: a pending frame waited for inside the P96 lock
         * only, as up to 0.18 (card.c PreSettle); the watchdog needs it */
        k->prewait = (GetVar((STRPTR) "RadeonGLPreWait", (STRPTR) flag, sizeof(flag), 0) > 0
                      && flag[0] == '0') ? 0UL : 1UL;
        k->serial = EnvNum("RadeonGLSerial", 0);
#ifdef RGL_PROBES
        Card_WdArm(k, EnvNum("RadeonGLWatchdog", 50), EnvNum("RadeonGLTestWatchdog", 0),
                   EnvNum("RadeonGLIsync", 0));
        Card_DcLine(k, EnvNum("RadeonGLDcLine", 0xff));
#else
        Card_WdArm(k, EnvNum("RadeonGLWatchdog", 50), EnvNum("RadeonGLTestWatchdog", 0), 0);
#endif
    }
    R2E_PpWait = EnvNum("RadeonGLPpWait", 4);
    R2E_BlendMtWait = EnvNum("RadeonGLBlendWait", 1);
#ifdef RGL_PROBES
    R2E_PpWaitNoClean = EnvNum("RadeonGLPpWaitNoClean", 1);
    R2E_PpWaitBlend = EnvNum("RadeonGLPpWaitBlend", 0);
    R2E_TclFlushOn = EnvNum("RadeonGLTclFlush", 0);
    R2E_TamDebug = EnvNum("RadeonGLTamDebug", 0);
    Texgen_FixUnits = EnvNum("RadeonGLTexgenFix", 0);
    R2E_TclWait = EnvNum("RadeonGLTclWait", 0);
    R2E_MiscWait = EnvNum("RadeonGLMiscWait", 0);
    R2E_NoDedup = EnvNum("RadeonGLNoDedup", 0);
    R2E_TexFlush = EnvNum("RadeonGLTexFlush", 0);
    R2E_Tc4 = EnvNum("RadeonGLTc4", 0);
    R2E_SepBlend = EnvNum("RadeonGLSepBlend", 0);
#endif
}
