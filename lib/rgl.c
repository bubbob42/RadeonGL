/*
 * rgl.c -- the RadeonGL back end of minigl.library: the dispatch hooks
 * (SDK types in, the core's types out), the context, and what runs it:
 * the Radeon (card.h: an R200 on the board P96 offers) or, without one,
 * the null card (nullcard.h), which checks every packet and draws nothing.
 *
 * RadeonGLCard=null forces the null card on a machine with a Radeon;
 * RadeonGLCard=radeon refuses rather than fall back. A Radeon that is found
 * but cannot be used refuses the context, with the reason in the log and a
 * requester.
 *
 * One context at a time, owned by the task that made it (concept 7.1).
 * W3 (2026-11-08): several contexts, each program its own; a hook draws
 * with the context it is called with (the SDK passes the calling task's
 * current context: shell.c gives every task a table of its own).
 *
 * Log: when the context is deleted, a summary goes to the file named by
 * the variable RadeonGLLog (default PROGDIR:radeongl.log): frames, bus
 * dwords, draws, packet-checker errors with the first one decoded, and
 * every dispatch slot called that is not implemented yet.
 *
 * RadeonGLCheck=0 switches the packet checker off: the null card consumes
 * the ring unchecked (a timedemo times the emitter alone), the Radeon runs
 * each flush unchecked.
 *
 * RadeonGLDirect=1 (the Radeon): the locked arrays' vertices
 * and indices are written straight into the texture heap in video RAM,
 * one short heap lock a draw, instead of staged in fast RAM and copied in
 * the flush's lock (core.h Core_SetVram). With the stream written in place
 * (this, and always on the null card) the upload cache keeps repeated
 * ranges and index lists in video RAM across frames (array.c);
 * RadeonGLUploadCache=0 turns it off for A/B timing.
 *
 * Frame dump (null card only): with RadeonGLDump=N the null card records frame N (the core
 * re-emits its whole state at that frame's start, so the recording stands
 * alone) and writes it with the texture heap to RadeonGLDumpFile (default
 * PROGDIR:radeongl-frame.r2d). test/host/r200view renders it on the host.
 *
 * Built WITHOUT -msasregparm: clients call the table with GCC's stack
 * convention. -m68060 -m68881 together (docs/core.md).
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <libraries/minigl_dispatch.h>

#include "rgl_dispatch.h"
#include "backend.h"
#include "core.h"
#include "coreint.h"
#include "r2ring.h"
#include "r2check.h"
#include "r200reg.h"
#include "nullcard.h"
#include "card.h"
#include "cardflip.h"
#include "trapcatch.h"
#include "vtxfast.h"
#include "../src/RadeonGL_rev.h"

/* The library keeps minigl.library's version (29.1: what clients ask for);
 * the date and the RadeonGL release come from the project's revision file,
 * which only `gmake release` bumps. The CPU says which of the two builds it
 * is (Makefile: RGL_CPU_040). */
#ifdef RGL_CPU_040
#define RGL_CPU "68040/68060"
#else
#define RGL_CPU "68020/68030 + 68881/68882"
#endif
const char Back_IdString[] =
    "$VER: minigl.library 29.1 (" DATE ") " VERS " (R200 through P96; null card without one), "
    "dispatch ABI 5, for the " RGL_CPU "\r\n";

/* The fake GPU address map of the null card. */
#define RING_GPU    0x00100000UL
#define COLOR_GPU   0x01000000UL
#define DEPTH_GPU   0x01800000UL
#define HEAP_GPU    0x02000000UL
#define RING_DW     65536UL         /* 256 KB */
#define FENCE_REG   R_SCRATCH_REG2
#define REC_DW      (512UL * 1024)  /* a recorded frame: 2 MB of packets at most */
#define DUMP_MAGIC  0x5232444dUL    /* 'R2DM' */

#include "rglint.h"
#include "rgltable.h"

GLcontext Rgl_CurrentContext = 0;
struct Rgl *rgl_list = NULL;            /* every context, newest first (W3; rglcompat.c reads it) */
static ULONG choose_depth = 16;     /* mglChoosePixelDepth */
static ULONG choose_window = 0;     /* mglChooseWindowMode: MGLCreateContext opens a window */
ULONG Rgl_UnimplEarly[(DISPATCH_SLOTS + 31) / 32];    /* rgllog.c reads it */
/* a context is being made or freed: one at a time, and none while another
 * is torn down (P96Base, TimerBase and the card's globals are shared) */
static ULONG rgl_busy = 0;

/* --- Memory and the core's environment ------------------------------------------ */

static APTR EnvAlloc(APTR env, ULONG size)
{
    (void) env;
    return AllocVec(size, MEMF_ANY | MEMF_CLEAR);
}

/* The upload cache's mirror (core.h CoreEnv alloc_raw): not zeroed, 2 MB
 * cleared cost ~90 ms of every create on the A4000D (2026-11-12) */
static APTR EnvAllocRaw(APTR env, ULONG size)
{
    (void) env;
    return AllocVec(size, MEMF_ANY);
}

static void EnvFree(APTR env, APTR mem, ULONG size)
{
    (void) env;
    (void) size;
    FreeVec(mem);
}

static LONG EnvIdle(APTR env)
{
    struct Rgl *r;

    r = (struct Rgl *) env;
    if (r->real)
        return Card_WaitIdle(&r->rad);
    return R2Ring_Idle(&r->ring);
}

/* The Radeon's heap is reached only inside a lock (card.h). */
static ULONG *EnvVramBegin(APTR env)
{
    return Card_VramBegin(&((struct Rgl *) env)->rad);
}

static void EnvVramEnd(APTR env)
{
    Card_VramEnd(&((struct Rgl *) env)->rad);
}

/* The checker reads the indices of every indexed draw from the heap
 * (locked arrays, array.c). */
static LONG HeapPeek(APTR ctx, ULONG gpu, ULONG *val)
{
    struct Rgl *r;

    r = (struct Rgl *) ctx;
    if (gpu < HEAP_GPU || gpu - HEAP_GPU + 4 > r->heap_size)
        return -1;
    *val = r->heap_mem[(gpu - HEAP_GPU) / 4];
    return 0;
}

/* A stub was called (no context argument): the calling task's context
 * notes it */
void Rgl_Unimpl(ULONG slot)
{
    struct Rgl *r;
    struct Task *me;
    ULONG *bits;

    me = FindTask(NULL);
    for (r = rgl_list; r != NULL && r->owner != me; r = r->next)
        ;
    bits = (r != NULL) ? r->unimpl : Rgl_UnimplEarly;
    bits[slot / 32] |= 1UL << (slot % 32);
    if (r != NULL)
        Core_Error(&r->core, GL_INVALID_OPERATION);
}

/* Making and freeing contexts, one at a time (P96Base, the card's
 * globals): wait for another task's turn instead of refusing (W3) */
static void Busy(void)
{
    ULONG got;

    for (;;)
    {
        Forbid();
        got = rgl_busy ? 0UL : 1UL;
        if (got)
            rgl_busy = 1;
        Permit();
        if (got)
            return;
        Delay(1);
    }
}

/* --- The log: rgllog.c (linked behind the hot code, 2026-11-13) ----------------------- */

/* --- Context ------------------------------------------------------------------------- */

/* The recorded frame and the texture heap, as big-endian dwords:
 * magic, version, stream swap, texel swap, width, height, colour GPU,
 * pitch, format, depth GPU, pitch, heap GPU, heap bytes, packet dwords,
 * frame number; the packets; the heap. */
static void WriteDump(struct Rgl *r)
{
    BPTR f;
    ULONG h[15];
    ULONG top;

    f = Open((STRPTR) r->dump_path, MODE_NEWFILE);
    if (f == 0)
        return;
    top = Heap_Top(&r->core);
    h[0] = DUMP_MAGIC;
    h[1] = 1;
    h[2] = r->ring.cs.swap;
    h[3] = r->core.tex_swap;
    h[4] = r->core.target.width;
    h[5] = r->core.target.height;
    h[6] = r->core.target.color_gpu;
    h[7] = r->core.target.color_pitch;
    h[8] = r->core.target.color_format;
    h[9] = r->core.target.depth_gpu;
    h[10] = r->core.target.depth_pitch;
    h[11] = r->core.heap.gpu;
    h[12] = top;
    h[13] = r->card.rec_n;
    h[14] = r->frame;
    Write(f, h, sizeof(h));
    Write(f, r->card.rec, (LONG) (r->card.rec_n * 4));
    Write(f, r->heap_mem, (LONG) top);
    Close(f);
    r->dump_written = r->card.rec_n;
}

static struct Window *InputWindow(struct Rgl *r)
{
    return r->real ? r->rad.win : r->win;
}

/* MiniGL's vid_DeletePointer: the window's own pointer again, the empty
 * sprite freed (after ClearPointer: Intuition shows it until then) */
static void PointerBack(struct Rgl *r)
{
    if (r->ptr_blank == NULL)
        return;
    if (InputWindow(r) != NULL)
        ClearPointer(InputWindow(r));
    FreeMem(r->ptr_blank, 8);
    r->ptr_blank = NULL;
}

static void FreeRgl(struct Rgl *r)
{
    ULONG i;

    Trap_Remove();
    PointerBack(r);
    if (r->card.rec != NULL)
        FreeVec(r->card.rec);
    Rgldl_Free(r);
    Core_Free(&r->core);
    if (r->real)
        Card_Close(&r->rad);
    if (r->win != NULL && !r->win_app)
    {
        ModifyIDCMP(r->win, 0);
        CloseWindow(r->win);
    }
    if (r->heap_mem != NULL)
        FreeVec(r->heap_mem);
    if (r->ring_mem != NULL)
        FreeVec(r->ring_mem);
    FreeVec(r);
    /* what one program chose is not the next one's */
    choose_depth = 16;
    for (i = 0; i < (DISPATCH_SLOTS + 31) / 32; i++)
        Rgl_UnimplEarly[i] = 0;
}

void Back_Init(void)
{
    Card_Init();                    /* the card semaphore (W3) */
    Port_Add();                     /* the public port "RadeonGL" (rglport.c) */
}

void Back_Expunge(void)
{
    Port_Remove();
}

/* The null card's ring and heap in fast RAM, and its checker. FALSE when
 * there is no memory for them. */
static BOOL NullCardOpen(struct Rgl *r, ULONG w, ULONG h, ULONG check, ULONG heap_max,
                         struct CoreTarget *t)
{
    struct R2ChkCfg cfg;
    ULONG pitch;
    ULONG i;

    r->ring_mem = (ULONG *) AllocVec(RING_DW * 4, MEMF_ANY | MEMF_CLEAR);
    /* the texture heap: as much of 32 MB as there is (or of RadeonGLHeapMax) */
    r->heap_size = (heap_max >= 4 && heap_max < 32) ? heap_max << 20 : 32UL << 20;
    for (; r->heap_size >= (4UL << 20); r->heap_size >>= 1)
    {
        r->heap_mem = (ULONG *) AllocVec(r->heap_size, MEMF_ANY);
        if (r->heap_mem != NULL)
            break;
    }
    if (r->ring_mem == NULL || r->heap_mem == NULL)
        return FALSE;

    /* rows a multiple of 32 bytes: the present reads the back buffer as a
     * texture (R200 TXPITCH, Mesa); 16 pixels at 16 bits */
    pitch = (w + 15) & ~15UL;
    for (i = 0; i < sizeof(cfg); i++)
        ((UBYTE *) &cfg)[i] = 0;
    cfg.range[0].gpu = COLOR_GPU; cfg.range[0].size = pitch * h * 2; cfg.range[0].kinds = R2K_COLOR;
    cfg.range[1].gpu = DEPTH_GPU; cfg.range[1].size = pitch * h * 2; cfg.range[1].kinds = R2K_DEPTH;
    cfg.range[2].gpu = HEAP_GPU;  cfg.range[2].size = r->heap_size;
    cfg.range[2].kinds = R2K_TEXTURE | R2K_VERTEX | R2K_INDEX;      /* textures, the stream */
    cfg.nranges = 3;
    cfg.peek = HeapPeek;
    cfg.peek_ctx = r;
    cfg.fence_reg = FENCE_REG;
    NullCard_Init(&r->card, r->ring_mem, RING_DW, 1, FENCE_REG, &cfg);
    r->card.no_check = check ? 0 : 1;
    if (R2Ring_Init(&r->ring, r->ring_mem, RING_GPU, RING_DW, &r->card.hw, 1, FENCE_REG,
                    1000000) != R2_OK)
        return FALSE;
    r->ring.kick = 2048;        /* hand work to the "GPU" every 8 KB */

    t->width = w;
    t->height = h;
    t->color_gpu = COLOR_GPU;
    t->color_pitch = pitch;
    t->color_format = R_COLOR_FORMAT_RGB565;
    t->depth_gpu = DEPTH_GPU;
    t->depth_pitch = pitch;
    return TRUE;
}

/* The null card's window: the client takes its input from it (GLQuake
 * reads mglGetWindowHandle unchecked). FALSE: none. */
static BOOL NullCardWindow(struct Rgl *r)
{
    struct Screen *scr;

    scr = LockPubScreen(NULL);
    if (scr == NULL)
        return FALSE;
    r->win = OpenWindowTags(NULL,
        WA_PubScreen, (ULONG) scr,
        WA_Left, 20,
        WA_Top, 20,
        WA_InnerWidth, 320UL,
        WA_InnerHeight, 120UL,
        WA_Title, (ULONG) "RadeonGL null card -- checks, draws nothing",
        WA_DragBar, TRUE,
        WA_DepthGadget, TRUE,
        WA_Activate, TRUE,
        WA_RMBTrap, TRUE,
        WA_NoCareRefresh, TRUE,
        TAG_DONE);
    UnlockPubScreen(NULL, scr);
    return (BOOL) (r->win != NULL);
}

/* What a program asked for (W1: a screen, a window, its window, its bitmap) */
struct Ask
{
    ULONG kind;                 /* CARD_KIND_* */
    LONG x, y;                  /* our own window's place */
    LONG w, h;                  /* the screen's or our own window's size */
    ULONG modeid;               /* MGLCreateContextFromID */
    struct Window *win;         /* MGLCreateContextFromWindow */
    struct BitMap *bm;          /* MGLCreateContextFromBitMap */
};

static void *CreateContext(const struct Ask *a);

static void *CreateScreenOrWindow(LONG x, LONG y, LONG w, LONG h, ULONG modeid)
{
    struct Ask a;

    a.kind = choose_window ? CARD_KIND_WINDOW : CARD_KIND_SCREEN;
    a.x = x;
    a.y = y;
    a.w = w;
    a.h = h;
    a.modeid = modeid;
    a.win = NULL;
    a.bm = NULL;
    return CreateContext(&a);
}

/* mglChooseWindowMode(GL_TRUE) first: a window of ours on the default
 * public screen, offx/offy its place (W1) */
void *Hook_MGLCreateContext(int offx, int offy, int w, int h)
{
    return CreateScreenOrWindow(offx, offy, w, h, 0);
}

/* W1: the program's window; the drawable is its inner area, the frame is
 * blitted in at every MGLSwitchDisplay. The window stays the program's. */
void *Hook_MGLCreateContextFromWindow(struct Window *window)
{
    struct Ask a;

    if (window == NULL)
        return NULL;
    a.kind = CARD_KIND_WINDOW;
    a.x = a.y = 0;
    a.w = a.h = 0;
    a.modeid = 0;
    a.win = window;
    a.bm = NULL;
    return CreateContext(&a);
}

/* W1: the program's bitmap; MGLSwitchDisplay copies the frame into it and
 * the program shows it itself. */
void *Hook_MGLCreateContextFromBitMap(struct BitMap *bitmap)
{
    struct Ask a;

    if (bitmap == NULL)
        return NULL;
    a.kind = CARD_KIND_BITMAP;
    a.x = a.y = 0;
    a.w = a.h = 0;
    a.modeid = 0;
    a.win = NULL;
    a.bm = bitmap;
    return CreateContext(&a);
}

/* 2026-11-06: a context in exactly that Picasso96 mode (*w, *h get its
 * size); MGL_SM_BESTMODE: the best mode for *w x *h, as MGLCreateContext;
 * MGL_SM_WINDOWMODE (a window on the Workbench): not offered, NULL. */
void *Hook_MGLCreateContextFromID(GLint ID, GLint *w, GLint *h)
{
    struct Ask a;
    ULONG mw, mh;

    if (w == NULL || h == NULL)
        return NULL;
    if ((ULONG) ID == MGL_SM_WINDOWMODE)
    {
        /* W1: a window of ours on the default public screen */
        a.kind = CARD_KIND_WINDOW;
        a.x = a.y = 0;
        a.w = *w;
        a.h = *h;
        a.modeid = 0;
        a.win = NULL;
        a.bm = NULL;
        return CreateContext(&a);
    }
    if ((ULONG) ID == MGL_SM_BESTMODE)
        return CreateScreenOrWindow(0, 0, *w, *h, 0);
    if (!Card_ModeSize((ULONG) ID, &mw, &mh))
        return NULL;
    *w = (GLint) mw;
    *h = (GLint) mh;
    a.kind = CARD_KIND_SCREEN;
    a.x = a.y = 0;
    a.w = (LONG) mw;
    a.h = (LONG) mh;
    a.modeid = (ULONG) ID;
    a.win = NULL;
    a.bm = NULL;
    return CreateContext(&a);
}

/* 2026-11-06: the Radeon boards' modes in the depth mglChoosePixelDepth
 * asked for, each to CallbackFn until it returns GL_TRUE: that mode's id,
 * else MGL_SM_BESTMODE. */
struct ModeWalk
{
    MGLScreenModeCallback fn;
};

static BOOL ModeToClient(const struct CardMode *m, void *ud)
{
    struct ModeWalk *mw;
    MGLScreenMode s;
    ULONG i;

    mw = (struct ModeWalk *) ud;
    s.id = (GLint) m->id;
    s.width = (GLint) m->width;
    s.height = (GLint) m->height;
    s.bit_depth = (GLint) m->depth;
    for (i = 0; i < MGL_MAX_MODE - 1 && m->name[i] != 0; i++)
        s.mode_name[i] = m->name[i];
    s.mode_name[i] = 0;
    return (BOOL) (mw->fn(&s) == GL_TRUE);
}

GLint Hook_mglGetSupportedScreenModes(MGLScreenModeCallback CallbackFn)
{
    struct ModeWalk mw;

    if (CallbackFn == NULL)
        return (GLint) MGL_SM_BESTMODE;
    mw.fn = CallbackFn;
    return (GLint) Card_EachMode(choose_depth, ModeToClient, &mw);
}

/* The null card's size for what was asked (it draws nothing; tests) */
static BOOL AskSize(const struct Ask *a, LONG *w, LONG *h)
{
    const struct Window *win;

    *w = a->w;
    *h = a->h;
    if (a->win != NULL)
    {
        win = a->win;
        if (win->Flags & WFLG_GIMMEZEROZERO)
        {
            *w = win->GZZWidth;
            *h = win->GZZHeight;
        }
        else
        {
            *w = win->Width - win->BorderLeft - win->BorderRight;
            *h = win->Height - win->BorderTop - win->BorderBottom;
        }
    }
    else if (a->bm != NULL)
    {
        *w = (LONG) GetBitMapAttr(a->bm, BMA_WIDTH);
        *h = (LONG) GetBitMapAttr(a->bm, BMA_HEIGHT);
    }
    return (BOOL) (*w > 0 && *h > 0 && *w <= 2048 && *h <= 2048);
}

static void *CreateContext(const struct Ask *a)
{
    struct Rgl *r;
    struct CoreTarget t;
    struct CoreEnv env;
    struct CardOpts opts;
    LONG w, h;
    char why[96];
    char which[12];
    ULONG check;
    ULONG heap_max;
    ULONG res;
    LONG n;
    ULONG i;
    ULONG staged;
    char flag[8];

    if (!AskSize(a, &w, &h))
        return NULL;
    Busy();
    r = (struct Rgl *) AllocVec(sizeof(*r), MEMF_ANY | MEMF_CLEAR);
    if (r == NULL)
    {
        rgl_busy = 0;
        return NULL;
    }
    r->owner = FindTask(NULL);
    r->kind = a->kind;
    n = GetVar((STRPTR) "RadeonGLLog", (STRPTR) r->log, sizeof(r->log), 0);
    if (n <= 0)
        for (i = 0; i < sizeof("PROGDIR:radeongl.log"); i++)
            r->log[i] = "PROGDIR:radeongl.log"[i];
    check = 1;
    if (GetVar((STRPTR) "RadeonGLCheck", (STRPTR) flag, sizeof(flag), 0) > 0 && flag[0] == '0')
        check = 0;
    /* a test switch: RadeonGLHeapMax=n caps the texture heap at n MB (4 and
     * up), to try a game on the heap a smaller board or Workbench gives */
    heap_max = 0;
    n = GetVar((STRPTR) "RadeonGLHeapMax", (STRPTR) flag, sizeof(flag), 0);
    for (i = 0; n > 0 && i < (ULONG) n && flag[i] >= '0' && flag[i] <= '9'; i++)
        heap_max = heap_max * 10 + (ULONG) (flag[i] - '0');

    /* the Radeon, unless told otherwise; the null card without one */
    which[0] = 0;
    (void) GetVar((STRPTR) "RadeonGLCard", (STRPTR) which, sizeof(which), 0);
    res = CARD_NOT_RADEON;
    if (which[0] != 'n' && which[0] != 'N')
    {
        /* a test switch: RadeonGLHeap32=0 skips the last-resort heap as a
         * friend of a 32-bit Workbench (card.c, the heap ladder) */
        Card_NoHeap32 = (GetVar((STRPTR) "RadeonGLHeap32", (STRPTR) flag, sizeof(flag), 0) > 0
                         && flag[0] == '0') ? 1UL : 0UL;
        Card_HeapMax = heap_max;
        /* RadeonGLHeapNarrow=1: straight to the last rung, 0.4's narrow heap */
        Card_HeapNarrow = (GetVar((STRPTR) "RadeonGLHeapNarrow", (STRPTR) flag, sizeof(flag), 0) > 0
                           && flag[0] == '1') ? 1UL : 0UL;
        opts.heap_top = 0;
        if (a->kind == CARD_KIND_SCREEN)
            res = Card_Open(&r->rad, (ULONG) w, (ULONG) h, choose_depth, a->modeid, check, why, sizeof(why));
        else if (a->win != NULL)
            res = Card_OpenWindow(&r->rad, a->win, &opts, check, why, sizeof(why));
        else if (a->bm != NULL)
            res = Card_OpenBitMap(&r->rad, a->bm, &opts, check, why, sizeof(why));
        else
            res = Card_OpenOwnWindow(&r->rad, a->x, a->y, (ULONG) w, (ULONG) h, &opts, check, why,
                                     sizeof(why));
        /* W1: a window or bitmap context goes to the null card only when a
         * test asks for it (RadeonGLCard=n): a program drawing into its own
         * window must hear that nothing would be drawn */
        if (res == CARD_FAILED
            || (res == CARD_NOT_RADEON && (which[0] == 'r' || which[0] == 'R' || a->kind != CARD_KIND_SCREEN)))
        {
            Rgl_Refused(r, res == CARD_FAILED ? why : "there is no R200 Radeon on a P96 board");
            FreeRgl(r);
            rgl_busy = 0;
            return NULL;
        }
    }
    r->real = (res == CARD_OK) ? 1 : 0;
    /* RadeonGLOverlap=0: every flush waits for the Radeon before the
     * unlock (card.h) */
    if (r->real)
        r->rad.overlap = (GetVar((STRPTR) "RadeonGLOverlap", (STRPTR) flag, sizeof(flag), 0) > 0
                          && flag[0] == '0') ? 0UL : 1UL;
    /* 2026-11-07, RadeonGLMove16=1 (with a data-cache push) or 2 (without):
     * the flush's copies into video RAM as MOVE16 line bursts -- a 68060
     * only, off unless set; a HARDWARE RISK until Move16Bench has shown the
     * bus takes bursts (card.h) */
    if (r->real && (SysBase->AttnFlags & AFF_68060)
        && GetVar((STRPTR) "RadeonGLMove16", (STRPTR) flag, sizeof(flag), 0) > 0
        && (flag[0] == '1' || flag[0] == '2'))
        r->rad.move16 = (ULONG) (flag[0] - '0');
    /* 2026-11-07, RadeonGLDisplayPriority=high (the display FIFO's critical
     * point 0: high priority always) or a critical point 0-127, while the
     * context lives; unset leaves Radeon.card's (card.c DisplayPriority) */
    if (r->real && GetVar((STRPTR) "RadeonGLDisplayPriority", (STRPTR) flag, sizeof(flag), 0) > 0)
    {
        if (flag[0] == 'h' || flag[0] == 'H')
        {
            r->rad.dprio_on = 1;
            r->rad.dprio_point = 0;
        }
        else if (flag[0] >= '0' && flag[0] <= '9')
        {
            r->rad.dprio_on = 1;
            for (i = 0; i < sizeof(flag) && flag[i] >= '0' && flag[i] <= '9'; i++)
                r->rad.dprio_point = r->rad.dprio_point * 10 + (ULONG) (flag[i] - '0');
            if (r->rad.dprio_point > 127)
                r->rad.dprio_point = 127;
        }
    }
    if (r->real)
        Card_Target(&r->rad, &t);
    else
    {
        if (!NullCardOpen(r, (ULONG) w, (ULONG) h, check, heap_max, &t))
        {
            FreeRgl(r);
            rgl_busy = 0;
            return NULL;
        }
        n = GetVar((STRPTR) "RadeonGLDump", (STRPTR) r->dump_path, sizeof(r->dump_path), 0);
        for (i = 0; n > 0 && i < (ULONG) n && r->dump_path[i] >= '0' && r->dump_path[i] <= '9'; i++)
            r->dump_frame = r->dump_frame * 10 + (ULONG) (r->dump_path[i] - '0');
        n = GetVar((STRPTR) "RadeonGLDumpFile", (STRPTR) r->dump_path, sizeof(r->dump_path), 0);
        if (n <= 0)
            for (i = 0; i < sizeof("PROGDIR:radeongl-frame.r2d"); i++)
                r->dump_path[i] = "PROGDIR:radeongl-frame.r2d"[i];
    }

    env.alloc = EnvAlloc;
    env.alloc_raw = EnvAllocRaw;
    env.free = EnvFree;
    env.wait_idle = EnvIdle;
    env.env = r;
    /* r->core is all zero (Core_Init's promise, core.h): AllocVec
     * MEMF_CLEAR above, untouched until here */
    if (Core_Init(&r->core, r->real ? &r->rad.cs : &r->ring.cs, &env, &t,
                  r->real ? r->rad.heap_gpu : HEAP_GPU, r->real ? NULL : r->heap_mem,
                  r->real ? r->rad.heap_size : r->heap_size) != GL_NO_ERROR)
    {
        FreeRgl(r);
        rgl_busy = 0;
        return NULL;
    }
    Rgl_ViewsAttach(r);             /* R4: GL_RGL_bitmap_view (cardview.c) */
    if (r->real)
    {
        Card_Attach(&r->rad, &r->core);
        /* the locked-array stream: staged in fast RAM and copied inside the
         * flush's lock (the default: one lock a frame), or with
         * RadeonGLDirect=1 straight into video RAM, one short heap lock a
         * locked draw (P96's data-bitmap pattern: lock, fill, unlock; the
         * address fresh at every lock). The checker then checks the
         * indices' bounds, not their values (card.c). */
        staged = !(GetVar((STRPTR) "RadeonGLDirect", (STRPTR) flag, sizeof(flag), 0) > 0
                   && flag[0] == '1');
        Core_SetVram(&r->core, EnvVramBegin, EnvVramEnd, staged ? r->rad.staging : NULL);
    }
    /* RadeonGLLocked=0: locked arrays go the immediate way (A/B timing; a
     * way back if indexed draws misbehave on the card) */
    if (GetVar((STRPTR) "RadeonGLLocked", (STRPTR) flag, sizeof(flag), 0) > 0 && flag[0] == '0')
    {
        r->no_lock = 1;
        r->core.stream_size = 0;
    }
    /* RadeonGLUploadCache=0: every locked upload written again (A/B);
     * =n: n MB, never grown or shrunk (ucgrow.c; tests, and a way back) */
    if (GetVar((STRPTR) "RadeonGLUploadCache", (STRPTR) flag, sizeof(flag), 0) > 0)
    {
        for (i = 0, n = 0; i < sizeof(flag) && flag[i] >= '0' && flag[i] <= '9'; i++)
            n = n * 10 + (flag[i] - '0');
        if (i > 0 && n == 0)
            r->core.no_ucache = 1;
        else if (n > 0 && n <= 256)
            Core_UCacheFix(&r->core, (ULONG) n << 20);
    }
    if (!r->real && a->win != NULL)
    {
        r->win = a->win;            /* the program's window gives the input */
        r->win_app = 1;
    }
    else if (!r->real && a->bm == NULL && !NullCardWindow(r))
    {
        FreeRgl(r);
        rgl_busy = 0;
        return NULL;
    }
    Forbid();
    r->next = rgl_list;
    rgl_list = r;
    Permit();
    Port_Count();                   /* the public port (rglport.c) */
    Trap_Install();                 /* RadeonGLTrap=1, debug builds only */
    rgl_busy = 0;
    return (void *) r;
}

/* Only the owner deletes its context, and only a context in the list
 * (GLQuake's VID_Shutdown deletes unconditionally, NULL after a refused
 * create; the audit of 2026-11-03 found a second program freeing the
 * first one's context through the shared currentContext). */
void Hook_MGLDeleteContext(GLcontext context)
{
    struct Rgl *r;
    struct Rgl **pp;
    ULONG mine;

    r = (struct Rgl *) context;
    if (r == NULL)
        return;
    Busy();
    mine = 0;
    Forbid();
    for (pp = &rgl_list; *pp != NULL && *pp != r; pp = &(*pp)->next)
        ;
    if (*pp != NULL && r->owner == FindTask(NULL))
    {
        *pp = r->next;
        mine = 1;
    }
    Permit();
    if (mine)
        Port_Count();
    if (!mine)
    {
        rgl_busy = 0;
        return;
    }
    if (Rgl_CurrentContext == context)
        Rgl_CurrentContext = 0;
    if (!r->real)
        R2Ring_Idle(&r->ring);
    Rgl_WriteLog(r);
    FreeRgl(r);
    choose_window = 0;              /* what one program chose is not the next one's */
    rgl_busy = 0;
}

/* LibClose: the owner's last close without mglDeleteContext (shell.c
 * counts its opens). No file I/O here. */
void Back_ClientGone(struct Task *task)
{
    struct Rgl *r;
    struct Rgl **pp;

    if (rgl_busy)
        return;                     /* (under Forbid: no waiting here) */
    rgl_busy = 1;
    pp = &rgl_list;
    while (*pp != NULL)
    {
        r = *pp;
        if (r->owner != task)
        {
            pp = &r->next;
            continue;
        }
        *pp = r->next;
        if (Rgl_CurrentContext == (GLcontext) r)
            Rgl_CurrentContext = 0;
        FreeRgl(r);
    }
    choose_window = 0;
    Rglg_ClientGone(task);
    Port_Count();
    rgl_busy = 0;
}

/* From here on every hook draws with the context it is given (W3): the
 * old single context's name now means the argument */
#define rgl ((struct Rgl *) context)

void Hook_MGLSwitchDisplay(GLcontext context)
{
    struct Rgl *r;
    ULONG seq;
    ULONG dw;

    r = (struct Rgl *) context;
    if (r == NULL)
        return;
    if (r->real)
    {
        /* end of frame: one flush shows it, then every texture is free */
        (void) Card_Flush(&r->rad, TRUE);
        Core_GpuIdle(&r->core);
        Core_UCacheFrame(&r->core);     /* the GPU idle: the cache may grow */
        r->frame++;
        return;
    }
    /* end of frame: a fence, hand it over, wait (the null card is instant) */
    if (R2Ring_Fence(&r->ring, &seq) == R2_OK && R2Ring_WaitFence(&r->ring, seq) == R2_OK)
    {
        Core_GpuIdle(&r->core);     /* every texture is free to rewrite */
        Core_UCacheFrame(&r->core);
    }
    dw = r->card.dwords - r->last_dwords;
    if (dw > r->frame_dw_max)
        r->frame_dw_max = dw;
    r->last_dwords = r->card.dwords;
    if (r->dump_frame != 0)
    {
        if (r->frame + 1 == r->dump_frame && r->card.rec == NULL)
        {
            /* record the next frame, from a full state */
            r->card.rec = (ULONG *) AllocVec(REC_DW * 4, MEMF_ANY);
            r->card.rec_max = REC_DW;
            r->card.rec_n = 0;
            r->core.dirty |= D_INIT;
            Hw_Forget(&r->core);
        }
        else if (r->frame == r->dump_frame && r->card.rec != NULL)
        {
            WriteDump(r);
            FreeVec(r->card.rec);
            r->card.rec = NULL;
            r->dump_frame = 0;
        }
    }
    r->frame++;
}

GLboolean Hook_MGLLockDisplay(GLcontext context)
{
    (void) context;
    return GL_TRUE;
}

void Hook_MGLUnlockDisplay(GLcontext context)
{
    (void) context;
}

void *Hook_MGLGetWindowHandle(GLcontext context)
{
    (void) context;
    if (rgl == NULL)
        return NULL;
    return rgl->real ? (void *) rgl->rad.win : (void *) rgl->win;
}

void *Hook_MGLGetInputWindowHandle(GLcontext context)
{
    return Hook_MGLGetWindowHandle(context);
}

/* 2026-11-06: the context at a new size, as MiniGL's mglResizeContext: a
 * new GL screen (the best mode for the size on the same Radeon, the same
 * format), input window and back buffer; the texture heap and the ring
 * stay, so textures and static arrays survive. The client asks for the
 * window again (mglGetWindowHandle). GL_FALSE: the size stayed (the log
 * says why). The null card: only its target changes. */
GLboolean Hook_MGLResizeContext(GLcontext context, GLsizei width, GLsizei height)
{
    struct CoreTarget t;
    char why[sizeof(rgl->resize_why)];
    ULONG res;
    ULONG pitch;
    ULONG i;
    BOOL hidden;

    (void) context;
    if (rgl == NULL || width <= 0 || height <= 0 || width > 2048 || height > 2048
        || IN_BEGIN(&rgl->core))
        return GL_FALSE;
    /* the same size: nothing to do -- unless the program changed its
     * buffering wish (2026-11-12: GLQuake's Buffering item), then the GL
     * screen opens again the new way */
    if (rgl->real && (ULONG) width == rgl->rad.width && (ULONG) height == rgl->rad.height
        && !(rgl->rad.kind == CARD_KIND_SCREEN && CardFlip_Changed(&rgl->rad)))
        return GL_TRUE;
    hidden = (BOOL) (rgl->ptr_blank != NULL);
    PointerBack(rgl);                   /* its window goes */
    res = CARD_OK;
    if (rgl->real)
    {
        /* what was built for the old back buffer is drawn first */
        (void) Card_Flush(&rgl->rad, FALSE);
        res = Card_Resize(&rgl->rad, (ULONG) width, (ULONG) height, why, sizeof(why));
        /* the back buffer is new either way (the old size reopened) */
        if (rgl->rad.lost == 0)
        {
            Card_Target(&rgl->rad, &t);
            Core_Retarget(&rgl->core, &t);
        }
    }
    else
    {
        R2Ring_Commit(&rgl->ring);
        pitch = ((ULONG) width + 15) & ~15UL;
        rgl->card.cfg.range[0].size = pitch * (ULONG) height * 2;
        rgl->card.cfg.range[1].size = pitch * (ULONG) height * 2;
        t.width = (ULONG) width;
        t.height = (ULONG) height;
        t.color_gpu = COLOR_GPU;
        t.color_pitch = pitch;
        t.color_format = R_COLOR_FORMAT_RGB565;
        t.depth_gpu = DEPTH_GPU;
        t.depth_pitch = pitch;
        Core_Retarget(&rgl->core, &t);
    }
    /* the reason stays until the next refusal: a client asks for it after a
     * refusal (GL_RGL_RESIZE_ERROR), and the log names the last one even
     * when a later resize worked (2026-11-07: "refused 1 (last: )", as
     * Card_Resize empties its why[] first) */
    if (res == CARD_OK)
        rgl->resizes++;
    else
    {
        rgl->resize_failed++;
        for (i = 0; i < sizeof(rgl->resize_why); i++)
            rgl->resize_why[i] = why[i];
    }
    if (hidden)
        Hook_MGLSetPointer(context);
    return (res == CARD_OK) ? GL_TRUE : GL_FALSE;
}

/* MiniGL's extras: stored or harmless */
/* mglDrawMultitexBuffer and mglLockMode live in rglcompat.c since
 * 2026-11-12: triple buffering's log line (WriteCardLog, cardflip.c) grew
 * this file's code by as much as the two take away, so GLQuake's
 * glBegin/glVertex hooks below and everything linked after this object
 * keep their places (the 68060 punishes moved hot code;
 * tools/hot_layout.py). */

void Hook_MGLMinTriArea(GLcontext context, GLfloat area)
{
    (void) context;
    if (rgl != NULL)
        rgl->core.min_tri_area = area;
}

void Hook_MGLSetZOffset(GLcontext context, GLfloat offset)
{
    (void) context;
    if (rgl != NULL)
        Core_SetZOffset(&rgl->core, offset);
}

void Hook_MGLEnableSync(GLcontext context, GLboolean enable)
{
    (void) context;
    if (rgl != NULL)
        rgl->core.sync = enable;
}

/* MiniGL's vid_Pointer: GLQuake leaves hiding the mouse pointer to the
 * library (mglSetPointer at its start, mglClearPointer at its end). An
 * empty sprite, 0 x 0, as MiniGL sets it. */
void Hook_MGLClearPointer(GLcontext context)
{
    (void) context;
    if (rgl != NULL)
        PointerBack(rgl);
}

void Hook_MGLSetPointer(GLcontext context)
{
    (void) context;
#ifdef RGL_KEEP_POINTER
    return;                         /* a debug build that must behave as 0.6 */
#endif
    if (rgl == NULL || rgl->ptr_blank != NULL || InputWindow(rgl) == NULL)
        return;
    rgl->ptr_blank = (UWORD *) AllocMem(8, MEMF_CHIP | MEMF_CLEAR);
    if (rgl->ptr_blank != NULL)
        SetPointer(InputWindow(rgl), rgl->ptr_blank, 0, 0, 0, 0);
}

void Hook_mglChooseGuardBand(GLboolean flag) { (void) flag; }
void Hook_mglChooseMtexBufferSize(int size) { (void) size; }
/* mglChooseNumberOfBuffers lives in cardflip.c (triple buffering,
 * 2026-11-12) */
void Hook_mglChoosePixelDepth(int depth) { choose_depth = (depth > 16) ? 32UL : 16UL; }
void Hook_mglChooseTextureBufferSize(int size) { (void) size; }
void Hook_mglChooseVertexBufferSize(int size) { (void) size; }
void Hook_mglChooseWindowMode(GLboolean flag) { choose_window = (flag != GL_FALSE) ? 1UL : 0UL; }
void Hook_mglChooseZBufferDepth(int bits) { (void) bits; }

/* --- GL --------------------------------------------------------------------------- */

#define C (&rgl->core)
/* a GL call without a context (before mglCreateContext, after
 * mglDeleteContext) does nothing instead of following NULL */
#define GUARD if (rgl == NULL) return
/* after a call that takes an enum: remember who raised the first error */
#define NOTE(name, arg) \
    if (rgl->core.error != 0 && rgl->err_where == NULL) \
    { \
        rgl->err_where = name; \
        rgl->err_arg = (ULONG) (arg); \
    }

void Hook_GLBegin(GLcontext context, GLenum mode) { (void) context; GUARD; Core_Begin(C, mode); NOTE("glBegin", mode); }
void Hook_GLEnd(GLcontext context) { (void) context; GUARD; Core_End(C); }

/* The per-vertex hooks move floats as bit patterns (core.h
 * Core_VertexBits): GLQuake's 700 vertices and as many coordinates a frame
 * then never touch the FPU. w == 1.0f exactly when its bits are
 * 0x3f800000. */
#define FB(f) R2_FBITS(f)
#define FV(v, i) (((const ULONG *) (const void *) (v))[i])

void Hook_GLVertex4f(GLcontext context, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    struct Core *c;
    ULONG bx, by, bz;

    (void) context; GUARD;
    c = C;
    if (FB(w) == 0x3f800000UL)
    {
        bx = FB(x);
        by = FB(y);
        bz = FB(z);
        CORE_VERTEX_FAST(c, bx, by, bz);    /* glVertex3f arrives here (w 1) */
    }
    else
        Core_Vertex4f(c, x, y, z, w);
}

void Hook_GLVertex2fv(GLcontext context, GLfloat *v) { (void) context; GUARD; Core_VertexBits(C, FV(v, 0), FV(v, 1), 0); }
void Hook_GLVertex3fv(GLcontext context, GLfloat *v)
{
    struct Core *c;
    ULONG bx, by, bz;

    (void) context; GUARD;
    c = C;
    bx = FV(v, 0);
    by = FV(v, 1);
    bz = FV(v, 2);
    CORE_VERTEX_FAST(c, bx, by, bz);
}
void Hook_GLVertex4fv(GLcontext context, GLfloat *v) { (void) context; GUARD; Core_Vertex4f(C, v[0], v[1], v[2], v[3]); }
void Hook_GLTexCoord2f(GLcontext context, GLfloat s, GLfloat t) { (void) context; GUARD; CORE_TEXCOORD_BITS(C, 0, FB(s), FB(t)); }
void Hook_GLTexCoord2fv(GLcontext context, GLfloat *v) { (void) context; GUARD; CORE_TEXCOORD_BITS(C, 0, FV(v, 0), FV(v, 1)); }

void Hook_GLMultiTexCoord2fARB(GLcontext context, GLenum unit, GLfloat s, GLfloat t)
{
    (void) context; GUARD;
    if (unit - GL_TEXTURE0_ARB < CORE_UNITS)
        CORE_TEXCOORD_BITS(C, unit - GL_TEXTURE0_ARB, FB(s), FB(t));
    else
        Core_MultiTexCoord2f(C, unit, s, t);    /* the error */
}

void Hook_GLMultiTexCoord2fvARB(GLcontext context, GLenum unit, GLfloat *v)
{
    (void) context; GUARD;
    if (unit - GL_TEXTURE0_ARB < CORE_UNITS)
        CORE_TEXCOORD_BITS(C, unit - GL_TEXTURE0_ARB, FV(v, 0), FV(v, 1));
    else
        Core_MultiTexCoord2f(C, unit, v[0], v[1]);
}

void Hook_GLColor4ub(GLcontext context, GLubyte red, GLubyte green, GLubyte blue, GLubyte alhpa)
{
    (void) context; GUARD;
    Core_Color4ub(C, red, green, blue, alhpa);
}

void Hook_GLColor4ubv(GLcontext context, GLubyte *v) { (void) context; GUARD; Core_Color4ub(C, v[0], v[1], v[2], v[3]); }
void Hook_GLColor3ubv(GLcontext context, GLubyte *v) { (void) context; GUARD; Core_Color4ub(C, v[0], v[1], v[2], 255); }

void Hook_GLColor4f(GLcontext context, GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    (void) context; GUARD;
    Core_Color4f(C, red, green, blue, alpha);
}

void Hook_GLColor4fv(GLcontext context, GLfloat *v) { (void) context; GUARD; Core_Color4f(C, v[0], v[1], v[2], v[3]); }
void Hook_GLColor3fv(GLcontext context, GLfloat *v) { (void) context; GUARD; Core_Color4f(C, v[0], v[1], v[2], 1.0f); }
void Hook_GLNormal3f(GLcontext context, GLfloat x, GLfloat y, GLfloat z) { (void) context; GUARD; Core_Normal3f(C, x, y, z); }
void Hook_GLNormal3fv(GLcontext context, GLfloat *n) { (void) context; GUARD; Core_Normal3f(C, n[0], n[1], n[2]); }

void Hook_MGLSetState(GLcontext context, GLenum cap, GLboolean state)
{
    (void) context;
    GUARD;
    Core_Enable(C, cap, state);     /* the caps 29.1 adds: Core_Enable's unknown-cap path (compat.c) */
    NOTE("glEnable/glDisable", cap);
}

GLboolean Hook_GLIsEnabled(GLcontext context, GLenum cap) { (void) context; return rgl ? (GLboolean) Core_IsEnabled(C, cap) : GL_FALSE; }
void Hook_GLActiveTextureARB(GLcontext context, GLenum unit) { (void) context; GUARD; Core_ActiveTexture(C, unit); NOTE("glActiveTextureARB", unit); }
void Hook_GLClientActiveTextureARB(GLcontext context, GLenum unit) { (void) context; GUARD; Core_ClientActiveTexture(C, unit); NOTE("glClientActiveTextureARB", unit); }

void Hook_GLTexEnvi(GLcontext context, GLenum target, GLenum pname, GLint param)
{
    (void) context; GUARD;
    Core_TexEnvi(C, target, pname, param);
    NOTE("glTexEnvi", param);
}

void Hook_GLDrawElements(GLcontext context, GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
    (void) context; GUARD;
    Core_DrawElements(C, mode, count, type, indices);
    NOTE("glDrawElements", type);
}

void Hook_GLDrawArrays(GLcontext context, GLenum mode, GLint first, GLsizei count)
{
    (void) context; GUARD;
    Core_DrawArrays(C, mode, first, count);
    NOTE("glDrawArrays", mode);
}

void Hook_GLLockArrays(GLcontext context, GLuint first, GLsizei count)
{
    (void) context; GUARD;
    Core_LockArrays(C, (LONG) first, count);
    NOTE("glLockArrays", count);
}

void Hook_GLUnlockArrays(GLcontext context) { (void) context; GUARD; Core_UnlockArrays(C); NOTE("glUnlockArrays", 0); }
void Hook_GLEnableClientState(GLcontext context, GLenum cap) { (void) context; GUARD; Core_EnableClientState(C, cap, 1); NOTE("glEnableClientState", cap); }
void Hook_GLDisableClientState(GLcontext context, GLenum cap) { (void) context; GUARD; Core_EnableClientState(C, cap, 0); NOTE("glDisableClientState", cap); }

void Hook_GLVertexPointer(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    (void) context; GUARD;
    Core_VertexPointer(C, size, type, stride, pointer);
    NOTE("glVertexPointer", type);
}

void Hook_GLTexCoordPointer(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    (void) context; GUARD;
    Core_TexCoordPointer(C, size, type, stride, pointer);
    NOTE("glTexCoordPointer", type);
}

void Hook_GLColorPointer(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    (void) context; GUARD;
    Core_ColorPointer(C, size, type, stride, pointer);
    NOTE("glColorPointer", type);
}

void Hook_GLRotatefEXT(GLcontext context, GLfloat angle, const GLint xyz)
{
    (void) context; GUARD;
    Core_RotateAxisDeg(C, angle, xyz);
    NOTE("glRotatefEXT", xyz);
}

void Hook_GLRotatefEXTs(GLcontext context, GLfloat sin_an, GLfloat cos_an, const GLint xyz)
{
    (void) context; GUARD;
    Core_RotateAxis(C, sin_an, cos_an, xyz);
    NOTE("glRotatefEXTs", xyz);
}

void Hook_GLRotatef(GLcontext context, GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    (void) context; GUARD;
    Core_Rotatef(C, angle, x, y, z);
}

void Hook_GLTranslatef(GLcontext context, GLfloat x, GLfloat y, GLfloat z) { (void) context; GUARD; Core_Translatef(C, x, y, z); }
void Hook_GLScalef(GLcontext context, GLfloat x, GLfloat y, GLfloat z) { (void) context; GUARD; Core_Scalef(C, x, y, z); }
void Hook_GLPushMatrix(GLcontext context) { (void) context; GUARD; Core_PushMatrix(C); NOTE("glPushMatrix", 0); }
void Hook_GLPopMatrix(GLcontext context) { (void) context; GUARD; Core_PopMatrix(C); NOTE("glPopMatrix", 0); }
void Hook_GLMatrixMode(GLcontext context, GLenum mode) { (void) context; GUARD; Core_MatrixMode(C, mode); NOTE("glMatrixMode", mode); }
void Hook_GLLoadIdentity(GLcontext context) { (void) context; GUARD; Core_LoadIdentity(C); }
void Hook_GLLoadMatrixf(GLcontext context, const GLfloat *m) { (void) context; GUARD; Core_LoadMatrixf(C, m); }
void Hook_GLMultMatrixf(GLcontext context, const GLfloat *m) { (void) context; GUARD; Core_MultMatrixf(C, m); }

void Hook_GLOrtho(GLcontext context, GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
                  GLdouble zNear, GLdouble zFar)
{
    (void) context; GUARD;
    Core_Ortho(C, left, right, bottom, top, zNear, zFar);
    NOTE("glOrtho", 0);
}

void Hook_GLFrustum(GLcontext context, GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
                    GLdouble zNear, GLdouble zFar)
{
    (void) context; GUARD;
    Core_Frustum(C, left, right, bottom, top, zNear, zFar);
    NOTE("glFrustum", 0);
}

void Hook_GLShadeModel(GLcontext context, GLenum mode) { (void) context; GUARD; Core_ShadeModel(C, mode); NOTE("glShadeModel", mode); }
void Hook_GLDepthMask(GLcontext context, GLboolean flag) { (void) context; GUARD; Core_DepthMask(C, flag); }
void Hook_GLDepthFunc(GLcontext context, GLenum func) { (void) context; GUARD; Core_DepthFunc(C, func); NOTE("glDepthFunc", func); }
void Hook_GLDepthRange(GLcontext context, GLclampd n, GLclampd f) { (void) context; GUARD; Core_DepthRange(C, n, f); }
void Hook_GLBlendFunc(GLcontext context, GLenum sfactor, GLenum dfactor) { (void) context; GUARD; Core_BlendFunc(C, sfactor, dfactor); NOTE("glBlendFunc", sfactor); }
void Hook_GLAlphaFunc(GLcontext context, GLenum func, GLclampf ref) { (void) context; GUARD; Core_AlphaFunc(C, func, ref); NOTE("glAlphaFunc", func); }
void Hook_GLCullFace(GLcontext context, GLenum mode) { (void) context; GUARD; Core_CullFace(C, mode); NOTE("glCullFace", mode); }
void Hook_GLFrontFace(GLcontext context, GLenum mode) { (void) context; GUARD; Core_FrontFace(C, mode); NOTE("glFrontFace", mode); }

void Hook_GLViewport(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height)
{
    (void) context; GUARD;
    Core_Viewport(C, x, y, width, height);
    NOTE("glViewport", width);
}

void Hook_GLScissor(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height)
{
    (void) context; GUARD;
    Core_Scissor(C, x, y, width, height);
    NOTE("glScissor", width);
}

void Hook_GLColorMask(GLcontext context, GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
    (void) context; GUARD;
    Core_ColorMask(C, red, green, blue, alpha);
}

void Hook_GLPolygonOffset(GLcontext context, GLfloat factor, GLfloat units) { (void) context; GUARD; Core_PolygonOffset(C, factor, units); }
void Hook_GLPolygonMode(GLcontext context, GLenum face, GLenum mode) { (void) context; GUARD; Rgltab_PolygonMode(C, face, mode); NOTE("glPolygonMode", mode); }
void Hook_GLPointSize(GLcontext context, GLfloat size) { (void) context; GUARD; Core_PointSize(C, size); NOTE("glPointSize", 0); }
void Hook_GLHint(GLcontext context, GLenum target, GLenum mode) { (void) context; GUARD; Core_Hint(C, target, mode); NOTE("glHint", target); }

void Hook_GLClearColor(GLcontext context, GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
    (void) context; GUARD;
    Core_ClearColor(C, red, green, blue, alpha);
}

void Hook_GLClearDepth(GLcontext context, GLclampd depth) { (void) context; GUARD; Core_ClearDepth(C, depth); }
void Hook_GLClear(GLcontext context, GLbitfield mask) { (void) context; GUARD; Core_Clear(C, mask); NOTE("glClear", mask); }

void Hook_GLTexImage2D(GLcontext context, GLenum target, GLint level, GLint internalformat,
                       GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type,
                       const GLvoid *pixels)
{
    (void) context; GUARD;
    Core_TexImage2DX(C, target, level, internalformat, width, height, border, format, type, pixels);
    NOTE("glTexImage2D", internalformat);
}

void Hook_GLTexSubImage2D(GLcontext context, GLenum target, GLint level, GLint xoffset, GLint yoffset,
                          GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
    (void) context; GUARD;
    Core_TexSubImage2DX(C, target, level, xoffset, yoffset, width, height, format, type, pixels);
    NOTE("glTexSubImage2D", format);
}

void Hook_GLTexParameteri(GLcontext context, GLenum target, GLenum pname, GLint param)
{
    (void) context; GUARD;
    Core_TexParameteri(C, target, pname, param);
    NOTE("glTexParameteri", pname);
}

void Hook_GLBindTexture(GLcontext context, GLenum target, GLuint texture) { (void) context; GUARD; Core_BindTexture(C, target, texture); NOTE("glBindTexture", target); }

void Hook_GLGenTextures(GLcontext context, GLsizei n, GLuint *textures)
{
    (void) context; GUARD;
    Core_GenTextures(C, n, (ULONG *) textures);
    NOTE("glGenTextures", n);
}

void Hook_GLDeleteTextures(GLcontext context, GLsizei n, const GLuint *textures)
{
    (void) context; GUARD;
    Core_DeleteTextures(C, n, (const ULONG *) textures);
    NOTE("glDeleteTextures", n);
}

GLboolean Hook_GLIsTexture(GLcontext context, GLuint texture) { (void) context; return rgl ? (GLboolean) Core_IsTexture(C, texture) : GL_FALSE; }
void Hook_GLGetFloatv(GLcontext context, GLenum pname, GLfloat *params) { (void) context; GUARD; Rglc_GetFloatv(C, pname, params); NOTE("glGetFloatv", pname); }

void Hook_GLGetIntegerv(GLcontext context, GLenum pname, GLint *params)
{
    (void) context; GUARD;
    Rglc_GetIntegerv(C, pname, (LONG *) params);
    NOTE("glGetIntegerv", pname);
}

void Hook_GLGetBooleanv(GLcontext context, GLenum pname, GLboolean *params)
{
    float v[16];
    ULONG i;
    ULONG e0;
    ULONG e1;

    (void) context;
    GUARD;
    for (i = 0; i < 16; i++)
        v[i] = 0.0f;
    /* GL leaves params alone when pname is refused */
    e0 = rgl->core.error;
    rgl->core.error = GL_NO_ERROR;
    Rglc_GetFloatv(C, pname, v);
    e1 = rgl->core.error;
    rgl->core.error = (e0 != GL_NO_ERROR) ? e0 : e1;
    NOTE("glGetBooleanv", pname);
    if (e1 != GL_NO_ERROR)
        return;
    for (i = 0; i < Core_GetCountX(pname); i++)
        params[i] = (v[i] != 0.0f) ? GL_TRUE : GL_FALSE;
}

const GLubyte *Hook_GLGetString(GLcontext context, GLenum name)
{
    (void) context;
    if (rgl == NULL)
        return NULL;
    if (name == GL_RGL_RESIZE_ERROR)
        return (const GLubyte *) rgl->resize_why;      /* extension GL_RGL_resize_error */
    if (name == GL_EXTENSIONS)
        return (const GLubyte *) Rglc_Extensions;      /* + 29.1's palette names */
    return (const GLubyte *) Core_GetString(C, name);
}

GLenum Hook_GLGetError(GLcontext context) { (void) context; return rgl ? (GLenum) Core_GetError(C) : GL_NO_ERROR; }
/* glFlush: on the Radeon a frame's commands run at mglSwitchDisplay, which
 * a double-buffered context must reach before anything shows; the null
 * card hands its ring over. glFinish: everything so far run and finished
 * (a flush of its own on the Radeon). */
void Hook_GLFlush(GLcontext context)
{
    (void) context;
    GUARD;
    if (!rgl->real)
        R2Ring_Commit(&rgl->ring);
}

void Hook_GLFinish(GLcontext context)
{
    (void) context;
    GUARD;
    if (IN_BEGIN(C))
    {
        Core_Error(C, GL_INVALID_OPERATION);
        return;
    }
    if (rgl->real ? Card_WaitIdle(&rgl->rad) == R2_OK : R2Ring_Idle(&rgl->ring) == R2_OK)
        Core_GpuIdle(C);
}
