/*
 * core.c -- life cycle and errors of the GL core; see core.h.
 */

#include "core.h"
#include "coreint.h"
#include "r200reg.h"

static void Ident(float *m)
{
    ULONG i;

    for (i = 0; i < 16; i++)
        m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

ULONG Core_Init(struct Core *c, struct R2Cs *cs, const struct CoreEnv *env,
                const struct CoreTarget *t, ULONG heap_gpu, ULONG *heap_cpu,
                ULONG heap_size)
{
    UBYTE *p;
    ULONG i;
    ULONG u;

    /* the whole struct, not a field list that rots */
    p = (UBYTE *) c;
    for (i = 0; i < sizeof(*c); i++)
        p[i] = 0;
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
    c->stream_off = Heap_Alloc(c, CORE_STREAM_BYTES, 32);
    c->stream_size = (c->stream_off != 0xffffffffUL) ? CORE_STREAM_BYTES : 0;
    c->stream_cpu = (c->stream_size != 0) ? heap_cpu + c->stream_off / 4 : NULL;
    /* the upload cache: only beside a stream, and only if all of it fits */
    if (c->stream_size != 0)
    {
        c->ucache_off = Heap_Alloc(c, CORE_UCACHE_BYTES, 32);
        c->ucache_size = (c->ucache_off != 0xffffffffUL) ? CORE_UCACHE_BYTES : 0;
        c->ucache_on = (c->ucache_size != 0) ? 1 : 0;   /* in place until Core_SetVram */
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
    c->ucache_on = 0;
    c->ucache_size = 0;
    Heap_FreeAll(c);
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
    /* a staged stream is copied at the flush; the cache writes in place */
    c->ucache_on = (c->ucache_size != 0 && staging == NULL) ? 1 : 0;
    if (staging != NULL && c->stream_size != 0)
        c->stream_cpu = staging;
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
    c->dirty = D_ALL;
    Hw_Forget(c);
    UCache_Clear(c);        /* a dropped frame's uploads may never have landed */
    Core_GpuIdle(c);
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
    c->epoch++;
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
