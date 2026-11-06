/*
 * r200view.c -- render a frame dump of the RadeonGL library (RadeonGLDump=N,
 * lib/rgl.c) to a PPM with the reference rasteriser.
 *
 * Usage: r200view DUMP.r2d OUT.ppm
 */

#include <stdio.h>
#include "r200sim.h"

static struct Sim sim;

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "usage: r200view DUMP.r2d OUT.ppm\n");
        return 2;
    }
    if (Sim_RenderDump(argv[1], argv[2], &sim) != 0)
        return 1;
    printf("r200view: %lu triangles, %lu points, %lu pixels, %lu culled, %lu unsupported\n",
           (unsigned long) sim.tris, (unsigned long) sim.points, (unsigned long) sim.pixels,
           (unsigned long) sim.culled, (unsigned long) sim.unsupported);
    return 0;
}
