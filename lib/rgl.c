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

GLcontext Rgl_CurrentContext = 0;
struct Rgl *rgl_list = NULL;            /* every context, newest first (W3; rglcompat.c reads it) */
static ULONG choose_depth = 16;     /* mglChoosePixelDepth */
static ULONG choose_window = 0;     /* mglChooseWindowMode: MGLCreateContext opens a window */
static ULONG unimpl_early[(DISPATCH_SLOTS + 31) / 32];
/* a context is being made or freed: one at a time, and none while another
 * is torn down (P96Base, TimerBase and the card's globals are shared) */
static ULONG rgl_busy = 0;

/* --- Memory and the core's environment ------------------------------------------ */

static APTR EnvAlloc(APTR env, ULONG size)
{
    (void) env;
    return AllocVec(size, MEMF_ANY | MEMF_CLEAR);
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
    bits = (r != NULL) ? r->unimpl : unimpl_early;
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

/* --- The log ------------------------------------------------------------------------- */

static ULONG StrLen(const char *s)
{
    ULONG n;

    for (n = 0; s[n] != 0; n++)
        ;
    return n;
}

static void Put(BPTR f, const char *s)
{
    Write(f, (APTR) s, (LONG) StrLen(s));
}

static void PutU(BPTR f, ULONG v)
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
    Put(f, b + i);
}

/* The upload cache's line in both logs. */
static void PutCache(BPTR f, const struct Core *c)
{
    Put(f, "upload cache: ");
    if (!c->ucache_on)
    {
        Put(f, "off (no room)\n");
        return;
    }
    if (c->no_ucache)
    {
        Put(f, "off (RadeonGLUploadCache=0)\n");
        return;
    }
    Put(f, c->ustaged ? "(staged, copied at the flush) " : "(written in place) ");
    PutU(f, c->stat_uc_hits); Put(f, " hits, "); PutU(f, c->stat_uc_misses);
    Put(f, " misses; bytes not written again: "); PutU(f, c->stat_uc_saved);
    Put(f, ", written into it: "); PutU(f, c->stat_uc_written);
    if (c->stat_uc_smiss_v || c->stat_uc_smiss_i)
    {
        Put(f, "; static misses: "); PutU(f, c->stat_uc_smiss_v);
        Put(f, " vertex blocks, "); PutU(f, c->stat_uc_smiss_i); Put(f, " index lists");
    }
    if (c->stat_dyn_v != 0)
    {
        Put(f, "; streamed as dynamic (no key): "); PutU(f, c->stat_dyn_v); Put(f, " vertex blocks");
    }
    Put(f, "\n");
}

static void PutX(BPTR f, ULONG v)
{
    char b[11];
    int i;

    b[0] = '0';
    b[1] = 'x';
    for (i = 0; i < 8; i++)
        b[2 + i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 15];
    b[10] = 0;
    Put(f, b);
}

static void PutSlots(struct Rgl *r, BPTR f)
{
    ULONG i;

    Put(f, "slots called but not implemented:");
    for (i = 0; i < DISPATCH_SLOTS; i++)
        if ((r->unimpl[i / 32] | unimpl_early[i / 32]) & (1UL << (i % 32)))
        {
            Put(f, " ");
            Put(f, Rgl_SlotName[i]);
        }
    Put(f, "\n");
}

static void WriteCardLog(struct Rgl *r, BPTR f)
{
    struct Card *k;
    const char *name;
    ULONG i;

    k = &r->rad;
    Put(f, "RadeonGL on the Radeon (PCI "); PutX(f, k->pci_id); Put(f, ")\n");
    Put(f, (k->kind == CARD_KIND_WINDOW) ? (k->own_win ? "context: its own window on the default public screen\n"
                                                      : "context: the program's window\n")
         : (k->kind == CARD_KIND_BITMAP) ? "context: the program's bitmap\n" : "context: a screen of its own\n");
    Put(f, (k->kind == CARD_KIND_SCREEN) ? "screen: " : "frame: ");
    PutU(f, k->width); Put(f, " x "); PutU(f, k->height);
    Put(f, k->show_format == R_COLOR_FORMAT_ARGB1555 ? ", 15 bit (R5G5B5PC)\n"
         : k->bpp == 2 ? ", 16 bit (R5G6B5PC)\n" : ", 32 bit (B8G8R8A8)\n");
    if (k->kind != CARD_KIND_SCREEN)
    {
        Put(f, "texture heap: "); PutU(f, k->heap_size >> 10); Put(f, " KB");
        if (k->kind == CARD_KIND_WINDOW)
        {
            Put(f, "; frames blitted into the window: "); PutU(f, k->blits);
            Put(f, ", cut to a smaller window: "); PutU(f, k->blit_cut);
        }
        Put(f, "\n");
    }
    if (r->resizes != 0 || r->resize_failed != 0)
    {
        Put(f, "resized: "); PutU(f, r->resizes); Put(f, " times, refused "); PutU(f, r->resize_failed);
        if (r->resize_failed != 0)
        {
            Put(f, " (last: "); Put(f, r->resize_why); Put(f, ")");
        }
        Put(f, "\n");
    }
    Put(f, "frames: "); PutU(f, r->frame); Put(f, "\n");
    Put(f, "flushes: "); PutU(f, k->flushes); Put(f, ", presents: "); PutU(f, k->presents);
    Put(f, ", without the screen on the board: "); PutU(f, k->noscreen); Put(f, "\n");
    Put(f, "vertical sync (mglEnableSync): "); Put(f, r->core.sync ? "on" : "off");
    Put(f, "; presents that waited for the beam: "); PutU(f, k->vsyncs);
    if (k->vsyncs != 0)
    {
        Put(f, " (CRTC"); Put(f, k->vsync_crtc ? "2" : ""); Put(f, ", ");
        PutU(f, k->vsync_lines); Put(f, " lines)");
    }
    Put(f, ", with no CRTC running: "); PutU(f, k->vsync_off); Put(f, "\n");
    Put(f, "frames dropped: "); PutU(f, k->dropped); Put(f, " (bitmaps moved "); PutU(f, k->moved);
    Put(f, ", off the board "); PutU(f, k->offboard); Put(f, ")\n");
    Put(f, "frames not drawn, the GL screen behind another: "); PutU(f, k->hidden); Put(f, "\n");
    Put(f, "display FIFO (GRPH_BUFFER_CNTL): Radeon.card's "); PutX(f, k->grph_seen);
    if (k->dprio_on)
    {
        Put(f, ", RadeonGLDisplayPriority: critical point "); PutU(f, k->dprio_point);
        Put(f, " ("); PutU(f, k->dprio_writes); Put(f, " writes)");
    }
    else
        Put(f, ", RadeonGLDisplayPriority not set");
    Put(f, "\n");
    Put(f, "dwords: ");
    if (k->dwords_hi != 0)
    {
        PutU(f, k->dwords_hi); Put(f, " x 2^32 + ");
    }
    PutU(f, k->dwords); Put(f, "\n");
    Put(f, "lock per flush: mean "); PutU(f, k->lock_flushes ? k->lock_us_sum / k->lock_flushes : 0);
    Put(f, " us, max "); PutU(f, k->lock_us_max); Put(f, " us; fence wait max ");
    PutU(f, k->fence_us_max); Put(f, " us\n");
#ifdef CARD_OVERLAP_STAT
    Put(f, "overlap estimate: "); PutU(f, k->ov_n);
    Put(f, " unlocks after a fence; the GPU's tail at the unlock: mean ");
    PutU(f, k->ov_n ? k->ov_tail_sum / k->ov_n : 0);
    Put(f, " us; the next lock after a mean "); PutU(f, k->ov_n ? k->ov_gap_sum / k->ov_n : 0);
    Put(f, " us (a frame's flush "); PutU(f, k->ov_by[0]);
    Put(f, ", a flush mid-frame "); PutU(f, k->ov_by[1]);
    Put(f, ", a heap lock "); PutU(f, k->ov_by[2]);
    Put(f, "); the GPU running on after the unlock would save a mean ");
    PutU(f, k->ov_n ? k->ov_save_sum / k->ov_n : 0); Put(f, " us a flush\n");
#endif
    Put(f, "heap locks for texture uploads and the stream: "); PutU(f, k->vram_locks);
    Put(f, ", writes lost (heap out of reach): "); PutU(f, r->core.stat_vram_lost); Put(f, "\n");
    Put(f, "locked-array stream: ");
    Put(f, r->core.stream_direct ? "into video RAM, a lock a draw\n"
                                 : "staged in fast RAM, copied at the flush\n");
    Put(f, "CP resets: "); PutU(f, k->cp_resets); Put(f, "\n");
    if (k->overlap)
    {
        Put(f, "overlap: on; frames the Radeon finished after the unlock: "); PutU(f, k->ov_frames);
        Put(f, ", of them still drawing when the next lock got through: "); PutU(f, k->ov_waits);
        Put(f, "\n");
    }
    else
        Put(f, "overlap: off (RadeonGLOverlap=0)\n");
    if (k->m16_failed != 0)
        Put(f, k->m16_failed == 1 ? "move16: OFF -- its self-test read back a different line (RadeonGLMove16 ignored)\n"
                                  : "move16: OFF -- no room in the heap for its self-test\n");
    if (k->move16 != 0)
    {
        Put(f, k->move16 == 1 ? "move16: on (the source pushed out of the data cache first); dwords as lines: "
                              : "move16: on (no cache push); dwords as lines: ");
        PutU(f, k->m16_lines);
        Put(f, ", as longwords: "); PutU(f, k->m16_plain); Put(f, "\n");
    }
    if (Card_ScreensLeft != 0)
    {
        Put(f, "GL screens left open by a window on them (since the library loaded): ");
        PutU(f, Card_ScreensLeft); Put(f, "\n");
    }
    Put(f, "idle waits (flushes for texture hazards and stream wraps): ");
    PutU(f, r->core.stat_idle_waits); Put(f, "\n");
    Put(f, "locked draws: "); PutU(f, r->core.stat_lock_draws); Put(f, ", uploads: ");
    PutU(f, r->core.stat_lock_uploads); Put(f, ", stream bytes: ");
    PutU(f, r->core.stat_stream_bytes); Put(f, "\n");
    PutCache(f, &r->core);
    Put(f, "texture heap: "); PutU(f, k->heap_size >> 20);
    Put(f, k->heap_way == CARD_HEAP_WB32 ? " MB, a 32-bit friend of the Workbench"
           : k->heap_way == CARD_HEAP_WIDE ? " MB, the screen's format, a friend, 8192-byte rows"
           : k->heap_way == CARD_HEAP_BLIT ? " MB, the screen's format, moved on by a blit"
           : " MB, the screen's format, 4096-byte rows");
    Put(f, " (sizes P96 kept off the board first: "); PutU(f, k->heap_offboard); Put(f, ")\n");
    Put(f, "heap tries:");
    for (i = 0; i < k->heap_try_n; i++)
    {
        Put(f, " "); PutU(f, k->heap_try[i] >> 3);
        Put(f, ((k->heap_try[i] >> 1) & 3) == CARD_HEAP_WB32 ? " MB 32-bit "
               : ((k->heap_try[i] >> 1) & 3) == CARD_HEAP_WIDE ? " MB wide "
               : ((k->heap_try[i] >> 1) & 3) == CARD_HEAP_BLIT ? " MB blit " : " MB narrow ");
        Put(f, (k->heap_try[i] & 1) ? "ON;" : "off;");
    }
    Put(f, "\n");
    Put(f, "texture heap used at the end: "); PutU(f, r->core.heap.used);
    Put(f, ", at most: "); PutU(f, r->core.heap.peak);
    Put(f, "; allocations refused: "); PutU(f, r->core.heap.fails); Put(f, "\n");
    Put(f, "checker: "); Put(f, k->check ? "on" : "OFF (RadeonGLCheck=0)");
    if (k->check)
    {
        Put(f, ", indexed draws checked by bounds only (stream in video RAM): ");
        PutU(f, k->chk.index_unchecked);
    }
    Put(f, "\n");
    if (k->lost != 0)
    {
        Put(f, "CONTEXT LOST: ");
        Put(f, k->lost == CARD_LOST_CHECKER ? "the checker refused a packet"
             : k->lost == CARD_LOST_FENCE ? "a fence timed out"
             : k->lost == CARD_LOST_EMIT ? "a command did not fit or was malformed (R2_E "
             : k->lost == CARD_LOST_HEAP ? "Picasso96 moved the texture heap off a 32-byte boundary"
             : k->lost == CARD_LOST_SCREEN ? "a resize could open neither the new GL screen nor the old one"
             : "the CP would not start or stop");
        if (k->lost == CARD_LOST_EMIT)
        {
            PutU(f, (ULONG) -k->err_code); Put(f, ")");
        }
        Put(f, "\n");
        if (k->lost == CARD_LOST_CHECKER)
        {
            Put(f, "  "); Put(f, R2Chk_ErrorName(k->err_code));
            Put(f, " at dword "); PutU(f, k->err.at);
            Put(f, ", what "); PutX(f, k->err.what);
            name = R2Chk_RegName(k->err.what);
            if (name != NULL)
            {
                Put(f, " ("); Put(f, name); Put(f, ")");
            }
            Put(f, ", value "); PutX(f, k->err.value); Put(f, "\n");
        }
    }
    Put(f, "GL error pending: "); PutX(f, r->core.error); Put(f, "\n");
    if (r->err_where != NULL)
    {
        Put(f, "first GL error raised by "); Put(f, r->err_where);
        Put(f, ", argument "); PutX(f, r->err_arg); Put(f, "\n");
    }
    PutSlots(r, f);
}

/* A context refused on a Radeon: why, in the log and a requester -- not
 * for a window or bitmap context, whose program falls back to its own
 * drawing without a word (Reflector, Herald). */
static void Refused(struct Rgl *r, const char *why)
{
    struct EasyStruct es;
    BPTR f;

    f = Open((STRPTR) r->log, MODE_NEWFILE);
    if (f != 0)
    {
        Put(f, "RadeonGL: the Radeon cannot be used: ");
        Put(f, why);
        Put(f, "\n");
        Close(f);
    }
    if (r->kind != CARD_KIND_SCREEN)
        return;
    es.es_StructSize = sizeof(es);
    es.es_Flags = 0;
    es.es_Title = (UBYTE *) "RadeonGL";
    es.es_TextFormat = (UBYTE *) "The Radeon cannot be used:\n%s";
    es.es_GadgetFormat = (UBYTE *) "OK";
    EasyRequest(NULL, &es, NULL, (ULONG) why);
}

static void WriteLog(struct Rgl *r)
{
    BPTR f;
    const char *name;

    f = Open((STRPTR) r->log, MODE_NEWFILE);
    if (f == 0)
        return;
    if (r->real)
    {
        WriteCardLog(r, f);
        Close(f);
        return;
    }
    Put(f, "RadeonGL null card\n");
    Put(f, "frames: "); PutU(f, r->frame); Put(f, "\n");
    Put(f, "packets: "); PutU(f, r->card.packets); Put(f, "\n");
    Put(f, "dwords: "); PutU(f, r->card.dwords); Put(f, "\n");
    Put(f, "dwords per frame, max: "); PutU(f, r->frame_dw_max); Put(f, "\n");
    Put(f, "draws: "); PutU(f, r->card.st.draws); Put(f, "\n");
    Put(f, "vertices: "); PutU(f, r->card.st.vertices); Put(f, "\n");
    Put(f, "state blocks: "); PutU(f, r->core.stat_state_emits); Put(f, "\n");
    Put(f, "split packets: "); PutU(f, r->core.stat_splits); Put(f, "\n");
    Put(f, "idle waits: "); PutU(f, r->core.stat_idle_waits); Put(f, "\n");
    Put(f, "locked draws: "); PutU(f, r->core.stat_lock_draws); Put(f, "\n");
    Put(f, "locked uploads: "); PutU(f, r->core.stat_lock_uploads); Put(f, "\n");
    PutCache(f, &r->core);
    Put(f, "stream wraps: "); PutU(f, r->core.stat_stream_wraps); Put(f, "\n");
    Put(f, "stream bytes: "); PutU(f, r->core.stat_stream_bytes);
    Put(f, r->no_lock ? " (locked arrays OFF: RadeonGLLocked=0)\n" : "\n");
    Put(f, "ring wraps: "); PutU(f, r->ring.stats.wraps); Put(f, "\n");
    Put(f, "texture heap used: "); PutU(f, r->core.heap.used); Put(f, " of ");
    PutU(f, r->heap_size); Put(f, ", at most "); PutU(f, r->core.heap.peak);
    Put(f, ", allocations refused "); PutU(f, r->core.heap.fails); Put(f, "\n");
    Put(f, "WPTR misaligned: "); PutU(f, r->card.bad_align); Put(f, "\n");
    Put(f, "packets across the ring end: "); PutU(f, r->card.straddle); Put(f, "\n");
    Put(f, "checker errors: "); PutU(f, r->card.errors);
    Put(f, r->card.no_check ? " (checker OFF: RadeonGLCheck=0)\n" : "\n");
    if (r->card.errors != 0)
    {
        Put(f, "first: "); Put(f, R2Chk_ErrorName(r->card.first_code));
        Put(f, " at ring dword "); PutU(f, r->card.first.at);
        Put(f, ", what "); PutX(f, r->card.first.what);
        name = R2Chk_RegName(r->card.first.what);
        if (name != NULL)
        {
            Put(f, " ("); Put(f, name); Put(f, ")");
        }
        Put(f, ", value "); PutX(f, r->card.first.value); Put(f, "\n");
    }
    Put(f, "GL error pending: "); PutX(f, r->core.error); Put(f, "\n");
    if (r->err_where != NULL)
    {
        Put(f, "first GL error raised by "); Put(f, r->err_where);
        Put(f, ", argument "); PutX(f, r->err_arg); Put(f, "\n");
    }
    if (r->dump_written != 0)
    {
        Put(f, "frame dumped to "); Put(f, r->dump_path); Put(f, ": ");
        PutU(f, r->dump_written); Put(f, " dwords of packets, ");
        PutU(f, Heap_Top(&r->core)); Put(f, " bytes of texture heap, ");
        PutU(f, r->card.rec_lost); Put(f, " packets lost\n");
    }
    PutSlots(r, f);
    Close(f);
}

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
        unimpl_early[i] = 0;
}

void Back_Init(void)
{
    Card_Init();                    /* the card semaphore (W3) */
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
            Refused(r, res == CARD_FAILED ? why : "there is no R200 Radeon on a P96 board");
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
    env.free = EnvFree;
    env.wait_idle = EnvIdle;
    env.env = r;
    if (Core_Init(&r->core, r->real ? &r->rad.cs : &r->ring.cs, &env, &t,
                  r->real ? r->rad.heap_gpu : HEAP_GPU, r->real ? NULL : r->heap_mem,
                  r->real ? r->rad.heap_size : r->heap_size) != GL_NO_ERROR)
    {
        FreeRgl(r);
        rgl_busy = 0;
        return NULL;
    }
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
    /* RadeonGLUploadCache=0: every locked upload written again (A/B) */
    if (GetVar((STRPTR) "RadeonGLUploadCache", (STRPTR) flag, sizeof(flag), 0) > 0 && flag[0] == '0')
        r->core.no_ucache = 1;
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
    if (!mine)
    {
        rgl_busy = 0;
        return;
    }
    if (Rgl_CurrentContext == context)
        Rgl_CurrentContext = 0;
    if (!r->real)
        R2Ring_Idle(&r->ring);
    WriteLog(r);
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
        r->frame++;
        return;
    }
    /* end of frame: a fence, hand it over, wait (the null card is instant) */
    if (R2Ring_Fence(&r->ring, &seq) == R2_OK && R2Ring_WaitFence(&r->ring, seq) == R2_OK)
        Core_GpuIdle(&r->core);     /* every texture is free to rewrite */
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
    if (rgl->real && (ULONG) width == rgl->rad.width && (ULONG) height == rgl->rad.height)
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
void Hook_MGLDrawMultitexBuffer(GLcontext context, GLenum BSrc, GLenum BDst, GLenum TexEnv)
{
    /* the fake-multitexture flush; with real multitexture GLQuake still
     * calls it once a frame. Nothing is buffered here, so nothing to do
     * (MiniGLV3D's side effect on unit 1's texenv is not copied). */
    (void) context;
    (void) BSrc;
    (void) BDst;
    (void) TexEnv;
}

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

void Hook_MGLLockMode(GLcontext context, GLenum lockMode)
{
    (void) context;
    if (rgl != NULL)
        rgl->core.lock_mode = lockMode;
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
void Hook_mglChooseNumberOfBuffers(int number) { (void) number; }
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
void Hook_GLPolygonMode(GLcontext context, GLenum face, GLenum mode) { (void) context; GUARD; Core_PolygonMode(C, face, mode); NOTE("glPolygonMode", mode); }
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
