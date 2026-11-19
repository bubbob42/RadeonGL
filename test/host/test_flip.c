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
 *
 * Step R (2026-11-14, docs/step-r-design.md, TESTS): TestBeamR (R's row in
 * the beam model, and R2's refresh rule), TestFlipLine (the line time's
 * bound), TestStartPanBfs (R1's bookkeeping, exhaustively, with the real
 * FlipSm_StartPan/Panned/ChoosePan) and TestRunR (R1's flush order in time
 * against the beam -- PanPace, EngineIdle, the pan, BeamSampleB's in-front
 * test, B -- with its negatives and R2 for later).
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

/* --- step R (2026-11-14, docs/step-r-design.md): the pan at the flush's start ---------------- */

/*
 * R1 as lib/cardflip.c builds it for panning without a helper. CardFlip_Safe
 * (the flush's first call, outside every Picasso96 lock) waits until none of
 * our work is on the engine -- k->pending clear, our fence passed, or 1 ms
 * sleeps for it, at most 100 -- then settles the kicked frame
 * (FlipSm_StartPan) and pans onto it in the GL task. FlipSm_Panned frees the
 * third the pan left at once; cure B guards it by the beam (BeamNote's
 * record and beam_left, BeamRef, CardFlip_Beam). A present takes the next
 * frame's third with FlipSm_ChoosePan; CardFlip_After only runs FlipSm_Ran.
 * The helper path (RadeonGLHelperPri set) is 0.15's: Run and TestBeam above.
 * Before the pan, PanPace sleeps until the last pan's offset is known taken
 * (one pan a refresh); at the kick, BeamSampleB takes its line-time sample
 * and late reference only with the GL screen in front.
 */

/* FlipLine_Sample's answer: the pair taken (1), or taken after a start
 * over (2); 0 refused, 3 refused after a start over */
static int LineTook(ULONG r)
{
    return r == 1 || r == 2;
}

/* the E clock at t us (its 32 bits wrap after 6054 s) */
static ULONG TicksAt(double t)
{
    unsigned long long k;

    k = (unsigned long long) (t * 0.709379);
    return (ULONG) (k & 0xffffffffULL);
}

/* lib/cardflip.c's FlipUs: E clock ticks to us, in 32 bits */
static ULONG EUs(ULONG t)
{
    ULONG s;
    ULONG r;
    ULONG ms;
    ULONG rest;

    s = t / 709379UL;
    r = t - s * 709379UL;
    ms = (r * 1000UL) / 709379UL;
    rest = (r * 1000UL) - ms * 709379UL;
    return s * 1000000UL + ms * 1000UL + (rest * 1000UL) / 709379UL;
}

static long Pct(long a, long b)
{
    return (b > 0) ? (a * 100 + b / 2) / b : 0;
}

/* 1. The bookkeeping, exhaustively: every state the real FlipSm_* functions
 * reach from FlipSm_Init under every sequence of the events a flush can
 * meet, breadth first. Beside the bookkeeping the state carries the truth
 * the checks need: which thirds may still be on show (owes), which hold a
 * whole frame, which one the engine still writes, how new each third's
 * picture is, and the third ScrollVPort wrote last. */

#define BFS_SLOTS     (1UL << 20)

#define BV_I1         0     /* draw == show at a kick, or a present chose the shown third */
#define BV_OWES       1     /* a kick into a third perhaps on show, without B's verdict */
#define BV_TARGET     2     /* a pan onto a third not finished, drawn into, written, or older */
#define BV_BUSY       3     /* a pan with our work on the engine */
#define BV_LEAVE      4     /* leave set outside FlipSm_Panned */
#define BV_IDLE       5     /* a Safe in front with the engine idle left a frame unshown */
#define BV_DROP       6     /* a frame dropped without a gave-up or hidden Safe */
#define BV_SHOW       7     /* sm.show is not the third ScrollVPort wrote last */
#define BV_N          8

#define BX_NONE       0     /* R1 as built */
#define BX_CHOOSE     1     /* FlipSm_Choose after a gave-up or hidden Safe */
#define BX_BUSY       2     /* StartPan settles while the engine is busy */
#define BX_NOMARK     3     /* Panned without the beam_left mark */
#define BX_BET        4     /* design 3's bet: Choose always, the fence wait never gives up */
#define BX_KICK       5     /* design 1's EngineIdle: the fence read only while sm.kick is set */

#define BE_KICK       0     /* the flush kicks its commands */
#define BE_HIDDEN     1     /* a present: the screen behind at card.c's step 1c */
#define BE_MOVED      2     /* a bitmap moved, drop_next: after CardFlip_Settled */
#define BE_OFFBOARD   3     /* LockBm failed: SettleAlone, no CardFlip_Settled */
#define BE_LOST       4     /* the context lost after CardFlip_Settled: the end */
#define BE_EXITS      5
#define BFS_EVENTS    (128 * BE_EXITS)

struct BfsState
{
    struct FlipSm sm;
    ULONG beam_left;            /* cardflip.c's: bit n = a pan left third n, no verdict since */
    ULONG owes;                 /* the truth: bit n = third n may still be on show */
    ULONG whole;                /* bit n = third n holds a whole frame (its present was kicked) */
    ULONG queued;               /* the third the engine still writes, NONE */
    ULONG pending;              /* k->pending */
    ULONG rank[FLIP_N];         /* 0 = the third written last */
    ULONG reg;                  /* the third of the last ScrollVPort */
};

static unsigned long bfs_set[BFS_SLOTS];        /* key + 1; 0 = empty */
static unsigned long bfs_queue[BFS_SLOTS];

static unsigned long BfsT(ULONG x)
{
    return (x < FLIP_N) ? (unsigned long) x : 3UL;
}

static ULONG BfsU(unsigned long x)
{
    return (x < FLIP_N) ? (ULONG) x : NONE;
}

static ULONG BfsBit(ULONG i)
{
    return (i < FLIP_N) ? (ULONG) (1UL << i) : 0UL;
}

static unsigned long BfsKey(const struct BfsState *s)
{
    unsigned long k;

    k = BfsT(s->sm.show);
    k = (k << 2) | BfsT(s->sm.draw);
    k = (k << 2) | BfsT(s->sm.kick);
    k = (k << 2) | BfsT(s->sm.ready);
    k = (k << 2) | BfsT(s->sm.leave);
    k = (k << 3) | (unsigned long) (s->beam_left & 7UL);
    k = (k << 3) | (unsigned long) (s->owes & 7UL);
    k = (k << 3) | (unsigned long) (s->whole & 7UL);
    k = (k << 2) | BfsT(s->queued);
    k = (k << 1) | (unsigned long) (s->pending & 1UL);
    k = (k << 2) | (unsigned long) s->rank[0];
    k = (k << 2) | (unsigned long) s->rank[1];
    k = (k << 2) | (unsigned long) s->rank[2];
    k = (k << 2) | BfsT(s->reg);
    return k;
}

static void BfsFrom(struct BfsState *s, unsigned long k)
{
    FlipSm_Init(&s->sm);
    s->sm.dropped = 0;
    s->reg = BfsU(k & 3UL);
    k >>= 2;
    s->rank[2] = (ULONG) (k & 3UL);
    k >>= 2;
    s->rank[1] = (ULONG) (k & 3UL);
    k >>= 2;
    s->rank[0] = (ULONG) (k & 3UL);
    k >>= 2;
    s->pending = (ULONG) (k & 1UL);
    k >>= 1;
    s->queued = BfsU(k & 3UL);
    k >>= 2;
    s->whole = (ULONG) (k & 7UL);
    k >>= 3;
    s->owes = (ULONG) (k & 7UL);
    k >>= 3;
    s->beam_left = (ULONG) (k & 7UL);
    k >>= 3;
    s->sm.leave = BfsU(k & 3UL);
    k >>= 2;
    s->sm.ready = BfsU(k & 3UL);
    k >>= 2;
    s->sm.kick = BfsU(k & 3UL);
    k >>= 2;
    s->sm.draw = BfsU(k & 3UL);
    k >>= 2;
    s->sm.show = BfsU(k & 3UL);
}

static int BfsAdd(unsigned long key)
{
    unsigned long h;

    h = (key * 2654435761UL) & (BFS_SLOTS - 1);
    while (bfs_set[h] != 0)
    {
        if (bfs_set[h] == key + 1)
            return 0;
        h = (h + 1) & (BFS_SLOTS - 1);
    }
    bfs_set[h] = key + 1;
    return 1;
}

/* One flush, and what happened since the last one, from state in; ev: the
 * events (bit 0 the engine finished between the flushes, 1 a heap lock
 * settled our work, 2 Safe's fence wait gave up, 3 the GL screen behind at
 * Safe, 4 a present, 5 the flush waited for its fence, 6 BeamRef's verdict
 * by age or the 68k's sleep; above: the exit, BE_*). FALSE: no next state
 * (the events do not apply, or the context is lost). */
static BOOL BfsFlush(const struct BfsState *in, int ev, int variant, struct BfsState *out, long *viol)
{
    struct BfsState s;
    int finished;
    int heap;
    int gaveup;
    int behind;
    int present;
    int waited;
    int aged;
    int exitk;
    int safe_gaveup;
    int safe_hidden;
    int beam_go;
    BOOL idle;
    ULONG to;
    ULONG old;
    ULONG left;
    ULONG next;
    ULONG d;
    ULONG i;

    finished = ev & 1;
    heap = (ev >> 1) & 1;
    gaveup = (ev >> 2) & 1;
    behind = (ev >> 3) & 1;
    present = (ev >> 4) & 1;
    waited = (ev >> 5) & 1;
    aged = (ev >> 6) & 1;
    exitk = ev >> 7;
    if (exitk == BE_HIDDEN && !present)
        return FALSE;
    if (variant == BX_BET && gaveup)
        return FALSE;
    s = *in;
    s.sm.dropped = 0;
    next = NONE;
    beam_go = 0;
    safe_gaveup = 0;
    safe_hidden = 0;
    if (finished)
        s.queued = NONE;
    if (heap)
    {
        s.queued = NONE;
        s.pending = 0;
    }
    /* CardFlip_Safe */
    if (s.sm.kick != NONE || s.sm.ready != NONE)
    {
        /* EngineIdle: k->pending clear, the fence passed, or the sleeps
         * (BX_KICK: idle whenever no kicked frame is unsettled) */
        idle = TRUE;
        if (s.pending && s.queued != NONE && (variant != BX_KICK || s.sm.kick != NONE))
        {
            if (gaveup)
            {
                idle = FALSE;
                safe_gaveup = 1;
            }
            else
                s.queued = NONE;
        }
        if (idle || variant == BX_BUSY)
        {
            to = FlipSm_StartPan(&s.sm, TRUE);
            if (to != NONE && behind)
                safe_hidden = 1;
            else if (to != NONE)
            {
                /* PanNow */
                if (to >= FLIP_N || (s.whole & BfsBit(to)) == 0 || s.queued == to || to == s.sm.draw
                    || s.reg >= FLIP_N || s.rank[to] >= s.rank[s.reg])
                    viol[BV_TARGET]++;
                if (s.queued != NONE)
                    viol[BV_BUSY]++;
                old = s.reg;
                s.reg = to;
                left = FlipSm_Panned(&s.sm);
                if (left != old)
                    viol[BV_SHOW]++;
                s.owes |= BfsBit(left);
                if (variant != BX_NOMARK)
                    s.beam_left |= BfsBit(left);
            }
            if (idle && !behind && (s.sm.kick != NONE || s.sm.ready != NONE))
                viol[BV_IDLE]++;
        }
    }
    if (s.sm.leave != NONE)
        viol[BV_LEAVE]++;
    /* BeamRef for the third this flush draws into */
    d = s.sm.draw;
    if ((s.beam_left & BfsBit(d)) != 0)
    {
        if (aged)
        {
            s.beam_left &= ~BfsBit(d);
            s.owes &= ~BfsBit(d);
        }
        else
            beam_go = 1;
    }
    if (exitk == BE_OFFBOARD)
    {
        s.queued = NONE;            /* SettleAlone */
        s.pending = 0;
    }
    else
    {
        /* the lock (Radeon.card idles the engine), Settle, CardFlip_Settled */
        s.queued = NONE;
        s.pending = 0;
        FlipSm_Settled(&s.sm);
        if (exitk == BE_KICK)
        {
            if (present)
            {
                if (variant == BX_BET || (variant == BX_CHOOSE && (safe_gaveup || safe_hidden)))
                    next = FlipSm_Choose(&s.sm);
                else
                    next = FlipSm_ChoosePan(&s.sm);
                if (next >= FLIP_N || next == s.sm.show || next == s.sm.draw)
                    viol[BV_I1]++;
            }
            /* CardFlip_Beam, then the kick into draw */
            if (d >= FLIP_N || d == s.sm.show || d == s.reg)
                viol[BV_I1]++;
            if ((s.owes & BfsBit(d)) != 0 && !beam_go)
                viol[BV_OWES]++;
            if (beam_go)
            {
                s.beam_left &= ~BfsBit(d);
                s.owes &= ~BfsBit(d);
            }
            if (present)
                s.whole |= BfsBit(d);
            else
                s.whole &= ~BfsBit(d);
            if (d < FLIP_N)
            {
                for (i = 0; i < FLIP_N; i++)
                    if (s.rank[i] < s.rank[d])
                        s.rank[i]++;
                s.rank[d] = 0;
            }
            s.queued = d;
            s.pending = 1;
            if (waited)
            {
                s.queued = NONE;
                s.pending = 0;
            }
            /* CardFlip_After */
            if (present)
            {
                FlipSm_Ran(&s.sm, (BOOL) (s.pending != 0), next);
                if (s.sm.draw == s.sm.show)
                    viol[BV_I1]++;
            }
        }
    }
    if (s.sm.leave != NONE)
        viol[BV_LEAVE]++;
    if (s.sm.show != s.reg)
        viol[BV_SHOW]++;
    if (s.sm.dropped != 0 && !safe_gaveup && !safe_hidden)
        viol[BV_DROP]++;
    s.sm.dropped = 0;
    *out = s;
    return (BOOL) (exitk != BE_LOST);
}

/* the search from FlipSm_Init; returns the states reached */
static long BfsRun(int variant, long *viol)
{
    struct BfsState s;
    struct BfsState n;
    unsigned long head;
    unsigned long tail;
    unsigned long key;
    int ev;
    int i;

    memset(bfs_set, 0, sizeof(bfs_set));
    for (i = 0; i < BV_N; i++)
        viol[i] = 0;
    FlipSm_Init(&s.sm);
    s.sm.dropped = 0;
    s.beam_left = 0;
    s.owes = 0;
    s.whole = 0;
    s.queued = NONE;
    s.pending = 0;
    s.rank[0] = 2;
    s.rank[1] = 1;
    s.rank[2] = 0;
    s.reg = 0;
    head = 0;
    tail = 0;
    key = BfsKey(&s);
    BfsFrom(&n, key);
    CHECK(BfsKey(&n) == key, "the search's state key does not round-trip");
    (void) BfsAdd(key);
    bfs_queue[tail++] = key;
    while (head < tail)
    {
        BfsFrom(&s, bfs_queue[head++]);
        for (ev = 0; ev < BFS_EVENTS; ev++)
            if (BfsFlush(&s, ev, variant, &n, viol))
            {
                key = BfsKey(&n);
                if (BfsAdd(key) && tail < BFS_SLOTS)
                    bfs_queue[tail++] = key;
            }
    }
    return (long) tail;
}

static void TestStartPanBfs(void)
{
    static const char *const what[BV_N] =
    {
        "R1: draw == show at a kick, or a present chose the shown third (I1)",
        "R1: a kick into a third perhaps on show without B's verdict (I5/I6)",
        "R1: a pan onto a third not finished, drawn into, still written or no newer (I2)",
        "R1: a pan with our work on the engine (I3)",
        "R1: leave set outside FlipSm_Panned (I4)",
        "R1: a Safe in front with the engine idle left a frame unshown",
        "R1: a frame dropped without a gave-up or hidden Safe",
        "R1: sm.show is not the third ScrollVPort wrote last (I8)"
    };
    long viol[BV_N];
    long neg[5];
    long states;
    int i;

    regime = "R1 bookkeeping, breadth first";
    states = BfsRun(BX_NONE, viol);
    CHECK(states < (long) (BFS_SLOTS / 2), "the search outgrew its table");
    for (i = 0; i < BV_N; i++)
        CHECK(viol[i] == 0, what[i]);
    /* each guard's absence must be found */
    regime = "R1 bookkeeping, breadth first: FlipSm_Choose after a gave-up or hidden Safe";
    (void) BfsRun(BX_CHOOSE, viol);
    neg[0] = viol[BV_I1];
    CHECK(neg[0] > 0, "the search sees no draw == show");
    regime = "R1 bookkeeping, breadth first: StartPan on a busy engine";
    (void) BfsRun(BX_BUSY, viol);
    neg[1] = viol[BV_TARGET];
    CHECK(neg[1] > 0, "the search sees no pan onto an unfinished third");
    regime = "R1 bookkeeping, breadth first: Panned without the beam_left mark";
    (void) BfsRun(BX_NOMARK, viol);
    neg[2] = viol[BV_OWES];
    CHECK(neg[2] > 0, "the search sees no kick that owes a verdict");
    regime = "R1 bookkeeping, breadth first: design 3's bet (Choose, no give-up) with the hidden skip";
    (void) BfsRun(BX_BET, viol);
    neg[3] = viol[BV_I1];
    CHECK(neg[3] > 0, "the search sees no draw == show");
    /* design 1's idle test: a ready frame (after a gave-up or hidden Safe)
     * with a later non-present flush still on the engine */
    regime = "R1 bookkeeping, breadth first: EngineIdle on sm.kick only (design 1)";
    (void) BfsRun(BX_KICK, viol);
    neg[4] = viol[BV_BUSY];
    CHECK(neg[4] > 0, "the search sees no pan with our work on the engine");
    printf("test_flip: R1 bookkeeping, breadth first: %ld states x %d events, I1 I2 I3 I4 I5 I8 held; it finds "
           "Choose after a gave-up/hidden Safe (%ld), StartPan on a busy engine (%ld), Panned without the mark (%ld), "
           "design 3's bet behind another screen (%ld), EngineIdle on sm.kick only (%ld pans on a busy engine)\n",
           states, BFS_EVENTS, neg[0], neg[1], neg[2], neg[3], neg[4]);
}

/* 2. The order in time: R1's flush, step by step as lib/cardflip.c and
 * card.c run it, against TestBeam's beam (its modes, the user's reduced
 * blanking 1080p50 and the four latch models): PanPace's 1 ms sleeps until
 * the last pan's offset is known taken; ScrollVPort waits for the engine
 * and writes the offset somewhere in its 0.3-0.5 ms; BeamNote reads the
 * beam 5-60 us after its return; the E clock quantised; the engine one job
 * at a time (every lock idles it), behind B's packet from the next blank;
 * heap locks wait for it; 1% of line reads fail ($7FF); now and then a
 * pre-emption of up to 2 ms between a clock read and its line read; spells
 * behind another screen (in its own mode, or in the GL screen's at another
 * epoch: the same mode key) that begin and end at a frame's start or
 * anywhere between a flush's Safe and its kick. The truth: a third a pan
 * left is on show until BeamTaken of that pan's write (or a hide of the
 * screen); the register's third always. */

static const struct BeamMode beam_mode_rb = { "1920x1080@50 RB", 1111.0, 1080.0, 20000.0 };

#define R_MODES   (BEAM_MODES + 1)
#define RM_STEADY 0x13ffUL          /* 50 to 85 Hz: beam_modes 0-9 and the reduced-blanking 1080p50 */
#define RM_60UP   0x023fUL          /* 60 to 85 Hz */
#define RM_50     0x11c0UL          /* 50 Hz */
#define RM_TV     0x0c00UL          /* 24 and 30 Hz */
#define RM_RB     0x1000UL          /* the user's 1920 x 1080 at 50 Hz, reduced blanking */
#define RM_ALL    0x1fffUL
#define RM_VGA60  0x0001UL          /* 640x480@60, Zod's */

#define R_HUGE    1e300

#define RX_NOB       1              /* no cure B */
#define RX_NOMARK    2              /* PanNow without the beam_left mark */
#define RX_NOIDLE    4              /* the pan without EngineIdle */
#define RX_014       8              /* 0.14's order: no pan at Safe, the pan in CardFlip_After */
#define RX_R2        16             /* R2: B also passes once FlipLine's refresh has gone by */
#define RX_R2LOW     32             /* ... with r_low of the true refresh instead, in B and in
                                     * PanPace alike: a bound too low (must tear) */
#define RX_STRICTA   64             /* sample B and the late reference only after a pan read in the
                                     * picture (a_valid = line < disp, as the task first put it) */
#define RX_NOPACE    128            /* no PanPace: the pan as soon as the engine is idle */
#define RX_KICKIDLE  256            /* design 1's EngineIdle: idle whenever sm.kick == NONE, k->pending
                                     * not read */
#define RX_UPANY     512            /* the kick's late reference from any line the counter gives
                                     * (line < vtot, blank lines too) instead of FlipBeam_RefOk */
#define RX_NOFRONT   1024           /* BeamSampleB without its in-front test */
#define RX_PACEBLANK 2048           /* PanPace's first build: passes by the beam already in the
                                     * blank (a blank may take two pans) -- the library now waits
                                     * until the beam is back above the reference */
#define RX_NOHOLDPAIR 4096          /* PanPace without its own line-time pairs (0.16): FlipLine
                                     * learns only from BeamSampleB's pan-to-kick pair */

static double r_low = 0.95;         /* RX_R2LOW's refresh, of the true one */

static const struct BeamMode *RMode(int i)
{
    return (i < BEAM_MODES) ? &beam_modes[i] : &beam_mode_rb;
}

/* CRTC_H_TOTAL_DISP's part of the mode key: one value a line time */
static ULONG RHtot(const struct BeamMode *m)
{
    return (ULONG) (BeamLt(m) * 100.0);
}

static int RPick(unsigned long modes)
{
    int n;
    int k;
    int i;

    n = 0;
    for (i = 0; i < R_MODES; i++)
        if (modes & (1UL << i))
            n++;
    k = (int) Rand(0.0, (double) n - 0.01);
    for (i = 0; i < R_MODES; i++)
        if (modes & (1UL << i))
        {
            if (k == 0)
                return i;
            k--;
        }
    return 0;
}

struct RCfg
{
    const char *name;
    double cpu_lo, cpu_hi;      /* the 68k's frame, us */
    double gpu_lo, gpu_hi;      /* the Radeon's */
    int mid;                    /* 0-3 flushes mid-frame */
    int heap;                   /* 0-3 heap locks a frame (they wait for the engine) */
    int overlap;                /* 0: every flush waits for its fence */
    double spell;               /* the chance a frame starts a spell behind another screen */
    double resize;              /* ... a resize: another mode, CardFlip_Open */
    unsigned long modes;        /* RM_* */
    double spike;               /* the chance a job takes 110-200 ms (Safe's fence wait gives up) */
    double copy_lo, copy_hi;    /* the flush's CP start and copies, us (with the locks before them:
                                 * the pan to the kick) */
};

static const struct RCfg r_regimes[] =
{
    { "steady GLQuake", 25000.0, 40000.0, 3000.0, 15000.0, 0, 1, 1, 0.0, 0.0, RM_STEADY, 0.0, 1000.0, 3000.0 },
    { "1080p heavy (50 Hz RB)", 28000.0, 36000.0, 10000.0, 18000.0, 0, 1, 1, 0.0, 0.0, RM_RB, 0.0, 1000.0, 3000.0 },
    { "near the refresh, 60-85 Hz", 14000.0, 24000.0, 3000.0, 12000.0, 1, 1, 1, 0.0, 0.0, RM_60UP, 0.0, 1000.0,
      3000.0 },
    { "near the refresh, 50 Hz", 17000.0, 26000.0, 3000.0, 12000.0, 1, 1, 1, 0.0, 0.0, RM_50, 0.0, 1000.0, 3000.0 },
    { "fast (68k 3-12 ms)", 3000.0, 12000.0, 1000.0, 4000.0, 0, 1, 1, 0.0, 0.0, RM_STEADY, 0.0, 1000.0, 3000.0 },
    { "slow GPU 20-45 ms, mid-frame flushes", 15000.0, 30000.0, 20000.0, 45000.0, 1, 1, 1, 0.0, 0.0, RM_STEADY,
      0.0, 1000.0, 3000.0 },
    { "overlap off", 15000.0, 40000.0, 2000.0, 15000.0, 1, 1, 0, 0.0, 0.0, RM_STEADY, 0.0, 1000.0, 3000.0 },
    { "behind another screen now and then", 25000.0, 40000.0, 3000.0, 15000.0, 1, 1, 1, 0.01, 0.0, RM_STEADY, 0.0,
      1000.0, 3000.0 },
    { "resizes", 25000.0, 40000.0, 3000.0, 15000.0, 1, 1, 1, 0.0, 0.005, RM_ALL, 0.0, 1000.0, 3000.0 },
    { "24 and 30 Hz", 25000.0, 40000.0, 3000.0, 15000.0, 1, 1, 1, 0.0, 0.0, RM_TV, 0.0, 1000.0, 3000.0 },
    { "GPU spikes (1% of jobs 110-200 ms), mid-frame flushes", 15000.0, 30000.0, 10000.0, 35000.0, 1, 1, 1, 0.0,
      0.0, RM_STEADY, 0.01, 1000.0, 3000.0 },
    /* 2026-11-19: Zod at 640x480 -- frames longer than the refresh, flushes
     * of ~1.7 ms (pan to kick mostly under BeamSampleB's 48 lines): FlipLine
     * gets its bound from PanPace's own pairs */
    { "short flushes (Zod: 640x480@60, 22-30 ms frames, ~1 ms copies)", 22000.0, 30000.0, 2000.0, 8000.0, 0, 1, 1,
      0.0, 0.0, RM_VGA60, 0.0, 50.0, 300.0 }
};

#define R_REGIMES ((int) (sizeof(r_regimes) / sizeof(r_regimes[0])))
#define R_STEADY  0
#define R_HEAVY   1
#define R_NEAR60  2
#define R_NEAR50  3
#define R_FAST    4
#define R_SLOW    5
#define R_NOOV    6
#define R_SPELL   7
#define R_RESIZE  8
#define R_TV      9
#define R_SPIKE   10
#define R_SHORT   11

struct RStats
{
    long runs, frames, flushes, pans, unseen, hidden_drops, spells, spells_same, resizes, dropped, spikes;
    long fin_lock, fin_fence, fence_waits, fence_sleeps, gaveup, pan_hidden;
    long pace_waits, pace_sleeps, pace_gaveup, pace_trusted;
    long pace_samples;          /* line-time pairs from PanPace's own reads (no bound yet) */
    long b_kicks, b_waits, b_passed, b_aged, b_mode, b_late, b_polls, b_timed, b_refresh, ref_kick, would;
    long tears, unfinished, busy, show_bad, i1_bad, leave_bad, order_bad, idle_bad;
    long age1, age2, age_more;
    long samples, resets, pred[3];
    long hide_mid, ret_mid, pan_behind, front_skips, key_skips, line_across, line_hidden, ref_across;
    long run_max;               /* the longest run of consecutive pans one blank took (only the last
                                 * one's frame scanned out) */
    long pace_by[6];            /* PanPace passed: 0 no pan before, 1 aged, 2 FlipLine's refresh, 3 the beam
                                 * back above the reference, 4 the beam in the blank, 5 gave up */
    long shared_by[6];          /* pans a blank took after the pan before, by how PanPace passed */
    long shared_latch[LATCHES]; /* ... by the latch model */
    double stall, call_us, seconds;
    double pans_rate;           /* pans a refresh: the highest of the runs' means */
};

struct RModel
{
    double t;                   /* now, us */
    const struct BeamMode *m;   /* the GL screen's mode */
    double epoch;               /* the top of one of its pictures */
    int latch;
    const struct BeamMode *other;       /* a mode not the GL screen's */
    const struct BeamMode *spell_mode;  /* the other screen's in this spell: other, or the GL screen's */
    double other_epoch;
    double hs;                  /* a spell behind another screen: [hs, he); R_HUGE none */
    double he;
    double epoch2;              /* the GL mode's epoch from he on */
    int hide_done;              /* the hide's truth applied (RSync) */
    int hs_at_safe;             /* the spell begins at a random point of the next flush */
    int he_at_safe;             /* ... ends so */
    double spell_until;         /* the spell lasts at least until then */
    double last_hs, last_he;    /* the last hide and return RSync applied */
    long views;                 /* two a spell gone: which screen a read saw (RView) */
    ULONG reg;                  /* the third the last ScrollVPort wrote */
    double U[FLIP_N];           /* third n is on show until then */
    double last_taken;          /* the blank that took the last pan */
    int taken_ok;
    long run;                   /* pans in a row last_taken took */
    ULONG job_third;            /* the engine's job: its third, NONE */
    double job_end;
    long job_frame;             /* the frame it finishes, -2 part of one */
    long content[FLIP_N];       /* the finished frame in each third, -1 none, -2 partly drawn */
    long last_shown;
    ULONG pending;              /* k->pending */
    double clock_base;          /* the E clock's offset */
    int var;                    /* RX_* */
    double preempt;
    long a_view;                /* the screen sample A's read saw */
    int pace_why;               /* how PanPace passed before this pan (RStats.pace_by) */
    /* lib/cardflip.c's */
    struct FlipSm sm;
    struct FlipLine line;
    ULONG next;
    ULONG beam_left, beam_go, beam_third, ref_line, pkt_crtc, pkt_disp, pan_ticks;
    ULONG h_ok[FLIP_N], h_line[FLIP_N], h_disp[FLIP_N], h_crtc[FLIP_N], h_vtot[FLIP_N], h_htot[FLIP_N];
    ULONG h_ticks[FLIP_N];
    ULONG h_shown;
    ULONG a_valid, a_line, a_ticks, a_crtc, a_disp, a_vtot, a_htot, last_left;
};

static ULONG RTicks(const struct RModel *m, double t)
{
    return TicksAt(t + m->clock_base);
}

/* another screen in front at t? */
static int RHiddenAt(const struct RModel *m, double t)
{
    return (t >= m->hs && t < m->he) ? 1 : 0;
}

/* which screen a read at t sees: one number a view, a new one at each hide
 * and each return */
static long RView(const struct RModel *m, double t)
{
    return m->views + ((t >= m->hs) ? 1 : 0) + ((t >= m->he) ? 1 : 0);
}

/* the CRTC the GL screen is on, at t: its mode then, and the top of one of
 * its pictures */
static const struct BeamMode *RShown(const struct RModel *m, double t, double *ep)
{
    if (t >= m->he)
    {
        *ep = m->epoch2;
        return m->m;
    }
    if (t >= m->hs)
    {
        *ep = m->other_epoch;
        return m->spell_mode;
    }
    *ep = m->epoch;
    return m->m;
}

/* the spell's moments passed by now: behind another screen (every third
 * but the register's off the display for good), in front again (the GL
 * mode from a new epoch, the register's third on show) */
static void RSync(struct RModel *m)
{
    ULONG i;

    if (!m->hide_done && m->t >= m->hs)
    {
        for (i = 0; i < FLIP_N; i++)
            if (i != m->reg && m->U[i] > m->hs)
                m->U[i] = m->hs;
        m->hide_done = 1;
        m->taken_ok = 0;
        m->last_hs = m->hs;
    }
    if (m->t >= m->he)
    {
        m->epoch = m->epoch2;
        m->views += 2;
        m->last_he = m->he;
        m->hs = R_HUGE;
        m->he = R_HUGE;
        m->hide_done = 0;
        m->taken_ok = 0;
    }
}

static void RSetStart(struct RModel *m, double t)
{
    m->hs = t;
    m->he = R_HUGE;
    m->hide_done = 0;
    m->other_epoch = t - Rand(0.0, m->spell_mode->T);
}

static void RSetEnd(struct RModel *m, double t)
{
    m->he = t;
    m->epoch2 = t - Rand(0.0, m->m->T);
}

/* a spell's start or end that falls in a flush: from its Safe on, often
 * within ScrollVPort, mostly before the kick */
static double RSpellOffset(void)
{
    if (Rand(0.0, 1.0) < 0.3)
        return Rand(0.0, 700.0);
    return Rand(0.0, 5000.0);
}

/* BeamRead plus H_TOTAL at t; 1% of the line reads fail */
static void RRead(const struct RModel *m, double t, ULONG *crtc, ULONG *disp, ULONG *line, ULONG *vtot,
                  ULONG *htot)
{
    const struct BeamMode *b;
    double ep;

    b = RShown(m, t, &ep);
    *crtc = 0;
    *disp = (ULONG) b->disp;
    *vtot = (ULONG) b->vt;
    *htot = RHtot(b);
    *line = BeamLine(b, t - ep);
    if (Rand(0.0, 1.0) < 0.01)
        *line = 0x7ffUL;
}

/* the engine's job done by now? */
static void RJob(struct RModel *m)
{
    if (m->job_third != NONE && m->job_end <= m->t)
    {
        if (m->job_frame >= 0)
            m->content[m->job_third] = m->job_frame;
        m->job_third = NONE;
    }
}

/* wait for the engine (a lock, a heap lock, Radeon.card's ScrollVPort) */
static void RIdle(struct RModel *m)
{
    if (m->job_third != NONE && m->job_end > m->t)
        m->t = m->job_end;
    RJob(m);
}

/* EngineIdle: none of our work on the engine? (RX_KICKIDLE: design 1's
 * test, the fence read only while a kicked frame is unsettled) */
static BOOL REngineIdle(struct RModel *m, struct RStats *st)
{
    int i;

    if (!m->pending || ((m->var & RX_KICKIDLE) && m->sm.kick == NONE))
    {
        st->fin_lock++;
        return TRUE;
    }
    RJob(m);
    if (m->job_third == NONE)
    {
        st->fin_fence++;
        return TRUE;
    }
    st->fence_waits++;
    for (i = 0; i < 100; i++)
    {
        m->t += Rand(1000.0, 1300.0);
        st->fence_sleeps++;
        RJob(m);
        if (m->job_third == NONE)
            return TRUE;
    }
    st->gaveup++;
    return FALSE;
}

/* PanPace: one pan a refresh at most -- the pan waits in 1 ms sleeps until
 * the last pan's offset is known taken: by its record's age, by FlipLine's
 * refresh in the record's mode, or by the beam back above the record's
 * reference, through the whole blank (RX_PACEBLANK: the first build's
 * test, already in the blank) */
static void RPace(struct RModel *m, struct RStats *st)
{
    ULONG lft;
    ULONG crtc;
    ULONG disp;
    ULONG line;
    ULONG vtot;
    ULONG htot;
    ULONG us;
    ULONG safe;
    ULONG t0;
    ULONG t1;
    ULONG r;
    ULONG p_line;
    ULONG p_ticks;
    ULONG p_crtc;
    ULONG p_disp;
    ULONG p_vtot;
    long p_view;
    int p_hid;
    BOOL wait;
    BOOL p_ok;
    int hid;
    long view;
    int i;

    m->pace_why = 0;
    lft = m->last_left;
    if (lft >= FLIP_N)
        return;                     /* no pan since the open */
    p_ok = FALSE;
    p_line = 0;
    p_ticks = 0;
    p_crtc = 0;
    p_disp = 0;
    p_vtot = 0;
    p_view = 0;
    p_hid = 0;
    for (i = 0; ; i++)
    {
        RSync(m);
        us = EUs(RTicks(m, m->t) - m->h_ticks[lft]);
        if (us >= FLIP_BEAM_AGED_US)
        {
            m->pace_why = 1;
            break;
        }
        if (m->var & RX_R2LOW)
            safe = (ULONG) (r_low * m->m->T);
        else if (m->line.crtc == m->h_crtc[lft] && m->line.disp == m->h_disp[lft]
                 && m->line.vtot == m->h_vtot[lft] && m->line.htot == m->h_htot[lft])
            safe = FlipLine_SafeUs(&m->line);
        else
            safe = 0;
        if (FlipBeam_Refresh(us, safe))
        {
            m->pace_why = 2;
            break;
        }
        m->t += Rand(2.0, 20.0);    /* the clock, FlipLine_SafeUs */
        if (m->h_ok[lft] || (safe == 0 && !(m->var & RX_NOHOLDPAIR)))
        {
            /* the E clock, the read (a pre-emption may fall between), the
             * E clock again: lib/cardflip.c's t0 / BeamRead / t1 */
            t0 = RTicks(m, m->t);
            m->t += Rand(1.0, 10.0);
            if (Rand(0.0, 1.0) < m->preempt)
                m->t += Rand(0.0, 2000.0);
            hid = RHiddenAt(m, m->t);
            view = RView(m, m->t);
            RRead(m, m->t, &crtc, &disp, &line, &vtot, &htot);
            m->t += Rand(3.0, 15.0);
            if (Rand(0.0, 1.0) < m->preempt)
                m->t += Rand(0.0, 2000.0);
            t1 = RTicks(m, m->t);
            if (safe == 0 && !(m->var & RX_NOHOLDPAIR))
            {
                /* the library's in-front test at the read (FirstScreen) */
                if (hid || line >= disp)
                    p_ok = FALSE;
                else if (p_ok && crtc == p_crtc && disp == p_disp && vtot == p_vtot
                         && line >= p_line + FLIP_LINE_SPAN)
                {
                    r = FlipLine_Sample(&m->line, crtc, disp, vtot, htot, p_line, line, EUs(t1 - p_ticks));
                    if (LineTook(r))
                    {
                        st->samples++;
                        st->pace_samples++;
                        if (view != p_view)
                            st->line_across++;
                        if (hid || p_hid)
                            st->line_hidden++;
                    }
                    if (r >= 2)
                        st->resets++;
                    p_ok = FALSE;
                }
                else if (!p_ok || crtc != p_crtc || disp != p_disp || vtot != p_vtot || line < p_line)
                {
                    p_ok = TRUE;
                    p_line = line;
                    p_ticks = t0;
                    p_crtc = crtc;
                    p_disp = disp;
                    p_vtot = vtot;
                    p_view = view;
                    p_hid = hid;
                }
            }
            if (!m->h_ok[lft])
                wait = TRUE;        /* read only for the pair: no reference to test against */
            else if (m->var & RX_PACEBLANK)
                wait = FlipBeam_MustWait(m->h_line[lft], us, line, disp, vtot);
            else
                wait = (BOOL) (line >= m->h_line[lft]);
            if (m->h_ok[lft] && crtc == m->h_crtc[lft] && disp == m->h_disp[lft] && !wait)
            {
                m->pace_why = (line < disp) ? 3 : 4;
                break;
            }
        }
        if (i == 0)
        {
            st->pace_waits++;
            if (safe != 0)
                st->pace_trusted++;
        }
        if (i >= 60)
        {
            st->pace_gaveup++;
            m->pace_why = 5;
            break;
        }
        m->t += Rand(1000.0, 1300.0);
        st->pace_sleeps++;
    }
}

/* PanNow: ScrollVPort in the GL task, BeamNote, FlipSm_Panned, sample A */
static void RPanNow(struct RModel *m, struct RStats *st, ULONG to)
{
    const struct BeamMode *b;
    double t0;
    double dur;
    double W;
    double taken;
    double tr;
    double ep;
    ULONG crtc;
    ULONG disp;
    ULONG line;
    ULONG vtot;
    ULONG htot;
    ULONG left;
    ULONG old;

    RSync(m);
    RJob(m);
    if (m->job_third != NONE)
        st->busy++;
    if (to >= FLIP_N)
    {
        st->unfinished++;
        return;
    }
    if (m->content[to] < 0 || m->job_third == to || to == m->sm.draw)
        st->unfinished++;
    else if (m->content[to] <= m->last_shown)
        st->order_bad++;
    /* Radeon.card waits for the engine, then ~0.4 ms; the write inside */
    t0 = m->t;
    RIdle(m);
    dur = Rand(300.0, 500.0);
    W = m->t + Rand(0.0, dur);
    m->t += dur;
    st->call_us += m->t - t0;
    old = m->reg;
    if (RHiddenAt(m, W))
    {
        /* behind another screen since Safe's test: the third left is not
         * on show now, nor when the GL screen is in front again (the
         * register's third is) */
        st->pan_behind++;
        if (m->U[old] > W)
            m->U[old] = W;
        m->taken_ok = 0;
    }
    else
    {
        b = RShown(m, W, &ep);
        taken = BeamTaken(b, m->latch, W - ep) + ep;
        if (m->U[old] > taken)
            m->U[old] = taken;
        if (m->taken_ok && taken == m->last_taken)
        {
            st->unseen++;           /* the last pan's frame was never scanned out */
            st->shared_by[m->pace_why]++;
            st->shared_latch[m->latch]++;
            m->run++;
        }
        else
            m->run = 1;
        if (m->run > st->run_max)
            st->run_max = m->run;
        m->last_taken = taken;
        m->taken_ok = 1;
    }
    m->U[to] = R_HUGE;
    m->reg = to;
    if (m->content[to] > m->last_shown)
        m->last_shown = m->content[to];
    st->pans++;
    /* sample A's clock, then BeamNote's read: the record of the third left */
    m->a_ticks = RTicks(m, m->t);
    tr = m->t + Rand(5.0, 60.0);
    if (Rand(0.0, 1.0) < m->preempt)
        tr += Rand(0.0, 2000.0);
    RRead(m, tr, &crtc, &disp, &line, &vtot, &htot);
    m->a_view = RView(m, tr);
    m->t = tr + Rand(1.0, 20.0);
    left = m->h_shown;
    m->h_shown = to;
    if (left < FLIP_N && left != to)
    {
        m->h_ok[left] = FlipBeam_RefOk(line, disp) ? 1UL : 0UL;
        m->h_line[left] = line;
        m->h_disp[left] = disp;
        m->h_crtc[left] = crtc;
        m->h_vtot[left] = vtot;
        m->h_htot[left] = htot;
        m->h_ticks[left] = RTicks(m, m->t);
    }
    left = FlipSm_Panned(&m->sm);
    if (left != old)
        st->show_bad++;
    if (!(m->var & RX_NOMARK) && left < FLIP_N)
        m->beam_left |= 1UL << left;
    m->last_left = left;
    /* cardflip.c: BeamNote's BeamRead worked (here always; a failed line
     * read still gives the mode): the kick reads again */
    m->a_valid = (!(m->var & RX_STRICTA) || line < disp) ? 1UL : 0UL;
    m->a_line = line;
    m->a_crtc = crtc;
    m->a_disp = disp;
    m->a_vtot = vtot;
    m->a_htot = htot;
    RSync(m);
}

/* R2's rule (R1 only counts it): the record is in FlipLine's mode and the
 * refresh FlipLine measured has gone by since it */
static BOOL RRefresh(const struct RModel *m, ULONG d, ULONG us)
{
    if (m->var & RX_R2LOW)
        return FlipBeam_Refresh(us, (ULONG) (r_low * m->m->T));
    if (m->h_crtc[d] != m->line.crtc || m->h_disp[d] != m->line.disp || m->h_vtot[d] != m->line.vtot
        || m->h_htot[d] != m->line.htot)
        return FALSE;
    return FlipBeam_Refresh(us, FlipLine_SafeUs(&m->line));
}

/* BeamRef, unchanged from 0.15 (R2: the refresh rule after the age) */
static void RBeamRef(struct RModel *m, struct RStats *st)
{
    ULONG d;
    ULONG crtc;
    ULONG disp;
    ULONG line;
    ULONG vtot;
    ULONG htot;
    ULONG us;
    ULONG i;
    BOOL have;

    RSync(m);
    d = m->sm.draw;
    m->pan_ticks = m->h_ticks[d];
    us = EUs(RTicks(m, m->t) - m->pan_ticks);
    if (us >= FLIP_BEAM_AGED_US)
    {
        m->beam_left &= ~(1UL << d);
        st->b_aged++;
        return;
    }
    if ((m->var & (RX_R2 | RX_R2LOW)) && RRefresh(m, d, us))
    {
        m->beam_left &= ~(1UL << d);
        st->b_refresh++;
        return;
    }
    crtc = m->h_crtc[d];
    disp = m->h_disp[d];
    line = m->h_line[d];
    have = (BOOL) (m->h_ok[d] != 0);
    if (!have)
    {
        st->b_late++;
        for (i = 0; i < 8 && !have; i++)
        {
            RRead(m, m->t, &crtc, &disp, &line, &vtot, &htot);
            if (FlipBeam_RefOk(line, disp))
                have = TRUE;
            else
            {
                st->b_polls++;
                m->t += Rand(1000.0, 1300.0);
            }
        }
    }
    if (!have)
    {
        st->b_timed++;
        us = EUs(RTicks(m, m->t) - m->pan_ticks);
        if (us < FLIP_BEAM_AGED_US)
            m->t += (double) (FLIP_BEAM_AGED_US - us) + Rand(0.0, 200.0);
        m->beam_left &= ~(1UL << d);
        return;
    }
    m->pkt_crtc = crtc;
    m->pkt_disp = disp;
    m->ref_line = line;
    m->beam_third = d;
    m->beam_go = 1;
}

/* CardFlip_Beam: BeamSampleB (sample B and the reference taken at the
 * kick, only with the GL screen in front), then B's verdict. TRUE: the
 * Radeon waits for the blank */
static BOOL RBeamKick(struct RModel *m, struct RStats *st)
{
    ULONG crtc;
    ULONG disp;
    ULONG line;
    ULONG vtot;
    ULONG htot;
    ULONG us;
    ULONG r;
    ULONG lam;
    ULONG pred;
    ULONG dl;
    ULONG tb;
    ULONG d;
    BOOL key;
    long vb;
    int hid;
    double t;

    RSync(m);
    if (m->a_valid)
    {
        /* BeamSampleB */
        m->a_valid = 0;
        hid = RHiddenAt(m, m->t);
        if (hid && !(m->var & RX_NOFRONT))
            st->front_skips++;      /* another screen in front: no sample, no late reference */
        else
        {
            vb = RView(m, m->t);
            RRead(m, m->t, &crtc, &disp, &line, &vtot, &htot);
            t = m->t + Rand(1.0, 20.0);
            if (Rand(0.0, 1.0) < m->preempt)
                t += Rand(0.0, 2000.0);
            m->t = t;
            tb = RTicks(m, t);
            key = (BOOL) (crtc == m->a_crtc && disp == m->a_disp && vtot == m->a_vtot && htot == m->a_htot);
            if (!key)
                st->key_skips++;
            if (key && line < disp && m->a_line < disp)
            {
                us = EUs(tb - m->a_ticks);
                lam = FlipLine_Lam(&m->line);
                if (lam != 0 && m->line.crtc == crtc && m->line.disp == disp && m->line.vtot == vtot
                    && m->line.htot == htot && us < FLIP_LINE_US_MAX)
                {
                    /* the line FlipLine predicts against the one read */
                    pred = (ULONG) ((m->a_line + ((unsigned long) us * 256UL) / lam) % vtot);
                    dl = (pred > line) ? pred - line : line - pred;
                    if (dl > vtot / 2)
                        dl = vtot - dl;
                    st->pred[(dl <= 32) ? 0 : (dl <= 128) ? 1 : 2]++;
                }
                r = FlipLine_Sample(&m->line, crtc, disp, vtot, htot, m->a_line, line, us);
                if (LineTook(r))
                {
                    st->samples++;
                    if (vb != m->a_view)
                        st->line_across++;
                    if (hid)
                        st->line_hidden++;
                }
                if (r >= 2)
                    st->resets++;
            }
            /* lib/cardflip.c returns before both on a line in the blank;
             * RX_UPANY lets the reference through from there too */
            if (key && (line < disp || (m->var & RX_UPANY)))
            {
                d = m->last_left;
                if (d < FLIP_N && !m->h_ok[d] && m->h_crtc[d] == crtc && m->h_disp[d] == disp
                    && ((m->var & RX_UPANY) ? (line < vtot) : FlipBeam_RefOk(line, disp)))
                {
                    m->h_ok[d] = 1;
                    m->h_line[d] = line;
                    st->ref_kick++;
                    if (vb != m->a_view)
                        st->ref_across++;
                }
            }
        }
    }
    if (!m->beam_go)
        return FALSE;
    m->beam_go = 0;
    m->beam_left &= ~(1UL << m->beam_third);
    st->b_kicks++;
    us = EUs(RTicks(m, m->t) - m->pan_ticks);
    if (us >= FLIP_BEAM_AGED_US)
    {
        st->b_aged++;
        return FALSE;
    }
    if ((m->var & (RX_R2 | RX_R2LOW)) && RRefresh(m, m->beam_third, us))
    {
        st->b_refresh++;
        return FALSE;
    }
    RRead(m, m->t, &crtc, &disp, &line, &vtot, &htot);
    if (crtc != m->pkt_crtc || disp != m->pkt_disp)
    {
        st->b_mode++;
        return FALSE;
    }
    if (!FlipBeam_MustWait(m->ref_line, us, line, disp, vtot))
    {
        st->b_passed++;
        return FALSE;
    }
    st->b_waits++;
    if (RRefresh(m, m->beam_third, us))
        st->would++;
    return TRUE;
}

/* a spell's hide or return between this flush's Safe (at t_safe) and now */
static void RMid(const struct RModel *m, struct RStats *st, double t_safe)
{
    if (m->last_hs >= t_safe && m->last_hs <= m->t)
        st->hide_mid++;
    if (m->last_he >= t_safe && m->last_he <= m->t)
        st->ret_mid++;
}

/* one flush: card.c's CardFlush with lib/cardflip.c's R1 calls */
static void RFlush(struct RModel *m, struct RStats *st, const struct RCfg *c, int present, long frame, double gpu)
{
    const struct BeamMode *b;
    ULONG to;
    ULONG d;
    BOOL idle;
    BOOL wait;
    int behind;
    double tcp;
    double ts;
    double ep;
    double u;
    double t_safe;
    long k;

    st->flushes++;
    if (m->hs_at_safe)
    {
        m->hs_at_safe = 0;
        RSetStart(m, m->t + RSpellOffset());
    }
    if (m->he_at_safe)
    {
        m->he_at_safe = 0;
        RSetEnd(m, m->t + RSpellOffset());
    }
    RSync(m);
    t_safe = m->t;
    /* CardFlip_Safe */
    m->beam_go = 0;
    m->a_valid = 0;
    if (!(m->var & RX_014) && (m->sm.kick != NONE || m->sm.ready != NONE))
    {
        idle = (m->var & RX_NOIDLE) ? TRUE : REngineIdle(m, st);
        RSync(m);
        if (idle)
        {
            to = FlipSm_StartPan(&m->sm, TRUE);
            behind = RHiddenAt(m, m->t);
            if (to != NONE && behind)
            {
                st->pan_hidden++;
                FlipLine_Reset(&m->line);
            }
            else if (to != NONE)
            {
                m->pace_why = 0;
                if (!(m->var & RX_NOPACE))
                    RPace(m, st);
                st->pace_by[m->pace_why]++;
                RPanNow(m, st, to);
            }
            if (!behind && (m->sm.kick != NONE || m->sm.ready != NONE))
                st->idle_bad++;
        }
    }
    if (m->sm.leave != NONE)
        st->leave_bad++;
    d = m->sm.draw;
    if (!(m->var & RX_NOB) && d < FLIP_N && (m->beam_left & (1UL << d)) != 0)
        RBeamRef(m, st);
    /* the checker (off), OvLock; the locks: Radeon.card idles the engine */
    m->t += Rand(300.0, 1500.0);
    RIdle(m);
    RSync(m);
    m->pending = 0;                 /* Settle */
    FlipSm_Settled(&m->sm);         /* CardFlip_Settled: a no-op unless the wait gave up */
    if (present && RHiddenAt(m, m->t))
    {
        st->hidden_drops++;         /* card.c's step 1c, then Delay(2) */
        RMid(m, st, t_safe);
        m->t += 40000.0;
        RSync(m);
        return;
    }
    if (present)
    {
        m->next = (m->var & RX_014) ? FlipSm_Choose(&m->sm) : FlipSm_ChoosePan(&m->sm);
        if (m->next >= FLIP_N || m->next == m->sm.show || m->next == m->sm.draw)
            st->i1_bad++;
    }
    /* the CP's start, the stream's and the upload cache's copies */
    m->t += Rand(c->copy_lo, c->copy_hi);
    RSync(m);
    RMid(m, st, t_safe);
    wait = RBeamKick(m, st);
    d = m->sm.draw;
    if (d >= FLIP_N || d == m->sm.show)
    {
        st->i1_bad++;
        return;
    }
    /* the job: from the CP's arrival, or behind B's packet from the blank
     * of the mode the CRTC shows */
    tcp = m->t + Rand(2.0, 60.0);
    ts = tcp;
    if (wait)
    {
        b = RShown(m, tcp, &ep);
        if (BeamLine(b, tcp - ep) < (ULONG) b->disp)
        {
            k = (long) ((tcp - ep) / b->T);
            ts = ep + BeamBlank(b, k);
            if (ts < tcp)
                ts = ep + BeamBlank(b, k + 1);
        }
        st->stall += ts - tcp;
    }
    /* on show until U, or until a hide RSync has not applied yet */
    u = m->U[d];
    if (d != m->reg && !m->hide_done && m->hs <= ts && m->hs < u)
        u = m->hs;
    if (ts < u)
        st->tears++;
    if (present)
    {
        if (frame - m->last_shown == 1)
            st->age1++;
        else if (frame - m->last_shown == 2)
            st->age2++;
        else
            st->age_more++;
    }
    m->job_third = d;
    m->job_end = ts + gpu;
    m->job_frame = present ? frame : -2;
    m->content[d] = -2;
    if (c->overlap)
        m->pending = 1;
    else
    {
        RIdle(m);
        m->pending = 0;
    }
    m->t += Rand(50.0, 300.0);
    RSync(m);
    /* CardFlip_After */
    if (present)
    {
        FlipSm_Ran(&m->sm, (BOOL) (m->pending != 0), m->next);
        if (m->sm.draw == m->sm.show)
            st->i1_bad++;
    }
    if ((m->var & RX_014) && !RHiddenAt(m, m->t))
    {
        to = FlipSm_ToShow(&m->sm);
        m->pace_why = 0;
        if (to != NONE)
            RPanNow(m, st, to);
    }
    if (m->sm.leave != NONE)
        st->leave_bad++;
    if (m->sm.show != m->reg)
        st->show_bad++;
}

/* CardFlip_Open: FlipSm_Init, the beam's state cleared, the pan to 0 */
static void ROpen(struct RModel *m)
{
    ULONG i;

    RIdle(m);
    m->pending = 0;
    FlipSm_Init(&m->sm);
    FlipLine_Reset(&m->line);
    m->beam_left = 0;
    m->beam_go = 0;
    m->a_valid = 0;
    m->last_left = NONE;
    for (i = 0; i < FLIP_N; i++)
    {
        m->h_ok[i] = 0;
        m->content[i] = -1;
        m->U[i] = m->t;             /* nothing of ours on show in the new mode */
    }
    m->h_shown = m->sm.show;
    m->t += Rand(300.0, 500.0);
    m->reg = 0;
    m->U[0] = R_HUGE;
    m->taken_ok = 0;
}

/* at a frame's start: a spell behind another screen begins or ends (now, or
 * at a random point of the next flush), a resize */
static void RBetween(struct RModel *m, struct RStats *st, const struct RCfg *c)
{
    int mi;

    RSync(m);
    if (m->hs < R_HUGE || m->hs_at_safe)
    {
        if (m->hs <= m->t && m->he >= R_HUGE && !m->he_at_safe && m->t >= m->spell_until)
        {
            /* in front again: its mode on the CRTC from a new epoch, the
             * register's third on show */
            if (Rand(0.0, 1.0) < 0.5)
            {
                RSetEnd(m, m->t);
                RSync(m);
            }
            else
                m->he_at_safe = 1;
        }
        return;
    }
    if (c->spell > 0.0 && Rand(0.0, 1.0) < c->spell)
    {
        st->spells++;
        if (Rand(0.0, 1.0) < 0.4)
        {
            m->spell_mode = m->m;   /* the GL screen's own mode: the same key, another epoch */
            st->spells_same++;
        }
        else
            m->spell_mode = m->other;
        m->spell_until = m->t + Rand(100000.0, 2000000.0);
        if (Rand(0.0, 1.0) < 0.5)
        {
            RSetStart(m, m->t);
            RSync(m);
        }
        else
            m->hs_at_safe = 1;
        return;
    }
    if (c->resize > 0.0 && Rand(0.0, 1.0) < c->resize)
    {
        st->resizes++;
        mi = RPick(c->modes);
        m->m = RMode(mi);
        m->other = (mi == 1) ? &beam_modes[0] : &beam_modes[1];
        m->epoch = m->t - Rand(0.0, m->m->T);
        ROpen(m);
    }
}

static void RunR(const struct RCfg *c, int var, int mode, int latch, unsigned long seed, long frames,
                 struct RStats *st)
{
    struct RModel m;
    double at[8];
    int kind[8];
    double cpu;
    double gpu;
    double g;
    double prev;
    double x;
    long f;
    long pans0;
    int n;
    int nh;
    int ne;
    int k;
    int j;

    rng = seed;
    memset(&m, 0, sizeof(m));
    m.var = var;
    m.preempt = 0.02;
    m.m = RMode(mode);
    m.other = (mode == 1) ? &beam_modes[0] : &beam_modes[1];
    m.spell_mode = m.other;
    m.hs = R_HUGE;
    m.he = R_HUGE;
    m.last_hs = -1.0;
    m.last_he = -1.0;
    m.latch = latch;
    /* every other run's E clock wraps in its first 30 s */
    m.clock_base = (seed & 1UL) ? 4294967296.0 / 0.709379 - Rand(0.0, 30.0e6) : Rand(0.0, 3.0e9);
    m.t = 1000.0;
    m.epoch = m.t - Rand(0.0, m.m->T);
    m.job_third = NONE;
    m.last_shown = -1;
    m.sm.dropped = 0;
    ROpen(&m);
    pans0 = st->pans;
    for (f = 0; f < frames; f++)
    {
        RBetween(&m, st, c);
        n = c->mid ? (int) Rand(0.0, 3.99) : 0;
        nh = c->heap ? (int) Rand(0.0, 3.99) : 0;
        cpu = Rand(c->cpu_lo, c->cpu_hi);
        gpu = Rand(c->gpu_lo, c->gpu_hi);
        /* the frame's events by its 68k time: n + 1 flushes (the last
         * presents), nh heap locks at random points */
        ne = 0;
        for (k = 0; k <= n; k++)
        {
            at[ne] = cpu * (double) (k + 1) / (double) (n + 1);
            kind[ne] = (k == n) ? 2 : 1;
            ne++;
        }
        for (k = 0; k < nh; k++)
        {
            x = Rand(0.0, cpu);
            for (j = ne; j > 0 && at[j - 1] >= x; j--)
            {
                at[j] = at[j - 1];
                kind[j] = kind[j - 1];
            }
            at[j] = x;
            kind[j] = 0;
            ne++;
        }
        prev = 0.0;
        for (k = 0; k < ne; k++)
        {
            m.t += at[k] - prev;
            prev = at[k];
            if (kind[k] == 0)
            {
                /* a heap lock (Card_VramBegin): the engine idle, Settle */
                RIdle(&m);
                m.pending = 0;
                m.t += Rand(20.0, 200.0);
            }
            else
            {
                g = gpu / (double) (n + 1);
                if (c->spike > 0.0 && Rand(0.0, 1.0) < c->spike)
                {
                    g = Rand(110000.0, 200000.0);
                    st->spikes++;
                }
                RFlush(&m, st, c, kind[k] == 2, f, g);
            }
        }
    }
    x = m.t / m.m->T;               /* refreshes */
    if ((double) (st->pans - pans0) / x > st->pans_rate)
        st->pans_rate = (double) (st->pans - pans0) / x;
    st->runs++;
    st->frames += frames;
    st->dropped += (long) m.sm.dropped;
    st->seconds += m.t / 1.0e6;
}

/* a regime in every mode of its set and every latch model, 20 runs at least */
static void RunRegime(int ri, int var, long frames, struct RStats *st)
{
    const struct RCfg *c;
    unsigned long seed;
    int mi;
    int li;
    int s;
    int nm;
    int per;

    c = &r_regimes[ri];
    memset(st, 0, sizeof(*st));
    nm = 0;
    for (mi = 0; mi < R_MODES; mi++)
        if (c->modes & (1UL << mi))
            nm++;
    per = (20 + nm * LATCHES - 1) / (nm * LATCHES);
    seed = 7000UL + (unsigned long) ri * 997UL + (unsigned long) var * 101UL;
    for (mi = 0; mi < R_MODES; mi++)
        if (c->modes & (1UL << mi))
            for (li = 0; li < LATCHES; li++)
                for (s = 0; s < per; s++)
                    RunR(c, var, mi, li, seed++, frames, st);
}

/* what must hold in every regime, R1 and R2 alike */
static void RCheckAll(const struct RStats *st)
{
    CHECK(st->tears == 0, "the Radeon wrote into a third before the blank that took the pan leaving it");
    CHECK(st->unfinished == 0, "a pan onto a third not finished");
    CHECK(st->order_bad == 0, "a pan onto a frame no newer than the one shown");
    CHECK(st->busy == 0, "a pan with our work on the engine (I3)");
    CHECK(st->show_bad == 0, "sm.show is not the register's third (I8)");
    CHECK(st->i1_bad == 0, "draw == show (I1)");
    CHECK(st->leave_bad == 0, "leave set outside FlipSm_Panned (I4)");
    CHECK(st->idle_bad == 0, "a Safe in front with the engine idle left a frame unshown");
    CHECK(st->line_across == 0 && st->line_hidden == 0,
          "a FlipLine sample from two reads across a hide or return, or behind another screen");
    CHECK(st->ref_across == 0, "a reference taken at the kick across a hide or return");
    CHECK(st->pace_gaveup == 0, "PanPace gave up before FLIP_BEAM_AGED_US");
}

/* PanPace's cap. As built (the beam test only back above the reference):
 * one pan a blank, every frame scanned out. RX_PACEBLANK, the first
 * build's test (passing as soon as the beam is in the blank), let a blank
 * take two pans: with the latch mid-blank or at the blank's end the next
 * pan's write could still come before the latch (the first frame never
 * scanned out; no tear) -- never three, never otherwise, never with the
 * latch at or before the blank's start. */
static void RCheckPace(const struct RStats *st, int var)
{
    if (!(var & RX_PACEBLANK))
    {
        CHECK(st->run_max <= 1 && st->unseen == 0, "PanPace: a blank took two pans in a row");
        return;
    }
    CHECK(st->run_max <= 2, "PanPace (first build): a blank took three pans in a row");
    CHECK(st->unseen == st->shared_by[4], "PanPace: a blank took two pans in a row, not after a pass in the blank");
    CHECK(st->shared_latch[LATCH_PREFETCH] + st->shared_latch[LATCH_START] == 0,
          "PanPace: a blank took two pans in a row with the latch at or before the blank's start");
}

static void RPrint(const char *tag, const char *name, const struct RStats *st)
{
    long ages;

    ages = st->age1 + st->age2 + st->age_more;
    printf("test_flip: %s, %s: %ld runs, frames %ld (%.1f ms each), pans %ld (%.2f a refresh in the busiest run, "
           "a blank took %ld in a row at most, never scanned out %ld), dropped %ld, picture age 1 frame %ld%%; at "
           "the start: finished by a lock %ld%%, by the fence %ld%%, waited for %ld%% of the flushes (gave up %ld), "
           "behind %ld; paced %ld (1 ms sleeps %ld, FlipLine known %ld, gave up %ld)\n",
           tag, name, st->runs, st->frames, st->frames ? st->seconds * 1000.0 / (double) st->frames : 0.0, st->pans,
           st->pans_rate, st->run_max, st->unseen, st->dropped,
           Pct(st->age1, ages), Pct(st->fin_lock, st->fin_lock + st->fin_fence + st->fence_waits),
           Pct(st->fin_fence, st->fin_lock + st->fin_fence + st->fence_waits), Pct(st->fence_waits, st->flushes),
           st->gaveup, st->pan_hidden, st->pace_waits, st->pace_sleeps, st->pace_trusted, st->pace_gaveup);
    printf("test_flip:   B waited in %ld%% of the frames (mean %.1f ms), passed %ld%%, aged %ld, another mode %ld, "
           "late %ld (polls %ld), timed %ld, reference at the kick %ld; %s %ld%% of the waits; ScrollVPort %.0f us; "
           "line samples %ld, predicted within 32 lines %ld%%\n",
           Pct(st->b_waits, st->frames), st->b_waits ? st->stall / (double) st->b_waits / 1000.0 : 0.0,
           Pct(st->b_passed, st->frames), st->b_aged, st->b_mode, st->b_late, st->b_polls, st->b_timed,
           st->ref_kick, "the refresh rule would have passed", Pct(st->would, st->b_waits),
           st->pans ? st->call_us / (double) st->pans : 0.0, st->samples,
           Pct(st->pred[0], st->pred[0] + st->pred[1] + st->pred[2]));
    printf("test_flip:   PanPace passed: nothing panned before %ld, aged %ld, by FlipLine's refresh %ld, by the beam "
           "back above the reference %ld, in the blank %ld; pans a blank took after the last one: after a pass in "
           "the blank %ld, otherwise %ld (latch 2 lines early %ld, at the blank %ld, mid-blank %ld, at its end %ld)\n",
           st->pace_by[0], st->pace_by[1], st->pace_by[2], st->pace_by[3], st->pace_by[4], st->shared_by[4],
           st->unseen - st->shared_by[4], st->shared_latch[LATCH_PREFETCH], st->shared_latch[LATCH_START],
           st->shared_latch[LATCH_MID], st->shared_latch[LATCH_END]);
    if (st->spells > 0)
        printf("test_flip:   spells %ld (%ld in the GL screen's own mode), begun between a Safe and its kick %ld, "
               "ended so %ld; pans behind (hidden after Safe's test) %ld; the kick's in-front test %ld, another key "
               "there %ld; samples across a hide or return %ld, behind %ld, references across %ld\n",
               st->spells, st->spells_same, st->hide_mid, st->ret_mid, st->pan_behind, st->front_skips,
               st->key_skips, st->line_across, st->line_hidden, st->ref_across);
}

static void TestRunR(void)
{
    static char name[200];
    struct RStats st;
    long tears[4];
    long ages;
    long n0;
    long n1;
    long pb_max;
    int ri;

    /* R1 as built: every regime */
    for (ri = 0; ri < R_REGIMES; ri++)
    {
        RunRegime(ri, 0, 2000, &st);
        sprintf(name, "R1, %s", r_regimes[ri].name);
        regime = name;
        RCheckAll(&st);
        RCheckPace(&st, 0);
        ages = st.age1 + st.age2 + st.age_more;
        if (ri == R_STEADY || ri == R_HEAVY)
        {
            CHECK(st.dropped == 0, "a frame dropped although the 68k is slower than the refresh");
            CHECK(st.pans >= st.frames - st.runs, "frames not shown");
            CHECK(st.unseen == 0, "a frame panned but never scanned out");
            CHECK(st.age1 == ages, "a picture older than one frame");
            CHECK(st.b_timed == 0, "the 68k slept out B's time rule");
            /* GLQuake's last pan is a 68k frame old, longer than FlipLine's
             * refresh: PanPace waits only while FlipLine has no bound yet */
            CHECK(st.pace_trusted == 0, "PanPace waited with FlipLine's refresh known");
            CHECK(st.pace_waits <= 3 * st.runs, "PanPace waited after the first frames");
        }
        if (ri == R_STEADY)
        {
            CHECK(st.fence_waits * 100 < st.flushes, "fence waits in 1% of the flushes or more");
            CHECK(st.b_waits * 100 >= st.frames * 30 && st.b_waits * 100 <= st.frames * 65,
                  "B's waits outside 30-65% of the frames");
            CHECK(st.would * 10 >= st.b_waits * 9, "the refresh rule would have passed under 90% of B's waits");
            CHECK(st.pred[0] * 10 >= (st.pred[0] + st.pred[1] + st.pred[2]) * 9,
                  "FlipLine predicts the line within 32 in under 90%");
        }
        if (ri == R_FAST)
        {
            CHECK(st.fence_waits > 0, "no fence wait although frames come faster than the refresh");
            CHECK(st.b_waits * 10 > st.frames * 4, "B waited in 40% of the frames or fewer");
            /* capped at the refresh, by PanPace (and B's waits through the
             * next Safe's fence wait) */
            CHECK(st.pace_waits > 0, "PanPace never waited although frames come faster than the refresh");
            CHECK(st.pans_rate <= 1.01, "more than a pan a refresh");
            CHECK(st.unseen * 20 < st.pans, "5% of the pans or more never scanned out");
        }
        if (ri == R_SLOW)
            CHECK(st.fence_waits > 0, "no fence wait with the Radeon slower than the 68k");
        if (ri == R_SHORT)
        {
            /* Zod's case (2026-11-19): the pan-to-kick pair seldom spans
             * 48 lines, so FlipLine's bound comes from PanPace's own pairs
             * within the first holds -- then no pan waits, the frames being
             * longer than the refresh */
            CHECK(st.pace_samples > 0, "PanPace's own reads gave FlipLine no pair");
            /* with only FLIP_LINE_TRUST samples the bound is their largest:
             * one stretched by a pre-emption (up to 2 ms in a ~2 ms pair) is
             * loose, never low, for the first frames (MEASURED in this model:
             * safe 31.7 ms 0.25 s into a run) -- a wait then, no more */
            CHECK(st.pace_trusted <= st.runs, "PanPace waited with FlipLine's refresh known more than once a run");
            CHECK(st.pace_waits <= 3 * st.runs, "PanPace waited after the first frames");
        }
        if (ri == R_SPELL)
        {
            CHECK(st.spells > 0 && st.pan_hidden > 0 && st.hidden_drops > 0, "no spell behind another screen");
            CHECK(st.hide_mid > 0 && st.ret_mid > 0, "no spell began or ended between a flush's Safe and its kick");
            CHECK(st.spells_same > 0, "no spell with the other screen in the GL screen's mode");
            CHECK(st.front_skips > 0, "the kick's in-front test never mattered");
            CHECK(st.pan_behind > 0, "no pan with the screen hidden after Safe's test");
        }
        if (ri == R_RESIZE)
            CHECK(st.resizes > 0, "no resize");
        if (ri == R_SPIKE)
            CHECK(st.spikes > 0 && st.gaveup > 0, "no fence wait gave up");
        RPrint("R1", r_regimes[ri].name, &st);
    }

    /* 0.16's PanPace (no pairs of its own) in Zod's regime: FlipLine waits
     * long for BeamSampleB's samples and the holds go on far beyond the first
     * frames -- the R_SHORT check above has teeth */
    RunRegime(R_SHORT, RX_NOHOLDPAIR, 2000, &st);
    regime = "R1, short flushes, PanPace without its own pairs (0.16)";
    RCheckAll(&st);
    CHECK(st.pace_waits > 3 * st.runs, "without its own pairs PanPace still waited only in the first frames: "
                                       "the short-flush check has no teeth");
    printf("test_flip: R1, short flushes, 0.16's PanPace (no pairs of its own): paced %ld in %ld runs (1 ms sleeps "
           "%ld), samples %ld\n", st.pace_waits, st.runs, st.pace_sleeps, st.samples);
    RunRegime(R_SHORT, 0, 2000, &st);
    printf("test_flip: R1, short flushes, PanPace as built: paced %ld in %ld runs (1 ms sleeps %ld), samples %ld, "
           "of them from the holds %ld\n", st.pace_waits, st.runs, st.pace_sleeps, st.samples, st.pace_samples);

    /* the first build's PanPace (passing already in the blank) must show
     * what the fix cured: two pans a blank somewhere, or the check above
     * has no teeth */
    pb_max = 0;
    for (ri = 0; ri < R_REGIMES; ri++)
    {
        RunRegime(ri, RX_PACEBLANK, 2000, &st);
        sprintf(name, "R1, PanPace's first build (in the blank), %s", r_regimes[ri].name);
        regime = name;
        RCheckAll(&st);
        RCheckPace(&st, RX_PACEBLANK);
        if (st.run_max > pb_max)
            pb_max = st.run_max;
        printf("test_flip: R1, PanPace's first build (in the blank), %-38s a blank took %ld in a row at most, "
               "never scanned out %ld, %.2f pans a refresh in the busiest run, paced %ld (1 ms sleeps %ld), "
               "tears %ld\n",
               r_regimes[ri].name, st.run_max, st.unseen, st.pans_rate, st.pace_waits, st.pace_sleeps, st.tears);
    }
    regime = "PanPace's first build";
    CHECK(pb_max >= 2, "the first build's PanPace never let a blank take two pans: the cap check has no teeth");

    /* the model must see each guard's absence */
    regime = "R without PanPace";
    RunRegime(R_FAST, RX_NOPACE, 2000, &st);
    RCheckAll(&st);
    CHECK(st.run_max >= 2, "the model sees no blank take two pans without PanPace (fast)");
    printf("test_flip: R1 without PanPace (fast): %.2f pans a refresh in the busiest run, a blank took %ld in a row "
           "at most, %ld of %ld pans never scanned out\n",
           st.pans_rate, st.run_max, st.unseen, st.pans);
    regime = "R without B";
    RunRegime(R_FAST, RX_NOB | RX_NOPACE, 2000, &st);
    tears[0] = st.tears;
    CHECK(st.tears > 0, "the model sees no tear without B (fast)");
    RunRegime(R_NEAR60, RX_NOB | RX_NOPACE, 2000, &st);
    tears[1] = st.tears;
    RunRegime(R_NEAR50, RX_NOB | RX_NOPACE, 2000, &st);
    tears[1] += st.tears;
    CHECK(tears[1] > 0, "the model sees no tear without B (near the refresh)");
    RunRegime(R_TV, RX_NOB | RX_NOPACE, 2000, &st);
    tears[2] = st.tears;
    CHECK(st.tears > 0, "the model sees no tear without B (24 and 30 Hz)");
    RunRegime(R_STEADY, RX_NOB | RX_NOPACE, 2000, &st);
    tears[3] = st.tears;
    printf("test_flip: R without B (nor PanPace) tears: fast %ld, near the refresh %ld, 24/30 Hz %ld (steady GLQuake "
           "at 50-85 Hz: %ld, B's waits there are what R2 would save)\n",
           tears[0], tears[1], tears[2], tears[3]);
    n0 = 0;
    RunRegime(R_FAST, RX_NOB, 2000, &st);
    n0 += st.tears;
    RunRegime(R_NEAR60, RX_NOB, 2000, &st);
    n0 += st.tears;
    RunRegime(R_NEAR50, RX_NOB, 2000, &st);
    n0 += st.tears;
    RunRegime(R_TV, RX_NOB, 2000, &st);
    n0 += st.tears;
    printf("test_flip: R without B but with PanPace tears %ld in the fast, near-refresh and 24/30 Hz regimes (the pan "
           "waits for the last one's offset by B's own beam rule; the kick into the third it left comes after)\n",
           n0);
    regime = "R, PanNow without the beam_left mark";
    RunRegime(R_FAST, RX_NOMARK | RX_NOPACE, 2000, &st);
    tears[0] = st.tears;
    RunRegime(R_TV, RX_NOMARK | RX_NOPACE, 2000, &st);
    tears[0] += st.tears;
    CHECK(tears[0] > 0, "the model sees no tear without the beam_left mark");
    regime = "R, the pan without EngineIdle";
    RunRegime(R_SLOW, RX_NOIDLE, 2000, &st);
    CHECK(st.unfinished > 0, "the model sees no pan onto a third still being drawn");
    tears[1] = st.unfinished;
    tears[2] = st.busy;
    regime = "R, EngineIdle on sm.kick only (design 1)";
    RunRegime(R_SPIKE, RX_KICKIDLE, 2000, &st);
    n0 = st.busy;
    CHECK(st.busy > 0, "the model sees no pan on a busy engine with design 1's idle test");
    RunRegime(R_SPELL, RX_KICKIDLE, 2000, &st);
    n1 = st.busy;
    regime = "0.14's order: the pan in CardFlip_After";
    RunRegime(R_STEADY, RX_014, 2000, &st);
    ages = st.age1 + st.age2 + st.age_more;
    CHECK(st.busy * 2 > st.pans, "the model sees no pans on a busy engine in 0.14's order");
    CHECK(st.age2 * 10 > ages * 9, "the model sees no picture two frames old in 0.14's order");
    printf("test_flip: R's negatives: without the beam_left mark (nor PanPace) tears %ld; without EngineIdle (slow "
           "GPU) %ld pans onto a third still drawn, %ld on a busy engine; EngineIdle on sm.kick only: %ld pans on a "
           "busy engine with GPU spikes, %ld behind another screen now and then; 0.14's order (steady): %ld of %ld "
           "pans on a busy engine (ScrollVPort %.1f ms), picture age 2 frames %ld%%\n",
           tears[0], tears[1], tears[2], n0, n1, st.busy, st.pans,
           st.pans ? st.call_us / (double) st.pans / 1000.0 : 0.0, Pct(st.age2, ages));

    /* the kick's late reference from a blank line (FlipBeam_RefOk dropped) */
    regime = "R, the kick's late reference from any line";
    RunRegime(R_NEAR60, RX_UPANY, 2000, &st);
    tears[0] = st.tears;
    RunRegime(R_NEAR50, RX_UPANY, 2000, &st);
    tears[0] += st.tears;
    RunRegime(R_FAST, RX_UPANY, 2000, &st);
    tears[0] += st.tears;
    CHECK(tears[0] > 0, "the model sees no tear with a blank line as the kick's reference");
    /* BeamSampleB without its in-front test: a sample from a pan read in
     * front and a kick read behind a screen in the same mode */
    regime = "R, BeamSampleB without the in-front test";
    RunRegime(R_SPELL, RX_NOFRONT, 2000, &st);
    CHECK(st.line_across > 0, "the model sees no FlipLine sample across a hide without the in-front test");
    printf("test_flip: R's negatives: the kick's reference from any line tears %ld (near the refresh and fast); "
           "BeamSampleB without the in-front test: samples across a hide %ld, behind %ld, references across %ld, "
           "refused by the mode key %ld\n",
           tears[0], st.line_across, st.line_hidden, st.ref_across, st.key_skips);

    /* the late reference at the kick only after a pan read in the picture
     * (a_valid = line < disp): sound too, but a pan read in the blank then
     * waits for BeamRef's polls a 68k frame later, and B waits more */
    RunRegime(R_STEADY, 0, 2000, &st);
    tears[0] = st.b_late;
    tears[1] = st.b_waits;
    tears[2] = st.ref_kick;
    RunRegime(R_STEADY, RX_STRICTA, 2000, &st);
    regime = "R1, sample B only after a pan read in the picture";
    RCheckAll(&st);
    printf("test_flip: R1 (steady), the kick's late reference only after a pan read in the picture: late %ld (as "
           "built %ld), reference at the kick %ld (%ld), B waited in %ld%% of the frames (%ld%%)\n",
           st.b_late, tears[0], st.ref_kick, tears[2], Pct(st.b_waits, st.frames), Pct(tears[1], st.frames));

    /* R2 (branch step-r2, as the library builds it there: cardflip.c
     * BeamRefreshed in BeamRef and CardFlip_Beam): B also passes once
     * FlipLine's refresh has gone by since the record, in the record's own
     * mode. The R1 rows above are the same model without that rule */
    for (ri = 0; ri < R_REGIMES; ri++)
    {
        RunRegime(ri, RX_R2, 2000, &st);
        sprintf(name, "R2, %s", r_regimes[ri].name);
        regime = name;
        RCheckAll(&st);
        RCheckPace(&st, 0);
        if (ri == R_STEADY || ri == R_HEAVY)
            CHECK(st.b_waits * 50 < st.frames, "R2: B waited in 2% of the frames or more");
        printf("test_flip: R2, %-38s tears %ld, B waited in %ld.%ld%% of the frames (%ld), passed by the refresh "
               "%ld, by the beam %ld, aged %ld\n",
               r_regimes[ri].name, st.tears, st.b_waits * 1000 / (st.frames ? st.frames : 1) / 10,
               st.b_waits * 1000 / (st.frames ? st.frames : 1) % 10, st.b_waits, st.b_refresh, st.b_passed,
               st.b_aged);
    }
    /* a refresh bound too low: B's rule alone (no PanPace) with 0.95 T;
     * behind PanPace (the same bound there) the kick comes at least ~1.6 ms
     * after the pan, so only a bound lower still tears: 0.8 T */
    regime = "R2 with 0.95 T";
    RunRegime(R_NEAR60, RX_R2 | RX_R2LOW | RX_NOPACE, 2000, &st);
    tears[0] = st.tears;
    RunRegime(R_NEAR50, RX_R2 | RX_R2LOW | RX_NOPACE, 2000, &st);
    tears[0] += st.tears;
    CHECK(tears[0] > 0, "the model sees no tear with a refresh rule of 0.95 T");
    RunRegime(R_NEAR60, RX_R2 | RX_R2LOW, 2000, &st);
    tears[1] = st.tears;
    RunRegime(R_NEAR50, RX_R2 | RX_R2LOW, 2000, &st);
    tears[1] += st.tears;
    r_low = 0.8;
    regime = "R2 with 0.8 T, PanPace too";
    RunRegime(R_NEAR60, RX_R2 | RX_R2LOW, 2000, &st);
    tears[2] = st.tears;
    RunRegime(R_NEAR50, RX_R2 | RX_R2LOW, 2000, &st);
    tears[2] += st.tears;
    r_low = 0.95;
    CHECK(tears[2] > 0, "the model sees no tear with a refresh rule of 0.8 T in B and PanPace");
    printf("test_flip: R2 with a bound under the refresh tears near the refresh: 0.95 T in B without PanPace %ld; in B "
           "and PanPace 0.95 T %ld, 0.8 T %ld\n",
           tears[0], tears[1], tears[2]);
}

/* 3. TestBeam's rows for R: the third rests a 68k frame before the next
 * flush's BeamRef; R2's refresh rule in front of B's. At 50-85 Hz that
 * rest outlasts the refresh, so the R row has no tear there even without
 * the wait: its "no tear" says nothing about B. Only the 24/30 Hz rows
 * test B (without the wait they tear: checked). */

static ULONG beam_safe_us;          /* WaitRefresh's refresh, us */

static BOOL WaitRefresh(ULONG ref_line, ULONG us_since_pan, ULONG line_now, ULONG disp, ULONG vtot)
{
    if (FlipBeam_Refresh(us_since_pan, beam_safe_us))
        return FALSE;
    return FlipBeam_MustWait(ref_line, us_since_pan, line_now, disp, vtot);
}

static const struct BeamTiming beam_timing_r =
{
    "R: a 68k frame's rest", 25000.0, 40000.0, 1500.0, 4500.0, 2.0, 60.0, 5.0, 300.0
};

static void BeamAdd(struct BeamStats *sum, const struct BeamStats *st)
{
    sum->trials += st->trials;
    sum->waits += st->waits;
    sum->passed += st->passed;
    sum->aged += st->aged;
    sum->late += st->late;
    sum->polls += st->polls;
    sum->timed += st->timed;
    sum->extra += st->extra;
    sum->stall += st->stall;
}

static void TestBeamR(void)
{
    static char name[160];
    const struct BeamMode *m;
    struct BeamStats st;
    struct BeamStats sr;
    struct BeamStats ss;
    struct BeamStats sn;
    unsigned long seed;
    long tears_low;
    long tv_waits;
    long tv_trials;
    long tv_never[R_MODES];
    long never_tv;
    long never_steady;
    long rate_r;
    long rate_s;
    int mi;
    int li;
    int ti;

    memset(&sr, 0, sizeof(sr));
    memset(&ss, 0, sizeof(ss));
    tears_low = 0;
    tv_waits = 0;
    tv_trials = 0;
    never_tv = 0;
    never_steady = 0;
    for (mi = 0; mi < R_MODES; mi++)
    {
        m = RMode(mi);
        tv_never[mi] = 0;
        for (li = 0; li < LATCHES; li++)
        {
            seed = (unsigned long) (3000 + mi * 31 + li * 7);
            sprintf(name, "beam R: %s, %s", m->name, latch_names[li]);
            regime = name;
            /* with B: no tear (evidence about B only at 24/30 Hz, below) */
            BeamRun(m, li, &beam_timing_r, FlipBeam_RefOk, FlipBeam_MustWait, seed, 20000, &st);
            CHECK(st.tears == 0, "R: the Radeon wrote into a third still on show");
            CHECK(st.waits + st.passed + st.aged + st.timed == st.trials, "R: a trial without a verdict");
            /* ... and without the wait: tears at 24/30 Hz, none at 50-85 Hz */
            BeamRun(m, li, &beam_timing_r, FlipBeam_RefOk, WaitNever, seed + 3, 20000, &sn);
            if (m->T > 20100.0)
            {
                tv_never[mi] += sn.tears;
                never_tv += sn.tears;
            }
            else
                never_steady += sn.tears;
            if (li == LATCH_START && m->T <= 20100.0)
            {
                BeamAdd(&sr, &st);
                BeamRun(m, li, &beam_timings[0], FlipBeam_RefOk, FlipBeam_MustWait, seed, 20000, &st);
                BeamAdd(&ss, &st);
            }
            /* R2's rule with the exact refresh: never a tear, whatever the timing */
            beam_safe_us = (ULONG) ((m->vt + (double) FLIP_BEAM_GUARD) * BeamLt(m) + 0.999) + FLIP_LINE_MARGIN_US;
            BeamRun(m, li, &beam_timing_r, FlipBeam_RefOk, WaitRefresh, seed, 20000, &st);
            CHECK(st.tears == 0, "R + the refresh rule: the Radeon wrote into a third still on show");
            if (m->T <= 20100.0)
                CHECK(st.waits == 0, "R + the refresh rule: a wait after a whole 68k frame");
            else
            {
                tv_waits += st.waits;
                tv_trials += st.trials;
            }
            for (ti = 0; ti < BEAM_TIMINGS; ti++)
            {
                BeamRun(m, li, &beam_timings[ti], FlipBeam_RefOk, WaitRefresh, seed + 1, 20000, &st);
                CHECK(st.tears == 0, "the refresh rule: the Radeon wrote into a third still on show");
            }
            /* ... and with 0.95 T in its place it must tear */
            beam_safe_us = (ULONG) (0.95 * m->T);
            BeamRun(m, li, &beam_timings[1], FlipBeam_RefOk, WaitRefresh, seed + 2, 20000, &st);
            tears_low += st.tears;
        }
    }
    for (mi = 0; mi < R_MODES; mi++)
        if (RMode(mi)->T > 20100.0)
        {
            sprintf(name, "beam R: %s, without the wait", RMode(mi)->name);
            regime = name;
            CHECK(tv_never[mi] > 0, "R: the model sees no tear without the wait at 24/30 Hz (the row has no teeth)");
        }
    rate_r = sr.waits * 100 / sr.trials;
    rate_s = ss.waits * 100 / ss.trials;
    printf("test_flip: beam, R (a 68k frame's rest), 50-85 Hz, %ld trials: the Radeon waited %ld%% (mean %.1f ms), "
           "0.15's steady row %ld%% (mean %.1f ms); with the refresh rule (exact T) no tear in any mode, latch or "
           "timing, waits 0 at 50-85 Hz, %ld%% at 24/30 Hz; with 0.95 T %ld tears\n",
           sr.trials, rate_r, sr.waits ? sr.stall / sr.waits / 1000.0 : 0.0, rate_s,
           ss.waits ? ss.stall / ss.waits / 1000.0 : 0.0, Pct(tv_waits, tv_trials), tears_low);
    regime = "beam R";
    CHECK(rate_r < 65 && rate_r + 20 <= rate_s, "R waits about as often as 0.15");
    CHECK(rate_r >= 35, "R1 waits in under 35%: nothing left for the refresh rule");
    CHECK(tears_low > 0, "the model sees no tear with a refresh rule of 0.95 T");
    printf("test_flip: beam, R row with B: no tear in any mode or latch; without the wait %ld tears at 24/30 Hz, %ld "
           "at 50-85 Hz (a 68k frame's rest outlasts those refreshes: no evidence about B there)\n",
           never_tv, never_steady);
}

/* 4. FlipLine, the line time's upper bound */

/* the line a beam of vtot lines, lt us each, its top at phase, is on at t */
static ULONG LineAt(double t, double phase, double lt, ULONG vtot)
{
    double r;
    double T;
    long k;
    ULONG line;

    T = lt * (double) vtot;
    r = t - phase;
    k = (long) (r / T);
    r -= (double) k * T;
    if (r < 0.0)
        r = 0.0;
    line = (ULONG) (r / lt);
    if (line >= vtot)
        line = vtot - 1;
    return line;
}

/* the last sample FlipLine took */
static ULONG LastLam(const struct FlipLine *l)
{
    return l->lam[(l->next + FLIP_LINE_N - 1) % FLIP_LINE_N];
}

static ULONG CeilDiv(unsigned long a, unsigned long b)
{
    return (ULONG) ((a + b - 1) / b);
}

static void TestFlipLine(void)
{
    struct FlipLine l;
    ULONG la;
    ULONG lb;
    ULONG us;
    ULONG r;
    ULONG vtot;
    ULONG disp;
    ULONG htot;
    ULONG lam;
    ULONG safe;
    ULONG v[FLIP_LINE_N];
    ULONG tv;
    double lt;
    double phase;
    double t0;
    double ta;
    double tb;
    double d1;
    double d2;
    double gap;
    double ref;
    double worst;
    double wrap;
    long samples;
    long under;
    long under_m1;
    long under_low;
    long tight;
    long tight_under;
    long tight_m1;
    long tight_p3;
    long skipped;
    int mode;
    int j;
    int i;
    int k;
    int bad;
    int taken;
    int pos[3];

    regime = "FlipLine";
    wrap = 4294967296.0 / 0.709379;
    /* the gates */
    FlipLine_Reset(&l);
    CHECK(FlipLine_Lam(&l) == 0 && FlipLine_SafeUs(&l) == 0, "a bound before any sample");
    CHECK(FlipLine_Sample(&l, 0, 720, 750, 2222, 100, 147, 1100) == 0 && l.n == 0, "a span of 47 lines taken");
    CHECK(FlipLine_Sample(&l, 0, 720, 750, 2222, 100, 148, 1100) == 1 && l.n == 1, "a span of 48 lines refused");
    CHECK(FlipLine_Sample(&l, 0, 720, 750, 2222, 720, 790, 1100) == 0, "a first read in the blank taken");
    CHECK(FlipLine_Sample(&l, 0, 720, 750, 2222, 100, 720, 1100) == 0, "a second read in the blank taken");
    CHECK(FlipLine_Sample(&l, 0, 720, 750, 2222, 0x7ff, 100, 1100) == 0, "a failed first read taken");
    CHECK(FlipLine_Sample(&l, 0, 720, 750, 2222, 100, 0x7ff, 1100) == 0, "a failed second read taken");
    CHECK(FlipLine_Sample(&l, 0, 720, 750, 2222, 300, 100, 1100) == 0, "a pair across a wrap taken");
    CHECK(FlipLine_Sample(&l, 0, 720, 750, 2222, 100, 300, FLIP_LINE_US_MAX) == 0, "a pair 100 ms apart taken");
    CHECK(l.n == 1, "a refused pair kept");
    CHECK(FlipLine_Sample(&l, 0, 720, 750, 2222, 100, 300, FLIP_LINE_US_MAX - 1) == 1 && l.n == 2,
          "a pair just under 100 ms refused");
    CHECK(FlipLine_Lam(&l) == 0 && FlipLine_SafeUs(&l) == 0, "a bound from two samples");
    CHECK(FlipLine_Sample(&l, 0, 720, 750, 2222, 100, 300, 4500) == 1 && l.n == 3, "a pair refused");
    CHECK(FlipLine_Lam(&l) != 0 && FlipLine_SafeUs(&l) != 0, "no bound from three samples");
    /* another mode starts over: each part of the key */
    CHECK(FlipLine_Sample(&l, 1, 720, 750, 2222, 100, 300, 4500) == 2 && l.n == 1, "another CRTC kept the samples");
    for (i = 0; i < 3; i++)
        (void) FlipLine_Sample(&l, 1, 720, 750, 2222, 100, 300, 4500);
    CHECK(FlipLine_Sample(&l, 1, 721, 750, 2222, 100, 300, 4500) == 2 && l.n == 1, "another disp kept the samples");
    CHECK(FlipLine_Sample(&l, 1, 721, 751, 2222, 100, 300, 4500) == 2 && l.n == 1, "another vtot kept the samples");
    CHECK(FlipLine_Sample(&l, 1, 721, 751, 2223, 100, 300, 4500) == 2 && l.n == 1, "another htot kept the samples");
    CHECK(FlipLine_Sample(&l, 1, 721, 751, 2224, 100, 120, 4500) == 3 && l.n == 0,
          "another mode with a refused pair kept the samples");
    CHECK(FlipLine_Lam(&l) == 0, "a bound after a mode change");
    CHECK(FlipLine_Sample(&l, 1, 721, 751, 2224, 100, 300, 4500) == 1 && l.n == 1,
          "a first sample after a start-over counted as another start-over");

    /* the bound: random modes, quantised clocks, pre-emption, unseen wraps;
     * and the estimators it must not be (design 5: without the -1, the
     * lower bound inner bracket / (lines + 1)) */
    samples = 0;
    under = 0;
    under_m1 = 0;
    under_low = 0;
    rng = 4242;
    for (mode = 0; mode < 400; mode++)
    {
        vtot = (ULONG) Rand(449.0, 2047.99);
        disp = vtot - 2 - (ULONG) Rand(0.0, (double) vtot * 0.15);
        lt = Rand(7.0, 60.0);
        htot = (ULONG) (lt * 100.0);
        phase = Rand(0.0, lt * (double) vtot);
        FlipLine_Reset(&l);
        for (j = 0; j < 64; j++)
        {
            t0 = ((mode & 1) && (j & 1)) ? wrap - Rand(0.0, 5000.0) : Rand(1.0e6, 5.0e9);
            d1 = Rand(1.0, 60.0);
            if (Rand(0.0, 1.0) < 0.05)
                d1 += Rand(0.0, 2000.0);
            d2 = Rand(1.0, 20.0);
            if (Rand(0.0, 1.0) < 0.05)
                d2 += Rand(0.0, 2000.0);
            gap = Rand(200.0, 2.5 * lt * (double) vtot);
            ta = t0 + d1;
            tb = ta + gap;
            la = LineAt(ta, phase, lt, vtot);
            lb = LineAt(tb, phase, lt, vtot);
            us = EUs(TicksAt(tb + d2) - TicksAt(t0));
            r = FlipLine_Sample(&l, 0, disp, vtot, htot, la, lb, us);
            if (LineTook(r))
            {
                samples++;
                if ((double) LastLam(&l) < lt * 256.0)
                    under++;
                if ((double) CeilDiv(((unsigned long) us + 3UL) * 256UL, lb - la) < lt * 256.0)
                    under_m1++;
                if ((double) EUs(TicksAt(tb) - TicksAt(ta)) * 256.0 / (double) (lb - la + 1) < lt * 256.0)
                    under_low++;
            }
            if (l.n >= FLIP_LINE_TRUST)
            {
                CHECK((double) FlipLine_Lam(&l) >= lt * 256.0, "FlipLine's bound under the line time");
                CHECK((double) FlipLine_SafeUs(&l) >= ((double) vtot + 4.0) * lt + 100.0,
                      "FlipLine_SafeUs under a refresh, the guard and the margin");
            }
        }
    }
    CHECK(under == 0, "a FlipLine sample under the line time");
    CHECK(samples > 2000, "too few FlipLine samples taken");
    CHECK(under_m1 > 0, "the model sees no bound violation without the -1");
    CHECK(under_low > 0, "the model sees no bound violation with the lower-bound estimator");

    /* tight pairs: each read at a line's edge, the clocks right at the
     * reads -- the +3 and the -1 must both matter */
    tight = 0;
    tight_under = 0;
    tight_m1 = 0;
    tight_p3 = 0;
    skipped = 0;
    for (j = 0; j < 200000; j++)
    {
        vtot = (ULONG) Rand(449.0, 2047.99);
        disp = vtot - 2 - (ULONG) Rand(0.0, (double) vtot * 0.15);
        lt = Rand(7.0, 60.0);
        phase = Rand(1.0e6, 1.0e8);
        la = (ULONG) Rand(0.0, (double) disp - 60.0);
        lb = la + 48 + (ULONG) Rand(0.0, (double) (disp - la - 50 < 200 ? disp - la - 50 : 200));
        if (lb >= disp)
        {
            skipped++;
            continue;
        }
        ta = phase + ((double) la + 1.0) * lt - Rand(0.001, 0.3);
        tb = phase + (double) lb * lt + Rand(0.001, 0.3);
        if (LineAt(ta, phase, lt, vtot) != la || LineAt(tb, phase, lt, vtot) != lb)
        {
            skipped++;
            continue;
        }
        us = EUs(TicksAt(tb) - TicksAt(ta));
        FlipLine_Reset(&l);
        if (FlipLine_Sample(&l, 0, disp, vtot, 1, la, lb, us) != 1)
        {
            skipped++;
            continue;
        }
        tight++;
        if ((double) l.lam[0] < lt * 256.0)
            tight_under++;
        if ((double) CeilDiv((unsigned long) us * 256UL, lb - la - 1) < lt * 256.0)
            tight_p3++;
        if ((double) CeilDiv(((unsigned long) us + 3UL) * 256UL, lb - la) < lt * 256.0)
            tight_m1++;
    }
    CHECK(tight_under == 0, "a tight FlipLine sample under the line time");
    CHECK(skipped * 100 < tight, "too many tight pairs skipped");
    CHECK(tight_p3 > 0, "the model sees no bound violation without the +3");
    CHECK(tight_m1 > 0, "the model sees no bound violation without the -1 (tight)");

    /* within 10% after 16 steady samples (the pan, 1.5-4.5 ms, the kick) */
    worst = 1.0;
    rng = 4343;
    for (mode = 0; mode < 200; mode++)
    {
        vtot = (ULONG) Rand(449.0, 2047.99);
        disp = vtot - 2 - (ULONG) Rand(0.0, (double) vtot * 0.15);
        lt = Rand(7.0, 60.0);
        phase = Rand(0.0, lt * (double) vtot);
        FlipLine_Reset(&l);
        taken = 0;
        for (j = 0; j < 4000 && taken < 16; j++)
        {
            t0 = Rand(1.0e6, 1.0e9);
            ta = t0 + Rand(10.0, 30.0);
            tb = ta + Rand(1500.0, 4500.0);
            us = EUs(TicksAt(tb + Rand(1.0, 20.0)) - TicksAt(t0));
            if (LineTook(FlipLine_Sample(&l, 0, disp, vtot, 1, LineAt(ta, phase, lt, vtot),
                                         LineAt(tb, phase, lt, vtot), us)))
                taken++;
        }
        CHECK(taken == 16, "FlipLine: steady pairs refused");
        lam = FlipLine_Lam(&l);
        CHECK((double) lam >= lt * 256.0, "FlipLine's steady bound under the line time");
        CHECK((double) lam <= 1.10 * lt * 256.0, "FlipLine not within 10% after 16 steady samples");
        if ((double) lam / (lt * 256.0) > worst)
            worst = (double) lam / (lt * 256.0);
    }

    /* two bad reads among eight still give a bound, three do not */
    rng = 4444;
    for (mode = 0; mode < 200; mode++)
        for (bad = 2; bad <= 3; bad++)
        {
            vtot = (ULONG) Rand(449.0, 2047.99);
            disp = vtot - 2 - (ULONG) Rand(0.0, (double) vtot * 0.15);
            lt = Rand(7.0, 60.0);
            phase = Rand(1.0e6, 2.0e6);
            FlipLine_Reset(&l);
            for (i = 0; i < bad; i++)
            {
                do
                {
                    pos[i] = (int) Rand(0.0, 7.99);
                    for (k = 0; k < i && pos[k] != pos[i]; k++)
                        ;
                } while (k < i);
            }
            for (j = 0; j < FLIP_LINE_N; j++)
            {
                la = (ULONG) Rand(0.0, (double) disp / 8.0);
                ta = phase + ((double) la + 0.5) * lt;
                tb = ta + (double) disp / 4.0 * lt;
                lb = LineAt(tb, phase, lt, vtot);
                us = EUs(TicksAt(tb + Rand(1.0, 20.0)) - TicksAt(ta - Rand(1.0, 20.0)));
                for (k = 0; k < bad && pos[k] != j; k++)
                    ;
                if (k < bad)
                    lb = la + 2 * (lb - la);    /* a wrong but plausible line: twice the lines */
                r = FlipLine_Sample(&l, 0, disp, vtot, 1, la, lb, us);
                CHECK(LineTook(r), "FlipLine: a pair for the glitch test refused");
                if (k < bad)
                    CHECK((double) LastLam(&l) < lt * 256.0, "a glitch that is not low");
                phase += 50000.0;
            }
            if (bad == 2)
                CHECK((double) FlipLine_Lam(&l) >= lt * 256.0, "two bad reads among eight broke the bound");
            else
                CHECK((double) FlipLine_Lam(&l) < lt * 256.0, "three bad reads among eight left a bound");
        }

    /* 32 bits whatever the host: ULONG is 32 bits here as on the Amiga (the
     * shim), and the products themselves fit (FLIP_BEAM_GUARD was 4UL: on a
     * 64-bit host its long math hid an overflow from the checks after) */
    CHECK(sizeof(ULONG) == 4, "the host's ULONG is not 32 bits: the overflow checks see wider math");
    CHECK((2048.0 + FLIP_BEAM_GUARD) * FLIP_LINE_LAM_MAX + 255.0 < 4294967296.0,
          "FlipLine_SafeUs's product overflows 32 bits at vtot 2048 with the cap");
    CHECK((FLIP_LINE_US_MAX + 3.0) * 256.0 + 2047.0 < 4294967296.0,
          "FlipLine_Sample's numerator overflows 32 bits at FLIP_LINE_US_MAX");
    /* 32 bits: vtot 2047 with the slowest sample a pair may give, and the cap */
    FlipLine_Reset(&l);
    for (i = 0; i < 3; i++)
        (void) FlipLine_Sample(&l, 0, 2000, 2047, 1, 100, 148, FLIP_LINE_US_MAX - 1);
    lam = CeilDiv(((unsigned long) FLIP_LINE_US_MAX - 1UL + 3UL) * 256UL, 47UL);
    CHECK(FlipLine_Lam(&l) == lam, "FlipLine's slowest sample wrong (overflow?)");
    ref = 2051.0 * (double) lam / 256.0 + 100.0;
    safe = FlipLine_SafeUs(&l);
    CHECK((double) safe >= ref && (double) safe < ref + 1.0, "FlipLine_SafeUs wrong at vtot 2047 (overflow?)");
    for (tv = 2047; tv <= 2048; tv++)
    {
        for (i = 0; i < FLIP_LINE_N; i++)
            l.lam[i] = FLIP_LINE_LAM_MAX;
        l.n = FLIP_LINE_N;
        l.vtot = tv;
        ref = ((double) tv + 4.0) * (double) FLIP_LINE_LAM_MAX / 256.0 + 100.0;
        safe = FlipLine_SafeUs(&l);
        CHECK((double) safe >= ref && (double) safe < ref + 1.0, "FlipLine_SafeUs overflows at the cap");
    }
    /* the integer SafeUs against a double */
    rng = 4545;
    for (j = 0; j < 100000; j++)
    {
        FlipLine_Reset(&l);
        l.vtot = (ULONG) Rand(2.0, 2048.99);
        l.n = FLIP_LINE_TRUST + (ULONG) Rand(0.0, (double) (FLIP_LINE_N - FLIP_LINE_TRUST) + 0.99);
        for (i = 0; i < (int) l.n; i++)
        {
            l.lam[i] = (ULONG) Rand(256.0, (double) FLIP_LINE_LAM_MAX);
            v[i] = l.lam[i];
        }
        for (i = 0; i < (int) l.n; i++)
            for (k = i + 1; k < (int) l.n; k++)
                if (v[k] < v[i])
                {
                    tv = v[i];
                    v[i] = v[k];
                    v[k] = tv;
                }
        CHECK(FlipLine_Lam(&l) == v[FLIP_LINE_TRUST - 1], "FlipLine_Lam is not the 3rd smallest");
        ref = ((double) l.vtot + 4.0) * (double) v[FLIP_LINE_TRUST - 1] / 256.0 + 100.0;
        safe = FlipLine_SafeUs(&l);
        CHECK((double) safe >= ref && (double) safe < ref + 1.0, "FlipLine_SafeUs off a double by 1 us or more");
    }
    printf("test_flip: FlipLine: %ld samples (vtot 449-2047, 7-60 us a line, quantised clocks, pre-emption, unseen "
           "wraps) never under the line time; within %.1f%% after 16 steady samples; 2 bad reads of 8 held, 3 broke "
           "it; under the line time without the -1: %ld (tight %ld), without the +3: %ld of %ld tight, the lower-"
           "bound estimator: %ld\n",
           samples, (worst - 1.0) * 100.0, under_m1, tight_m1, tight_p3, tight, under_low);
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
    /* step R (docs/step-r-design.md, TESTS) */
    TestBeamR();
    TestFlipLine();
    TestStartPanBfs();
    TestRunR();
    printf("test_flip: %s (%d checks)\n", failures ? "FAILED" : "OK", checks);
    if (failures)
        printf("test_flip: %d of %d checks FAILED\n", failures, checks);
    return failures ? 1 : 0;
}
