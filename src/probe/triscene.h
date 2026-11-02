/*
 * triscene.h -- the scene the Tri probe draws (concept section 11, probe 4).
 *
 * Shared by the probe (src/probe/tri.c, through the R200) and the host test
 * (test/host/test_sim.c, through r200sim), so the picture the Mac shows is
 * the one the card must draw. Pure C89 on the GL core, no OS calls.
 */

#ifndef PROBE_TRISCENE_H
#define PROBE_TRISCENE_H

#include <exec/types.h>
#include "core.h"

#define TRI_W 256
#define TRI_H 256

/* A pixel the probe reads back. x, y in memory order (row 0 = the top of
 * the picture). exact: the pixel must be rgb (0xRRGGBB); otherwise it must
 * NOT be rgb (a shaded area must not show the background). */
struct TriPixel
{
    ULONG x, y;
    ULONG rgb;
    ULONG exact;
    const char *what;
};

#define TRI_BACKGROUND 0x000040UL
#define TRI_NPIXELS    8
extern const struct TriPixel TriScene_Pixels[TRI_NPIXELS];

/* Draw the scene with c, a core on a TRI_W x TRI_H ARGB8888 target:
 * glClear to dark blue, a solid red triangle bottom left, a solid green
 * one top left, a shaded one (red, green, blue corners) top right. */
void TriScene_Draw(struct Core *c);

#endif /* PROBE_TRISCENE_H */
