/*
 * cardflip.c -- triple buffering for the screen kind (2026-11-12,
 * docs/concept-triple-buffering.md). Linked last: GLQuake's hot code keeps
 * its places (the Makefile's notes on code layout).
 *
 * Without it the Radeon draws into a back buffer and the present copies
 * it onto the GL screen. With vertical sync the copy waits in the ring for
 * the beam, and the next flush's locks wait for the ring: the 68k waits
 * for the beam. Without vertical sync the copy lands wherever the beam is:
 * a tear.
 *
 * A program that asks for three buffers (mglChooseNumberOfBuffers(3):
 * GLQuake) gets a GL screen three times as tall, the display clipped to
 * one third. Each frame is drawn straight into a third that is neither
 * shown nor about to be; once it is known finished (the next flush settles
 * it), the display is panned onto it (RasInfo RyOffset + ScrollVPort:
 * Picasso96's SetPanning, the display's start address). The bitmap is on
 * the display all the time; that Picasso96 then never moves it is not
 * documented, so every flush still checks the address its lock returns
 * (card.c). No copy, no wait
 * in the ring. The third a switch left is drawn into again only a refresh
 * period after the switch (FLIP_SAFE_US), so the rate never passes the
 * refresh.
 *
 * MEASURED 2026-11-12 on the A4000D, the first version: separate screen
 * buffers through AllocScreenBuffer/ChangeScreenBuffer on the CUSTOMBITMAP
 * screen Picasso96 opens. ChangeScreenBuffer blocked the 68k ~11 ms a
 * switch (93 ms at most, unexplained), 245 of 980 frames were dropped, and
 * the exit crashed (guru 81000005). The audit the same day
 * (docs/p96-changescreenbuffer-audit.md) found both failures were ours:
 * buffer 0 wrapped scr->RastPort.BitMap -- on a CUSTOMBITMAP screen the
 * BitMap copy Intuition overwrites on every swap (NDK: "Do NOT pass
 * &(screenptr->BitMap)!"), so it aliased the buffer on display -- and our
 * bitmaps were freed before p96CloseScreen while one was still installed.
 * Not Picasso96 moving buffers (unmeasured). Panning one tall screen needs
 * no ScreenBuffers at all: one bitmap, nobody else's to free.
 *
 * The pan runs in a helper task ("RadeonGL triple buffering", one above the
 * game's priority): MEASURED at 1280 x 720, ScrollVPort waits ~7 ms for the
 * beam, which in the game's own task made triple buffering 15% slower than
 * the copy with vertical sync (26.0 against 30.7 fps, demo1). The game asks
 * and goes on; the helper's return from ScrollVPort marks the third it left
 * as off the display -- by the bookkeeping: the CRTC shows it until the next
 * blank (2026-11-14, cure B below: BeamNote, BeamRef, CardFlip_Beam). Without
 * the helper (it did not start)
 * the pan runs in the game's task, the left third safe 20 ms later.
 * 2026-11-13: the helper now runs ONE BELOW the game while panning
 * (RadeonGLHelperPri, EnvHelperPri): ScrollVPort's wait spins until the
 * card's engine has finished the frame, and above the game that spin came
 * out of the game's time. Below it, the pan happens while the game waits:
 * 32.1 fps at 1280 x 720 (vsync copy 29.9, the copy without vsync 32.2),
 * 30.6 at 1920 x 1080 (22.4, 28.7). ChangeScreenBuffer keeps one above.
 *
 * RadeonGLFlip=csb (2026-11-12): the corrected ChangeScreenBuffer way, to
 * be measured against the panning on the card -- the NDK's documented
 * recipe (docs/p96-changescreenbuffer-audit.md, route 1):
 *  - the GL screen is opened by Intuition (OpenScreenTagList with the
 *    card's mode id): on 3.2 Intuition allocates the RTG bitmap itself, so
 *    the screen is not CUSTOMBITMAP; checked, else the copy path;
 *  - buffer 0 is AllocScreenBuffer(SB_SCREEN_BITMAP), buffers 1 and 2
 *    AllocScreenBuffer(NULL): Intuition owns all three bitmaps;
 *  - each DBufInfo's SafeMessage and DispMessage have a port, each message
 *    is taken back to its buffer; a switch waits until the last one was
 *    seen at least once (its DispMessage) and never reuses a DBufInfo with
 *    a message outstanding (graphics.doc ChangeVPBitMap);
 *  - ChangeScreenBuffer runs in the helper task (refused: tried again
 *    after a WaitTOF for a second, then given up and the bookkeeping turned
 *    back); the helper then waits for that switch's SafeMessage itself and
 *    only then reports the switch done, so the GL task never waits on a
 *    port, it only reads a flag (Tim's advice: the buffer wait in a helper
 *    too; his Balls demo, a single task, does the same protocol: Intuition
 *    screen, SB_SCREEN_BITMAP + two Intuition buffers, SafeMessage before
 *    drawing, WaitTOF between refused switches, FreeScreenBuffer x 3 and
 *    CloseScreen);
 *  - the end: the helper stopped, every message in, the input window
 *    closed (card.c: its layer is on the first bitmap), FreeScreenBuffer
 *    for all three, CloseScreen. A buffer whose message never came is not
 *    freed (counted as leaked, its ports kept);
 *  - every buffer's address at every lock is compared with its last one:
 *    the log says whether Picasso96 moved a screen buffer (unmeasured: the
 *    first version's "moves" were its own buffer-0 aliasing).
 *
 * 2026-11-13, RadeonGLFlip=csbnowait, pipelined (Tim's Balls scheme,
 * measured with docs/hw/a4000d/CsbBench PIPE): Radeon.card returns from
 * ChangeScreenBuffer at the blank where the switch took effect and sends
 * both messages a refresh later. Waiting for them (the DispMessage gate
 * above, and the helper's SafeMessage wait) made every switch cost two
 * refreshes: 30 a second at most. csbnowait counts a switch done when its
 * call returned; the next may follow at once, while the buffer it left
 * waits for its SafeMessage (cardflip_sm.h pipe). A DBufInfo is still never
 * reused with a message out. CsbBench, nothing drawn: 60.1 switches a
 * second at 1280 x 720 against 30.0.
 * UNSAFE (Thomas Richter, Picasso96, 2026-11-13): calling ChangeVPBitMap
 * again before the last DispMessage came is outside graphics.doc's
 * contract and not safe on Picasso96. So it is never the default: only a
 * program or user that asks for csbnowait gets it; csb keeps the gate.
 *
 * The cost: a frame is shown one flush later than the copy showed it.
 * RadeonGLBuffers=2 keeps the copy path for a program that asked for three
 * (the benchmarks: their series stay comparable); =3 gives three to one
 * that did not ask (its status bar may flicker: GLQuake redraws its bar
 * for as many frames as it believes there are buffers).
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <dos/dostags.h>
#include <devices/timer.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/view.h>
#include <libraries/Picasso96.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/timer.h>
#include <proto/Picasso96.h>
#include <libraries/minigl_dispatch.h>

#include "rgl_dispatch.h"
#include "card.h"
#define FLIP_SM_FUNCTIONS
#include "cardflip.h"
#include "r200reg.h"
#include "r2emit.h"

#define NONE CARD_FLIP_NONE

ULONG Card_Buffers = 0;

static ULONG flip_freq = 709379;    /* the E clock's, from the open */

void Hook_mglChooseNumberOfBuffers(int number)
{
    Card_Buffers = (number == 2 || number == 3) ? (ULONG) number : 0UL;
}

/* --- the clock (as card.c's) ------------------------------------------------------------ */

static ULONG FlipTicks(void)
{
    struct EClockVal ev;

    ReadEClock(&ev);
    return ev.ev_lo;
}

static ULONG FlipUs(ULONG t)
{
    ULONG s;
    ULONG r;
    ULONG ms;
    ULONG rest;

    s = t / flip_freq;
    r = t - s * flip_freq;
    ms = (r * 1000UL) / flip_freq;
    rest = (r * 1000UL) - ms * flip_freq;
    return s * 1000000UL + ms * 1000UL + (rest * 1000UL) / flip_freq;
}

static void FlipWhy(struct CardFlip *f, const char *s)
{
    ULONG i;

    for (i = 0; s[i] != 0 && i + 1 < sizeof(f->why); i++)
        f->why[i] = s[i];
    f->why[i] = 0;
}

/* RadeonGLBuffers: 2 (or 1) the copy path, 3 three buffers; 0 unset */
static ULONG EnvBuffers(void)
{
    char flag[4];

    if (GetVar((STRPTR) "RadeonGLBuffers", (STRPTR) flag, sizeof(flag), 0) > 0)
    {
        if (flag[0] == '1' || flag[0] == '2')
            return 2;
        if (flag[0] == '3')
            return 3;
    }
    return 0;
}

/* RadeonGLFlip: "csb" (or "csbnowait") ChangeScreenBuffer, else the panning */
static ULONG EnvFlip(void)
{
    char flag[8];

    if (GetVar((STRPTR) "RadeonGLFlip", (STRPTR) flag, sizeof(flag), 0) > 0
        && (flag[0] == 'c' || flag[0] == 'C'))
        return CARD_FLIP_CSB;
    return CARD_FLIP_PAN;
}

/* RadeonGLFlip=csbnowait: ChangeScreenBuffer without waiting for the
 * DispMessage (pipelined; UNSAFE on Picasso96, above); csb: the gate */
static ULONG EnvCsbPipe(void)
{
    char flag[12];

    if (GetVar((STRPTR) "RadeonGLFlip", (STRPTR) flag, sizeof(flag), 0) > 3
        && (flag[0] == 'c' || flag[0] == 'C') && (flag[3] == 'n' || flag[3] == 'N'))
        return 1;
    return 0;
}

/* The program's wish now (2026-11-12, GLQuake's Buffering item switches it
 * live): RadeonGLBuffers, else mglChooseNumberOfBuffers; 0 = none said */
static ULONG Wish(void)
{
    ULONG w;

    w = EnvBuffers();
    if (w == 0)
        w = Card_Buffers;
    return w;
}

/* The state, once a program wants three buffers (else NULL) */
static struct CardFlip *Want(struct Card *k)
{
    struct CardFlip *f;
    struct EClockVal ev;
    ULONG want;

    f = k->flip;
    if (f != NULL)
        return f;
    want = EnvBuffers();
    if (want == 0)
        want = Card_Buffers;
    if (want != 3)
        return NULL;
    f = (struct CardFlip *) AllocVec(sizeof(*f), MEMF_ANY | MEMF_CLEAR);
    if (f == NULL)
        return NULL;
    k->flip = f;
    f->want = want;
    f->sig = -1;
    f->last_shown = NONE;
    flip_freq = ReadEClock(&ev);
    if (flip_freq == 0)
        flip_freq = 709379;
    return f;
}

/* --- the third a switch left ------------------------------------------------------------- */

/* Off the display by the bookkeeping once the helper's pan returned (on show
 * until the next blank: cure B guards the first frame drawn into it), or,
 * panning in the game's task, a refresh period after the switch */
static void Expire(struct CardFlip *f)
{
    if (f->sm.leave == NONE)
        return;
    if (f->helper != NULL ? (f->done == f->asked)
                          : (FlipUs(FlipTicks() - f->switched) >= FLIP_SAFE_US))
        FlipSm_Safe(&f->sm);
}

/* Wait for the helper's last pan, 100 ms at most */
static void WaitPan(struct Card *k, struct CardFlip *f)
{
    struct timerequest *t;

    if (f->done == f->asked || f->sig < 0)
        return;
    t = k->treq;
    t->tr_node.io_Command = TR_ADDREQUEST;
    t->tr_time.tv_secs = 0;
    t->tr_time.tv_micro = 100000;
    SendIO((struct IORequest *) t);
    while (f->done != f->asked && f->alive)
    {
        (void) Wait((1UL << f->sig) | (1UL << k->tport->mp_SigBit));
        if (CheckIO((struct IORequest *) t) != NULL)
            break;
    }
    if (CheckIO((struct IORequest *) t) == NULL)
        AbortIO((struct IORequest *) t);
    WaitIO((struct IORequest *) t);
}

/* The flush must draw into the third the last switch left before a
 * refresh period has passed: wait out the rest (timer.device) */
static void WaitSafe(struct Card *k, struct CardFlip *f)
{
    struct timerequest *t;
    ULONG t0;
    ULONG us;
    ULONG gone;

    f->safe_waits++;
    t0 = FlipTicks();
    gone = FlipUs(t0 - f->switched);
    if (f->helper != NULL)
        WaitPan(k, f);
    else if (gone < FLIP_SAFE_US)
    {
        t = k->treq;
        t->tr_node.io_Command = TR_ADDREQUEST;
        t->tr_time.tv_secs = 0;
        t->tr_time.tv_micro = FLIP_SAFE_US - gone;
        DoIO((struct IORequest *) t);
    }
    FlipSm_Safe(&f->sm);
    us = FlipUs(FlipTicks() - t0);
    if (us > f->wait_us_max)
        f->wait_us_max = us;
    f->wait_us_sum += us;
}

/* Where the beam is right after a pan (read only): CRTC_VLINE_CRNT_VLINE
 * (0x0210, bits 16-26) against the displayed lines (CRTC_V_TOTAL_DISP), and
 * CRTC_OFFSET's GUI_TRIG_OFFSET bit 30 (an offset update pending) */
#define CF_CRTC_VLINE      0x0210UL
#define CF_OFFSET_PENDING  (1UL << 30)

static void PanBeam(struct CardFlip *f)
{
    ULONG line;
    ULONG disp;

    line = (Card_ReadReg(f->card, CF_CRTC_VLINE) >> 16) & 0x7ffUL;
    if (Card_ReadReg(f->card, R_CRTC_OFFSET) & CF_OFFSET_PENDING)
        f->pan_pending++;
    disp = ((Card_ReadReg(f->card, R_CRTC_V_TOTAL_DISP) >> 16) & 0x7ffUL) + 1;
    if (line >= disp)
        f->pan_in_blank++;
    else
        f->pan_eighths[(line * 8) / disp]++;
}

/* The display onto third i */
static void Pan(struct Card *k, ULONG i)
{
    struct ViewPort *vp;

    vp = &k->scr->ViewPort;
    vp->RasInfo->RxOffset = 0;
    vp->RasInfo->RyOffset = (WORD) (i * k->height);
    ScrollVPort(vp);
}

/* --- the beam after a pan (2026-11-14, docs/concept-panning-tear.md, cure B) ---------------- */

/*
 * MEASURED by eye 2026-11-14 on the A4000D (GLQuake demo1, 1024 x 768 and
 * 1280 x 720): with the helper one below the game the picture splits, with
 * it one above (RadeonGLHelperPri 1: the pan ~7 ms before the next kick) it
 * does not. The CRTC takes ScrollVPort's new offset at the next vertical
 * blank; until then the third the pan left is scanned out, and the next
 * frame was kicked into it ~2 ms after the pan. "The pan returned" never
 * meant "off the display".
 *
 * The cure: the first flush that draws into a third a pan left puts the
 * Radeon's own wait for the blank (R2E_WaitVline, the vsync copy's packets:
 * the engine's wait window and WAIT_UNTIL, nothing that changes the
 * picture) in front of its commands, unless a
 * blank is known to have passed since the pan (cardflip_sm.h, FlipBeam_*).
 * The 68k goes on; it waits (timer.device) only for a reference (the
 * helper's line was in the blank: 1 ms polls, at most CF_BEAM_POLLS) or,
 * with no beam at all, for the rest of FLIP_BEAM_AGED_US.
 */

#define CF_CRTC2_VLINE        0x0310UL
#define CF_CRTC2_OFFSET_CNTL  0x0328UL
/* CRTC_OFFSET's address bits (above them GUI_TRIG_OFFSET and its like) */
#define CF_OFFSET_MASK        0x07ffffffUL
/* 1 ms sleeps for a line in the picture (the longest blank the Radeon's
 * modes have is ~3 ms) */
#define CF_BEAM_POLLS         8

/* The CRTC that scans the GL screen out, its displayed and total lines and
 * the beam's line now (read only; the CRTC chosen as card.c's VsyncWindow
 * chooses it). FALSE: no CRTC is on, or its lines do not fit the Radeon's
 * wait */
static BOOL BeamRead(struct Card *k, ULONG *crtc, ULONG *disp, ULONG *line, ULONG *vtot)
{
    ULONG v;
    ULONG c;

    if (Card_ReadReg(k, R_CRTC_GEN_CNTL) & R_CRTC_EN)
        c = 0;
    else if (Card_ReadReg(k, R_CRTC2_GEN_CNTL) & R_CRTC_EN)
        c = 1;
    else
        return FALSE;
    v = Card_ReadReg(k, c ? R_CRTC2_V_TOTAL_DISP : R_CRTC_V_TOTAL_DISP);
    *crtc = c;
    *disp = ((v >> R_CRTC_V_DISP_SHIFT) & R_CRTC_V_DISP_MASK) + 1;
    *vtot = (v & R_CRTC_V_DISP_MASK) + 1;       /* CRTC_V_TOTAL, bits 0-10 */
    *line = (Card_ReadReg(k, c ? CF_CRTC2_VLINE : CF_CRTC_VLINE) >> 16) & 0x7ffUL;
    return (BOOL) (*disp >= 2 && *disp <= R_TRIG_VLINE_LINE_MASK && *disp < *vtot);
}

/* us on timer.device (the flush's request: nothing else of it is out) */
static void FlipSleep(struct Card *k, ULONG us)
{
    struct timerequest *t;

    t = k->treq;
    t->tr_node.io_Command = TR_ADDREQUEST;
    t->tr_time.tv_secs = 0;
    t->tr_time.tv_micro = us;
    DoIO((struct IORequest *) t);
}

/* The rest of the beam's time rule since the E clock read since */
static void SleepRest(struct Card *k, ULONG since)
{
    ULONG gone;

    gone = FlipUs(FlipTicks() - since);
    if (gone < FLIP_BEAM_AGED_US)
        FlipSleep(k, FLIP_BEAM_AGED_US - gone);
}

/* The helper, right after its pan onto third to: the beam, as the
 * reference of the third the display was on; and whether ScrollVPort wrote
 * the offset before it returned (o1, o2: CRTC_OFFSET and CRTC2_OFFSET
 * before the call) -- the rule's premise, counted for the log */
static void BeamNote(struct CardFlip *f, ULONG to, ULONG o1, ULONG o2)
{
    ULONG crtc;
    ULONG disp;
    ULONG line;
    ULONG vtot;
    ULONG left;
    BOOL ok;

    crtc = 0;
    disp = 0;
    line = 0;
    vtot = 0;
    ok = BeamRead(f->card, &crtc, &disp, &line, &vtot);
    left = f->h_shown;
    f->h_shown = to;
    if (left >= CARD_FLIP_N || left == to)
        return;
    f->h_ok[left] = (ok && FlipBeam_RefOk(line, disp)) ? 1UL : 0UL;
    f->h_line[left] = line;
    f->h_disp[left] = disp;
    f->h_crtc[left] = crtc;
    f->h_ticks[left] = FlipTicks();
    if (ok)
    {
        f->pan_crtc = crtc;
        f->pan_off0 = crtc ? o2 : o1;
        f->pan_off1 = Card_ReadReg(f->card, crtc ? R_CRTC2_OFFSET : R_CRTC_OFFSET);
        f->pan_cntl = Card_ReadReg(f->card, crtc ? CF_CRTC2_OFFSET_CNTL : R_CRTC_OFFSET_CNTL);
        f->pan_cmp++;
        if (((f->pan_off0 ^ f->pan_off1) & CF_OFFSET_MASK) == 0)
            f->pan_same++;
    }
}

/* The Radeon's wait for the blank of this CRTC: built once a mode and, when
 * the checker runs, through it (the pair alone leaves the checker's state
 * as it was) */
static BOOL BeamPacket(struct Card *k, struct CardFlip *f, ULONG crtc, ULONG disp)
{
    struct R2Cs cs;
    struct R2ChkError err;

    if (f->pkt_ok && f->pkt_crtc == crtc && f->pkt_disp == disp)
        return TRUE;
    f->pkt_ok = 0;
    R2Cs_InitBuffer(&cs, f->beam_pkt, 4, 1);
    (void) R2E_WaitVline(&cs, crtc, 0, disp);
    if (cs.error != R2_OK || cs.ptr != f->beam_pkt + 4)
        return FALSE;
    if (k->check && R2Chk_Run(&k->cfg, &k->chk, f->beam_pkt, 4, 1, 0, &err) != R2CE_OK)
    {
        f->beam_refused++;
        return FALSE;
    }
    f->pkt_crtc = crtc;
    f->pkt_disp = disp;
    f->pkt_ok = 1;
    return TRUE;
}

/* CardFlip_Safe: this flush draws into a third a pan left, no verdict
 * since. The reference -- the helper's line, or (it read in the blank) a
 * later one read here -- and the Radeon's wait, for CardFlip_Beam to decide
 * with at the kick; or the verdict now */
static void BeamRef(struct Card *k, struct CardFlip *f)
{
    ULONG d;
    ULONG crtc;
    ULONG disp;
    ULONG line;
    ULONG vtot;
    ULONG i;
    BOOL have;

    d = f->sm.draw;
    if ((LONG) (f->done - f->left_seq[d]) < 0)
    {
        /* its pan has not come back (WaitPan gave up): nothing is known,
         * the next flush asks again */
        f->beam_out++;
        return;
    }
    f->pan_ticks = f->h_ticks[d];
    if (FlipUs(FlipTicks() - f->pan_ticks) >= FLIP_BEAM_AGED_US)
    {
        f->beam_left &= ~(1UL << d);
        f->beam_aged++;
        return;
    }
    crtc = f->h_crtc[d];
    disp = f->h_disp[d];
    line = f->h_line[d];
    have = (BOOL) (f->h_ok[d] != 0);
    if (!have)
    {
        f->beam_late++;
        for (i = 0; i < CF_BEAM_POLLS && !have; i++)
        {
            if (!BeamRead(k, &crtc, &disp, &line, &vtot))
                break;
            if (FlipBeam_RefOk(line, disp))
                have = TRUE;
            else
            {
                f->beam_polls++;
                FlipSleep(k, 1000);
            }
        }
    }
    if (have && !BeamPacket(k, f, crtc, disp))
        have = FALSE;
    if (!have)
    {
        /* no beam to go by: the refresh period, in this task */
        f->beam_timed++;
        SleepRest(k, f->pan_ticks);
        f->beam_left &= ~(1UL << d);
        return;
    }
    f->ref_line = line;
    f->beam_third = d;
    f->beam_go = 1;
}

/* --- the ChangeScreenBuffer way (RadeonGLFlip=csb) ------------------------------------------ */

/* The buffer a DBufInfo message belongs to (by its address), or NONE */
static ULONG BufOf(struct CardFlip *f, struct Message *m, BOOL safe)
{
    ULONG i;

    for (i = 0; i < CARD_FLIP_N; i++)
        if (f->sb[i] != NULL
            && m == (safe ? &f->sb[i]->sb_DBufInfo->dbi_SafeMessage : &f->sb[i]->sb_DBufInfo->dbi_DispMessage))
            return i;
    return NONE;
}

/* The replies that came: graphics.library sends them back to us, so they
 * are only taken off the port, never replied */
static void CollectSafe(struct CardFlip *f)
{
    struct Message *m;
    ULONG i;

    if (f->sport == NULL)
        return;
    while ((m = GetMsg(f->sport)) != NULL)
        if ((i = BufOf(f, m, TRUE)) != NONE)
            f->safe_out[i] = 0;
}

static void CollectDisp(struct CardFlip *f)
{
    struct Message *m;
    ULONG i;

    if (f->dport == NULL)
        return;
    while ((m = GetMsg(f->dport)) != NULL)
        if ((i = BufOf(f, m, FALSE)) != NONE)
            f->disp_out[i] = 0;
}

/* Show buffer i (the helper, or the GL task without one). First the gate:
 * the last switch seen at least once (its DispMessage: graphics.doc
 * AllocDBufInfo) and no message of buffer i's own DBufInfo outstanding
 * (never reuse one with a message out: ChangeVPBitMap NOTES). Intuition
 * refuses a switch while it is busy (menus, a window dragged): tried again
 * for about a second. TRUE: it took. */
static BOOL CsbShow(struct CardFlip *f, ULONG i)
{
    ULONG n;
    ULONG prev;
    ULONG t0;
    ULONG us;

    for (n = 0; n < 100 && !f->quit; n++)
    {
        CollectDisp(f);
        if (f->sm.pipe)
        {
            /* pipelined: only buffer i's own DBufInfo must be home */
            CollectSafe(f);
            if (!f->safe_out[i] && !f->disp_out[i])
                break;
            if (n == 0)
                f->disp_waits++;
            Delay(1);
            continue;
        }
        prev = f->last_shown;
        if ((prev == NONE || !f->disp_out[prev]) && !f->safe_out[i] && !f->disp_out[i])
            break;
        if (n == 0)
            f->disp_waits++;
        Delay(1);
    }
    if (n == 100 || f->quit)
        return FALSE;
    for (n = 0; n < 50 && !f->quit; n++)
    {
        /* outstanding before the call: the messages may come at once */
        f->safe_out[i] = 1;
        f->disp_out[i] = 1;
        t0 = FlipTicks();
        if (ChangeScreenBuffer(f->card->scr, f->sb[i]))
        {
            us = FlipUs(FlipTicks() - t0);
            if (us > f->call_us_max)
                f->call_us_max = us;
            f->call_us_sum += us;
            f->last_shown = i;
            return TRUE;
        }
        f->safe_out[i] = 0;         /* refused: no message will come */
        f->disp_out[i] = 0;
        f->retries++;
        WaitTOF();                  /* as Tim's Balls does */
    }
    return FALSE;
}

/* The buffer the last switch left: safe once that switch took and its
 * SafeMessage came (graphics.doc: "replied to when it is safe to write to
 * the old BitMap") */
static void CsbExpire(struct CardFlip *f)
{
    ULONG p;

    if (!f->adopted)
        CollectSafe(f);             /* else the helper takes the messages */
    if (f->sm.pipe)
    {
        if (!f->adopted)
            CollectDisp(f);
        /* the switch in flight: its call returned (or was given up) */
        if (f->sm.inflight && f->done == f->asked)
        {
            if (!f->csb_ok)
            {
                FlipSm_Unshow(&f->sm);
                f->given_up++;
            }
            else
                FlipSm_Done(&f->sm);
        }
        /* each left buffer: safe once the switch away from it is done and
         * that switch's SafeMessage came */
        for (p = 0; p < CARD_FLIP_N; p++)
            if ((f->sm.pend & (1UL << p)) != 0 && !(p == f->sm.leave && f->sm.inflight)
                && f->sm.freed_by[p] < CARD_FLIP_N && !f->safe_out[f->sm.freed_by[p]])
                FlipSm_SafeBuf(&f->sm, p);
        return;
    }
    if (f->sm.leave == NONE || f->done != f->asked)
        return;
    if (!f->csb_ok)
    {
        FlipSm_Unshow(&f->sm);      /* the display still on the buffer left */
        f->given_up++;
        return;
    }
    if (!f->safe_out[f->sm.show])
        FlipSm_Safe(&f->sm);
}

/* Wait for it: the helper's return and the SafeMessage. Given up after
 * 5 s (counted): the flush goes on, the DBufInfo stays outstanding and is
 * neither reused nor freed while its message is out. */
static void CsbWaitSafe(struct Card *k, struct CardFlip *f)
{
    struct timerequest *t;
    ULONG mask;
    ULONG t0;
    ULONG us;
    ULONG n;

    f->safe_waits++;
    t0 = FlipTicks();
    t = k->treq;
    mask = 1UL << k->tport->mp_SigBit;
    if (!f->adopted)
        mask |= 1UL << f->own_sbit;  /* the SafeMessage itself */
    if (f->sig >= 0)
        mask |= 1UL << f->sig;      /* the helper: the switch done and safe */
    for (n = 0; n < 50; n++)
    {
        CsbExpire(f);
        if (!FlipSm_MustWait(&f->sm))
            break;
        t->tr_node.io_Command = TR_ADDREQUEST;
        t->tr_time.tv_secs = 0;
        t->tr_time.tv_micro = 100000;
        SendIO((struct IORequest *) t);
        while (CheckIO((struct IORequest *) t) == NULL)
        {
            CsbExpire(f);
            if (!FlipSm_MustWait(&f->sm))
                break;
            (void) Wait(mask);
        }
        if (CheckIO((struct IORequest *) t) == NULL)
            AbortIO((struct IORequest *) t);
        WaitIO((struct IORequest *) t);
    }
    if (FlipSm_MustWait(&f->sm))
    {
        if (f->sm.pipe)
            FlipSm_SafeBuf(&f->sm, f->sm.draw);
        else
            FlipSm_Safe(&f->sm);
        f->safe_forced++;
    }
    us = FlipUs(FlipTicks() - t0);
    if (us > f->wait_us_max)
        f->wait_us_max = us;
    f->wait_us_sum += us;
}

/* The helper takes the ports over (their signals to it): it waits for the
 * SafeMessage itself. Without a signal of its own the ports stay the GL
 * task's (CsbExpire takes the messages, the helper only switches). */
static void Adopt(struct CardFlip *f)
{
    f->hsig = AllocSignal(-1);
    if (f->hsig < 0)
        return;
    Disable();                      /* a reply may come from an interrupt */
    f->sport->mp_SigTask = FindTask(NULL);
    f->sport->mp_SigBit = (UBYTE) f->hsig;
    f->dport->mp_SigTask = f->sport->mp_SigTask;
    f->dport->mp_SigBit = (UBYTE) f->hsig;
    Enable();
    f->adopted = 1;
}

/* ... and gives them back as it ends (DeleteMsgPort frees the GL task's
 * bits; a message from now on is taken by CsbUnbuffer, which polls) */
static void GiveBack(struct CardFlip *f)
{
    if (!f->adopted)
        return;
    Disable();
    f->sport->mp_SigTask = f->port_task;
    f->sport->mp_SigBit = (UBYTE) f->own_sbit;
    f->dport->mp_SigTask = f->port_task;
    f->dport->mp_SigBit = (UBYTE) f->own_dbit;
    Enable();
    f->adopted = 0;
    FreeSignal(f->hsig);
    f->hsig = -1;
}

/* The helper, after a switch to i took: its SafeMessage (the buffer it
 * left may be drawn into). No time limit: the GL task gives up after 5 s
 * (CsbWaitSafe), the quit (CTRL-C) ends the wait. */
static void HelperSafe(struct CardFlip *f, ULONG i)
{
    ULONG t0;
    ULONG us;

    t0 = FlipTicks();
    for (;;)
    {
        CollectSafe(f);
        CollectDisp(f);
        if (!f->safe_out[i] || f->quit)
            break;
        (void) Wait((1UL << f->hsig) | SIGBREAKF_CTRL_C);
    }
    us = FlipUs(FlipTicks() - t0);
    if (us > f->hsafe_us_max)
        f->hsafe_us_max = us;
    f->hsafe_us_sum += us;
    f->hsafe_n++;
}

/* A buffer's address at a lock against its last one */
static void Seen(struct CardFlip *f, ULONG i, ULONG gpu)
{
    if (f->last_gpu[i] != 0 && f->last_gpu[i] != gpu)
        f->moved[i]++;
    f->last_gpu[i] = gpu;
}

/* The helper: waits for CTRL-F (a pan asked for) or the quit; pans; marks
 * it done and signals the game. Started by CardFlip_Open, which sets
 * tc_UserData before the first CTRL-F. */
static void FlipTask(void)
{
    struct CardFlip *f;
    ULONG seq;
    ULONG t0;
    ULONG us;
    ULONG to;
    ULONG o1;
    ULONG o2;

    ULONG i;
    BOOL ok;

    (void) Wait(SIGBREAKF_CTRL_F);
    f = (struct CardFlip *) FindTask(NULL)->tc_UserData;
    if (f->mode == CARD_FLIP_CSB)
        Adopt(f);
    while (!f->quit)
    {
        while (!f->quit && (seq = f->asked) != f->done)
        {
            if (f->mode == CARD_FLIP_CSB)
            {
                i = f->pan_to;
                ok = CsbShow(f, i);         /* times itself */
                if (ok && f->adopted && !f->sm.pipe)
                    HelperSafe(f, i);       /* done = switched AND the left buffer safe */
                f->csb_ok = ok ? 1UL : 0UL; /* pipe: done = the call returned */
            }
            else
            {
                to = f->pan_to;
                o1 = Card_ReadReg(f->card, R_CRTC_OFFSET);
                o2 = Card_ReadReg(f->card, R_CRTC2_OFFSET);
                t0 = FlipTicks();
                Pan(f->card, to);
                us = FlipUs(FlipTicks() - t0);
                if (us > f->call_us_max)
                    f->call_us_max = us;
                f->call_us_sum += us;
                PanBeam(f);
                BeamNote(f, to, o1, o2);    /* before done: the game reads it then */
            }
            f->done = seq;
            Signal(f->owner, 1UL << f->sig);
        }
        if (!f->quit)
        {
            if (f->mode == CARD_FLIP_CSB && f->sm.pipe && f->adopted)
            {
                /* pipelined: the messages come while the helper idles; it
                 * takes them and wakes the GL task (CsbWaitSafe) */
                (void) Wait(SIGBREAKF_CTRL_F | SIGBREAKF_CTRL_C | (1UL << f->hsig));
                CollectSafe(f);
                CollectDisp(f);
                Signal(f->owner, 1UL << f->sig);
            }
            else
                (void) Wait(SIGBREAKF_CTRL_F | SIGBREAKF_CTRL_C);
        }
    }
    if (f->mode == CARD_FLIP_CSB)
        GiveBack(f);
    Forbid();                       /* gone before the owner frees f */
    f->alive = 0;
    Signal(f->owner, 1UL << f->sig);
}

/* RadeonGLHelperPri: the helper's priority against the GL task's, -9..9.
 * 2026-11-13: ChangeScreenBuffer spins ~6 ms a call (CsbBench CPU) and
 * ScrollVPort waits, spinning, for the card's engine to finish the frame
 * (6.9 ms at 1280 x 720, 15 ms at 1920 x 1080); a helper above the game
 * takes that from the game, one below it only gets the game's waits.
 * MEASURED (tb_fps6.log, GLQuake demo1): panning at -1 32.1 fps at
 * 1280 x 720 and 30.6 at 1920 x 1080 (+1: 26.6 and 21.9; the copy without
 * vsync 32.2 and 28.7), its ScrollVPort 0.4 ms; ChangeScreenBuffer at -1
 * 16.3 and 14.5 (+1: 21.6 and 17.0: its calls start late and miss blanks).
 * So the default: -1 panning, +1 ChangeScreenBuffer. */
static LONG EnvHelperPri(LONG dflt)
{
    char v[8];
    LONG n;
    LONG sign;
    int i;

    if (GetVar((STRPTR) "RadeonGLHelperPri", (STRPTR) v, sizeof(v), 0) <= 0)
        return dflt;
    i = 0;
    sign = 1;
    if (v[0] == '-' || v[0] == '+')
    {
        sign = (v[0] == '-') ? -1 : 1;
        i = 1;
    }
    if (v[i] < '0' || v[i] > '9')
        return dflt;
    n = v[i] - '0';
    return sign * n;
}

/* The helper and the owner's signal, once per context (the GL task) */
static void StartHelper(struct CardFlip *f)
{
    struct TagItem t[6];

    if (f->helper != NULL)
        return;
    f->owner = FindTask(NULL);
    if (f->sig < 0)
        f->sig = AllocSignal(-1);
    if (f->sig < 0)
        return;
    f->quit = 0;
    f->asked = 0;
    f->done = 0;
    t[0].ti_Tag = NP_Entry;     t[0].ti_Data = (ULONG) FlipTask;
    t[1].ti_Tag = NP_Name;      t[1].ti_Data = (ULONG) "RadeonGL triple buffering";
    f->helper_pri = EnvHelperPri((f->mode == CARD_FLIP_CSB) ? 1 : -1);
    t[2].ti_Tag = NP_Priority;  t[2].ti_Data = (ULONG) (LONG) (f->owner->tc_Node.ln_Pri + f->helper_pri);
    t[3].ti_Tag = NP_StackSize; t[3].ti_Data = 8192;
    t[4].ti_Tag = NP_Output;    t[4].ti_Data = 0;
    t[5].ti_Tag = TAG_DONE;     t[5].ti_Data = 0;
    f->alive = 1;
    f->helper = CreateNewProc(t);
    if (f->helper == NULL)
    {
        f->alive = 0;
        return;
    }
    f->helper->pr_Task.tc_UserData = (APTR) f;
    Signal(&f->helper->pr_Task, SIGBREAKF_CTRL_F);
}

/* The helper ends (the pan in flight first); the owner's signal back */
static void StopHelper(struct CardFlip *f)
{
    ULONG i;

    if (f->helper != NULL)
    {
        f->quit = 1;
        Signal(&f->helper->pr_Task, SIGBREAKF_CTRL_C);
        for (i = 0; i < 100 && f->alive; i++)
            Delay(1);
        f->helper = NULL;
    }
    if (f->sig >= 0 && FindTask(NULL) == f->owner)
    {
        FreeSignal(f->sig);
        f->sig = -1;
    }
}

/* --- open and close ---------------------------------------------------------------------- */

BOOL CardFlip_Tall(struct Card *k)
{
    struct CardFlip *f;

    f = Want(k);
    if (f == NULL)
        return FALSE;
    if (Wish() == 2)
    {
        f->mode = 0;                /* two buffers again (a live switch): the copy */
        FlipWhy(f, "the program switched to two buffers");
        return FALSE;
    }
    f->mode = CARD_FLIP_PAN;
    f->tall = 1;
    return TRUE;
}

BOOL CardFlip_Csb(struct Card *k)
{
    struct CardFlip *f;

    f = Want(k);
    if (f == NULL)
        return FALSE;
    if (Wish() == 2)
    {
        f->mode = 0;
        f->tall = 0;
        FlipWhy(f, "the program switched to two buffers");
        return FALSE;
    }
    f->mode = EnvFlip();
    f->tall = 0;
    return (BOOL) (f->mode == CARD_FLIP_CSB);
}

BOOL CardFlip_Changed(struct Card *k)
{
    ULONG w;
    ULONG want;
    ULONG now;

    w = Wish();
    if (w == 0)
        w = (k->flip != NULL) ? k->flip->want : 2;
    want = (w == 3) ? EnvFlip() : 0;
    now = (k->flip != NULL && k->flip_on) ? k->flip->mode : 0;
    /* csb and csbnowait differ too: the screen reopens (the bookkeeping
     * starts over in the other way) */
    if (want == CARD_FLIP_CSB && now == CARD_FLIP_CSB && EnvCsbPipe() != k->flip->sm.pipe)
        return TRUE;
    return (BOOL) (want != now);
}

void CardFlip_CsbFailed(struct Card *k, const char *why)
{
    if (k->flip == NULL)
        return;
    k->flip->mode = 0;              /* neither way: the copy path */
    FlipWhy(k->flip, why);
}

void CardFlip_TallFailed(struct Card *k)
{
    if (k->flip == NULL)
        return;
    k->flip->tall = 0;
    FlipWhy(k->flip, "a GL screen three times as tall did not open");
}

/* The end of the ChangeScreenBuffer way: the helper stopped, every message
 * in, FreeScreenBuffer for every buffer whose messages came (all three, the
 * one shown too: intuition.doc ChangeScreenBuffer NOTES), the ports. The
 * GL task, the Radeon idle, the input window closed (card.c), before
 * CloseScreen. */
static void CsbUnbuffer(struct Card *k, struct CardFlip *f)
{
    ULONG i;
    ULONG n;
    ULONG out;

    StopHelper(f);
    for (n = 0; n < 50; n++)
    {
        CollectSafe(f);
        CollectDisp(f);
        out = 0;
        for (i = 0; i < CARD_FLIP_N; i++)
            out |= f->safe_out[i] | f->disp_out[i];
        if (out == 0)
            break;
        Delay(1);
    }
    /* the installed bitmap survives its FreeScreenBuffer (CloseScreen frees
     * it): the screen's bitmap from now on, for the copy path or the close */
    if (f->last_shown != NONE && f->sb[f->last_shown] != NULL)
    {
        k->friend = f->sb[f->last_shown]->sb_BitMap;
        if (k->b[CARD_SCREEN].bm != NULL)
            k->b[CARD_SCREEN].bm = k->friend;
    }
    out = 0;
    for (i = 0; i < CARD_FLIP_N; i++)
    {
        if (f->sb[i] == NULL)
            continue;
        if (f->safe_out[i] || f->disp_out[i])
        {
            f->leaked++;            /* a message may still come: kept, with the ports */
            f->sb[i] = NULL;
            out = 1;
            continue;
        }
        if (k->scr != NULL)
            FreeScreenBuffer(k->scr, f->sb[i]);
        f->sb[i] = NULL;
    }
    if (f->adopted)
    {
        f->leaked++;                /* the helper did not end: its signal is on them */
        out = 1;
    }
    if (out == 0)
    {
        if (f->sport != NULL)
            DeleteMsgPort(f->sport);
        if (f->dport != NULL)
            DeleteMsgPort(f->dport);
    }
    f->sport = NULL;
    f->dport = NULL;
    f->last_shown = NONE;
    k->flip_on = 0;
}

/* The ChangeScreenBuffer way on the Intuition screen card.c opened: three
 * ScreenBuffers, checked for the Radeon (format, on the board, rows) */
static void CsbOpen(struct Card *k, struct CardFlip *f)
{
    struct BitMap *bm;
    struct RenderInfo ri;
    LONG lock;
    ULONG fmt;
    ULONG rows;
    ULONG r;
    ULONG i;
    BOOL on;

    if (!k->scr_intui)
    {
        FlipWhy(f, "the GL screen is not Intuition's");
        return;
    }
    /* a live switch from panning: CloseGLScreen left the pan helper
     * running, and a helper takes the ports only as it starts (FlipTask):
     * a new one, which adopts them (2026-11-13; before, the GL task kept
     * collecting the SafeMessages after such a switch) */
    if (f->helper != NULL && !f->adopted)
        StopHelper(f);
    f->adopted = 0;
    f->hsig = -1;
    f->sport = CreateMsgPort();
    f->dport = CreateMsgPort();
    if (f->sport == NULL || f->dport == NULL)
    {
        FlipWhy(f, "no message ports");
        CsbUnbuffer(k, f);
        return;
    }
    f->port_task = FindTask(NULL);
    f->own_sbit = f->sport->mp_SigBit;
    f->own_dbit = f->dport->mp_SigBit;
    fmt = p96GetBitMapAttr(k->scr->RastPort.BitMap, P96BMA_RGBFORMAT);
    rows = 0;
    for (i = 0; i < CARD_FLIP_N; i++)
    {
        f->safe_out[i] = 0;
        f->disp_out[i] = 0;
        f->last_gpu[i] = 0;
        f->sb[i] = NULL;
    }
    for (i = 0; i < CARD_FLIP_N; i++)
    {
        /* a screen that is not CUSTOMBITMAP: its own bitmap, then two that
         * Intuition allocates (intuition.doc AllocScreenBuffer) */
        f->sb[i] = AllocScreenBuffer(k->scr, NULL, (i == 0) ? SB_SCREEN_BITMAP : 0);
        if (f->sb[i] == NULL)
        {
            FlipWhy(f, (i == 0) ? "AllocScreenBuffer(SB_SCREEN_BITMAP) failed"
                                : "AllocScreenBuffer failed (memory, or a mode without double-buffering)");
            break;
        }
        f->sb[i]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = f->sport;
        f->sb[i]->sb_DBufInfo->dbi_DispMessage.mn_ReplyPort = f->dport;
        bm = f->sb[i]->sb_BitMap;
        if (i == 0)
        {
            /* the screen's actual bitmap: it outlives every FreeScreenBuffer
             * while installed, so the copy path can fall back on it */
            k->friend = bm;
            k->b[CARD_SCREEN].bm = bm;
        }
        if (!p96GetBitMapAttr(bm, P96BMA_ISP96) || p96GetBitMapAttr(bm, P96BMA_RGBFORMAT) != fmt
            || p96GetBitMapAttr(bm, P96BMA_BYTESPERPIXEL) != k->bpp)
        {
            FlipWhy(f, "a screen buffer is not in the GL screen's format");
            break;
        }
        lock = p96LockBitMap(bm, (UBYTE *) &ri, sizeof(ri));
        if (lock == 0)
        {
            FlipWhy(f, "a screen buffer could not be locked");
            break;
        }
        on = (BOOL) (ri.Memory != NULL && p96GetBitMapAttr(bm, P96BMA_ISONBOARD) != 0);
        r = (ULONG) ri.BytesPerRow;
        if (on && k->ap_size != 0 && k->membase != NULL)     /* at the first open: not yet */
            f->last_gpu[i] = k->fb_start + ((ULONG) ri.Memory - (ULONG) k->membase) % k->ap_size;
        p96UnlockBitMap(bm, lock);
        if (!on)
        {
            FlipWhy(f, "a screen buffer is not on the board");
            break;
        }
        /* the R200's colour buffer: 16-byte aligned, rows a multiple of 8
         * pixels, all three alike (card.c BitMapRows) */
        if ((f->last_gpu[i] & 15) != 0 || (r % k->bpp) != 0 || ((r / k->bpp) % 8) != 0
            || (i > 0 && r != rows))
        {
            FlipWhy(f, "a screen buffer's rows do not suit the Radeon");
            break;
        }
        rows = r;
    }
    if (i < CARD_FLIP_N)
    {
        CsbUnbuffer(k, f);
        return;
    }
    f->panel = 0;                   /* card.c TargetAt: the buffer's own address */
    k->pitch = rows / k->bpp;
    FlipSm_Init(&f->sm);            /* buffer 0 shown (installed: no switch, no message) */
    f->sm.pipe = EnvCsbPipe();
    f->last_shown = 0;
    f->presented = 0;
    f->why[0] = 0;
    f->card = k;
    k->b[CARD_SCREEN].bm = f->sb[f->sm.draw]->sb_BitMap;   /* card.c locks the one drawn into */
    StartHelper(f);                 /* none: the switches run in this task */
    f->asked = f->done;
    k->flip_on = 1;
}

void CardFlip_Open(struct Card *k)
{
    struct CardFlip *f;
    struct BitMap *bm;
    struct RenderInfo ri;
    LONG lock;
    ULONG rows;
    BOOL on;

    k->flip_on = 0;
    f = k->flip;
    if (f != NULL && f->mode == CARD_FLIP_CSB)
    {
        CsbOpen(k, f);
        return;
    }
    if (f == NULL || !f->tall)
        return;
    bm = k->scr->RastPort.BitMap;
    if (GetBitMapAttr(bm, BMA_HEIGHT) < CARD_FLIP_N * k->height)
    {
        FlipWhy(f, "the GL screen's bitmap is not three times as tall");
        return;
    }
    lock = p96LockBitMap(bm, (UBYTE *) &ri, sizeof(ri));
    if (lock == 0)
    {
        FlipWhy(f, "the GL screen's bitmap could not be locked");
        return;
    }
    on = (BOOL) (ri.Memory != NULL && p96GetBitMapAttr(bm, P96BMA_ISONBOARD) != 0);
    rows = (ULONG) ri.BytesPerRow;
    p96UnlockBitMap(bm, lock);
    if (!on)
    {
        FlipWhy(f, "the GL screen's bitmap is not on the board");
        return;
    }
    /* the R200's colour buffer: first pixel 16-byte aligned, rows a
     * multiple of 8 pixels (as card.c BitMapRows) -- so every third starts
     * 16-byte aligned too */
    if (((ULONG) ri.Memory & 15) != 0 || (rows % k->bpp) != 0 || ((rows / k->bpp) % 8) != 0)
    {
        FlipWhy(f, "the GL screen's rows do not suit the Radeon");
        return;
    }
    f->panel = rows * k->height;
    FlipSm_Init(&f->sm);
    f->presented = 0;
    f->why[0] = 0;
    k->pitch = rows / k->bpp;
    f->card = k;
    StartHelper(f);                 /* none: the pans run in this task */
    f->asked = f->done;
    Pan(k, f->sm.show);             /* the top third (where it opened) */
    f->beam_left = 0;               /* no third left by a pan yet */
    f->beam_go = 0;
    f->pkt_ok = 0;                  /* the mode may be another */
    f->h_shown = f->sm.show;
    k->flip_on = 1;
}

void CardFlip_Quiet(struct Card *k)
{
    /* switching: CardFlip_Unbuffer stops the helper (a switch may take
     * seconds while Intuition refuses it) */
    if (k->flip != NULL && k->flip->helper != NULL && k->flip->mode != CARD_FLIP_CSB)
        WaitPan(k, k->flip);
}

BOOL CardFlip_Switching(struct Card *k)
{
    return (BOOL) (k->flip != NULL && k->flip->mode == CARD_FLIP_CSB && k->flip_on);
}

void CardFlip_Unbuffer(struct Card *k)
{
    struct CardFlip *f;

    f = k->flip;
    if (f != NULL && f->mode == CARD_FLIP_CSB)
    {
        CsbUnbuffer(k, f);
        return;
    }
    if (f != NULL)
        StopHelper(f);
    if (f != NULL && k->flip_on && k->scr != NULL)
        Pan(k, 0);
    k->flip_on = 0;
}

void CardFlip_Free(struct Card *k)
{
    struct CardFlip *f;

    if (k->counted)
        Card_Buffers = 0;           /* what one program chose is not the next one's */
    f = k->flip;
    if (f == NULL)
        return;
    CardFlip_Unbuffer(k);
    FreeVec(f);
    k->flip = NULL;
}

/* --- the log (rgl.c WriteCardLog) ------------------------------------------------------- */

/* Into the delete log's buffer (rgl.c, Rgl_LogPut: 2026-11-12, the log goes
 * out in 4 KB writes) */
static void LogPut(struct LogOut *f, const char *s)
{
    Rgl_LogPut(f, s);
}

static void LogX(struct LogOut *f, ULONG v)
{
    char b[12];
    int i;

    b[0] = '0';
    b[1] = 'x';
    for (i = 0; i < 8; i++)
        b[2 + i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 15];
    b[10] = 0;
    LogPut(f, b);
}

static void LogU(struct LogOut *f, ULONG v)
{
    char b[12];
    int i;

    i = 11;
    b[i] = 0;
    do
    {
        b[--i] = (char) ('0' + v % 10);
        v /= 10;
    } while (v != 0 && i > 0);
    LogPut(f, b + i);
}

void CardFlip_Log(struct LogOut *f, struct Card *k)
{
    struct CardFlip *fl;

    if (k->kind != CARD_KIND_SCREEN)
        return;
    fl = k->flip;
    LogPut(f, "triple buffering: ");
    if (fl == NULL)
    {
        if (EnvBuffers() == 2)
            LogPut(f, "off (RadeonGLBuffers=2)\n");
        else
            LogPut(f, "off (the program asked for fewer than three buffers; RadeonGLBuffers=3 forces it)\n");
        return;
    }
    if (!k->flip_on)
    {
        LogPut(f, "off (");
        LogPut(f, fl->why[0] != 0 ? fl->why : "?");
        LogPut(f, ")\n");
        return;
    }
    if (fl->mode == CARD_FLIP_CSB)
    {
        LogPut(f, fl->helper != NULL ? "on, ChangeScreenBuffer on an Intuition screen (in a helper task at the game's priority "
                                     : "on, ChangeScreenBuffer on an Intuition screen (in the GL task, ");
        if (fl->helper != NULL)
        {
            LogPut(f, fl->helper_pri < 0 ? "-" : "+");
            LogU(f, (ULONG) (fl->helper_pri < 0 ? -fl->helper_pri : fl->helper_pri));
            LogPut(f, ", ");
        }
        LogPut(f, fl->sm.pipe ? "csbnowait: no DispMessage wait, unsafe); switches " : "waiting for the DispMessage); switches ");
        LogU(f, fl->switches);
        LogPut(f, " (given up ");
        LogU(f, fl->given_up);
        LogPut(f, ", refused and tried again ");
        LogU(f, fl->retries);
        LogPut(f, fl->sm.pipe ? ", waited for the buffer's own messages " : ", waited for the last DispMessage ");
        LogU(f, fl->disp_waits);
        LogPut(f, "); ChangeScreenBuffer us: max ");
        LogU(f, fl->call_us_max);
        LogPut(f, ", mean ");
        LogU(f, fl->switches > fl->given_up ? fl->call_us_sum / (fl->switches - fl->given_up) : 0UL);
        LogPut(f, "\n  safe waits ");
        LogU(f, fl->safe_waits);
        LogPut(f, " (us max ");
        LogU(f, fl->wait_us_max);
        LogPut(f, ", mean ");
        LogU(f, fl->safe_waits != 0 ? fl->wait_us_sum / fl->safe_waits : 0UL);
        LogPut(f, ", given up after 5 s ");
        LogU(f, fl->safe_forced);
        LogPut(f, "), finished frames never shown ");
        LogU(f, fl->sm.dropped);
        LogPut(f, ", frames drawn into the shown buffer ");
        LogU(f, fl->on_shown);
        LogPut(f, "\n  the helper's SafeMessage waits ");
        LogU(f, fl->hsafe_n);
        LogPut(f, " (us max ");
        LogU(f, fl->hsafe_us_max);
        LogPut(f, ", mean ");
        LogU(f, fl->hsafe_n != 0 ? fl->hsafe_us_sum / fl->hsafe_n : 0UL);
        LogPut(f, fl->hsafe_n != 0 ? ")" : (fl->sm.pipe ? "; none: pipelined)" : "; none: the GL task took the messages)"));
        LogPut(f, "\n  buffer addresses changed between locks: ");
        LogU(f, fl->moved[0]);
        LogPut(f, " ");
        LogU(f, fl->moved[1]);
        LogPut(f, " ");
        LogU(f, fl->moved[2]);
        LogPut(f, "; off the board ");
        LogU(f, fl->offboard);
        LogPut(f, "; rows unfit ");
        LogU(f, fl->badrows);
        LogPut(f, "; ScreenBuffers kept for a late message ");
        LogU(f, fl->leaked);
        LogPut(f, "\n");
        return;
    }
    LogPut(f, fl->helper != NULL ? "on, panning a screen three times as tall (in a helper task at the game's priority "
                                 : "on, panning a screen three times as tall (in the GL task); switches ");
    if (fl->helper != NULL)
    {
        LogPut(f, fl->helper_pri < 0 ? "-" : "+");
        LogU(f, (ULONG) (fl->helper_pri < 0 ? -fl->helper_pri : fl->helper_pri));
        LogPut(f, "); switches ");
    }
    LogU(f, fl->switches);
    LogPut(f, "; ScrollVPort us: max ");
    LogU(f, fl->call_us_max);
    LogPut(f, ", mean ");
    LogU(f, fl->switches != 0 ? fl->call_us_sum / fl->switches : 0UL);
    if (fl->helper != NULL)
    {
        ULONG i;

        LogPut(f, "\n  after ScrollVPort: beam in the blank ");
        LogU(f, fl->pan_in_blank);
        LogPut(f, ", offset update pending ");
        LogU(f, fl->pan_pending);
        LogPut(f, "; in the picture by eighths, top first:");
        for (i = 0; i < 8; i++)
        {
            LogPut(f, " ");
            LogU(f, fl->pan_eighths[i]);
        }
    }
    LogPut(f, "\n  safe waits ");
    LogU(f, fl->safe_waits);
    LogPut(f, " (us max ");
    LogU(f, fl->wait_us_max);
    LogPut(f, ", mean ");
    LogU(f, fl->safe_waits != 0 ? fl->wait_us_sum / fl->safe_waits : 0UL);
    LogPut(f, "), finished frames never shown ");
    LogU(f, fl->sm.dropped);
    LogPut(f, ", frames drawn into the shown third ");
    LogU(f, fl->on_shown);
    if (fl->helper != NULL)
    {
        /* 2026-11-14: "the shown third" above is the bookkeeping's; what
         * the CRTC scans out follows the pan only at the next blank */
        LogPut(f, "\n  a left third until the blank: the Radeon waited for it ");
        LogU(f, fl->beam_waits);
        LogPut(f, ", no wait by the beam ");
        LogU(f, fl->beam_passed);
        LogPut(f, ", by the pan's age ");
        LogU(f, fl->beam_aged);
        LogPut(f, "; the pan's line no reference ");
        LogU(f, fl->beam_late);
        LogPut(f, " (1 ms polls ");
        LogU(f, fl->beam_polls);
        LogPut(f, "), the 68k waited instead ");
        LogU(f, fl->beam_timed);
        LogPut(f, " (wait refused by the checker ");
        LogU(f, fl->beam_refused);
        LogPut(f, "), pan not back ");
        LogU(f, fl->beam_out);
        LogPut(f, ", another mode at the kick ");
        LogU(f, fl->beam_mode);
        LogPut(f, ", odd lines ");
        LogU(f, fl->beam_odd);
        LogPut(f, "\n  ScrollVPort left the offset as it was ");
        LogU(f, fl->pan_same);
        LogPut(f, " of ");
        LogU(f, fl->pan_cmp);
        LogPut(f, fl->pan_crtc ? "; the last pan CRTC2_OFFSET " : "; the last pan CRTC_OFFSET ");
        LogX(f, fl->pan_off0);
        LogPut(f, " -> ");
        LogX(f, fl->pan_off1);
        LogPut(f, ", OFFSET_CNTL ");
        LogX(f, fl->pan_cntl);
    }
    LogPut(f, "\n");
}

/* --- the flush ------------------------------------------------------------------------------ */

void CardFlip_Safe(struct Card *k)
{
    struct CardFlip *f;

    f = k->flip;
    f->beam_go = 0;                 /* a flush that left before its kick decided nothing */
    if (f->mode == CARD_FLIP_CSB)
    {
        CsbExpire(f);
        if (FlipSm_MustWait(&f->sm))
            CsbWaitSafe(k, f);
        return;
    }
    Expire(f);
    if (FlipSm_MustWait(&f->sm))
        WaitSafe(k, f);
    /* off the display by the bookkeeping; by the beam (cure B): */
    if (f->helper != NULL && f->sm.draw < CARD_FLIP_N && (f->beam_left & (1UL << f->sm.draw)) != 0)
        BeamRef(k, f);
}

void CardFlip_Beam(struct Card *k)
{
    struct CardFlip *f;
    ULONG crtc;
    ULONG disp;
    ULONG line;
    ULONG vtot;
    ULONG us;
    ULONG *p;

    f = k->flip;
    if (f == NULL || !f->beam_go)
        return;
    f->beam_go = 0;
    f->beam_left &= ~(1UL << f->beam_third);
    us = FlipUs(FlipTicks() - f->pan_ticks);
    if (us >= FLIP_BEAM_AGED_US)
    {
        f->beam_aged++;
        return;
    }
    /* the mode the wait was built for, read here in the lock as the vsync
     * copy reads its window (a screen switch outside the lock -- a
     * non-present flush is not held back by the hidden-screen test --
     * would leave the CP waiting for a blank the new mode never reaches);
     * another mode, or none: the GL screen is not what that CRTC shows */
    if (!BeamRead(k, &crtc, &disp, &line, &vtot) || crtc != f->pkt_crtc || disp != f->pkt_disp)
    {
        f->beam_mode++;
        return;
    }
    if (line >= vtot)
        f->beam_odd++;
    if (!FlipBeam_MustWait(f->ref_line, us, line, disp, vtot))
    {
        f->beam_passed++;
        return;
    }
    /* the wait first in this flush's part of the ring, and to the CP at
     * once: the engine is idle (the lock), so it stands at the wait while
     * the beam is still before the blank the 68k saw coming -- reached
     * only after the blank, it would wait a whole refresh more (no room:
     * the error stays in the ring's stream, CopyCommands returns it) */
    p = R2Cs_Room(&k->ring.cs, 4);
    if (p == NULL)
        return;
    p[0] = f->beam_pkt[0];
    p[1] = f->beam_pkt[1];
    p[2] = f->beam_pkt[2];
    p[3] = f->beam_pkt[3];
    k->ring.cs.ptr = p + 4;
    (void) R2Ring_Commit(&k->ring);
    f->beam_waits++;
}

void CardFlip_Settled(struct Card *k)
{
    struct CardFlip *f;
    struct CardBm *b;

    f = k->flip;
    if (f->mode == CARD_FLIP_CSB)
    {
        /* the buffer drawn into, as this flush's lock found it */
        b = &k->b[CARD_SCREEN];
        Seen(f, f->sm.draw, b->gpu);
        if (b->bpr != k->pitch * k->bpp || (b->gpu & 15) != 0)
            f->badrows++;
    }
    FlipSm_Settled(&f->sm);
}

void CardFlip_Present(struct Card *k)
{
    struct CardFlip *f;
    struct BitMap *bm;
    struct RenderInfo ri;
    LONG lock;
    ULONG gpu;

    f = k->flip;
    /* the frame's pixels out of the destination cache and the engine idle
     * before its fence: the display may show them at the next blank */
    k->cs.end = k->cmd + CARD_CMD_DW;   /* the reserve */
    R2E_Reg(&k->cs, R_RB3D_DSTCACHE_CTLSTAT, R_RB3D_DC_FLUSH_ALL);
    R2E_Reg(&k->cs, R_WAIT_UNTIL, R_WAIT_3D_IDLECLEAN);
    if (f->mode == CARD_FLIP_CSB)
    {
        /* the next frame's buffer, its address from a lock now -- inside the
         * flush's locks (Picasso96API.doc asks to avoid that), but after the
         * kick a lock would wait for the frame, which is the overlap */
        CsbExpire(f);
        f->next = FlipSm_Choose(&f->sm);
        f->next_gpu = 0;
        bm = f->sb[f->next]->sb_BitMap;
        lock = p96LockBitMap(bm, (UBYTE *) &ri, sizeof(ri));
        if (lock != 0)
        {
            if (ri.Memory == NULL || !p96GetBitMapAttr(bm, P96BMA_ISONBOARD))
                f->offboard++;
            else if (k->ap_size != 0)
            {
                gpu = k->fb_start + ((ULONG) ri.Memory - (ULONG) k->membase) % k->ap_size;
                Seen(f, f->next, gpu);
                if ((ULONG) ri.BytesPerRow != k->pitch * k->bpp || (gpu & 15) != 0)
                    f->badrows++;
                else
                    f->next_gpu = gpu;
            }
            p96UnlockBitMap(bm, lock);
        }
        /* no fit buffer now: this frame is drawn over (never shown) and the
         * next one goes into the same buffer -- the bookkeeping unchanged */
        f->presented = (f->next_gpu != 0) ? 1UL : 0UL;
        if (f->next_gpu == 0)
            f->sm.dropped++;
        return;
    }
    /* the next frame's third (cardflip_sm.h), its address from this lock:
     * the screen's bitmap is b[CARD_SCREEN] */
    Expire(f);
    f->next = FlipSm_Choose(&f->sm);
    f->next_gpu = k->b[CARD_SCREEN].gpu + f->next * f->panel;
    f->presented = 1;
}

void CardFlip_After(struct Card *k)
{
    struct CardFlip *f;
    struct Core *c;
    ULONG t0;
    ULONG us;
    ULONG old;
    ULONG show;
    BOOL presented;

    f = k->flip;
    presented = (BOOL) (f->presented != 0);
    f->presented = 0;
    if (k->lost != 0)
        return;
    if (presented)
    {
        /* the frame just run: kicked (the overlap), or finished already;
         * the next frame draws into the third the present chose */
        old = f->sm.draw;
        FlipSm_Ran(&f->sm, (BOOL) (k->pending != 0), f->next);
        if (f->sm.draw != old)
        {
            if (f->mode == CARD_FLIP_CSB)
                k->b[CARD_SCREEN].bm = f->sb[f->sm.draw]->sb_BitMap;   /* the next flush locks it */
            k->color_gpu = f->next_gpu;
            c = k->core;
            c->target.color_gpu = f->next_gpu;
            c->dirty |= D_TARGET;
        }
    }
    /* a finished frame on the display, if the last switch is a refresh old */
    if (f->mode == CARD_FLIP_CSB)
        CsbExpire(f);
    else
        Expire(f);
    show = FlipSm_ToShow(&f->sm);
    if (show != NONE && f->helper == NULL && f->mode == CARD_FLIP_CSB)
    {
        /* no helper: the switch in this task */
        if (CsbShow(f, show))
        {
            f->csb_ok = 1;
            FlipSm_Shown(&f->sm);
            FlipSm_Done(&f->sm);    /* pipe: the call returned (a no-op without) */
            f->switches++;
        }
        else
            f->given_up++;
        show = NONE;
    }
    if (show != NONE && f->helper != NULL)
    {
        /* the helper pans (or switches); its return marks the left buffer
         * off the display by the bookkeeping (or, switching, its
         * SafeMessage does); a pan's left third is on show until the next
         * blank: cure B guards the first frame drawn into it */
        f->pan_to = show;
        f->switched = FlipTicks();
        f->asked++;
        Signal(&f->helper->pr_Task, SIGBREAKF_CTRL_F);
        FlipSm_Shown(&f->sm);
        f->switches++;
        if (f->mode != CARD_FLIP_CSB && f->sm.leave < CARD_FLIP_N)
        {
            /* on show until the blank after this pan (CardFlip_Safe) */
            f->left_seq[f->sm.leave] = f->asked;
            f->beam_left |= 1UL << f->sm.leave;
        }
    }
    else if (show != NONE)
    {
        t0 = FlipTicks();
        Pan(k, show);
        f->switched = FlipTicks();
        us = FlipUs(f->switched - t0);
        if (us > f->call_us_max)
            f->call_us_max = us;
        f->call_us_sum += us;
        FlipSm_Shown(&f->sm);
        f->switches++;
    }
    if (presented && f->sm.draw == f->sm.show)
        f->on_shown++;
}
