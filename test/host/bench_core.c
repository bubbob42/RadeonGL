/*
 * bench_core.c -- GLBench's core patterns on the host, for a native
 * profiler (macOS: sample). The core writes into a command buffer that is
 * discarded when full, as the Radeon back end's buffer is copied away
 * (the bus left out); the stream goes into a staging buffer. Relative
 * costs only: the host is not a 68060.
 *
 *   build/host/bench_core <test> [reps]     tests: fan bind beginend bindbe
 *                                           locked matrix lightmap all
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "core.h"
#include "coreint.h"
#include "r200reg.h"

#define CMD_DW  65536

static struct Core core;
static struct R2Cs cs;
static ULONG cmd[CMD_DW];
static ULONG staging[CORE_STREAM_BYTES / 4];
static ULONG heap[(8u << 20) / 4];

static APTR HostAlloc(APTR env, ULONG size)
{
    (void) env;
    return calloc(1, size);
}

static void HostFree(APTR env, APTR mem, ULONG size)
{
    (void) env;
    (void) size;
    free(mem);
}

static LONG HostIdle(APTR env)
{
    (void) env;
    return 0;
}

static LONG Need(struct R2Cs *s, ULONG ndw)
{
    (void) ndw;
    s->ptr = cmd;
    s->end = cmd + CMD_DW;
    return R2_OK;
}

static float pos[100 * 3];
static UBYTE col[100 * 4];
static float st[100 * 2];
static UWORD idx[450];
static UWORD lm[16 * 16];
static UBYTE tex[64 * 64 * 3];

static void Fan(void)
{
    Core_Begin(&core, GL_TRIANGLE_FAN);
    Core_TexCoord2f(&core, 0.0f, 0.0f);
    Core_Vertex3f(&core, -0.5f, -0.5f, 0.0f);
    Core_TexCoord2f(&core, 1.0f, 0.0f);
    Core_Vertex3f(&core, 0.5f, -0.5f, 0.0f);
    Core_TexCoord2f(&core, 1.0f, 1.0f);
    Core_Vertex3f(&core, 0.6f, 0.4f, 0.0f);
    Core_TexCoord2f(&core, 0.5f, 1.2f);
    Core_Vertex3f(&core, 0.0f, 0.6f, 0.0f);
    Core_TexCoord2f(&core, 0.0f, 1.0f);
    Core_Vertex3f(&core, -0.6f, 0.4f, 0.0f);
    Core_End(&core);
}

static void Setup(void)
{
    struct CoreTarget t;
    struct CoreEnv e;
    ULONG i;

    R2Cs_InitBuffer(&cs, cmd, CMD_DW, 1);
    cs.need = Need;
    t.width = 320;
    t.height = 240;
    t.color_gpu = 0x00100000UL;
    t.color_pitch = 320;
    t.color_format = R_COLOR_FORMAT_RGB565;
    t.depth_gpu = 0x00200000UL;
    t.depth_pitch = 320;
    e.alloc = HostAlloc;
    e.free = HostFree;
    e.wait_idle = HostIdle;
    e.env = NULL;
    if (Core_Init(&core, &cs, &e, &t, 0x00400000UL, heap, sizeof(heap)) != GL_NO_ERROR)
        exit(1);
    Core_SetVram(&core, NULL, NULL, staging);
    for (i = 0; i < 100; i++)
    {
        pos[i * 3] = (float) (i % 10) * 0.1f;
        pos[i * 3 + 1] = (float) (i / 10) * 0.1f;
        col[i * 4] = 255;
        col[i * 4 + 1] = (UBYTE) i;
        st[i * 2] = (float) i * 0.01f;
    }
    for (i = 0; i < 450; i++)
        idx[i] = (UWORD) ((i * 37) % 100);
    for (i = 0; i < 256; i++)
        lm[i] = (UWORD) (0xf000 | i);
    Core_Viewport(&core, 0, 0, 320, 240);
    Core_MatrixMode(&core, GL_PROJECTION);
    Core_LoadIdentity(&core);
    Core_Ortho(&core, -1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
    Core_MatrixMode(&core, GL_MODELVIEW);
    Core_LoadIdentity(&core);
    Core_BindTexture(&core, GL_TEXTURE_2D, 1);
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, 3, 64, 64, 0, GL_RGB, GL_UNSIGNED_BYTE, tex);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    Core_BindTexture(&core, GL_TEXTURE_2D, 2);
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, 3, 64, 64, 0, GL_RGB, GL_UNSIGNED_BYTE, tex);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    Core_BindTexture(&core, GL_TEXTURE_2D, 3);
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA,
                    MGL_UNSIGNED_SHORT_4_4_4_4, lm);
    Core_Enable(&core, GL_TEXTURE_2D, 1);
    Core_BindTexture(&core, GL_TEXTURE_2D, 1);
}

static void Run(const char *name, unsigned long reps)
{
    unsigned long r;

    if (!strcmp(name, "fan"))
        for (r = 0; r < reps; r++)
            Fan();
    else if (!strcmp(name, "beginend"))
        for (r = 0; r < reps; r++)
        {
            Core_Begin(&core, GL_TRIANGLE_FAN);
            Core_End(&core);
        }
    else if (!strcmp(name, "bindbe"))
        for (r = 0; r < reps; r++)
        {
            Core_BindTexture(&core, GL_TEXTURE_2D, (r & 1) ? 2 : 1);
            Core_Begin(&core, GL_TRIANGLE_FAN);
            Core_End(&core);
        }
    else if (!strcmp(name, "bind"))
        for (r = 0; r < reps; r++)
        {
            Core_BindTexture(&core, GL_TEXTURE_2D, (r & 1) ? 2 : 1);
            Fan();
        }
    else if (!strcmp(name, "locked"))
    {
        Core_VertexPointer(&core, 3, GL_FLOAT, 0, pos);
        Core_ColorPointer(&core, 4, MGL_UBYTE_ARGB, 0, col);
        Core_TexCoordPointer(&core, 2, GL_FLOAT, 0, st);
        Core_EnableClientState(&core, GL_VERTEX_ARRAY, 1);
        Core_EnableClientState(&core, GL_COLOR_ARRAY, 1);
        Core_EnableClientState(&core, GL_TEXTURE_COORD_ARRAY, 1);
        for (r = 0; r < reps; r++)
        {
            Core_LockArrays(&core, 0, 100);
            Core_DrawElements(&core, GL_TRIANGLES, 450, GL_UNSIGNED_SHORT, idx);
            Core_UnlockArrays(&core);
            if ((r & 63) == 63)
                Core_GpuIdle(&core);
        }
    }
    else if (!strcmp(name, "matrix"))
        for (r = 0; r < reps; r++)
        {
            Core_PushMatrix(&core);
            Core_Translatef(&core, 0.1f, 0.0f, 0.0f);
            Core_Rotatef(&core, 10.0f, 1.0f, 0.0f, 0.0f);
            Core_Rotatef(&core, 20.0f, 0.0f, 1.0f, 0.0f);
            Core_Rotatef(&core, 30.0f, 0.0f, 0.0f, 1.0f);
            Fan();
            Core_PopMatrix(&core);
        }
    else if (!strcmp(name, "lightmap"))
    {
        Core_BindTexture(&core, GL_TEXTURE_2D, 3);
        for (r = 0; r < reps; r++)
        {
            lm[r & 255] ^= 0x0111;
            Core_TexSubImage2D(&core, GL_TEXTURE_2D, 0, 0, 0, 16, 16, GL_RGBA,
                               MGL_UNSIGNED_SHORT_4_4_4_4, lm);
            Core_GpuIdle(&core);
        }
        Core_BindTexture(&core, GL_TEXTURE_2D, 1);
    }
}

int main(int argc, char **argv)
{
    static const char *const all[] = { "fan", "beginend", "bindbe", "bind", "locked", "matrix",
                                       "lightmap" };
    static const unsigned long base[] = { 20000, 20000, 20000, 20000, 2000, 10000, 5000 };
    struct timespec t0;
    struct timespec t1;
    unsigned long reps;
    unsigned int i;
    double ns;

    Setup();
    for (i = 0; i < sizeof(all) / sizeof(all[0]); i++)
    {
        if (argc > 1 && strcmp(argv[1], "all") != 0 && strcmp(argv[1], all[i]) != 0)
            continue;
        reps = (argc > 2) ? strtoul(argv[2], NULL, 10) : base[i];
        clock_gettime(CLOCK_MONOTONIC, &t0);
        Run(all[i], reps);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ns = (double) (t1.tv_sec - t0.tv_sec) * 1e9 + (double) (t1.tv_nsec - t0.tv_nsec);
        printf("  %-9s %8.1f ns a rep (%lu reps), GL error 0x%lx\n", all[i], ns / (double) reps,
               reps, (unsigned long) Core_GetError(&core));
    }
    return 0;
}
