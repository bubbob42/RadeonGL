/*
 * cardflip.h -- triple buffering for the screen kind (cardflip.c, linked
 * last; docs/concept-triple-buffering.md).
 */

#ifndef RADEONGL_CARDFLIP_H
#define RADEONGL_CARDFLIP_H

#include <exec/types.h>
#include <exec/tasks.h>
#include <dos/dosextens.h>
#include <intuition/screens.h>
#include <dos/dos.h>

#include "card.h"
#include "cardflip_sm.h"

#define CARD_FLIP_N    FLIP_N
#define CARD_FLIP_NONE FLIP_NONE

/* How the three buffers are shown (RadeonGLFlip) */
#define CARD_FLIP_PAN  1        /* one screen three times as tall, panned (the default) */
#define CARD_FLIP_CSB  2        /* ChangeScreenBuffer on an Intuition screen (=csb) */

struct CardFlip
{
    ULONG want;                 /* 3: the program asked for three buffers (or RadeonGLBuffers=3) */
    ULONG tall;                 /* the GL screen opened three times as tall (OpenGLScreen) */
    ULONG panel;                /* bytes from one third of its bitmap to the next */
    struct FlipSm sm;           /* which third is what (cardflip_sm.h) */
    ULONG next, next_gpu;       /* a present's choice for the next frame, from its lock */
    ULONG presented;            /* this flush presented (CardFlip_Present) */
    ULONG switched;             /* E clock of the last switch: the third it left is safe a refresh later */
    char why[72];               /* off: why (the log) */

    /* The pan in a helper task (2026-11-12: ScrollVPort waits ~7 ms for
     * the beam on Radeon.card -- in the game's task that cost 15%) */
    struct Card *card;          /* for the helper */
    struct Task *owner;         /* the GL task: it flushes, it waits on sig */
    struct Process *helper;     /* NULL: the pans run in the GL task */
    LONG sig;                   /* the owner's signal: a pan is done; -1 none */
    volatile ULONG asked;       /* pans asked for (a sequence) */
    volatile ULONG done;        /* ... done: ScrollVPort returned, the left third is off the display */
    volatile ULONG pan_to;      /* the third the next pan shows */
    volatile ULONG quit;        /* the helper is to end */
    volatile ULONG alive;       /* the helper runs */
    LONG helper_pri;            /* its priority against the GL task's (RadeonGLHelperPri; default -1 panning, +1 csb) */

    /* 2026-11-12, the corrected ChangeScreenBuffer way (RadeonGLFlip=csb;
     * docs/p96-changescreenbuffer-audit.md): an Intuition screen in the
     * card's mode, not CUSTOMBITMAP, its three ScreenBuffers Intuition's */
    ULONG mode;                 /* CARD_FLIP_PAN or CARD_FLIP_CSB */
    struct ScreenBuffer *sb[CARD_FLIP_N];   /* [0] SB_SCREEN_BITMAP, [1] [2] allocated by Intuition */
    struct MsgPort *sport;      /* their SafeMessages (created by the GL task; the helper takes them
                                 * over while it runs: Tim's advice, the buffer wait in the helper) */
    struct MsgPort *dport;      /* their DispMessages (the same) */
    struct Task *port_task;     /* the GL task: the ports' own signal task, */
    ULONG own_sbit;             /* ... and their own signal bits, back when the helper ends */
    ULONG own_dbit;
    LONG hsig;                  /* the helper's signal for both ports (-1: none) */
    volatile ULONG adopted;     /* the helper has the ports (it takes every message) */
    ULONG hsafe_n;              /* SafeMessage waits in the helper, */
    ULONG hsafe_us_max;         /* ... the longest (us) */
    ULONG hsafe_us_sum;
    volatile ULONG safe_out[CARD_FLIP_N];   /* a SafeMessage of that buffer's DBufInfo outstanding */
    volatile ULONG disp_out[CARD_FLIP_N];   /* ... a DispMessage */
    volatile ULONG last_shown;  /* the buffer of the last ChangeScreenBuffer that took */
    volatile ULONG csb_ok;      /* the asked switch took (FALSE: refused until given up) */
    volatile ULONG retries;     /* ChangeScreenBuffer refused, tried again */
    volatile ULONG disp_waits;  /* switches that waited for the last one's DispMessage */
    ULONG given_up;             /* switches refused for good (the bookkeeping turned back) */
    ULONG safe_forced;          /* safe waits given up after 5 s (the DBufInfo kept outstanding) */
    ULONG leaked;               /* ScreenBuffers not freed: a message never came */
    ULONG last_gpu[CARD_FLIP_N];    /* each buffer's address at its last lock */
    ULONG moved[CARD_FLIP_N];   /* ... how often it differed from the lock before */
    ULONG offboard;             /* a buffer's lock found it off the board */
    ULONG badrows;              /* a buffer's lock found rows or alignment unfit */

    /* for the log */
    ULONG switches;             /* ScrollVPort / ChangeScreenBuffer calls */
    ULONG safe_waits;           /* flushes that waited for the third the last switch left */
    ULONG on_shown;             /* frames drawn into the shown third (never, by the bookkeeping) */
    ULONG call_us_max, call_us_sum;     /* ScrollVPort's time */
    /* 2026-11-12, does the pan tear? The beam line read (read only) right
     * after each helper ScrollVPort, and CRTC_OFFSET's update-pending bit:
     * returns in the blank = a beam wait; spread out with nothing pending =
     * the offset took effect at once, mid-scan (a horizontal tear); pending
     * = latched to the next blank, the left third still on show */
    ULONG pan_in_blank;         /* ScrollVPort returned with the beam in the blank */
    ULONG pan_pending;          /* ... with the offset update still pending */
    ULONG pan_eighths[8];       /* the return lines by eighths of the displayed lines */
    ULONG wait_us_max, wait_us_sum;     /* the safe waits' */
};

/* mglChooseNumberOfBuffers: the next screen context's wish (reset when a
 * card that opened closes) */
extern ULONG Card_Buffers;

/* OpenGLScreen, before the screen opens: TRUE = open an Intuition screen
 * in the card's mode for ChangeScreenBuffer (three buffers asked for and
 * RadeonGLFlip=csb); CsbFailed: it did not open as needed, the Picasso96
 * screen and the copy path follow */
BOOL CardFlip_Csb(struct Card *k);
void CardFlip_CsbFailed(struct Card *k, const char *why);

/* The program's buffering wish (RadeonGLBuffers or mglChooseNumberOfBuffers,
 * RadeonGLFlip) differs from what the GL screen does now: a resize to the
 * same size reopens it (2026-11-12, a live switch from the program's menu) */
BOOL CardFlip_Changed(struct Card *k);

/* ChangeScreenBuffer buffers on the GL screen now: card.c frees them
 * (CardFlip_Unbuffer) after the input window and before the screen closes */
BOOL CardFlip_Switching(struct Card *k);
/* OpenGLScreen, before the screen opens: TRUE = open it three times as
 * tall, the display clipped to the top third (the program asked for three
 * buffers); TallFailed: that open failed, the normal one follows */
BOOL CardFlip_Tall(struct Card *k);
void CardFlip_TallFailed(struct Card *k);
/* The GL screen just opened: if it is the tall one and suits the Radeon,
 * k->flip_on and k->pitch (its rows); anything else leaves the copy path */
void CardFlip_Open(struct Card *k);
/* The GL screen goes (resize, close): the display back on the top third,
 * k->flip_on 0 */
void CardFlip_Unbuffer(struct Card *k);
/* Card_Close: everything */
void CardFlip_Free(struct Card *k);

/* The flush (card.c CardFlush). Safe: before anything, the third drawn
 * into off the display. Settled: in the lock, after the last frame was
 * settled. Present: in the lock, the frame's end and the next frame's
 * third. After: after the unlock, the next frame retargeted and the
 * finished frame shown. */
void CardFlip_Safe(struct Card *k);
void CardFlip_Settled(struct Card *k);
void CardFlip_Present(struct Card *k);
void CardFlip_After(struct Card *k);

/* The GL screen is about to close (resize): no pan in flight */
void CardFlip_Quiet(struct Card *k);

/* The log's lines (rgl.c WriteCardLog) */
/* (rgl.c's delete log: a 4 KB buffer, one Write per 4 KB) */
struct LogOut;
void Rgl_LogPut(struct LogOut *f, const char *s);
void CardFlip_Log(struct LogOut *f, struct Card *k);

#endif /* RADEONGL_CARDFLIP_H */
