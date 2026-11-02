/*
 * test_sim.c -- frames you can look at: GL through the core, the packets
 * through the reference rasteriser (r200sim), the pixels checked. PPMs of
 * every scene go to build/host/sim-*.ppm (tools/ppm2png.py converts).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "testkit.h"
#include "core.h"
#include "r200reg.h"
#include "r200sim.h"

#define W_          320
#define H_          240
#define COLOR_GPU   0x00100000UL
#define DEPTH_GPU   0x00200000UL
#define TEX_GPU     0x00400000UL
#define TEX_SIZE    0x00200000UL
#define SCREEN_GPU  0x00800000UL
#define VRAM_SIZE   0x01000000UL

static ULONG stream[200000];
static ULONG *vram;
static struct Core core;
static struct R2Cs cs;
static struct Sim sim;

static APTR HostAlloc(APTR env, ULONG size) { (void) env; return calloc(1, size); }
static void HostFree(APTR env, APTR mem, ULONG size) { (void) env; (void) size; free(mem); }
static LONG HostIdle(APTR env) { (void) env; return 0; }

static void Fresh(void)
{
    struct CoreTarget t;
    struct CoreEnv e;

    Core_Free(&core);
    memset(vram, 0, VRAM_SIZE);
    R2Cs_InitBuffer(&cs, stream, sizeof(stream) / 4, 1);
    t.width = W_;
    t.height = H_;
    t.color_gpu = COLOR_GPU;
    t.color_pitch = W_;
    t.color_format = R_COLOR_FORMAT_RGB565;
    t.depth_gpu = DEPTH_GPU;
    t.depth_pitch = W_;
    e.alloc = HostAlloc;
    e.free = HostFree;
    e.wait_idle = HostIdle;
    e.env = NULL;
    CHECK_INT(Core_Init(&core, &cs, &e, &t, TEX_GPU, vram + TEX_GPU / 4, TEX_SIZE), GL_NO_ERROR);
    Core_Viewport(&core, 0, 0, W_, H_);
}

static void Render(const char *name)
{
    char path[128];

    Sim_Init(&sim, vram, 0, VRAM_SIZE, 1, 1);
    Sim_Run(&sim, stream, (ULONG) (cs.ptr - stream));
    snprintf(path, sizeof(path), "build/host/sim-%s.ppm", name);
    Sim_WritePPM(&sim, path, COLOR_GPU, W_, R_COLOR_FORMAT_RGB565, W_, H_);
    CHECK_INT(sim.unsupported, 0);
}

/* the back buffer at GL coordinates (lower-left origin) */
static ULONG Px(LONG x, LONG y)
{
    return Sim_Pixel(&sim, COLOR_GPU, W_, R_COLOR_FORMAT_RGB565, (ULONG) x, (ULONG) (H_ - 1 - y));
}

static void Ortho(void)
{
    Core_MatrixMode(&core, GL_PROJECTION);
    Core_LoadIdentity(&core);
    Core_Ortho(&core, 0, W_, 0, H_, -10, 10);
    Core_MatrixMode(&core, GL_MODELVIEW);
    Core_LoadIdentity(&core);
}

static void test_clear_and_triangle(void)
{
    Fresh();
    Core_ClearColor(&core, 0.0f, 0.0f, 1.0f, 1.0f);
    Core_Clear(&core, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    Ortho();
    Core_Begin(&core, GL_TRIANGLES);
    Core_Color4ub(&core, 255, 0, 0, 255);
    Core_Vertex3f(&core, 60.0f, 40.0f, 0.0f);
    Core_Vertex3f(&core, 260.0f, 40.0f, 0.0f);
    Core_Vertex3f(&core, 160.0f, 200.0f, 0.0f);
    Core_End(&core);
    Render("triangle");
    CHECK_HEX(Px(160, 100), 0xff0000UL);       /* inside, red */
    CHECK_HEX(Px(10, 10), 0x0000ffUL);         /* the clear colour */
    CHECK_HEX(Px(160, 230), 0x0000ffUL);       /* above the apex */
    /* GL's y points up: the apex is near the TOP of the image */
    CHECK_HEX(Sim_Pixel(&sim, COLOR_GPU, W_, R_COLOR_FORMAT_RGB565, 160, H_ - 1 - 190), 0xff0000UL);
}

static void test_depth_and_cull(void)
{
    Fresh();
    Core_Clear(&core, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    Ortho();
    Core_Enable(&core, GL_DEPTH_TEST, 1);
    /* near green first, far red after: green must stay (z: ortho maps
     * -10..10 to 1..0, bigger z is nearer) */
    Core_Begin(&core, GL_QUADS);
    Core_Color4ub(&core, 0, 255, 0, 255);
    Core_Vertex3f(&core, 50, 50, 5); Core_Vertex3f(&core, 150, 50, 5);
    Core_Vertex3f(&core, 150, 150, 5); Core_Vertex3f(&core, 50, 150, 5);
    Core_Color4ub(&core, 255, 0, 0, 255);
    Core_Vertex3f(&core, 100, 100, -5); Core_Vertex3f(&core, 200, 100, -5);
    Core_Vertex3f(&core, 200, 200, -5); Core_Vertex3f(&core, 100, 200, -5);
    Core_End(&core);
    /* a clockwise (back-facing) triangle with culling on: not drawn */
    Core_Enable(&core, GL_CULL_FACE, 1);
    Core_Begin(&core, GL_TRIANGLES);
    Core_Color4ub(&core, 255, 255, 0, 255);
    Core_Vertex3f(&core, 220, 20, 9); Core_Vertex3f(&core, 300, 100, 9); Core_Vertex3f(&core, 300, 20, 9);
    Core_End(&core);
    Render("depth");
    CHECK_HEX(Px(120, 120), 0x00ff00UL);       /* overlap: the near one */
    CHECK_HEX(Px(180, 180), 0xff0000UL);       /* only the far one */
    CHECK_HEX(Px(290, 40), 0x000000UL);        /* culled */
    CHECK_INT(sim.culled, 1);
}

static void test_textures(void)
{
    UBYTE chk[8 * 8 * 4];
    UBYTE lm[8 * 8];
    ULONG i;

    Fresh();
    for (i = 0; i < 64; i++)
    {
        /* a checkerboard: white / green */
        ULONG on = ((i % 8) / 4 + (i / 8) / 4) & 1;
        chk[i * 4 + 0] = on ? 255 : 0;
        chk[i * 4 + 1] = 255;
        chk[i * 4 + 2] = on ? 255 : 0;
        chk[i * 4 + 3] = 255;
        lm[i] = 128;                            /* half brightness */
    }
    Core_Clear(&core, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    Ortho();
    Core_BindTexture(&core, GL_TEXTURE_2D, 1);
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, 3, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, chk);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    Core_Enable(&core, GL_TEXTURE_2D, 1);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    /* left: the texture alone, one texel = 10 pixels */
    Core_Begin(&core, GL_QUADS);
    Core_TexCoord2f(&core, 0, 0); Core_Vertex3f(&core, 0, 0, 0);
    Core_TexCoord2f(&core, 1, 0); Core_Vertex3f(&core, 80, 0, 0);
    Core_TexCoord2f(&core, 1, 1); Core_Vertex3f(&core, 80, 80, 0);
    Core_TexCoord2f(&core, 0, 1); Core_Vertex3f(&core, 0, 80, 0);
    Core_End(&core);
    /* right: texture x lightmap (unit 1, MODULATE), GLQuake's world */
    Core_ActiveTexture(&core, GL_TEXTURE1_ARB);
    Core_BindTexture(&core, GL_TEXTURE_2D, 2);
    Core_TexImage2D(&core, GL_TEXTURE_2D, 0, GL_LUMINANCE, 8, 8, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, lm);
    Core_TexParameteri(&core, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    Core_Enable(&core, GL_TEXTURE_2D, 1);
    Core_TexEnvi(&core, GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    Core_Begin(&core, GL_QUADS);
    Core_MultiTexCoord2f(&core, GL_TEXTURE0_ARB, 0, 0); Core_MultiTexCoord2f(&core, GL_TEXTURE1_ARB, 0, 0);
    Core_Vertex3f(&core, 160, 0, 0);
    Core_MultiTexCoord2f(&core, GL_TEXTURE0_ARB, 1, 0); Core_MultiTexCoord2f(&core, GL_TEXTURE1_ARB, 1, 0);
    Core_Vertex3f(&core, 240, 0, 0);
    Core_MultiTexCoord2f(&core, GL_TEXTURE0_ARB, 1, 1); Core_MultiTexCoord2f(&core, GL_TEXTURE1_ARB, 1, 1);
    Core_Vertex3f(&core, 240, 80, 0);
    Core_MultiTexCoord2f(&core, GL_TEXTURE0_ARB, 0, 1); Core_MultiTexCoord2f(&core, GL_TEXTURE1_ARB, 0, 1);
    Core_Vertex3f(&core, 160, 80, 0);
    Core_End(&core);
    Render("textures");
    /* texel (0,0) green, texel (4,0) white: GL t = 0 is the first row */
    CHECK_HEX(Px(5, 5), 0x00ff00UL);
    CHECK_HEX(Px(45, 5), 0xffffffUL);
    CHECK_HEX(Px(45, 45), 0x00ff00UL);
    /* modulated by 128/255: white -> grey, green -> dark green (565 precision) */
    CHECK_HEX(Px(205, 5) & 0xf8fcf8UL, 0x808080UL & 0xf8fcf8UL);
    CHECK_HEX(Px(165, 5) & 0xf8fcf8UL, 0x008000UL & 0xf8fcf8UL);
}

static void test_present(void)
{
    struct CoreScreen scr;
    ULONG x, y, bad;

    test_clear_and_triangle();
    scr.gpu = SCREEN_GPU;
    scr.pitch = 640;
    scr.format = R_COLOR_FORMAT_ARGB8888;
    scr.width = 640;
    scr.height = 480;
    Core_Present(&core, &scr, 160, 120);
    Render("present-src");
    Sim_WritePPM(&sim, "build/host/sim-present.ppm", SCREEN_GPU, 640, R_COLOR_FORMAT_ARGB8888, 640, 480);
    bad = 0;
    for (y = 0; y < H_; y += 7)
        for (x = 0; x < W_; x += 7)
            if (Sim_Pixel(&sim, SCREEN_GPU, 640, R_COLOR_FORMAT_ARGB8888, 160 + x, 120 + y)
                != Sim_Pixel(&sim, COLOR_GPU, W_, R_COLOR_FORMAT_RGB565, x, y))
                bad++;
    CHECK_INT(bad, 0);
    CHECK_HEX(Sim_Pixel(&sim, SCREEN_GPU, 640, R_COLOR_FORMAT_ARGB8888, 10, 10), 0);   /* outside: untouched */
}

int main(void)
{
    vram = (ULONG *) calloc(1, VRAM_SIZE);
    memset(&core, 0, sizeof(core));
    test_clear_and_triangle();
    test_depth_and_cull();
    test_textures();
    test_present();
    Core_Free(&core);
    free(vram);
    return tk_done("test_sim");
}
