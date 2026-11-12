/*
 * rstat.c -- RadeonStat's sampling (rstat.h). Read-only.
 */

#include <exec/types.h>
#include <intuition/intuitionbase.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/Picasso96.h>
#include <libraries/Picasso96.h>

#include "eclock.h"
#include "mmio.h"
#include "rstat.h"

#define R_PCI_MIRROR        0x0f00
#define R_RBBM_STATUS       0x0e40
#define   S_GUI_ACTIVE      (1UL << 31)
#define   S_CP_BUSY         (1UL << 16)
#define   S_2D_BUSY         (1UL << 18)
#define   S_3D_BUSY         (0x1fUL << 19)  /* RB3D, SE, RE, TAM, TDM */
#define R_CP_RB_CNTL        0x0704
#define R_CP_RB_RPTR        0x0710
#define R_CP_RB_WPTR        0x0714
#define R_CRTC_H_TOTAL_DISP 0x0200
#define R_CRTC_V_TOTAL_DISP 0x0208
#define R_CRTC_VLINE        0x0210      /* CRTC_VLINE_CRNT_VLINE: current line in bits 16-26 */

extern struct IntuitionBase *IntuitionBase;

static ULONG Rd(struct RStat *s, ULONG off)
{
    ULONG v;

    v = Mmio_Read(s->io, off);
    return s->swp ? Mmio_Swap32(v) : v;
}

static ULONG Line(struct RStat *s)
{
    return (Rd(s, R_CRTC_VLINE) >> 16) & 0x7ffUL;
}

/* The refresh in 1/10 Hz, from five frames timed by the E clock (about
 * 100 ms, inside the lock); 0 when the beam does not move */
static ULONG Refresh(struct RStat *s)
{
    ULONG last, cur, start, frames, ticks, guard;

    last = Line(s);
    for (guard = 0; guard < 200000UL; guard++)  /* to the top of a frame */
    {
        cur = Line(s);
        if (cur < last)
            break;
        last = cur;
    }
    if (guard == 200000UL)
        return 0;
    start = Clock_Now();
    frames = 0;
    last = Line(s);
    for (guard = 0; frames < 5 && guard < 2000000UL; guard++)
    {
        cur = Line(s);
        if (cur < last)
            frames++;
        last = cur;
    }
    ticks = Clock_Since(start);
    if (frames < 5 || Clock_Us(ticks) == 0)
        return 0;
    return 50000000UL / Clock_Us(ticks);       /* 5 frames: 10 * 5e6 / us */
}

/* --- SIMULATE ------------------------------------------------------------ */

#define SIM_TOTAL   (64UL << 20)
#define SIM_FREE    (49807360UL)        /* 47.5 MB */

static ULONG SimRandom(struct RStat *s)
{
    s->seed = s->seed * 1103515245UL + 12345UL;
    return (s->seed >> 16) & 0x7fffUL;
}

/* A load that climbs from 0 to 100 % and back every 12 s */
static void SimSample(struct RStat *s)
{
    ULONG i, level, r, tri;

    tri = s->phase++ % 600;
    level = tri < 300 ? tri / 3 : (600 - tri) / 3;
    for (i = 0; i < RSTAT_BURST; i++)
    {
        r = SimRandom(s) % 100;
        s->n++;
        if (r < level)
        {
            s->gpu++;
            if (r < level * 8 / 10)
                s->d3++;
            if (r < level * 9 / 10)
                s->cp++;
            if (r % 7 == 0)
                s->d2++;
        }
    }
    if (level * 24 > s->ring)
        s->ring = level * 24;
}

/* --- the card ------------------------------------------------------------- */

LONG RStat_Open(struct RStat *s)
{
    ULONG mirror;

    s->n = s->gpu = s->d3 = s->d2 = s->cp = s->ring = 0;
    if (s->sim)
    {
        s->seed = 4711;
        s->phase = 0;
        s->board.boardName = (STRPTR) "Simulated";
        s->board.chipName = (STRPTR) "no card";
        s->board.totalMem = SIM_TOTAL;
        return RSTAT_OK;
    }
    if (!P96Board_Open(&s->board, 4096))
        return RSTAT_NOBOARD;
    if (!P96Board_IsRadeon(&s->board) || s->board.memIoBase == NULL)
        return RSTAT_NORADEON;
    s->io = s->board.memIoBase;
    if (!P96Board_Lock(&s->board))
        return RSTAT_NOLOCK;
    mirror = Mmio_Read(s->io, R_PCI_MIRROR);
    s->swp = ((mirror & 0xffffUL) != 0x1002UL && (Mmio_Swap32(mirror) & 0xffffUL) == 0x1002UL);
    P96Board_Unlock(&s->board);
    return RSTAT_OK;
}

void RStat_Close(struct RStat *s)
{
    if (!s->sim)
        P96Board_Close(&s->board);
    s->io = NULL;
}

BOOL RStat_Sample(struct RStat *s)
{
    ULONG i, v, rsize, rptr, wptr, lag;

    if (s->sim)
    {
        SimSample(s);
        return TRUE;
    }
    /* NOT inside a lock: Radeon.card waits for the engine to go idle before
     * it hands out a lock, so inside one the engine always reads idle
     * (MEASURED on the A4000D, 2026-11-08: 0 % with Spinner drawing). These
     * are status registers, read only and directly addressed (no index
     * register shared with the driver): reading them while others draw
     * changes nothing. The register base does not move (p96board.h). */
    if (s->io == NULL)
        return FALSE;
    for (i = 0; i < RSTAT_BURST; i++)
    {
        v = Rd(s, R_RBBM_STATUS);
        s->n++;
        if (v & S_GUI_ACTIVE)
            s->gpu++;
        if (v & S_3D_BUSY)
            s->d3++;
        if (v & S_2D_BUSY)
            s->d2++;
        if (v & S_CP_BUSY)
            s->cp++;
    }
    rsize = 2UL << (Rd(s, R_CP_RB_CNTL) & 0x3fUL);      /* dwords */
    rptr = Rd(s, R_CP_RB_RPTR);
    wptr = Rd(s, R_CP_RB_WPTR);
    lag = (rsize >= 2 && rsize <= (1UL << 23)) ? ((wptr - rptr) & (rsize - 1)) : 0;
    if (lag > s->ring)
        s->ring = lag;
    return TRUE;
}

void RStat_Take(struct RStat *s, struct RStatLoad *l)
{
    ULONG n;

    n = s->n;
    l->samples = n;
    l->gpu = n ? s->gpu * 100 / n : 0;
    l->d3 = n ? s->d3 * 100 / n : 0;
    l->d2 = n ? s->d2 * 100 / n : 0;
    l->cp = n ? s->cp * 100 / n : 0;
    l->ring = s->ring;
    s->n = s->gpu = s->d3 = s->d2 = s->cp = s->ring = 0;
}

BOOL RStat_Mode(struct RStat *s, struct RStatMode *m)
{
    m->ht = m->vt = m->hz10 = m->mhz10 = 0;
    if (s->sim)
    {
        m->ht = 2200;               /* 1920 x 1080 at 50 Hz, as the A4000D's Workbench */
        m->vt = 1125;
        m->hz10 = 500;
        m->mhz10 = m->ht * m->vt / 100UL * m->hz10 / 10000UL;
        return TRUE;
    }
    if (s->io == NULL || !P96Board_Lock(&s->board))
        return FALSE;
    m->ht = ((Rd(s, R_CRTC_H_TOTAL_DISP) & 0x3ffUL) + 1) * 8;
    m->vt = (Rd(s, R_CRTC_V_TOTAL_DISP) & 0xfffUL) + 1;
    m->hz10 = Refresh(s);
    P96Board_Unlock(&s->board);
    /* ht * vt * hz in 1/10 MHz, in steps that stay inside 32 bits */
    m->mhz10 = m->ht * m->vt / 100UL * m->hz10 / 10000UL;
    return TRUE;
}

void RStat_Memory(struct RStat *s, ULONG *total, ULONG *fr, ULONG *lg)
{
    struct TagItem t[3];

    *total = s->board.totalMem;
    *fr = *lg = 0;
    if (s->sim)
    {
        *fr = SIM_FREE;
        *lg = SIM_FREE - (3UL << 20);
        return;
    }
    if (P96Base == NULL)
        return;
    t[0].ti_Tag = P96BD_FreeMemory;         t[0].ti_Data = (ULONG) fr;
    t[1].ti_Tag = P96BD_LargestFreeMemory;  t[1].ti_Data = (ULONG) lg;
    t[2].ti_Tag = TAG_DONE;                 t[2].ti_Data = 0;
    p96GetBoardDataTagList(s->board.board, t);
}

BOOL RStat_Front(struct RStatFront *f)
{
    struct Screen *scr;
    ULONG il;
    CONST_STRPTR t;
    LONG i;
    BOOL changed;

    il = LockIBase(0);
    scr = IntuitionBase->FirstScreen;
    changed = (scr != f->scr);
    f->scr = scr;
    f->width = scr ? scr->Width : 0;
    f->height = scr ? scr->Height : 0;
    /* the screen's own title: Title is whatever the active window put in
     * the bar (RadeonStat's own version string while its window is active) */
    t = (scr && scr->DefaultTitle) ? (CONST_STRPTR) scr->DefaultTitle
      : (scr && scr->Title) ? (CONST_STRPTR) scr->Title : (CONST_STRPTR) "";
    for (i = 0; i < (LONG) sizeof(f->title) - 1 && t[i]; i++)
        f->title[i] = t[i];
    f->title[i] = 0;
    UnlockIBase(il);
    return changed;
}
