/*
 * rgllog.c -- the log a context writes when it is deleted (Rgl_WriteLog)
 * or refused (Rgl_Refused), from rgl.c. A file of its own, linked behind
 * the hot code (2026-11-13): a new log line no longer moves rgl.o and with
 * it everything up to the dispatch pad (Makefile RGL_DISPATCH_PAD).
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
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
#include "rglint.h"
#include "rgltable.h"

/* --- The log -------------------------------------------------------------------------- */

/* The log goes out through one buffer: one Write() per LOG_BUF bytes
 * instead of one per string, each a packet to the file system (2026-11-12,
 * the fast create: ~150 Writes cost ~9 ms of every delete). Static: only
 * WriteLog() and Refused() use it, both inside Busy() (rgl_busy), so never
 * two tasks at once; not in struct Rgl. Put() stays a function of its own:
 * an inlined one grew rgl.o by 2.5 KB and moved the hot code behind it. */
#define LOG_BUF 4096

struct LogOut
{
    BPTR fh;
    ULONG n;
    UBYTE b[LOG_BUF];
};

static struct LogOut log_out;

/* The log file, or NULL: no file, or NIL: (fh_Type NULL; also "*" with no
 * console), where nothing written would arrive */
static struct LogOut *LogOpen(const struct Rgl *r)
{
    BPTR fh;
    struct FileHandle *h;

    fh = Open((STRPTR) r->log, MODE_NEWFILE);
    if (fh == 0)
        return NULL;
    h = BADDR(fh);
    if (h->fh_Type == NULL)
    {
        Close(fh);
        return NULL;
    }
    log_out.fh = fh;
    log_out.n = 0;
    return &log_out;
}

static void LogFlush(struct LogOut *f)
{
    if (f->n != 0)
        (void) Write(f->fh, f->b, (LONG) f->n);
    f->n = 0;
}

static void LogClose(struct LogOut *f)
{
    LogFlush(f);
    Close(f->fh);
    f->fh = 0;
}

static void Put(struct LogOut *f, const char *s)
{
    while (*s != 0)
    {
        if (f->n == LOG_BUF)
            LogFlush(f);
        f->b[f->n++] = (UBYTE) *s++;
    }
}

/* Put for the log lines of other files (cardflip.c) */
void Rgl_LogPut(struct LogOut *f, const char *s)
{
    Put(f, s);
}

static void PutU(struct LogOut *f, ULONG v)
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
static void PutCache(struct LogOut *f, const struct Core *c)
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
    /* its size (ucgrow.c, 2026-11-14) */
    Put(f, "; size KB: started "); PutU(f, c->ucache_start >> 10);
    Put(f, ", now "); PutU(f, c->ucache_size >> 10);
    Put(f, ", largest "); PutU(f, c->ucache_max >> 10);
    if (c->ucache_fixed)
        Put(f, " (fixed: RadeonGLUploadCache)");
    Put(f, "; capacity misses "); PutU(f, c->stat_uc_cap);
    Put(f, ", grown "); PutU(f, c->stat_uc_grows);
    Put(f, ", shrunk for textures "); PutU(f, c->stat_uc_shrinks);
    if (c->stat_uc_refused != 0)
    {
        Put(f, ", grows refused "); PutU(f, c->stat_uc_refused);
        Put(f, c->ug_why == UG_HEAP ? " (last: no heap block)"
               : c->ug_why == UG_FASTRAM ? " (last: no fast RAM)" : " (last: over half the free heap)");
    }
    Put(f, "\n");
}

static void PutX(struct LogOut *f, ULONG v)
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

static void PutSlots(struct Rgl *r, struct LogOut *f)
{
    ULONG i;

    Put(f, "slots called but not implemented:");
    for (i = 0; i < DISPATCH_SLOTS; i++)
        if ((r->unimpl[i / 32] | Rgl_UnimplEarly[i / 32]) & (1UL << (i % 32)))
        {
            Put(f, " ");
            Put(f, Rgl_SlotName[i]);
        }
    Put(f, "\n");
}

static void WriteCardLog(struct Rgl *r, struct LogOut *f)
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
    CardFlip_Log(f, k);                 /* triple buffering (cardflip.c) */
    CardView_Log(f, k);                 /* R4: bitmap views (cardview.c) */
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
void Rgl_Refused(struct Rgl *r, const char *why)
{
    struct EasyStruct es;
    struct LogOut *f;

    f = LogOpen(r);
    if (f != NULL)
    {
        Put(f, "RadeonGL: the Radeon cannot be used: ");
        Put(f, why);
        Put(f, "\n");
        LogClose(f);
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

void Rgl_WriteLog(struct Rgl *r)
{
    struct LogOut *f;
    const char *name;

    f = LogOpen(r);
    if (f == NULL)
        return;
    if (r->real)
    {
        WriteCardLog(r, f);
        LogClose(f);
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
    LogClose(f);
}
