/*
 * cardflip_sm.h -- triple buffering's bookkeeping without the OS (2026-11-12):
 * which buffer the next frame draws into, which finished frame is shown,
 * when a flush must wait for a safe message. lib/cardflip.c drives it with
 * Picasso96's buffers, test/host/test_flip.c with a model of the display
 * and the Radeon (the invariant: the Radeon never draws into a buffer that
 * is shown or about to be, unless Intuition refused a switch).
 *
 * Three buffers: one shown (show), one the frame being built draws into
 * (draw), the third finished and waiting (ready), or left by the last
 * switch until its safe message comes (leave), or free.
 *
 * pipe (2026-11-13, ChangeScreenBuffer only; Tim's Balls scheme): a switch
 * counts as done when its call returned (FlipSm_Done), not when its safe
 * message came, so the next switch may follow while the left buffer still
 * waits for its message. Up to two left buffers wait at once (pend, a
 * bit per buffer); each is safe when the message of the switch AWAY from
 * it came (freed_by). Radeon.card returns from ChangeScreenBuffer at the
 * blank the switch took effect and sends the messages a refresh later:
 * without pipe that cost two refreshes a switch.
 */

#ifndef RADEONGL_CARDFLIP_SM_H
#define RADEONGL_CARDFLIP_SM_H

#include <exec/types.h>

#define FLIP_N    3
#define FLIP_NONE 0xffffffffUL

struct FlipSm
{
    ULONG show;                 /* the buffer shown, or asked to be */
    ULONG draw;                 /* the buffer the frame being built draws into */
    ULONG kick;                 /* a kicked frame's buffer, not yet known finished */
    ULONG ready;                /* a finished frame's buffer, not yet shown */
    ULONG leave;                /* the buffer the last switch left: its safe message outstanding */
    ULONG dropped;              /* finished frames drawn over before they were shown */
    ULONG pipe;                 /* the pipelined way (above); 0 = one switch until its safe message */
    ULONG pend;                 /* pipe: bit n = buffer n left, its safe message outstanding */
    ULONG inflight;             /* pipe: a switch asked whose call has not returned */
    ULONG freed_by[FLIP_N];     /* pipe: the buffer whose switch left buffer n */
};

/* The functions only where they are used (cardflip.c, the host test):
 * static, so every other includer would warn about them */
#ifdef FLIP_SM_FUNCTIONS

/* Buffer 0 (the screen's own) shown, the first frame into buffer 1 */
static void FlipSm_Init(struct FlipSm *s)
{
    ULONG i;

    s->show = 0;
    s->draw = 1;
    s->kick = FLIP_NONE;
    s->ready = FLIP_NONE;
    s->leave = FLIP_NONE;
    s->pipe = 0;
    s->pend = 0;
    s->inflight = 0;
    for (i = 0; i < FLIP_N; i++)
        s->freed_by[i] = FLIP_NONE;
}

/* A flush must wait for the last switch's safe message before the Radeon
 * draws: its buffer is the one that switch left */
static BOOL FlipSm_MustWait(const struct FlipSm *s)
{
    if (s->pipe)
        return (BOOL) (s->draw < FLIP_N && (s->pend & (1UL << s->draw)) != 0);
    return (BOOL) (s->leave != FLIP_NONE && s->leave == s->draw);
}

/* The safe message came (at most one switch is outstanding: FlipSm_ToShow) */
static void FlipSm_Safe(struct FlipSm *s)
{
    s->leave = FLIP_NONE;
}

/* pipe: buffer b's safe message came (the switch away from it) */
static void FlipSm_SafeBuf(struct FlipSm *s, ULONG b)
{
    if (b >= FLIP_N)
        return;
    s->pend &= ~(1UL << b);
    if (s->leave == b)
        s->leave = FLIP_NONE;
}

/* pipe: the asked switch's call returned and it took */
static void FlipSm_Done(struct FlipSm *s)
{
    s->inflight = 0;
}

/* The flush settled the last kicked frame: finished */
static void FlipSm_Settled(struct FlipSm *s)
{
    if (s->kick == FLIP_NONE)
        return;
    if (s->ready != FLIP_NONE)
        s->dropped++;               /* never shown: a newer frame is finished */
    s->ready = s->kick;
    s->kick = FLIP_NONE;
}

/* A present: the next frame's buffer -- neither this frame's nor the one
 * shown after this flush (FlipSm_ToShow's, when no switch is outstanding).
 * A finished frame waiting there is drawn over. */
static ULONG FlipSm_Choose(struct FlipSm *s)
{
    ULONG after;
    ULONG i;

    if (s->pipe)
        /* exactly FlipSm_ToShow's test: the ready frame is shown after this
         * flush only if it may be (a frame drawn into the displayed buffer
         * after a refused switch is ready while that buffer still waits for
         * its safe message) */
        after = (s->ready != FLIP_NONE && !s->inflight && s->ready != s->show
                 && (s->pend & (1UL << s->ready)) == 0) ? s->ready : s->show;
    else
        after = (s->ready != FLIP_NONE && s->leave == FLIP_NONE) ? s->ready : s->show;
    for (i = 0; i < FLIP_N - 1 && (i == after || i == s->draw); i++)
        ;
    if (i == s->ready)
    {
        s->ready = FLIP_NONE;
        s->dropped++;
    }
    return i;
}

/* The presented frame ran: kicked (pending: the overlap) or finished
 * already; the next frame draws into next */
static void FlipSm_Ran(struct FlipSm *s, BOOL pending, ULONG next)
{
    if (pending)
        s->kick = s->draw;
    else
    {
        if (s->ready != FLIP_NONE)
            s->dropped++;
        s->ready = s->draw;
    }
    s->draw = next;
}

/* The buffer to show now: a finished frame, no switch outstanding; else
 * FLIP_NONE */
static ULONG FlipSm_ToShow(const struct FlipSm *s)
{
    if (s->pipe)
    {
        /* pipe: no switch in flight; a left buffer may still wait */
        if (s->ready != FLIP_NONE && !s->inflight && s->ready != s->show
            && (s->pend & (1UL << s->ready)) == 0)
            return s->ready;
        return FLIP_NONE;
    }
    if (s->ready != FLIP_NONE && s->leave == FLIP_NONE && s->ready != s->show)
        return s->ready;
    return FLIP_NONE;
}

/* The switch to FlipSm_ToShow's buffer took */
static void FlipSm_Shown(struct FlipSm *s)
{
    if (s->pipe && s->show < FLIP_N)
    {
        s->pend |= 1UL << s->show;
        s->freed_by[s->show] = s->ready;
        s->inflight = 1;
    }
    s->leave = s->show;
    s->show = s->ready;
    s->ready = FLIP_NONE;
}

/* A switch FlipSm_Shown counted (the helper asked) never took: the display
 * still shows the buffer it was to leave. The frame meant for the display
 * is ready again unless a newer one is (then it is dropped). */
static void FlipSm_Unshow(struct FlipSm *s)
{
    if (s->ready == FLIP_NONE)
        s->ready = s->show;
    else
        s->dropped++;
    if (s->pipe && s->leave < FLIP_N)
    {
        s->pend &= ~(1UL << s->leave);  /* no message comes for a refused switch */
        s->inflight = 0;
    }
    s->show = s->leave;
    s->leave = FLIP_NONE;
}

#endif /* FLIP_SM_FUNCTIONS */

#endif /* RADEONGL_CARDFLIP_SM_H */
