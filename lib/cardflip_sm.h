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

/* 2026-11-14, step R (docs/step-r-design.md): an upper bound on the beam's
 * line time, from pairs of line reads the flush makes anyway (right after
 * the pan, and at the kick). Each sample: the time between the reads,
 * over-measured, divided by the lines between them, under-counted -- an
 * unseen wrap only adds lines, a pre-emption only adds time. The 3rd
 * smallest of the last 8 stays an upper bound with two bad reads among
 * them. R1 only logs it; R2 lets cure B pass once a refresh has gone by. */
#define FLIP_LINE_N          8
#define FLIP_LINE_SPAN       48UL       /* lines a pair must cover at least */
#define FLIP_LINE_TRUST      3          /* samples before the bound is used */
#define FLIP_LINE_MARGIN_US  100UL
#define FLIP_LINE_LAM_MAX    1000000UL  /* us * 256 a line: 3.9 ms, no mode is that slow */
#define FLIP_LINE_US_MAX     100000UL   /* a pair further apart is not used */

struct FlipLine
{
    ULONG crtc, disp, vtot, htot;   /* the mode the samples belong to */
    ULONG n;                        /* samples kept, up to FLIP_LINE_N */
    ULONG next;                     /* where the next one goes */
    ULONG lam[FLIP_LINE_N];         /* each an upper bound on the line time, us * 256 */
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

/*
 * 2026-11-14, the panning tear (docs/concept-panning-tear.md, cure B). A
 * pan's new offset is taken by the CRTC at the next vertical blank: until
 * then the third the pan left is still scanned out, and a frame kicked
 * into it shows half drawn. The rule, by the beam (read only):
 *
 * a reference is a beam line read AFTER the pan's register write with the
 * beam in the picture. Whenever the beam is in the blank later, or back
 * above the reference, the blank it passed began after the write: the
 * offset is taken (or will be before the next line is scanned). So a kick
 * needs no wait once that is seen; any other kick must wait for the blank
 * -- in the Radeon (R2E_WaitVline), the 68k goes on. The beam decides:
 * the time since the pan only cuts it short once it is longer than a
 * refresh in any mode a display takes (the review of 2026-11-14: a 21 ms
 * rule tore a 30 Hz mode).
 *
 * A line read in the blank is no reference: the write may have come after
 * the point of the blank where the CRTC takes the offset, and the third
 * stays on show for a whole picture more. A line no counter gives (at or
 * past the total, e.g. a failed bus read: $FFFFFFFF) is no verdict: wait.
 */

/* A refresh period at the slowest mode the Radeon offers (50 Hz) */
#define FLIP_SAFE_US     20000UL
/* The beam's time rule: this long after a pan's write the blank that takes
 * its offset has begun in any mode a display takes -- a refresh at 24 Hz
 * (41.7 ms), the lines a latch may come before the blank (FLIP_BEAM_GUARD)
 * and a margin. FLIP_SAFE_US (one refresh at 50 Hz) is too short even for
 * a 50 Hz mode with an early latch (test_flip: 20.06 ms at 720 x 576); it
 * stays as it was for the paths that use it (no helper). */
#define FLIP_BEAM_AGED_US 50000UL
/* lines a reference keeps off the picture's end (the counter against the
 * point the CRTC takes the offset at) */
#define FLIP_BEAM_GUARD  4UL

/* A beam line read after the pan's write: a reference? disp = the lines
 * displayed (the blank begins at line disp) */
static BOOL FlipBeam_RefOk(ULONG line, ULONG disp)
{
    return (BOOL) (disp > FLIP_BEAM_GUARD && line + FLIP_BEAM_GUARD < disp);
}

/* At the kick: TRUE = the Radeon must wait for the blank before it draws
 * into the third the pan left. ref_line: the reference; us_since_pan: no
 * more than the time since the pan's write; line_now: the beam now; disp,
 * vtot: the lines displayed and in all (the blank is [disp, vtot)) */
static BOOL FlipBeam_MustWait(ULONG ref_line, ULONG us_since_pan, ULONG line_now, ULONG disp, ULONG vtot)
{
    if (us_since_pan >= FLIP_BEAM_AGED_US)
        return FALSE;               /* longer than any refresh */
    if (line_now >= vtot)
        return TRUE;                /* no line the counter gives: no verdict */
    if (line_now >= disp)
        return FALSE;               /* in the blank that followed the reference */
    if (line_now < ref_line)
        return FALSE;               /* back above it: through a blank */
    return TRUE;
}

/*
 * 2026-11-14, step R (docs/step-r-design.md, R1): the pan moves to the
 * start of the flush, into the GL task, once the engine is known idle
 * (our fence passed). The frame the last flush kicked is settled there,
 * not in the lock, and shown at once: the picture is a 68k frame younger,
 * and the third the pan left rests a whole 68k frame before it is drawn
 * into again (cure B still guards it). No safe message is waited for:
 * leave is FLIP_NONE outside FlipSm_Panned.
 */

/* At the flush's start, outside the locks; idle = none of this card's
 * work is on the engine. The kicked frame settled (FlipSm_Settled in the
 * lock then finds nothing); returns the third to pan onto, or FLIP_NONE */
static ULONG FlipSm_StartPan(struct FlipSm *s, BOOL idle)
{
    if (!idle)
        return FLIP_NONE;
    FlipSm_Settled(s);
    return FlipSm_ToShow(s);
}

/* The GL task's pan to FlipSm_StartPan's third returned: cure B guards
 * the third it left, by the beam. Returns that third */
static ULONG FlipSm_Panned(struct FlipSm *s)
{
    ULONG left;

    FlipSm_Shown(s);
    left = s->leave;
    s->leave = FLIP_NONE;
    return left;
}

/* A present while the display is panned only at the next flush's start:
 * the next frame takes the third neither shown nor drawn into. A finished
 * frame waiting there (only after a fence wait gave up, or behind another
 * screen) is drawn over and counted */
static ULONG FlipSm_ChoosePan(struct FlipSm *s)
{
    ULONG i;

    for (i = 0; i < FLIP_N - 1 && (i == s->show || i == s->draw); i++)
        ;
    if (i == s->ready)
    {
        s->ready = FLIP_NONE;
        s->dropped++;
    }
    return i;
}

static void FlipLine_Reset(struct FlipLine *l)
{
    ULONG i;

    l->crtc = 0;
    l->disp = 0;
    l->vtot = 0;
    l->htot = 0;
    l->n = 0;
    l->next = 0;
    for (i = 0; i < FLIP_LINE_N; i++)
        l->lam[i] = 0;
}

/* One pair: line la read, then line lb read us microseconds later (the E
 * clock before the first read and after the second), in the mode (crtc,
 * disp, vtot, htot). Another mode starts over. Returns 1 when the pair was
 * taken, 0 when it does not qualify, 2 when it started over and took it,
 * 3 when it started over and the pair did not qualify */
static ULONG FlipLine_Sample(struct FlipLine *l, ULONG crtc, ULONG disp, ULONG vtot, ULONG htot,
                             ULONG la, ULONG lb, ULONG us)
{
    ULONG r;
    ULONG span;
    ULONG lam;

    r = 1;
    if (l->n == 0 || l->crtc != crtc || l->disp != disp || l->vtot != vtot || l->htot != htot)
    {
        if (l->n != 0)
            r = 2;
        FlipLine_Reset(l);
        l->crtc = crtc;
        l->disp = disp;
        l->vtot = vtot;
        l->htot = htot;
    }
    if (la >= disp || lb >= disp || lb < la + FLIP_LINE_SPAN || us >= FLIP_LINE_US_MAX)
        return (r == 2) ? 3UL : 0UL;
    span = lb - la - 1;             /* the beam moved more than this */
    lam = ((us + 3) * 256 + span - 1) / span;   /* +3: the E clock's quantisation */
    if (lam > FLIP_LINE_LAM_MAX)
        lam = FLIP_LINE_LAM_MAX;
    l->lam[l->next] = lam;
    l->next = (l->next + 1) % FLIP_LINE_N;
    if (l->n < FLIP_LINE_N)
        l->n++;
    return r;
}

/* The line time's upper bound (us * 256), 0 until FLIP_LINE_TRUST samples */
static ULONG FlipLine_Lam(const struct FlipLine *l)
{
    ULONG v[FLIP_LINE_N];
    ULONG i;
    ULONG j;
    ULONG t;

    if (l->n < FLIP_LINE_TRUST)
        return 0;
    for (i = 0; i < l->n; i++)
        v[i] = l->lam[i];
    for (i = 0; i < (ULONG) FLIP_LINE_TRUST; i++)     /* the smallest FLIP_LINE_TRUST to the front */
        for (j = i + 1; j < l->n; j++)
            if (v[j] < v[i])
            {
                t = v[i];
                v[i] = v[j];
                v[j] = t;
            }
    return v[FLIP_LINE_TRUST - 1];
}

/* A refresh plus the lines a latch may lie before the blank, plus a
 * margin, in us: this long after a pan's write the blank that takes its
 * offset has begun. 0 = not known */
static ULONG FlipLine_SafeUs(const struct FlipLine *l)
{
    ULONG lam;

    lam = FlipLine_Lam(l);
    if (lam == 0 || l->vtot == 0)
        return 0;
    /* 32 bits throughout, as on the Amiga (a host's 64-bit long would hide
     * an overflow): (2047 + 4) * FLIP_LINE_LAM_MAX + 255 < 2^32 */
    return (ULONG) ((ULONG) ((ULONG) (l->vtot + (ULONG) FLIP_BEAM_GUARD) * lam + (ULONG) 255) / (ULONG) 256)
           + (ULONG) FLIP_LINE_MARGIN_US;
}

/* R2's rule (R1 only counts it): a refresh has gone by since the record */
static BOOL FlipBeam_Refresh(ULONG us_since_record, ULONG safe_us)
{
    return (BOOL) (safe_us != 0 && us_since_record >= safe_us);
}

#endif /* FLIP_SM_FUNCTIONS */

#endif /* RADEONGL_CARDFLIP_SM_H */
