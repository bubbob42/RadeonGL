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

int main(void)
{
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
    printf("test_flip: %s (%d checks)\n", failures ? "FAILED" : "OK", checks);
    if (failures)
        printf("test_flip: %d of %d checks FAILED\n", failures, checks);
    return failures ? 1 : 0;
}
