/*
 * bench.h -- GLBench, a stress benchmark for minigl.library 29.1
 * (RadeonGL), 2026-11-08: seven scenes, the frame loop, the score, the
 * overlay and the picture check (bench.c), helpers (util.c).
 *
 * A scene draws frame f as a function of f (frames are counted, not
 * timed), so every machine draws the same frames and the figures
 * compare. A scene marked pure draws frame f the same whatever came
 * before: Burn's picture check can draw a reference frame in between.
 *
 * GCC only: it links the MiniGL SDK's import library, as the demos do.
 * C89 otherwise.
 */

#ifndef GLBENCH_BENCH_H
#define GLBENCH_BENCH_H

#include <exec/types.h>
#include <proto/minigl.h>

/* tokens RadeonGL takes that the 29.1 SDK headers do not name */
#ifndef GL_N3F_V3F
#define GL_N3F_V3F                  0x2a25
#endif
#ifndef GL_T2F_N3F_V3F
#define GL_T2F_N3F_V3F              0x2a2b
#endif
#ifndef GL_COLOR_INDEX8_EXT
#define GL_COLOR_INDEX8_EXT         0x80e5
#endif
#ifndef GL_RGL_STATIC_ARRAYS_HINT
#define GL_RGL_STATIC_ARRAYS_HINT   0x52474c01
#endif
#ifndef GL_RGL_STATIC_INDICES_HINT
#define GL_RGL_STATIC_INDICES_HINT  0x52474c02
#endif

struct BenchScene
{
    const char *name;
    const char *what;           /* what it presses on, for the log */
    ULONG ref10;                /* the reference machine's frames a second x 10 */
    BOOL pure;                  /* frame f is the same whatever came before */
    BOOL (*init)(LONG w, LONG h);   /* FALSE: cannot run (it says why) */
    void (*frame)(ULONG f, LONG w, LONG h);
    void (*done)(void);
};

extern const struct BenchScene scene_pelt;
extern const struct BenchScene scene_lanterns;
extern const struct BenchScene scene_chrome;
extern const struct BenchScene scene_embers;
extern const struct BenchScene scene_plasma;
extern const struct BenchScene scene_swarm;
extern const struct BenchScene scene_mirror;

#define BENCH_SCENES 7
extern const struct BenchScene *const bench_scenes[BENCH_SCENES];

/* what a frame did, added up by the scene as it draws */
extern ULONG bench_prims;       /* triangles, lines and points */
extern ULONG bench_bytes;       /* bytes it sent or read over the bus itself */
extern ULONG bench_shells;      /* Pelt's shells (SHELLS=) */
extern ULONG bench_frames;      /* frames this run of the scene draws; 0 in Burn */
extern BOOL bench_rgl;          /* the library names GL_RGL_static_arrays */

/* Text over everything, x, y in pixels from the bottom left, the overlay's
 * font (topaz 8), scale 1 or 2: a scene calls it last in a frame; it
 * leaves the state as every scene finds it (bench.c Reset) */
void B_Label(LONG x, LONG y, LONG scale, const char *s, float r, float g, float b);

/* --- util.c ------------------------------------------------------------------ */

#define B_PI 3.14159265f
#define B_TABLE 1024            /* entries in a turn of the sine table */

float B_Sin(float a);
float B_Cos(float a);
/* the sine table: a turn is B_TABLE steps; any integer phase */
float B_SinT(LONG phase);
float B_CosT(LONG phase);
void B_TableInit(void);

void B_Perspective(float fovy, float aspect, float znear, float zfar);
void B_Ortho2D(LONG w, LONG h);         /* pixels, origin bottom left */
void B_Background(float r0, float g0, float b0, float r1, float g1, float b1);
ULONG B_Rand(ULONG *seed);
ULONG B_Byte(float v);
LONG B_Floor(float v);                  /* whatever the FPU's cast does */
void B_Say(const char *fmt, LONG a, LONG b, LONG c, LONG d);
APTR B_Alloc(ULONG bytes);              /* cleared; NULL says why */
void B_Free(APTR p);

/* a texture from bytes: GL_LINEAR, or mipmaps through gluBuild2DMipmaps */
GLuint B_Texture(GLint internal, LONG w, LONG h, GLenum format, const void *pixels,
                 BOOL mipmap, GLenum wrap);

#endif /* GLBENCH_BENCH_H */
