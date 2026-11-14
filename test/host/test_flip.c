/*
 * test_flip.c -- triple buffering's bookkeeping (lib/cardflip_sm.h) against
 * a model of the display and the Radeon, 2026-11-12.
 *
 * The model: a refresh every 16.683 ms; a switch takes effect at the next
 * vertical blank and its safe message comes with it. The Radeon writes a
 * kicked frame's buffer until it finishes; a flush first settles what ran
 * before (waits for it), as card.c does. The program builds frames of a
 * random length, with random flushes in between, and presents, driving the
 * bookkeeping exactly as lib/cardflip.c does.
 *
 * Checked, in every regime (68k slower and faster than the refresh, a slow
 * Radeon, refused switches, flushes mid-frame):
 *  - the Radeon never writes a buffer that is on the display, or about to
 *    be, unless Intuition refused the last switch;
 *  - only finished frames are shown, each newer than the one before;
 *  - a flush never waits for a safe message that cannot come;
 *  - with the 68k slower than the refresh and no refusals, every frame is
 *    shown (none dropped) and no flush waits.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <exec/types.h>

#define FLIP_SM_FUNCTIONS
#include "../../lib/cardflip_sm.h"

#define NONE FLIP_NONE
#define REFRESH 16683.0

static int checks = 0;
static int failures = 0;

#define CHECK(cond, what) \
    do \
    { \
        checks++; \
        if (!(cond)) \
        { \
            failures++; \
            if (failures < 20) \
                printf("FAIL %s:%d: %s (%s)\n", __FILE__, __LINE__, what, regime); \
        } \
    } while (0)

static const char *regime = "";

/* a fixed generator: the same runs every time */
static unsigned long rng = 1;

static double Rand(double lo, double hi)
{
    rng = rng * 1103515245UL + 12345UL;
    return lo + (hi - lo) * (double) ((rng >> 8) & 0xffffUL) / 65535.0;
}

struct Model
{
    double t;                   /* now, us */
    double vblank;              /* the next vertical blank */
    ULONG displayed;            /* the buffer scanned out */
    ULONG pend;                 /* a switch waiting for the blank */
    int msg_out;                /* its safe message not yet come */
    int msg_in;                 /* a safe message came, not yet taken */
    ULONG writing;              /* the buffer the Radeon writes, NONE */
    double gpu_done;
    long writing_frame;         /* the frame it writes (-2: part of a frame) */
    long content[FLIP_N];       /* the finished frame in each buffer, -1 none */
    long last_shown;
    int refused;                /* the last switch was refused */
    int late;                   /* a switch counted shown (asked of the helper) will be given up */
    long shown;
    long waits;
    long refusals;
    long violations_allowed;
};

/* time runs to `to`: blanks and the Radeon's end on the way */
static void Advance(struct Model *m, double to)
{
    while (m->vblank <= to || (m->writing != NONE && m->gpu_done <= to))
    {
        if (m->writing != NONE && m->gpu_done <= m->vblank && m->gpu_done <= to)
        {
            if (m->writing_frame >= 0)
                m->content[m->writing] = m->writing_frame;
            m->writing = NONE;
            continue;
        }
        if (m->vblank > to)
            break;
        if (m->pend != NONE)
        {
            m->displayed = m->pend;
            m->pend = NONE;
            m->msg_out = 0;
            m->msg_in = 1;
        }
        if (m->writing != NONE && m->writing == m->displayed)
        {
            if (m->refused)
                m->violations_allowed++;
            else
                CHECK(0, "the Radeon writes the buffer on the display");
        }
        m->vblank += REFRESH;
    }
    if (to > m->t)
        m->t = to;
}

static void Drain(struct Model *m, struct FlipSm *s)
{
    if (m->msg_in)
    {
        m->msg_in = 0;
        FlipSm_Safe(s);
    }
}

/* one flush as card.c's CardFlush with lib/cardflip.c's calls */
static void Flush(struct Model *m, struct FlipSm *s, int present, long frame, double gpu_time,
                  double refuse, int late)
{
    ULONG next;
    ULONG show;

    next = NONE;
    /* CardFlip_Safe: CsbExpire finds the helper gave the last switch up */
    if (m->late)
    {
        m->late = 0;
        FlipSm_Unshow(s);
    }
    Drain(m, s);
    if (FlipSm_MustWait(s))
    {
        CHECK(m->msg_out, "a flush waits for a safe message that cannot come");
        if (!m->msg_out)
            return;
        m->waits++;
        Advance(m, m->vblank);
        Drain(m, s);
        CHECK(!FlipSm_MustWait(s), "the safe message came, still waiting");
    }
    /* the lock settles what ran before */
    if (m->writing != NONE)
        Advance(m, m->gpu_done);
    FlipSm_Settled(s);
    /* the present chooses the next frame's buffer in the lock */
    if (present)
        next = FlipSm_Choose(s);
    /* the kick: this flush's work into the draw buffer */
    if (s->draw == m->displayed || s->draw == m->pend)
    {
        if (m->refused)
            m->violations_allowed++;
        else
            CHECK(0, "a kick into the buffer on the display (or about to be)");
    }
    m->writing = s->draw;
    m->gpu_done = m->t + gpu_time;
    m->writing_frame = present ? frame : -2;
    m->content[s->draw] = -2;   /* drawn over */
    /* CardFlip_After */
    if (present)
        FlipSm_Ran(s, TRUE, next);
    Drain(m, s);
    show = FlipSm_ToShow(s);
    if (show != NONE)
    {
        if (Rand(0.0, 1.0) < refuse)
        {
            m->refused = 1;
            m->refusals++;
            if (late)
            {
                /* the helper switches: counted shown when asked, given up
                 * later (lib/cardflip.c CsbExpire, FlipSm_Unshow) */
                FlipSm_Shown(s);
                m->late = 1;
            }
        }
        else
        {
            CHECK(m->content[show] >= 0, "a buffer shown that holds no finished frame");
            CHECK(m->content[show] > m->last_shown, "frames shown out of order");
            CHECK(m->writing != show, "a buffer shown while the Radeon writes it");
            CHECK(m->pend == NONE && !m->msg_out, "a switch while another is outstanding");
            m->last_shown = m->content[show];
            m->pend = show;
            m->msg_out = 1;
            m->shown++;
            m->refused = 0;
            FlipSm_Shown(s);
        }
    }
}

static void Run(const char *name, unsigned long seed, long frames, double cpu_lo, double cpu_hi,
                double gpu_lo, double gpu_hi, double refuse, int late, int mid, long *dropped,
                long *shown, long *waits)
{
    struct Model m;
    struct FlipSm s;
    long f;
    int k;
    int n;
    ULONG i;

    regime = name;
    rng = seed;
    m.t = 0.0;
    m.vblank = REFRESH;
    m.displayed = 0;
    m.pend = NONE;
    m.msg_out = 0;
    m.msg_in = 0;
    m.writing = NONE;
    m.gpu_done = 0.0;
    m.writing_frame = -1;
    for (i = 0; i < FLIP_N; i++)
        m.content[i] = -1;
    m.content[0] = -1;
    m.last_shown = -1;
    m.refused = 0;
    m.late = 0;
    m.shown = 0;
    m.waits = 0;
    m.refusals = 0;
    m.violations_allowed = 0;
    s.dropped = 0;
    FlipSm_Init(&s);
    for (f = 0; f < frames; f++)
    {
        n = mid ? (int) Rand(0.0, 3.99) : 0;
        for (k = 0; k < n; k++)
        {
            Advance(&m, m.t + Rand(cpu_lo, cpu_hi) / (n + 1));
            Flush(&m, &s, 0, f, Rand(gpu_lo, gpu_hi) / (n + 1), refuse, late);
        }
        Advance(&m, m.t + Rand(cpu_lo, cpu_hi) / (n + 1));
        Flush(&m, &s, 1, f, Rand(gpu_lo, gpu_hi) / (n + 1), refuse, late);
        /* the three buffers stay three different ones */
        CHECK((s.draw < FLIP_N && s.show < FLIP_N && s.draw != s.show)
              || (m.refused && s.draw == s.show), "draw and show the same buffer");
        CHECK(m.late || s.leave == NONE || s.show == m.pend || s.show == m.displayed,
              "the bookkeeping shows a buffer the display never got");
    }
    if (refuse == 0.0)
        CHECK(m.violations_allowed == 0, "a refusal excused a write without a refusal");
    *dropped = (long) s.dropped;
    *shown = m.shown;
    *waits = m.waits;
    printf("  %-34s frames %ld shown %ld dropped %ld waits %ld refused %ld (excused writes %ld)\n",
           name, frames, m.shown, (long) s.dropped, m.waits, m.refusals, m.violations_allowed);
}

/* --- pipe (2026-11-13): Radeon.card's own timing ---------------------------------
 * A switch asked of the helper: its call returns at the next blank, where
 * the display changes; the left buffer's safe message comes a refresh
 * after that. One switch in flight at a time; several safe messages may be
 * out. The GL task learns of a returned call and of messages only when it
 * looks (Drain at its flushes and in its safe wait), as lib/cardflip.c's
 * CsbExpire. Checked: the Radeon never writes the displayed buffer, the
 * one being switched to, or a left buffer whose message is out; frames are
 * shown in order; with frames faster than the refresh the switches reach
 * the refresh rate (the point of the pipe). */
struct ModelP
{
    double t, vblank;
    ULONG displayed;
    ULONG inflight;             /* the buffer a call in flight switches to, NONE */
    int refused_call;           /* that call will be refused */
    int done_in;                /* a call returned (or was given up), not yet seen */
    int done_ok;
    double safe_at[FLIP_N];     /* buffer n's safe message comes then (0: none out) */
    int safe_in[FLIP_N];        /* came, not yet seen */
    ULONG writing;
    double gpu_done;
    long writing_frame;
    long content[FLIP_N];
    long last_shown, shown, waits, refusals;
    int refused_last;           /* the last call that ended was refused */
    long excused;               /* writes into the displayed buffer after a refusal */
};

static void AdvanceP(struct ModelP *m, double to)
{
    ULONG i;
    double next;

    for (;;)
    {
        next = to;
        if (m->writing != NONE && m->gpu_done < next)
            next = m->gpu_done;
        if (m->vblank < next)
            next = m->vblank;
        for (i = 0; i < FLIP_N; i++)
            if (m->safe_at[i] > 0.0 && m->safe_at[i] < next)
                next = m->safe_at[i];
        if (next >= to && !(m->writing != NONE && m->gpu_done <= to) && m->vblank > to)
        {
            for (i = 0; i < FLIP_N; i++)
                if (m->safe_at[i] > 0.0 && m->safe_at[i] <= to)
                {
                    m->safe_at[i] = 0.0;
                    m->safe_in[i] = 1;
                }
            break;
        }
        m->t = next;
        if (m->writing != NONE && m->gpu_done <= next)
        {
            if (m->writing_frame >= 0)
                m->content[m->writing] = m->writing_frame;
            m->writing = NONE;
            continue;
        }
        for (i = 0; i < FLIP_N; i++)
            if (m->safe_at[i] > 0.0 && m->safe_at[i] <= next)
            {
                m->safe_at[i] = 0.0;
                m->safe_in[i] = 1;
            }
        if (m->vblank <= next)
        {
            if (m->inflight != NONE)
            {
                if (m->refused_call)
                {
                    m->done_ok = 0;
                    m->refused_last = 1;
                }
                else
                {
                    m->safe_at[m->displayed] = m->vblank + REFRESH;
                    m->displayed = m->inflight;
                    m->done_ok = 1;
                    m->refused_last = 0;
                }
                m->inflight = NONE;
                m->done_in = 1;
            }
            if (m->writing != NONE && m->writing == m->displayed)
            {
                if (m->refused_last)
                    m->excused++;
                else
                    CHECK(0, "pipe: the Radeon writes the buffer on the display");
            }
            m->vblank += REFRESH;
        }
    }
    if (to > m->t)
        m->t = to;
}

/* what CsbExpire sees */
static void DrainP(struct ModelP *m, struct FlipSm *s)
{
    ULONG i;

    if (!s->pipe)
    {
        /* the gate (RadeonGLFlip=csb): the helper reports the switch done
         * with its safe message; refusals handled as Unshow */
        if (m->done_in && !m->done_ok)
        {
            m->done_in = 0;
            FlipSm_Unshow(s);
        }
        else if (m->done_in)
            m->done_in = 0;
        for (i = 0; i < FLIP_N; i++)
            if (m->safe_in[i])
            {
                m->safe_in[i] = 0;
                if (s->leave == i)
                    FlipSm_Safe(s);
            }
        return;
    }
    if (m->done_in && s->inflight)
    {
        m->done_in = 0;
        if (m->done_ok)
            FlipSm_Done(s);
        else
            FlipSm_Unshow(s);
    }
    for (i = 0; i < FLIP_N; i++)
        if (m->safe_in[i])
        {
            m->safe_in[i] = 0;
            FlipSm_SafeBuf(s, i);
        }
}

static void FlushP(struct ModelP *m, struct FlipSm *s, int present, long frame, double gpu_time,
                   double refuse)
{
    ULONG next;
    ULONG show;
    double until;
    ULONG i;

    next = NONE;
    DrainP(m, s);
    if (FlipSm_MustWait(s))
    {
        CHECK(m->safe_at[s->draw] > 0.0 || m->safe_in[s->draw] || m->inflight != NONE,
              "pipe: a flush waits for a safe message that cannot come");
        m->waits++;
        for (i = 0; i < 10 && FlipSm_MustWait(s); i++)
        {
            until = m->safe_at[s->draw] > 0.0 ? m->safe_at[s->draw] : m->vblank;
            AdvanceP(m, until);
            DrainP(m, s);
        }
        CHECK(!FlipSm_MustWait(s), "pipe: the safe message came, still waiting");
    }
    if (m->writing != NONE)
        AdvanceP(m, m->gpu_done);
    DrainP(m, s);
    FlipSm_Settled(s);
    if (present)
        next = FlipSm_Choose(s);
    if (s->draw == m->displayed && m->refused_last)
        m->excused++;               /* Intuition refused: as the gate (above) */
    else
        CHECK(s->draw != m->displayed && s->draw != m->inflight && m->safe_at[s->draw] == 0.0
              && !m->safe_in[s->draw],
              "pipe: a kick into a buffer shown, being switched to, or not yet safe");
    m->writing = s->draw;
    m->gpu_done = m->t + gpu_time;
    m->writing_frame = present ? frame : -2;
    m->content[s->draw] = -2;
    if (present)
        FlipSm_Ran(s, TRUE, next);
    DrainP(m, s);
    show = FlipSm_ToShow(s);
    if (show != NONE)
    {
        CHECK(m->inflight == NONE, "pipe: a switch while another call is in flight");
        CHECK(m->content[show] >= 0, "pipe: a buffer shown that holds no finished frame");
        CHECK(m->content[show] > m->last_shown, "pipe: frames shown out of order");
        CHECK(m->writing != show, "pipe: a buffer shown while the Radeon writes it");
        m->refused_call = Rand(0.0, 1.0) < refuse;
        if (m->refused_call)
            m->refusals++;
        else
        {
            m->last_shown = m->content[show];
            m->shown++;
        }
        m->inflight = show;
        FlipSm_Shown(s);
    }
}

static void RunP(const char *name, unsigned long seed, long frames, double cpu_lo, double cpu_hi,
                 double gpu_lo, double gpu_hi, double refuse, int mid, int pipe, long *dropped, long *shown,
                 long *waits, double *seconds)
{
    struct ModelP m;
    struct FlipSm s;
    long f;
    int k, n;
    ULONG i;

    regime = name;
    rng = seed;
    m.t = 0.0;
    m.vblank = REFRESH;
    m.displayed = 0;
    m.inflight = NONE;
    m.refused_call = 0;
    m.done_in = 0;
    m.done_ok = 0;
    for (i = 0; i < FLIP_N; i++)
    {
        m.safe_at[i] = 0.0;
        m.safe_in[i] = 0;
        m.content[i] = -1;
    }
    m.writing = NONE;
    m.gpu_done = 0.0;
    m.writing_frame = -1;
    m.last_shown = -1;
    m.shown = m.waits = m.refusals = 0;
    m.refused_last = 0;
    m.excused = 0;
    s.dropped = 0;
    FlipSm_Init(&s);
    s.pipe = (ULONG) pipe;
    for (f = 0; f < frames; f++)
    {
        n = mid ? (int) Rand(0.0, 3.99) : 0;
        for (k = 0; k < n; k++)
        {
            AdvanceP(&m, m.t + Rand(cpu_lo, cpu_hi) / (n + 1));
            FlushP(&m, &s, 0, f, Rand(gpu_lo, gpu_hi) / (n + 1), refuse);
        }
        AdvanceP(&m, m.t + Rand(cpu_lo, cpu_hi) / (n + 1));
        FlushP(&m, &s, 1, f, Rand(gpu_lo, gpu_hi) / (n + 1), refuse);
        CHECK(s.draw < FLIP_N && s.show < FLIP_N && (s.draw != s.show || refuse > 0.0),
              "pipe: draw and show the same buffer");
        CHECK((s.pend & ~7UL) == 0, "pipe: pend out of range");
    }
    *dropped = (long) s.dropped;
    *shown = m.shown;
    *waits = m.waits;
    *seconds = m.t / 1000000.0;
    if (refuse == 0.0)
        CHECK(m.excused == 0, "pipe: a write excused without a refusal");
    printf("  %s %-29s frames %ld shown %ld dropped %ld waits %ld refused %ld (excused %ld), %.1f shown/s\n",
           pipe ? "pipe" : "gate", name, frames, m.shown, (long) s.dropped, m.waits, m.refusals, m.excused,
           m.shown / (m.t / 1000000.0));
}

/* --- the panning tear (2026-11-14, docs/concept-panning-tear.md, cure B) --------------------- */

/*
 * MEASURED by eye on the A4000D: the CRTC takes a pan's new offset at the
 * next vertical blank, so the third a pan left is scanned out until then.
 * The rule (cardflip_sm.h FlipBeam_*) and the way lib/cardflip.c applies it
 * (BeamNote in the helper, BeamRef at the flush's start, CardFlip_Beam at
 * the kick), against a model of the beam:
 *
 * a mode of vt lines, disp of them displayed, a refresh every T us; time 0
 * is the top of a picture. The CRTC takes a pending offset at one latch
 * point a refresh -- four models: two lines before the blank (a prefetch),
 * the blank's start, its middle, its end. The third a pan left is on show
 * until the blank of the first latch point after the pan's register write
 * W: the Radeon's first write into it must come at that blank's start or
 * later.
 *
 * A trial: W at random; the helper reads the beam 5-300 us later (BeamNote)
 * and the E clock a little after; the game judges (BeamRef) some time
 * later -- the reference is the helper's line, or (that was in the blank)
 * a picture line polled in 1 ms sleeps; without one it sleeps out the time
 * rule; the kick (CardFlip_Beam) comes later still, reads the beam once
 * more and decides; the CP reaches the wait delta after that. With the wait
 * the engine starts at the first moment the beam is in a blank, else at
 * once.
 */

struct BeamMode
{
    const char *name;
    double vt;                  /* lines a refresh */
    double disp;                /* ... displayed */
    double T;                   /* us a refresh */
};

static const struct BeamMode beam_modes[] =
{
    { "640x480@60", 525.0, 480.0, 16683.0 },
    { "1024x768@60", 806.0, 768.0, 16661.0 },
    { "1280x720@60", 750.0, 720.0, 16667.0 },
    { "1920x1080@60", 1125.0, 1080.0, 16667.0 },
    { "800x600@75", 625.0, 600.0, 13333.0 },
    { "1024x768@85", 808.0, 768.0, 11765.0 },
    { "720x576@50", 625.0, 576.0, 20000.0 },
    { "1280x1024@50", 1066.0, 1024.0, 20000.0 },
    { "PAL-ish 49.92 Hz, blank 3 ms", 625.0, 530.0, 20032.0 },
    { "640x400@70", 449.0, 400.0, 14268.0 },
    { "1920x1080@30 (a TV)", 1125.0, 1080.0, 33333.0 },
    { "1920x1080@24 (a TV)", 1125.0, 1080.0, 41667.0 }
};

#define BEAM_MODES ((int) (sizeof(beam_modes) / sizeof(beam_modes[0])))

#define LATCH_PREFETCH 0        /* two lines before the blank */
#define LATCH_START    1
#define LATCH_MID      2
#define LATCH_END      3
#define LATCHES        4

static const char *latch_names[LATCHES] = { "latch 2 lines early", "latch at the blank", "latch mid-blank",
                                            "latch at the blank's end" };

static double BeamLt(const struct BeamMode *m)
{
    return m->T / m->vt;
}

static ULONG BeamLine(const struct BeamMode *m, double t)
{
    long k;
    double r;
    ULONG line;

    k = (long) (t / m->T);
    r = t - (double) k * m->T;
    if (r < 0.0)
        r = 0.0;
    line = (ULONG) (r / BeamLt(m));
    if ((double) line >= m->vt)
        line = (ULONG) m->vt - 1;
    return line;
}

static double BeamBlank(const struct BeamMode *m, long k)
{
    return (double) k * m->T + m->disp * BeamLt(m);
}

static double BeamLatch(const struct BeamMode *m, int latch, long k)
{
    double b;

    b = BeamBlank(m, k);
    if (latch == LATCH_PREFETCH)
        return b - 2.0 * BeamLt(m);
    if (latch == LATCH_START)
        return b;
    if (latch == LATCH_MID)
        return b + 0.5 * (m->vt - m->disp) * BeamLt(m);
    return (double) (k + 1) * m->T - 0.01;
}

/* the start of the blank in which the CRTC takes a write at W */
static double BeamTaken(const struct BeamMode *m, int latch, double W)
{
    long k;

    k = (long) (W / m->T) - 1;
    while (BeamLatch(m, latch, k) <= W)
        k++;
    return BeamBlank(m, k);
}

/* the rule's two halves, and the wrong ones the test must catch */
typedef BOOL (*BeamRefFn)(ULONG line, ULONG disp);
typedef BOOL (*BeamWaitFn)(ULONG ref_line, ULONG us_since_pan, ULONG line_now, ULONG disp, ULONG vtot);

static BOOL RefAny(ULONG line, ULONG disp)
{
    (void) line;
    (void) disp;
    return TRUE;                /* a line in the blank taken as a reference */
}

static BOOL RefNoGuard(ULONG line, ULONG disp)
{
    return (BOOL) (line < disp);
}

static BOOL WaitNever(ULONG ref_line, ULONG us_since_pan, ULONG line_now, ULONG disp, ULONG vtot)
{
    (void) ref_line;
    (void) us_since_pan;
    (void) line_now;
    (void) disp;
    (void) vtot;
    return FALSE;
}

/* the time rule of the first build (21 ms): a 30 Hz mode tears with it */
static BOOL Wait21(ULONG ref_line, ULONG us_since_pan, ULONG line_now, ULONG disp, ULONG vtot)
{
    if (us_since_pan >= 21000UL)
        return FALSE;
    return FlipBeam_MustWait(ref_line, 0, line_now, disp, vtot);
}

struct BeamTiming
{
    const char *name;
    double game_lo, game_hi;    /* the helper's E clock to BeamRef */
    double kick_lo, kick_hi;    /* BeamRef to the kick */
    double cp_lo, cp_hi;        /* the kick to the CP at the wait */
    double read_lo, read_hi;    /* the write to the helper's line read */
};

static const struct BeamTiming beam_timings[] =
{
    /* GLQuake: the helper pans while the game waits, the kick 1.5-4.5 ms
     * later (locks, the CP's start, the stream's copy); the wait goes to
     * the CP at once */
    { "steady", 10.0, 1000.0, 1500.0, 4500.0, 2.0, 60.0, 5.0, 300.0 },
    /* anything: mid-frame flushes, lock waits, a busy system */
    { "spread", 0.0, 25000.0, 300.0, 40000.0, 2.0, 60.0, 5.0, 300.0 },
    /* the CP late at the wait (as it would be without the commit) */
    { "late CP", 10.0, 1000.0, 300.0, 4500.0, 300.0, 3000.0, 5.0, 300.0 },
    /* the helper's line read right at the write */
    { "quick read", 0.0, 200.0, 300.0, 4500.0, 2.0, 60.0, 1.0, 30.0 }
};

#define BEAM_TIMINGS ((int) (sizeof(beam_timings) / sizeof(beam_timings[0])))

struct BeamStats
{
    long trials;
    long waits;
    long passed;
    long aged;
    long late;
    long polls;
    long timed;
    long tears;
    long extra;                 /* the engine waited a whole refresh more than needed */
    long extra_ref;             /* ... the write in the picture, the helper's line the reference */
    double stall;               /* us the engine stood at the wait */
};

static void BeamTrial(const struct BeamMode *m, int latch, const struct BeamTiming *tm, BeamRefFn ref_ok,
                      BeamWaitFn must_wait, struct BeamStats *st)
{
    double W;
    double R;
    double h_ticks;
    double t;
    double K;
    double te;
    double taken;
    ULONG disp;
    ULONG h_line;
    ULONG ref;
    ULONG line;
    BOOL h_ok;
    BOOL have;
    BOOL wait;
    long k;
    int i;

    disp = (ULONG) m->disp;
    W = 10.0 * m->T + Rand(0.0, m->T);
    R = W + Rand(tm->read_lo, tm->read_hi);
    h_line = BeamLine(m, R);
    h_ok = ref_ok(h_line, disp);
    h_ticks = R + Rand(1.0, 20.0);
    t = h_ticks + Rand(tm->game_lo, tm->game_hi);
    st->trials++;
    wait = FALSE;
    if ((ULONG) (t - h_ticks) >= FLIP_BEAM_AGED_US)
    {
        st->aged++;
        K = t + Rand(tm->kick_lo, tm->kick_hi);
    }
    else
    {
        ref = h_line;
        have = h_ok;
        if (!have)
        {
            st->late++;
            for (i = 0; i < 8 && !have; i++)
            {
                line = BeamLine(m, t);
                if (ref_ok(line, disp))
                {
                    have = TRUE;
                    ref = line;
                }
                else
                {
                    st->polls++;
                    t += Rand(1000.0, 1300.0);
                }
            }
        }
        if (!have)
        {
            st->timed++;
            if (t - h_ticks < (double) FLIP_BEAM_AGED_US)
                t = h_ticks + (double) FLIP_BEAM_AGED_US + Rand(0.0, 200.0);
            K = t + Rand(tm->kick_lo, tm->kick_hi);
        }
        else
        {
            K = t + Rand(tm->kick_lo, tm->kick_hi);
            line = BeamLine(m, K);
            wait = must_wait(ref, (ULONG) (K - h_ticks), line, disp, (ULONG) m->vt);
            if (wait)
                st->waits++;
            else
                st->passed++;
        }
    }
    t = K + Rand(tm->cp_lo, tm->cp_hi);
    te = t;
    if (wait && BeamLine(m, t) < disp)
    {
        k = (long) (t / m->T);
        te = BeamBlank(m, k);
        if (te < t)
            te = BeamBlank(m, k + 1);
    }
    taken = BeamTaken(m, latch, W);
    if (te < taken)
        st->tears++;
    if (wait)
    {
        st->stall += te - t;
        if (te - taken >= m->T)
        {
            /* a write in the blank, or a helper's line in the blank:
             * whether that blank took the write is not known (the latch
             * point is not), so the rule waits for the next one -- the
             * price, by design. A write in the picture with the helper's
             * line as the reference must never cost it. */
            st->extra++;
            if (h_ok && BeamLine(m, W) + FLIP_BEAM_GUARD < disp)
                st->extra_ref++;
        }
    }
    CHECK(te - K <= m->T + 3000.0, "the engine waited longer than a refresh past the CP");
}

static void BeamRun(const struct BeamMode *m, int latch, const struct BeamTiming *tm, BeamRefFn ref_ok,
                    BeamWaitFn must_wait, unsigned long seed, long trials, struct BeamStats *st)
{
    long i;

    rng = seed;
    memset(st, 0, sizeof(*st));
    for (i = 0; i < trials; i++)
        BeamTrial(m, latch, tm, ref_ok, must_wait, st);
}

static void TestBeam(void)
{
    static char name[160];
    struct BeamStats st;
    struct BeamStats sum;
    double worst;
    long tears;
    int mi;
    int li;
    int ti;

    /* the time rule: no later than FLIP_BEAM_AGED_US after any write the
     * blank that takes it has begun, in every mode and latch model */
    for (mi = 0; mi < BEAM_MODES; mi++)
    {
        regime = beam_modes[mi].name;
        worst = beam_modes[mi].T + (double) FLIP_BEAM_GUARD * BeamLt(&beam_modes[mi]);
        CHECK(worst <= (double) FLIP_BEAM_AGED_US, "the time rule is shorter than a refresh and the guard");
    }
    /* the reference: a line in the picture, kept off its end */
    regime = "FlipBeam_RefOk";
    CHECK(FlipBeam_RefOk(0, 768), "the top line is no reference");
    CHECK(FlipBeam_RefOk(763, 768), "line disp-5 is no reference");
    CHECK(!FlipBeam_RefOk(764, 768), "line disp-4 taken as a reference");
    CHECK(!FlipBeam_RefOk(768, 768), "the blank's first line taken as a reference");
    CHECK(!FlipBeam_RefOk(805, 768), "a blank line taken as a reference");
    CHECK(!FlipBeam_RefOk(2, 4), "a mode of four lines has a reference");
    regime = "FlipBeam_MustWait";
    CHECK(FlipBeam_MustWait(100, 2000, 100, 768, 806), "the reference's own line needs no wait");
    CHECK(FlipBeam_MustWait(100, 2000, 767, 768, 806), "the picture's last line needs no wait");
    CHECK(!FlipBeam_MustWait(100, 2000, 768, 768, 806), "a wait in the blank");
    CHECK(!FlipBeam_MustWait(100, 2000, 805, 768, 806), "a wait in the blank's last line");
    CHECK(FlipBeam_MustWait(100, 2000, 806, 768, 806), "a line past the total taken as the blank");
    CHECK(FlipBeam_MustWait(100, 2000, 0x7ff, 768, 806), "a failed read ($FFFFFFFF) taken as the blank");
    CHECK(!FlipBeam_MustWait(100, 2000, 99, 768, 806), "a wait after the beam wrapped");
    CHECK(!FlipBeam_MustWait(100, FLIP_BEAM_AGED_US, 500, 768, 806), "a wait after the time rule");
    CHECK(!FlipBeam_MustWait(100, FLIP_BEAM_AGED_US, 0x7ff, 768, 806), "a wait after the time rule");
    CHECK(FlipBeam_MustWait(100, FLIP_BEAM_AGED_US - 1, 500, 768, 806), "no wait before the time rule");

    /* the real rule: never a write into a third on show */
    memset(&sum, 0, sizeof(sum));
    for (mi = 0; mi < BEAM_MODES; mi++)
        for (li = 0; li < LATCHES; li++)
            for (ti = 0; ti < BEAM_TIMINGS; ti++)
            {
                sprintf(name, "beam: %s, %s, %s", beam_modes[mi].name, latch_names[li], beam_timings[ti].name);
                regime = name;
                BeamRun(&beam_modes[mi], li, &beam_timings[ti], FlipBeam_RefOk, FlipBeam_MustWait,
                        (unsigned long) (1000 + mi * 31 + li * 7 + ti), 20000, &st);
                CHECK(st.tears == 0, "the Radeon wrote into a third still on show");
                CHECK(st.waits + st.passed + st.aged + st.timed == st.trials, "a trial without a verdict");
                if (ti == 0 && li == LATCH_START && beam_modes[mi].T <= 20100.0)
                {
                    sum.trials += st.trials;
                    sum.waits += st.waits;
                    sum.passed += st.passed;
                    sum.aged += st.aged;
                    sum.late += st.late;
                    sum.polls += st.polls;
                    sum.timed += st.timed;
                    sum.extra += st.extra;
                    sum.stall += st.stall;
                }
                if (ti == 0)
                    CHECK(st.extra_ref == 0, "steady: a whole extra refresh for a write in the picture");
            }
    printf("test_flip: beam, steady, 50-85 Hz, %ld trials: the Radeon waited %ld%% (mean %.1f ms), no wait %ld%%, "
           "aged %ld%%, late %ld%% (polls %ld), timed %ld, extra refreshes %ld\n",
           sum.trials, sum.waits * 100 / sum.trials, sum.waits ? sum.stall / sum.waits / 1000.0 : 0.0,
           sum.passed * 100 / sum.trials, sum.aged * 100 / sum.trials, sum.late * 100 / sum.trials, sum.polls,
           sum.timed, sum.extra);

    /* the test must see a wrong rule: no wait at all; a line in the blank
     * as the reference (latch at the blank's start); no guard against a
     * latch two lines early */
    for (mi = 0; mi < BEAM_MODES; mi++)
    {
        regime = beam_modes[mi].name;
        BeamRun(&beam_modes[mi], LATCH_START, &beam_timings[0], FlipBeam_RefOk, WaitNever, 7, 20000, &st);
        CHECK(st.tears > 0, "the model sees no tear without the wait");
        BeamRun(&beam_modes[mi], LATCH_START, &beam_timings[0], RefAny, FlipBeam_MustWait, 7, 20000, &st);
        CHECK(st.tears > 0, "the model sees no tear with a blank line as the reference");
        tears = 0;
        BeamRun(&beam_modes[mi], LATCH_PREFETCH, &beam_timings[3], RefNoGuard, FlipBeam_MustWait, 7, 400000,
                &st);
        tears += st.tears;
        CHECK(tears > 0, "the model sees no tear without the guard");
        if (beam_modes[mi].T > 21000.0)
        {
            BeamRun(&beam_modes[mi], LATCH_START, &beam_timings[1], FlipBeam_RefOk, Wait21, 7, 20000, &st);
            CHECK(st.tears > 0, "the model sees no tear with the 21 ms time rule below 48 Hz");
        }
    }
}

int main(void)
{
    double secs;
    long d, sh, w;
    long d0, sh0, w0;
    unsigned long seed;

    /* the 68k slower than the refresh (GLQuake's 33 fps): every frame shown */
    for (seed = 1; seed <= 20; seed++)
    {
        Run("68k 25-40 ms, Radeon 2-10 ms", seed, 2000, 25000, 40000, 2000, 10000, 0.0, 0, 0,
            &d, &sh, &w);
        CHECK(d == 0, "a frame dropped although the 68k is slower than the refresh");
        CHECK(sh >= 1999, "frames not shown");
        CHECK(w == 0, "a flush waited although the 68k is slower than the refresh");
    }
    /* faster than the refresh: capped by the safe messages */
    for (seed = 1; seed <= 20; seed++)
    {
        Run("68k 3-10 ms (above the refresh)", seed, 2000, 3000, 10000, 1000, 4000, 0.0, 0, 0,
            &d, &sh, &w);
        CHECK(w > 0, "no flush waited although frames come faster than the refresh");
    }
    /* a slow Radeon, flushes mid-frame */
    for (seed = 1; seed <= 20; seed++)
        Run("68k 15-30 ms, Radeon 10-45 ms, mid", seed, 2000, 15000, 30000, 10000, 45000, 0.0, 0, 1,
            &d, &sh, &w);
    for (seed = 1; seed <= 20; seed++)
        Run("68k 5-40 ms, Radeon 1-30 ms, mid", seed, 2000, 5000, 40000, 1000, 30000, 0.0, 0, 1,
            &d, &sh, &w);
    /* Intuition refuses switches now and then */
    for (seed = 1; seed <= 20; seed++)
    {
        Run("refused 10%, mid", seed, 2000, 5000, 40000, 1000, 20000, 0.10, 0, 1, &d, &sh, &w);
        CHECK(sh > 1000, "too few frames shown with refusals");
    }
    /* the helper's switch given up after it was counted (RadeonGLFlip=csb):
     * FlipSm_Unshow must leave the bookkeeping exactly as a switch refused
     * at once does -- the same frames shown, dropped and waited for */
    for (seed = 1; seed <= 20; seed++)
    {
        Run("refused 10%, mid", seed, 2000, 5000, 40000, 1000, 20000, 0.10, 0, 1, &d0, &sh0, &w0);
        Run("refused late 10%, mid", seed, 2000, 5000, 40000, 1000, 20000, 0.10, 1, 1, &d, &sh, &w);
        CHECK(d == d0 && sh == sh0 && w == w0,
              "a switch given up later leaves other bookkeeping than one refused at once");
    }
    for (seed = 1; seed <= 20; seed++)
        Run("refused late 30%, fast", seed, 2000, 3000, 10000, 1000, 4000, 0.30, 1, 1, &d, &sh, &w);
    /* pipe against the gate, both on Radeon.card's timing (the call returns
     * at the blank, the safe message a refresh later) */
    for (seed = 1; seed <= 20; seed++)
    {
        RunP("68k 25-40 ms, Radeon 2-10 ms", seed, 2000, 25000, 40000, 2000, 10000, 0.0, 0, 0, &d0, &sh0,
             &w0, &secs);
        RunP("68k 25-40 ms, Radeon 2-10 ms", seed, 2000, 25000, 40000, 2000, 10000, 0.0, 0, 1, &d, &sh, &w,
             &secs);
        CHECK(d == 0, "pipe: a frame dropped although the 68k is slower than the refresh");
        CHECK(sh >= 1999, "pipe: frames not shown");
        CHECK(sh >= sh0, "pipe: fewer frames shown than with the gate");
    }
    for (seed = 1; seed <= 20; seed++)
    {
        RunP("68k 15-25 ms, Radeon 2-10 ms", seed, 2000, 15000, 25000, 2000, 10000, 0.0, 0, 0, &d0, &sh0,
             &w0, &secs);
        RunP("68k 15-25 ms, Radeon 2-10 ms", seed, 2000, 15000, 25000, 2000, 10000, 0.0, 0, 1, &d, &sh, &w,
             &secs);
        CHECK(sh >= sh0, "pipe: fewer frames shown than with the gate");
    }
    for (seed = 1; seed <= 20; seed++)
    {
        RunP("68k 3-10 ms (above the refresh)", seed, 2000, 3000, 10000, 1000, 4000, 0.0, 0, 0, &d0, &sh0,
             &w0, &secs);
        RunP("68k 3-10 ms (above the refresh)", seed, 2000, 3000, 10000, 1000, 4000, 0.0, 0, 1, &d, &sh, &w,
             &secs);
        CHECK(sh >= sh0, "pipe: fewer frames shown than with the gate");
    }
    for (seed = 1; seed <= 20; seed++)
        RunP("68k 5-40 ms, Radeon 1-30 ms, mid", seed, 2000, 5000, 40000, 1000, 30000, 0.0, 1, 1, &d, &sh, &w,
             &secs);
    for (seed = 1; seed <= 20; seed++)
        RunP("refused 10%, mid", seed, 2000, 5000, 40000, 1000, 20000, 0.10, 1, 1, &d, &sh, &w, &secs);
    TestBeam();
    printf("test_flip: %s (%d checks)\n", failures ? "FAILED" : "OK", checks);
    if (failures)
        printf("test_flip: %d of %d checks FAILED\n", failures, checks);
    return failures ? 1 : 0;
}
