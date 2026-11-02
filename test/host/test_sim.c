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
    /* a point of size 5 at (40, 200): a 5 x 5 square */
    Core_Enable(&core, GL_CULL_FACE, 0);
    Core_PointSize(&core, 5.0f);
    Core_Begin(&core, GL_POINTS);
    Core_Color4ub(&core, 255, 0, 255, 255);
    Core_Vertex3f(&core, 40.5f, 200.5f, 9);
    Core_End(&core);
    Render("depth");
    CHECK_HEX(Px(40, 200), 0xff00ffUL);
    CHECK_HEX(Px(42, 202), 0xff00ffUL);
    CHECK_HEX(Px(44, 200), 0x000000UL);
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

static void PutBE(FILE *f, ULONG v)
{
    fputc((int) (v >> 24), f);
    fputc((int) ((v >> 16) & 255), f);
    fputc((int) ((v >> 8) & 255), f);
    fputc((int) (v & 255), f);
}

/* The dump the library writes (lib/rgl.c WriteDump), from a host scene,
 * through the viewer's loader: the same picture must come out. */
static void test_dump(void)
{
    FILE *f;
    ULONG i;
    ULONG n;
    ULONG top;
    static struct Sim view;
    unsigned char hdr[32];
    unsigned char px[3];
    long off;

    test_clear_and_triangle();
    n = (ULONG) (cs.ptr - stream);
    top = Heap_Top(&core);
    f = fopen("build/host/sim-test.r2d", "wb");
    CHECK(f != NULL);
    if (f == NULL)
        return;
    PutBE(f, 0x5232444dUL); PutBE(f, 1); PutBE(f, cs.swap); PutBE(f, core.tex_swap);
    PutBE(f, W_); PutBE(f, H_); PutBE(f, COLOR_GPU); PutBE(f, W_); PutBE(f, R_COLOR_FORMAT_RGB565);
    PutBE(f, DEPTH_GPU); PutBE(f, W_); PutBE(f, TEX_GPU); PutBE(f, top); PutBE(f, n); PutBE(f, 1);
    for (i = 0; i < n; i++)
        PutBE(f, stream[i]);
    for (i = 0; i < top / 4; i++)
        PutBE(f, vram[TEX_GPU / 4 + i]);
    fclose(f);
    CHECK_INT(Sim_RenderDump("build/host/sim-test.r2d", "build/host/sim-dump.ppm", &view), 0);
    CHECK_INT(view.unsupported, 0);
    /* the pixel at GL (160, 100): row H - 1 - 100 */
    f = fopen("build/host/sim-dump.ppm", "rb");
    CHECK(f != NULL);
    if (f == NULL)
        return;
    CHECK(fread(hdr, 1, 15, f) == 15);      /* "P6\n320 240\n255\n" */
    off = 15 + 3L * ((H_ - 1 - 100) * W_ + 160);
    fseek(f, off, SEEK_SET);
    CHECK(fread(px, 1, 3, f) == 3);
    fclose(f);
    CHECK_INT(px[0], 255);
    CHECK_INT(px[1], 0);
    CHECK_INT(px[2], 0);
}

int main(void)
{
    vram = (ULONG *) calloc(1, VRAM_SIZE);
    memset(&core, 0, sizeof(core));
    test_clear_and_triangle();
    test_depth_and_cull();
    test_textures();
    test_present();
    test_dump();
    Core_Free(&core);
    free(vram);
    return tk_done("test_sim");
}
