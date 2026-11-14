/*
 * rstat.h -- RadeonStat's sampling, shared by its Shell lines and its
 * window (2026-11-08). Read-only: nothing is written to the card.
 *
 * The register base, the byte order and the display's timing are read
 * inside a short Picasso96 lock of a 4 KB friend bitmap on the board
 * (p96board.h). The engine's status and the CP ring pointers are read
 * WITHOUT a lock: Radeon.card idles the engine before it hands out a lock,
 * so inside one the engine always reads idle (A4000D, 2026-11-08).
 */

#ifndef PROBE_RSTAT_H
#define PROBE_RSTAT_H

#include <exec/types.h>
#include <intuition/screens.h>

#include "p96board.h"

#define RSTAT_TICKS  50     /* sample bursts a second (a 20 ms pace) */
#define RSTAT_BURST  64     /* RBBM_STATUS reads a burst */

/* RStat_Open's answers */
#define RSTAT_OK        0
#define RSTAT_NOBOARD   1   /* no Picasso96, or the Workbench is not on a board */
#define RSTAT_NORADEON  2   /* the Workbench's board is not a Radeon */
#define RSTAT_NOLOCK    3   /* the friend bitmap could not be locked */

struct RStat
{
    struct P96Board board;
    APTR io;                /* the registers (BAR2) */
    BOOL swp;               /* they read byte-swapped */
    /* since the last RStat_Take */
    ULONG n, gpu, d3, d2, cp, ring;
    /* SIMULATE: made-up readings and no card at all, to see the window
     * where there is no Radeon (FS-UAE); set before RStat_Open */
    BOOL sim;
    ULONG seed, phase;
};

/* What the engine did since the last RStat_Take */
struct RStatLoad
{
    ULONG gpu, d3, d2, cp;  /* per cent of the samples busy */
    ULONG ring;             /* most dwords the command processor lagged */
    ULONG samples;
};

/* The display, as the card shows it */
struct RStatMode
{
    ULONG ht, vt;           /* the CRTC's totals, pixels and lines */
    ULONG hz10;             /* refresh in 1/10 Hz, 0 when the beam stands */
    ULONG mhz10;            /* pixel clock in 1/10 MHz */
};

/* The front screen, read under LockIBase */
struct RStatFront
{
    struct Screen *scr;     /* to tell a change only: never dereferenced */
    LONG width, height;
    char title[64];
};

/* s zeroed before the first call; RStat_Close is right whatever this
 * answers. Needs IntuitionBase and Clock_Open. */
LONG RStat_Open(struct RStat *s);
void RStat_Close(struct RStat *s);

/* One burst of status reads, no lock (see above); FALSE without a card */
BOOL RStat_Sample(struct RStat *s);

/* The shares since the last call; starts counting anew */
void RStat_Take(struct RStat *s, struct RStatLoad *l);

/* The CRTC totals and five frames timed by the E clock: holds the lock
 * about 100 ms. FALSE (m zeroed) when the bitmap could not be locked. */
BOOL RStat_Mode(struct RStat *s, struct RStatMode *m);

/* Card memory in bytes, as Picasso96 counts it */
void RStat_Memory(struct RStat *s, ULONG *total, ULONG *fr, ULONG *lg);

/* The front screen; TRUE when it is another one than f held */
BOOL RStat_Front(struct RStatFront *f);

#endif /* PROBE_RSTAT_H */
