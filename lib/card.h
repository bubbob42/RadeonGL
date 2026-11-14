/*
 * card.h -- the Radeon behind minigl.library (docs/design-card-backend.md).
 *
 * The rules (measured by the probes):
 *  - the card comes through Picasso96: the GL screen's board, a locked
 *    bitmap's p96GetBitMapAttr bases, P96's chip name and the chip's own
 *    PCI mirror say it is an R200-family Radeon;
 *  - every register access, every write into video RAM and every GPU run
 *    happens inside a lock of a bitmap on the board; nothing of ours runs
 *    on the chip while P96 may use it;
 *  - video RAM is friend bitmaps of the GL screen (never displayable), their
 *    addresses taken anew at every lock: P96 may move an unlocked bitmap.
 *
 * The flush (Card_Flush): the core builds the frame in a fast-RAM command
 * buffer (and the locked-array stream in a fast-RAM staging buffer). At the
 * end of a frame, or when the buffer fills, ONE lock of the target, the
 * ring, the heap and the screen covers: the move check, the present, the
 * packet checker, CP start, the stream and command copies, a fence and its
 * bounded wait, CP stop. A frame whose bitmaps moved since it was built is
 * dropped and the core rebased.
 *
 * Texture uploads lock the heap briefly (Card_VramBegin/End): the GPU is
 * idle outside flushes, so the core's texture hazards wait by flushing
 * (Card_WaitIdle).
 *
 * Nothing here prints: while a lock is held no console may be written, and
 * the library's log is written at context deletion.
 */

#ifndef RADEONGL_CARD_H
#define RADEONGL_CARD_H

#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>

#include "core.h"
#include "r2cs.h"
#include "r2ring.h"
#include "r2cp.h"
#include "r2check.h"

#define CARD_TARGET  0          /* colour, then Z16 */
#define CARD_RING    1
#define CARD_HEAP    2
#define CARD_SCREEN  3          /* where the present copies the frame: the GL screen's bitmap,
                                 * a window context's own show bitmap (ours, own_show); a bitmap
                                 * context's colour buffer, the program's (not ours to free); with
                                 * triple buffering the screen buffer the frame draws into */
#define CARD_NBM     4

/* What a context draws into (W1, docs/concept-window-contexts.md) */
#define CARD_KIND_SCREEN  0     /* our own P96 screen: MGLCreateContext, ...FromID */
#define CARD_KIND_WINDOW  1     /* a window on an RTG screen: the program's (FromWindow) or
                                 * ours (mglChooseWindowMode); the frame is blitted into it */
#define CARD_KIND_BITMAP  2     /* the program's board bitmap (FromBitMap): the colour buffer,
                                 * the program shows it itself */

#define CARD_RING_DW  65536UL   /* 256 KB */
#define CARD_CMD_DW   65536UL   /* the fast-RAM command buffer, 256 KB */
#define CARD_RESERVE  2048UL    /* of it kept for the present and the fence */
#define CARD_ROW      4096UL    /* bytes a row of the data bitmaps */

/* The texture heap's ways (Card_Open tries them in this order) */
#define CARD_HEAP_WB32    0     /* 32 bit, a friend of a 32-bit Workbench (RadeonGL 0.4) */
#define CARD_HEAP_WIDE    1     /* the GL screen's format, 8192-byte rows, its friend */
#define CARD_HEAP_BLIT    2     /* the same rows without a friend, moved on by a blit */
#define CARD_HEAP_NARROW  3     /* the GL screen's format, 4096-byte rows (RadeonGL 0.4) */
#define CARD_HEAP_TRIES   24

/* Card_Open results */
#define CARD_OK          0
#define CARD_NOT_RADEON  1      /* no Radeon on the board P96 offers: use the null card */
#define CARD_FAILED      2      /* a Radeon, but it cannot be used: why[] says */

struct CardFlip;                /* cardflip.c */

struct CardBm
{
    struct BitMap *bm;
    ULONG bytes;                /* asked for (data bitmaps) */
    LONG lock;
    UBYTE *mem;                 /* inside a lock */
    ULONG bpr;
    ULONG gpu;                  /* GPU address of mem, inside a lock */
    ULONG bpp;                  /* bytes a pixel (the aperture swap depends on it) */
    ULONG p96;                  /* from p96AllocBitMap: freed with p96FreeBitMap */
};

struct Card
{
    ULONG kind;                 /* CARD_KIND_* */
    struct Screen *scr;         /* the GL screen (screen kind only) */
    struct Window *win;         /* screen kind: our input window; window kind: the window drawn into */
    ULONG own_win;              /* window kind: win is ours (mglChooseWindowMode), closed with the context */
    struct BitMap *friend;      /* the bitmap ours befriend: the GL screen's, the window
                                 * screen's, or the program's (bitmap kind) */
    ULONG own_show;             /* b[CARD_SCREEN] is ours (window kind) */
    ULONG show_format;          /* the present's colour format: R_COLOR_FORMAT_RGB565, _ARGB1555, _ARGB8888 */
    ULONG show_w, show_h;       /* the size of b[CARD_SCREEN] */
    ULONG heap_top, heap_min;   /* the heap ladder: first size, smallest size (bytes) */
    ULONG blits, blit_cut;      /* window kind: frames blitted; ... cut to a smaller inner area */
    ULONG counted;              /* W3: open, counted in card_users */
    ULONG p96open;              /* W3: holds one open of Picasso96API.library */
    struct CardBm b[CARD_NBM];
    ULONG width, height;
    ULONG bpp;                  /* bytes a pixel of the screen (2 or 4) */
    ULONG color_format;         /* R_COLOR_FORMAT_RGB565 or _ARGB8888 */
    ULONG pitch;                /* the target's colour pitch in pixels */
    ULONG depth_pitch;          /* its Z16 pitch in pixels: whole 64-pixel tiles */
    ULONG color_bytes, depth_bytes;
    ULONG heap_size;
    ULONG compat;               /* P96IDA_COMPATIBLEFORMATS of the GL screen's mode */
    ULONG board;                /* its P96 board number */
    ULONG heap_delta;           /* where the heap starts in its bitmap (fixed at the open) */

    /* the chip (from the first lock) */
    APTR mmio;
    APTR membase;
    ULONG mmio_swap;
    ULONG fb_start;             /* MC_FB_LOCATION: GPU address of video RAM */
    ULONG ap_size;              /* CONFIG_APER_SIZE */
    ULONG pci_id;               /* the chip's PCI mirror: device << 16 | vendor */

    /* the addresses the current commands were built with */
    ULONG color_gpu, depth_gpu, heap_gpu;

    struct R2Hw hw;
    struct R2CpSaved saved;
    struct R2Ring ring;
    struct MsgPort *tport;
    struct timerequest *treq;
    ULONG clock_base;

    struct Core *core;
    struct R2Cs cs;             /* the core writes here: the command buffer */
    ULONG *cmd;                 /* 16-byte aligned in cmd_mem */
    ULONG *staging;             /* the locked-array stream, CORE_STREAM_BYTES, 16-byte aligned */
    APTR cmd_mem, staging_mem;  /* as AllocVec gave them (for FreeVec) */
    ULONG copied;               /* stream bytes already in video RAM */
    ULONG gen;                  /* the core's stream_gen they belong to */
    ULONG in_flush;
    ULONG vram_lock;            /* Card_VramBegin holds the heap */
    ULONG reload_ucode;         /* a reset: the microcode again before the CP starts */

    /* The overlap (on unless RadeonGLOverlap=0): a flush kicks the frame and
     * unlocks without waiting; the Radeon finishes it while the 68k builds
     * the next. The next lock of any bitmap settles it (fence, CP stop,
     * resets) before anything is written: a P96 lock waits for the 3D
     * engine anyway (Overlap probe B), and Radeon.card's 2D is right with
     * the CP on (C). Card_WaitIdle and Card_Close wait as before. */
    ULONG overlap;
    ULONG pending;              /* a kicked frame not yet settled */
    ULONG pend_seq;             /* its fence */
    ULONG drop_next;            /* a settle reset the engine: the commands built are stale */
    ULONG ov_frames, ov_waits;  /* for the log: frames left running; settles that waited */

    /* 2026-11-07, RadeonGLMove16 (68060, the GCC build only; off by
     * default): the copies into video RAM at the flush send their whole
     * 16-byte lines as MOVE16 bursts -- 1 with a data-cache push of the
     * source first, 2 without. A HARDWARE RISK until Move16Bench has shown
     * the bus takes bursts (docs/library-asm-research.md). */
    ULONG move16;
    ULONG m16_lines, m16_plain; /* for the log: dwords sent as lines, as longwords */
    ULONG m16_checked;          /* the self-test ran (the first flush with move16 on) */
    ULONG m16_failed;           /* 1: it read back a different line, 2: no room for it -- move16 off */

    ULONG check;                /* run the packet checker before the CP */
    struct R2ChkCfg cfg;
    struct R2ChkState chk;
    struct R2ChkError err;
    LONG err_code;

    LONG lost;                  /* 0, or why the context was lost (CARD_LOST_*) */

    /* for the log */
    ULONG flushes, presents, dropped, moved, offboard, noscreen;
    ULONG vsyncs;               /* presents that waited for the beam (mglEnableSync) */
    ULONG vsync_off;            /* ... asked to, with no CRTC running: not waited */
    ULONG vsync_crtc, vsync_lines;  /* the last wait's CRTC (0, 1) and window */
    ULONG hidden;               /* frames not drawn: the GL screen was behind another */
    /* 2026-11-07, RadeonGLDisplayPriority=high or a number: the display FIFO's
     * critical point while the context lives (GRPH_BUFFER_CNTL, card.c
     * DisplayPriority), Radeon.card's value back at the close */
    ULONG dprio_on, dprio_point;
    ULONG grph_seen;            /* GRPH_BUFFER_CNTL as the first flush found it */
    ULONG grph_saved;           /* 1: grph_seen to be written back at the close */
    ULONG dprio_writes;
    ULONG dwords, fence_us_max, lock_us_max, lock_us_sum, vram_locks, cp_resets;
    ULONG dwords_hi;            /* dwords carried past 2^32 */
    ULONG lock_flushes;         /* the flushes lock_us_sum is the sum of */
    ULONG heap_offboard;        /* heap sizes P96 kept off the board at the open */

    /* What letting the GPU run on after the unlock would win (a measurement
     * for the overlap question, 2026-11-05): each flush's unlock notes the
     * fence wait it just did (the GPU's tail); the next lock of any bitmap,
     * where an overlapped GPU would be waited for, notes the time since.
     * The smaller of the two is what overlapping would have saved. */
    ULONG ov_unlock;            /* Ticks() at the unlock */
    ULONG ov_tail;              /* that flush's fence wait, us */
    ULONG ov_armed;             /* an unlock waits for its next lock */
    ULONG ov_n, ov_tail_sum, ov_gap_sum, ov_save_sum;
    ULONG ov_by[3];             /* that lock: a frame's flush, a flush mid-frame, a heap lock */

    /* how the texture heap was placed (Card_Open): every try, for the log */
    ULONG heap_way;             /* CARD_HEAP_* of the heap in use */
    ULONG heap_try_n;
    UBYTE heap_try[CARD_HEAP_TRIES]; /* (MB << 3) | (way << 1) | on the board */

    /* 2026-11-12, triple buffering (lib/cardflip.c): the screen kind draws
     * into three screen buffers, b[CARD_SCREEN] the one the frame being
     * built draws into, and shows them in turn -- no present copy */
    ULONG flip_on;
    struct CardFlip *flip;      /* asked for three buffers: the state (kept over a resize) */
    ULONG scr_intui;            /* the GL screen is Intuition's (CloseScreen, not p96CloseScreen) */
};

extern ULONG Card_ScreensLeft;  /* GL screens a visitor window kept open at Card_Close */
/* Breadcrumbs for the debug trap catcher (trapcatch.h, RGL_TRAPCATCH):
 * where the card code is, and the bitmaps' CPU addresses at the last
 * lock. Nothing in the release build. */
#if defined(RGL_TRACE)
/* The trace build (RGL_DEFS=-DRGL_TRACE, 2026-11-11, GLBench Pelt freezing
 * the A4000D with no MuForce hit): every WHERE() becomes a line on the
 * serial port, so the last line before a freeze names the step. */
void Card_Trace(ULONG n, ULONG v);
#define WHERE(n, v) Card_Trace((n), (ULONG) (v))
#elif defined(RGL_TRAPCATCH) && defined(RGL_CPU_040)
extern volatile ULONG rgl_where, rgl_where2, rgl_pending;
extern volatile ULONG rgl_bm[CARD_NBM];
#define WHERE(n, v) (rgl_where = (n), rgl_where2 = (ULONG) (v))
#else
#define WHERE(n, v) ((void) 0)
#endif

extern ULONG Card_NoHeap32;     /* test switch (RadeonGLHeap32=0): skip the 32-bit heap */
extern ULONG Card_HeapMax;      /* test switch (RadeonGLHeapMax=MB): a smaller heap; 0 = 16 MB */
extern ULONG Card_HeapNarrow;   /* test switch (RadeonGLHeapNarrow=1): only 0.4's narrow heap, which froze */

#define CARD_LOST_CHECKER  1    /* the checker refused a packet */
#define CARD_LOST_FENCE    2    /* a fence timed out */
#define CARD_LOST_CP       3    /* the CP would not start or stop */
#define CARD_LOST_EMIT     4    /* a command did not fit or was malformed (R2_E*) */
#define CARD_LOST_HEAP     5    /* P96 moved the heap off a 32-byte boundary */
#define CARD_LOST_SCREEN   6    /* a resize could open neither the new screen nor the old one again */

/* Open the GL screen (w x h, depth 16 or 32 asked) on a Radeon and set
 * everything up. CARD_OK, CARD_NOT_RADEON (nothing kept), or CARD_FAILED
 * with why[] (nothing kept). check: run the packet checker. modeid: a
 * Picasso96 mode of a Radeon board, w x h its size (MGLCreateContextFromID),
 * or 0 / CARD_BESTMODE for the best mode for w x h. */
#define CARD_BESTMODE 0xFFFFFFFFUL
ULONG Card_Open(struct Card *k, ULONG w, ULONG h, ULONG depth, ULONG modeid, ULONG check,
                char *why, ULONG whylen);

/* Options for the window and bitmap contexts, set before the open */
struct CardOpts
{
    ULONG heap_top;             /* the texture heap's first try, bytes (0: the default) */
};

/* W1: a context drawing into win's inner area (MGLCreateContextFromWindow),
 * the window staying the program's. The window's screen must be on a
 * Radeon's board, R5G6B5PC, R5G5B5PC or B8G8R8A8. The frame is rendered off
 * screen and blitted into the window after each flush. CARD_OK,
 * CARD_NOT_RADEON (nothing kept: the null card may take it) or CARD_FAILED
 * with why[]. */
ULONG Card_OpenWindow(struct Card *k, struct Window *win, const struct CardOpts *o, ULONG check,
                      char *why, ULONG whylen);

/* W1: the same with a window of our own, w x h inside, at x, y on the
 * default public screen (mglChooseWindowMode + MGLCreateContext): a drag
 * bar, a depth gadget, no IDCMP (the program asks for what it needs). */
ULONG Card_OpenOwnWindow(struct Card *k, LONG x, LONG y, ULONG w, ULONG h, const struct CardOpts *o,
                         ULONG check, char *why, ULONG whylen);

/* W1: a context drawing into the program's bitmap bm (MGLCreateContextFromBitMap):
 * a P96 bitmap on a Radeon's board, R5G6B5PC, R5G5B5PC or B8G8R8A8, its
 * rows 16-byte aligned and a multiple of 8 pixels. The Radeon draws straight
 * into bm (as MiniGLV3D does: what the program drew there stays where GL
 * draws nothing); the depth buffer is ours. MGLSwitchDisplay returns with
 * the frame finished; the program shows bm itself. */
ULONG Card_OpenBitMap(struct Card *k, struct BitMap *bm, const struct CardOpts *o, ULONG check,
                      char *why, ULONG whylen);

/* The window kind: the window's inner size now (for the client's resize). */
void Card_InnerSize(const struct Card *k, ULONG *w, ULONG *h);

/* A Radeon board's screen mode (mglGetSupportedScreenModes). */
struct CardMode
{
    ULONG id;
    ULONG width, height, depth;
    char name[48];
};

/* Each mode of the Radeon boards in the GL screen's format for depth (16:
 * R5G6B5PC, 32: B8G8R8A8), fn called for each until it returns TRUE: that
 * mode's id, else CARD_BESTMODE. Opens Picasso96API.library itself when no
 * context has it open. */
ULONG Card_EachMode(ULONG depth, BOOL (*fn)(const struct CardMode *m, void *ud), void *ud);

/* A mode's size; FALSE when it is not a Picasso96 mode of a Radeon board. */
BOOL Card_ModeSize(ULONG id, ULONG *w, ULONG *h);

/* 2026-11-06 (mglResizeContext): the GL screen, its input window and the
 * back buffer anew at w x h, the best mode for it on the same board and in
 * the same format; the ring and the texture heap stay, so textures and
 * static arrays survive. CARD_OK; CARD_FAILED with why[] and the old size
 * kept (k->lost = CARD_LOST_SCREEN if not even that could be reopened). The
 * caller updates the core (Card_Target, Core_Retarget).
 * Window kind: the back buffer and the show bitmap anew (the window is the
 * program's to size). Bitmap kind: the back buffer anew, at most the
 * program's bitmap's size; the frame lands in its top left corner. */
ULONG Card_Resize(struct Card *k, ULONG w, ULONG h, char *why, ULONG whylen);

/* For Core_Init: the target and the heap (heap_cpu is NULL: the core
 * reaches the heap only through Card_VramBegin). */
void Card_Target(const struct Card *k, struct CoreTarget *t);

/* After Core_Init: the hooks and the staging buffer. */
void Card_Attach(struct Card *k, struct Core *c);

/* Run what the core built; with present, show the back buffer on the
 * screen. R2_OK, or the error that lost the context (k->lost set). */
LONG Card_Flush(struct Card *k, BOOL present);

/* The core's hooks (env as the core passes it is the library's; it hands
 * over the card). */
LONG Card_WaitIdle(struct Card *k);
ULONG *Card_VramBegin(struct Card *k);
void Card_VramEnd(struct Card *k);

/* W3: the card semaphore (once, from Back_Init), and holding it around
 * work on a card's bitmaps outside these calls (lib/rglread.c) */
void Card_Init(void);
void Card_Obtain(void);
void Card_Release(void);

/* Free everything. No file I/O (LibClose may call it under Forbid). */
void Card_Close(struct Card *k);

/* A register read (cardflip.c's beam check around ScrollVPort; reads only) */
ULONG Card_ReadReg(struct Card *k, ULONG reg);

/* rglport.c: every flush to the Radeon, for the public port (rglport.h) */
void Port_Flushed(void);

#endif /* RADEONGL_CARD_H */
