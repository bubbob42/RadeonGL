/*
 * ucgrow.c -- the upload cache's size (2026-11-14;
 * docs/concept-dynamic-upload-cache.md, D1-D5 decided by the user).
 *
 * The cache (array.c) starts at CORE_UCACHE_BYTES. It grows when it keeps
 * missing blocks it had and dropped, or had to refuse: their keys are kept
 * as ghosts (array.c UPop, Core_UMissed), and a miss on a ghost is a
 * capacity miss. A miss on new data says nothing (GLQuake misses on new
 * data in every frame, at any size).
 *
 *   grow    too many capacity misses (UG_BLOCKS a frame, or UG_SHARE of
 *           the lookups) for UG_RUN frames in a row: twice the size, at the
 *           end of a frame with the GPU idle, if the heap has a block that
 *           big (D1, revised: half of the free heap never let a 16 MB heap
 *           grow past 4 MB -- GLGrow on the A4000D) and fast RAM has the
 *           bigger mirror (D3);
 *   shrink  a texture found no room: half, down to the start size (D2),
 *           then the texture tries again (texture.c);
 *   calm    no grow for UG_CALM frames after a shrink or a refused grow;
 *   fixed   RadeonGLUploadCache=n: n MB, never resized (D4).
 *
 * A resized cache starts empty (one frame of misses). The entries, their
 * lookup table and the ghosts are sized with it: one entry per
 * CORE_UQ_BYTES, four slots an entry.
 *
 * Linked behind the hot code (Makefile CORE_LATE): array.c only calls in
 * on a miss.
 */

#include <stddef.h>

#include "core.h"
#include "coreint.h"

#define UG_RUN       8          /* frames in a row (D5) */
#define UG_BLOCKS    64         /* capacity misses in a frame: too many alone */
#define UG_SHARE     20         /* ... or more than 1/20 (5%) of its lookups */
#define UG_SHARE_MIN 8          /* ... and at least this many (a frame of few draws) */
#define UG_CALM      300        /* frames after a shrink or a refused grow */

struct UTabs
{
    struct CoreUEntry *uq;
    UWORD *uslot;
    ULONG *ughost;
    ULONG n;                    /* entries (a power of two) */
};

/* Entries for a cache of bytes: one per CORE_UQ_BYTES, a power of two */
static ULONG Entries(ULONG bytes)
{
    ULONG n;

    n = CORE_UQ_MAX;
    while (n > 256 && n * CORE_UQ_BYTES > bytes)
        n /= 2;
    return n;
}

/* Fast RAM that every reader writes before it reads (not cleared) */
static APTR Raw(struct Core *c, ULONG bytes)
{
    if (c->env.alloc_raw != NULL)
        return c->env.alloc_raw(c->env.env, bytes);
    return c->env.alloc(c->env.env, bytes);
}

static void TabsFree(struct Core *c, struct UTabs *t)
{
    if (t->uq != NULL)
        c->env.free(c->env.env, t->uq, t->n * sizeof(struct CoreUEntry));
    if (t->uslot != NULL)
        c->env.free(c->env.env, t->uslot, 4 * t->n * sizeof(UWORD));
    if (t->ughost != NULL)
        c->env.free(c->env.env, t->ughost, 2 * t->n * sizeof(ULONG));
    t->uq = NULL;
    t->uslot = NULL;
    t->ughost = NULL;
}

/* The slots and the ghosts are cleared by UCache_Clear, the entries are
 * written before they are read */
static ULONG TabsAlloc(struct Core *c, ULONG bytes, struct UTabs *t)
{
    t->n = Entries(bytes);
    t->uq = (struct CoreUEntry *) Raw(c, t->n * sizeof(struct CoreUEntry));
    t->uslot = (UWORD *) Raw(c, 4 * t->n * sizeof(UWORD));
    t->ughost = (ULONG *) Raw(c, 2 * t->n * sizeof(ULONG));
    if (t->uq != NULL && t->uslot != NULL && t->ughost != NULL)
        return 1;
    TabsFree(c, t);
    return 0;
}

static void TabsGet(struct Core *c, struct UTabs *t)
{
    t->uq = c->uq;
    t->uslot = c->uslot;
    t->ughost = c->ughost;
    t->n = c->uq_mask + 1;
}

static void TabsPut(struct Core *c, const struct UTabs *t)
{
    c->uq = t->uq;
    c->uslot = t->uslot;
    c->ughost = t->ughost;
    c->uq_mask = (t->uq != NULL) ? t->n - 1 : 0;
    c->uslot_mask = (t->uq != NULL) ? 4 * t->n - 1 : 0;
}

ULONG Core_UTables(struct Core *c, ULONG bytes)
{
    struct UTabs t;

    if (!TabsAlloc(c, bytes, &t))
        return 0;
    TabsPut(c, &t);
    UCache_Clear(c);
    return 1;
}

void Core_UTablesFree(struct Core *c)
{
    struct UTabs t;

    if (c->uq == NULL && c->uslot == NULL && c->ughost == NULL)
        return;
    TabsGet(c, &t);
    TabsFree(c, &t);
    TabsPut(c, &t);
}

static void MirrorFree(struct Core *c)
{
    if (c->ushadow_mem != NULL)
        c->env.free(c->env.env, c->ushadow_mem, c->ucache_size + 16);
    c->ushadow_mem = NULL;
    c->ushadow = NULL;
}

static ULONG *Aligned16(APTR mem)
{
    return (ULONG *) ((UBYTE *) mem + ((16UL - ((ULONG) ((size_t) (const void *) mem & 15))) & 15UL));
}

/* The cache becomes bytes, empty. The GPU must be idle (nothing it still
 * reads is in the old region, no staged range waits for a copy). grow:
 * the new block is found while the old one is kept (refused: the old cache
 * stays); else the old block goes first, so the new one can take its place
 * (refused: the cache is off -- its memory is free). */
static ULONG Resize(struct Core *c, ULONG bytes, ULONG grow)
{
    struct UTabs t;
    struct UTabs old;
    APTR mem;
    ULONG off;

    mem = NULL;
    t.uq = NULL;
    t.uslot = NULL;
    t.ughost = NULL;
    t.n = 0;
    if (grow)
    {
        off = Heap_Alloc(c, bytes, 32);
        if (off == 0xffffffffUL)
        {
            c->ug_why = UG_HEAP;
            return 0;
        }
        if (c->ustaged && (mem = Raw(c, bytes + 16)) == NULL)
        {
            Heap_Free(c, off);
            c->ug_why = UG_FASTRAM;
            return 0;
        }
        if (!TabsAlloc(c, bytes, &t))
        {
            if (mem != NULL)
                c->env.free(c->env.env, mem, bytes + 16);
            Heap_Free(c, off);
            c->ug_why = UG_FASTRAM;
            return 0;
        }
        Heap_Free(c, c->ucache_off);
        MirrorFree(c);
        TabsGet(c, &old);
        TabsFree(c, &old);
    }
    else
    {
        Heap_Free(c, c->ucache_off);
        MirrorFree(c);
        Core_UTablesFree(c);
        off = Heap_Alloc(c, bytes, 32);
        if (off != 0xffffffffUL && c->ustaged)
            mem = Raw(c, bytes + 16);
        if (off == 0xffffffffUL || (c->ustaged && mem == NULL) || !TabsAlloc(c, bytes, &t))
        {
            if (mem != NULL)
                c->env.free(c->env.env, mem, bytes + 16);
            if (off != 0xffffffffUL)
                Heap_Free(c, off);
            c->ucache_on = 0;
            c->ucache_size = 0;
            c->ucache_region = 0;
            c->udirty_n = 0;
            c->udirty_all = 0;
            return 0;
        }
    }
    c->ucache_off = off;
    c->ucache_size = bytes;
    c->ucache_region = bytes;
    if (mem != NULL)
    {
        c->ushadow_mem = mem;
        c->ushadow = Aligned16(mem);
    }
    TabsPut(c, &t);
    UCache_Clear(c);
    if (bytes > c->ucache_max)
        c->ucache_max = bytes;
    return 1;
}

/* A missed key the cache had dropped or refused: a capacity miss. Refused
 * now, it becomes a ghost itself (missed again, it counts). */
static void Ghost(struct Core *c, const ULONG *h, ULONG refused)
{
    ULONG *g;

    if (h == NULL)
        return;
    g = c->ughost + 2 * ((h[0] ^ (h[1] >> 9)) & c->uq_mask);
    if (g[0] == h[0] && g[1] == h[1])
    {
        c->stat_uc_cap++;
        g[0] = 0;
        g[1] = 0;
    }
    if (refused)
    {
        g[0] = h[0];
        g[1] = h[1];
    }
}

void Core_UMissed(struct Core *c, const ULONG *vh, const ULONG *ih, ULONG refused)
{
    Ghost(c, vh, refused);
    Ghost(c, ih, refused);
}

void Core_UCacheFrame(struct Core *c)
{
    ULONG look;
    ULONG cap;
    ULONG want;

    look = c->stat_uc_hits + c->stat_uc_misses - c->ug_last_look;
    cap = c->stat_uc_cap - c->ug_last_cap;
    c->ug_last_look += look;
    c->ug_last_cap += cap;
    if (c->ug_calm != 0)
        c->ug_calm--;
    if (!c->ucache_on || c->no_ucache || c->ucache_fixed || c->ucache_size == 0)
        return;
    if (cap < UG_BLOCKS && !(cap >= UG_SHARE_MIN && cap * UG_SHARE > look))
    {
        c->ug_run = 0;
        return;
    }
    if (++c->ug_run < UG_RUN || c->ug_calm != 0)
        return;
    c->ug_run = 0;
    want = 2 * c->ucache_size;
    if (Resize(c, want, 1))
    {
        c->stat_uc_grows++;
        return;
    }
    c->stat_uc_refused++;
    c->ug_calm = UG_CALM;
}

ULONG Core_UCacheShrink(struct Core *c)
{
    ULONG want;

    c->ug_calm = UG_CALM;           /* a texture found no room: no grow for a while */
    c->ug_run = 0;
    if (!c->ucache_on || c->ucache_fixed || c->ucache_size <= c->ucache_start)
        return 0;
    want = c->ucache_size / 2;
    if (want < c->ucache_start)
        want = c->ucache_start;
    if (c->env.wait_idle != NULL)
        c->env.wait_idle(c->env.env);
    c->stat_idle_waits++;
    Core_GpuIdle(c);
    (void) Resize(c, want, 0);      /* refused: the cache is off, its memory free */
    c->stat_uc_shrinks++;
    return 1;
}

void Core_UCacheFix(struct Core *c, ULONG bytes)
{
    bytes &= ~31UL;
    if (!c->ucache_on || c->ucache_size == 0 || bytes == 0)
        return;
    if (bytes != c->ucache_size)
        (void) Resize(c, bytes, bytes > c->ucache_size);    /* a bigger one refused: the old stays */
    c->ucache_fixed = 1;
    c->ucache_start = c->ucache_size;
    /* the start's 4 MB, given back before the program drew or loaded
     * anything, is not the cache's largest nor the heap's peak (Reflector,
     * 2026-11-14: "largest 4096" and a 4.5 MB peak with =2) */
    c->ucache_max = c->ucache_size;
    c->heap.peak = c->heap.used;
}
