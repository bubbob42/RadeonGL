/*
 * core.c -- life cycle and errors of the GL core; see core.h.
 */

#include <stddef.h>
#if !defined(__mc68000__) && !defined(__SASC)
#include <stdio.h>      /* host builds: Core_Init's check of the caller's promise */
#include <stdlib.h>
#endif

#include "core.h"
#include "coreint.h"
#include "r200reg.h"

static void Ident(float *m)
{
    ULONG i;

    for (i = 0; i < 16; i++)
        m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

/* 2026-11-14: Core_Init's cache setup with a local size (the gcc 6.5.0b
 * workaround below) came out 28 bytes shorter; these keep the hot code
 * after core_core.o where ab_dyn.sh measured and accepted it
 * (tools/hot_layout.py). Remove with the next accepted layout change. */
const char Core_InitPad[28] = "layout pad";

ULONG Core_Init(struct Core *c, struct R2Cs *cs, const struct CoreEnv *env,
                const struct CoreTarget *t, ULONG heap_gpu, ULONG *heap_cpu,
                ULONG heap_size)
{
#if !defined(__mc68000__) && !defined(__SASC)
    const UBYTE *p;
#endif
    ULONG i;
    ULONG u;
    ULONG ub;
    ULONG uoff;

    /* c is all zero bits already (core.h): the library's struct Rgl comes
     * MEMF_CLEAR, so a second clear of these 72 KB only cost time
     * (2026-11-12, the fast create). Host builds check the promise, the
     * whole struct, and stop at the first breach: a test that reuses a
     * core clears it whole after Core_Free. */
#if !defined(__mc68000__) && !defined(__SASC)
    p = (const UBYTE *) c;
    for (i = 0; i < sizeof(*c); i++)
        if (p[i] != 0)
        {
            fprintf(stderr, "Core_Init: struct Core not zero at byte %lu (core.h)\n",
                    (unsigned long) i);
            abort();
        }
#endif
    c->cs = cs;
    c->env = *env;
    c->target = *t;
    Heap_Init(&c->heap, heap_gpu, heap_cpu, heap_size);
    c->tex = (struct CoreTex **) env->alloc(env->env, CORE_MAX_TEX * sizeof(struct CoreTex *));
    if (c->tex == NULL)
        return GL_OUT_OF_MEMORY;
    /* texture 0 is the default object every unit starts with */
    c->tex[0] = (struct CoreTex *) env->alloc(env->env, sizeof(struct CoreTex));
    if (c->tex[0] == NULL)
    {
        Core_Free(c);
        return GL_OUT_OF_MEMORY;
    }
    Tex_Defaults(c->tex[0], 0);
    Light_Defaults(c);
    Texgen_Defaults(c);
    Fog_Defaults(c);
    c->stream_off = Heap_Alloc(c, CORE_STREAM_BYTES, 32);
    c->stream_size = (c->stream_off != 0xffffffffUL) ? CORE_STREAM_BYTES : 0;
    c->stream_cpu = (c->stream_size != 0) ? heap_cpu + c->stream_off / 4 : NULL;
    /* the upload cache: only beside a stream, and only if all of it fits */
    if (c->stream_size != 0)
    {
        /* a small heap: half the size, down to CORE_UCACHE_MIN. The size
         * in a local, stored once at the end: written as c->ucache_size
         * all along, m68k-amigaos-gcc 6.5.0b (-O2, 030 and 060 builds)
         * stored what was left in d0 -- Core_UTables' result, 1 -- as
         * ucache_start and ucache_region (Reflector's log, 2026-11-14:
         * "started 0"; seen in the disassembly) */
        uoff = 0xffffffffUL;
        for (ub = CORE_UCACHE_BYTES; ub >= CORE_UCACHE_MIN; ub /= 2)
        {
            uoff = Heap_Alloc(c, ub, 32);
            if (uoff != 0xffffffffUL)
                break;
        }
        if (ub < CORE_UCACHE_MIN)
            ub = 0;
        /* its entries, their lookup table and the ghosts (ucgrow.c) */
        if (ub != 0 && !Core_UTables(c, ub))
        {
            Heap_Free(c, uoff);
            ub = 0;
        }
        c->ucache_off = uoff;
        c->ucache_size = ub;
        c->ucache_region = ub;
        c->ucache_start = ub;
        c->ucache_max = ub;
        c->ucache_on = (ub != 0) ? 1 : 0;   /* in place until Core_SetVram */
    }

    /* GL's initial state */
    c->enables = EN_DITHER | EN_PERSPECTIVE;
    c->blend_src = GL_ONE;
    c->blend_dst = GL_ZERO;
    c->alpha_func = GL_ALWAYS;
    c->depth_func = GL_LESS;
    c->depth_mask = 1;
    c->depth_far = 1.0f;
    c->cull_mode = GL_BACK;
    c->front_face = GL_CCW;
    c->shade_model = GL_SMOOTH;
    c->color_mask = 0xf;
    c->clear_depth = 1.0f;
    c->vp_w = (LONG) t->width;
    c->vp_h = (LONG) t->height;
    c->sc_w = (LONG) t->width;
    c->sc_h = (LONG) t->height;
    c->point_size = 1.0f;
    c->polygon_mode = GL_FILL;
    c->lock_mode = MGL_LOCK_AUTOMATIC;
    for (u = 0; u < CORE_UNITS; u++)
        c->tex_env[u] = GL_MODULATE;
    c->matrix_mode = GL_MODELVIEW;
    Ident(c->mv[0].m);
    Ident(c->proj[0].m);
    c->mat_ver = 1;
    c->mv[0].ver = 1;
    c->proj[0].ver = 1;
    for (u = 0; u < CORE_UNITS; u++)
        Ident(c->texm[u][0].m);
    c->cur_color = 0xffffffffUL;
    for (i = 0; i < 4; i++)
        c->cur_colorf[i] = 1.0f;
    c->cur_normal[2] = 1.0f;
    c->va.size = 4;
    c->va.type = GL_FLOAT;
    c->ca.size = 4;
    c->ca.type = GL_FLOAT;
    c->na.size = 3;
    c->na.type = GL_FLOAT;
    for (u = 0; u < CORE_UNITS; u++)
    {
        c->ta[u].size = 4;
        c->ta[u].type = GL_FLOAT;
    }
    c->prim = CORE_NO_PRIM;
    Hw_Forget(c);
    c->tex_swap = 1;
    c->epoch = 1;
    c->dirty = D_ALL;
    return GL_NO_ERROR;
}

void Core_Free(struct Core *c)
{
    ULONG i;

    if (c->tex != NULL)
    {
        for (i = 0; i < CORE_MAX_TEX; i++)
            if (c->tex[i] != NULL)
                c->env.free(c->env.env, c->tex[i], sizeof(struct CoreTex));
        c->env.free(c->env.env, c->tex, CORE_MAX_TEX * sizeof(struct CoreTex *));
        c->tex = NULL;
    }
    if (c->ushadow_mem != NULL)
    {
        c->env.free(c->env.env, c->ushadow_mem, c->ucache_size + 16);
        c->ushadow_mem = NULL;
        c->ushadow = NULL;
    }
    Core_UTablesFree(c);
    c->ucache_on = 0;
    c->ustaged = 0;
    c->ucache_size = 0;
    Compat_Free(c);
    Heap_FreeAll(c);
}

void Core_UCacheCopied(struct Core *c)
{
    c->udirty_n = 0;
    c->udirty_all = 0;
}

void Core_GpuIdle(struct Core *c)
{
    c->epoch++;
    c->stream_pos = 0;      /* everything in the stream has been read */
    c->stream_gen++;
    c->lk_valid = 0;
}

void Core_SetVram(struct Core *c, ULONG *(*begin)(APTR env), void (*end)(APTR env),
                  ULONG *staging)
{
    c->vram_begin = begin;
    c->vram_end = end;
    c->stream_direct = (begin != NULL && staging == NULL) ? 1 : 0;
    /* the upload cache: in place, or (a staged stream) through a fast-RAM
     * mirror the back end copies at its flush */
    c->ucache_on = 0;
    c->ustaged = 0;
    if (c->ucache_size != 0)
    {
        if (staging == NULL)
            c->ucache_on = 1;
        else
        {
            /* 16-byte aligned, as the video-RAM region (32): the copy at
             * the flush can then send whole lines as MOVE16 bursts (card.c,
             * RadeonGLMove16) */
            if (c->ushadow == NULL)
            {
                /* not zeroed (2026-11-12, the fast create): every reader --
                 * the GPU through an entry, the checker's Peek, a hit --
                 * reads only bytes the cached draw that made the entry
                 * wrote (array.c UInsert after the packing); the unwritten
                 * 32-byte tails and gaps the flush copies land where no
                 * entry points. Precondition: called before any cached
                 * draw, with the cache empty (the library calls it once,
                 * right after Core_Init, rgl.c) */
                /* 2026-11-14: fast RAM without room for the whole region's
                 * mirror gets a smaller cache (the video RAM past it stays
                 * unused); Core_Free frees ucache_size + 16 */
                for (;;)
                {
                    if (c->env.alloc_raw != NULL)
                        c->ushadow_mem = c->env.alloc_raw(c->env.env, c->ucache_size + 16);
                    else
                        c->ushadow_mem = c->env.alloc(c->env.env, c->ucache_size + 16);
                    if (c->ushadow_mem != NULL || c->ucache_size / 2 < CORE_UCACHE_MIN)
                        break;
                    c->ucache_size /= 2;
                }
                if (c->ushadow_mem != NULL)
                    c->ushadow = (ULONG *) ((UBYTE *) c->ushadow_mem
                                 + ((16UL - ((ULONG) ((size_t) (const void *) c->ushadow_mem & 15))) & 15UL));
                c->ucache_start = c->ucache_size;   /* what the mirror allowed (ucgrow.c's floor) */
                c->ucache_max = c->ucache_size;
            }
            if (c->ushadow != NULL)
            {
                c->ucache_on = 1;
                c->ustaged = 1;
            }
        }
    }
    if (staging != NULL && c->stream_size != 0)
        c->stream_cpu = staging;
}

void Core_Forget(struct Core *c)
{
    c->dirty = D_ALL;
    c->rebase_gen++;
    Hw_Forget(c);
}

void Core_Rebase(struct Core *c, ULONG heap_gpu, ULONG color_gpu, ULONG depth_gpu)
{
    ULONG i;

    if (c->tex != NULL)
        for (i = 0; i < CORE_MAX_TEX; i++)
            if (c->tex[i] != NULL && c->tex[i]->gpu != 0)
                c->tex[i]->gpu = heap_gpu + (c->tex[i]->gpu - c->heap.gpu);
    c->heap.gpu = heap_gpu;
    c->target.color_gpu = color_gpu;
    c->target.depth_gpu = depth_gpu;
    Core_Forget(c);
    UCache_Clear(c);        /* a dropped frame's uploads may never have landed */
    Core_GpuIdle(c);
}

void Core_Retarget(struct Core *c, const struct CoreTarget *t)
{
    c->target = *t;
    Core_Forget(c);
}

void Core_MarkTextures(struct Core *c, ULONG units)
{
    ULONG u;

    /* a unit in units has a valid texture bound (Core_UnitsOn) */
    for (u = 0; units != 0; u++, units >>= 1)
        if (units & 1)
            c->tex[c->bound[u]]->use_epoch = c->epoch;
}

/* The GPU may still read texture t (drawn in this epoch): wait until it
 * has finished, which makes every texture safe (a new epoch). */
void Core_TexHazard(struct Core *c, struct CoreTex *t)
{
    if (t->use_epoch != c->epoch || c->env.wait_idle == NULL)
        return;
    c->env.wait_idle(c->env.env);
    c->stat_idle_waits++;
    /* the whole idle, not just the epoch: a locked range drawn again
     * afterwards must look itself up (and re-stamp its cache entry), or a
     * later miss in the new epoch could overwrite it (audit 2026-11-03) */
    Core_GpuIdle(c);
}

void Core_Error(struct Core *c, ULONG err)
{
    if (c->error == GL_NO_ERROR)
        c->error = err;
}

ULONG Core_GetError(struct Core *c)
{
    ULONG e;

    e = c->error;
    c->error = GL_NO_ERROR;
    return e;
}

/* (LONG) f truncates on some builds and rounds to nearest on others (GCC's
 * 68060 tuning converts by the FPCR): correct either way to the floor. */
LONG Core_Floor(double f)
{
    LONG n;

    /* double: SAS/C evaluates float expressions as double anyway, and the
     * FPU costs the same */
    n = (LONG) f;
    if ((double) n > f)
        n--;
    return n;
}

ULONG Core_Unit255(float f)
{
    LONG n;

    if (!(f > 0.0f))
        return 0;
    if (f >= 1.0f)
        return 255;
    n = Core_Floor(f * 255.0f + 0.5f);
    return (ULONG) n;
}
