/*
 * demo.h -- the RadeonGL demonstrators (demos/README.md).
 *
 * One frame loop (demo.c) for every program: the arguments, the context,
 * Esc and Ctrl-C, the clock and the frame rate. A program is a list of
 * scenes (main_<name>.c); each scene (scene_<name>.c) makes its textures
 * and arrays in init, draws one frame in frame (it clears, the loop
 * swaps), and gives back what it made in done -- the showcase runs them in
 * turn in one context.
 *
 * Only the calls RadeonGL implements (lib/rgl.c's Hook_ functions): no GL
 * lighting, fog, display lists or texture coordinate generation -- the
 * lighting is computed per vertex on the CPU.
 */

#ifndef DEMO_H
#define DEMO_H

#include <exec/types.h>

struct Scene
{
    const char *name;
    BOOL (*init)(void);                     /* FALSE: cannot run (says why) */
    void (*frame)(float t, LONG w, LONG h); /* t: seconds since its init */
    void (*done)(void);
};

extern const struct Scene scene_spinner;
extern const struct Scene scene_glow;
extern const struct Scene scene_landscape;

/* The 68020/68030 builds say so in their $VER (demos.mk: RGL_FOR_030). */
#ifdef RGL_FOR_030
#define DEMO_CPU " for the 68020/68030"
#else
#define DEMO_CPU ""
#endif

/* Each program's own file: its scenes (NULL at the end) and its $VER. */
extern const struct Scene *const demo_scenes[];
extern const char demo_ver[];

/* Sine and cosine of radians (a polynomial: the 68060 has no FSIN). */
float Demo_Sin(float a);
float Demo_Cos(float a);

/* The projection: glFrustum for a vertical field of view in degrees. */
void Demo_Perspective(float fovy, float aspect, float znear, float zfar);

/* A full-screen gradient from top to bottom, depth test off, no texture;
 * the matrices are left as they were. */
void Demo_Background(float r0, float g0, float b0, float r1, float g1, float b1);

/* v rotated about axis 0 (x), 1 (y) or 2 (z) by deg degrees, in place. */
void Demo_Rotate(float *v, ULONG axis, float deg);

/* A small deterministic generator: 15 bits per call. */
ULONG Demo_Rand(ULONG *seed);

/* A colour channel 0..255 from 0..1 (clamped). */
ULONG Demo_Byte(float v);

/* Printf when there is somewhere to print (a Workbench start may have no
 * output). */
void Demo_Say(const char *fmt, LONG a, LONG b, LONG c, LONG d);

#define DEMO_PI 3.14159265f

#endif /* DEMO_H */
