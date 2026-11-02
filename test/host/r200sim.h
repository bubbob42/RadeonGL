/*
 * r200sim.h -- a reference rasteriser for the R200 packets RadeonGL emits
 * (concept 10.3). Host only.
 *
 * It reads a command stream the way the CP would (register writes, TCL
 * vector memory, the immediate / array / indexed draws) and draws into a
 * memory image of VRAM: hardware TCL (the MVP matrix in vector memory),
 * the viewport, culling, the scissor box, Z16, textures in the formats the
 * core stores, the two-stage combiner, alpha test, blending, the plane
 * mask. What it cannot show is where the real chip reads a register
 * differently from Mesa and Linux, which is what it is built from.
 *
 * Simplifications, on purpose: nearest-texel sampling at level 0 (or the
 * level the triangle's size suggests), no mip filtering between levels,
 * no lines or points wider than one pixel, no guard band. Pixels are
 * centred (VTX_PIX_CENTER_OGL), the fill rule is top-left.
 */

#ifndef R200SIM_H
#define R200SIM_H

#include <exec/types.h>

struct Sim
{
    ULONG reg[0x8000 / 4];      /* the register file, as written */
    float vec[0x200 * 4];       /* TCL vector memory */
    ULONG vec_addr;
    float scal[0x200];
    ULONG scal_addr;
    /* VRAM: one block of GPU addresses mapped to host memory */
    ULONG vram_gpu;
    ULONG vram_size;
    ULONG *vram;                /* dwords as the 68k stored them */
    ULONG tex_swap;             /* stored dwords are byte-reversed (as the core writes) */
    ULONG swap;                 /* the stream's byte order */
    ULONG aos_n;
    ULONG aos_comps[12], aos_stride[12], aos_gpu[12];
    ULONG pend_cntl, pend_nidx;
    /* statistics */
    ULONG tris, pixels, culled, unsupported;
};

void Sim_Init(struct Sim *s, ULONG *vram, ULONG vram_gpu, ULONG vram_size,
              ULONG swap, ULONG tex_swap);

/* Run n stream dwords. */
void Sim_Run(struct Sim *s, const ULONG *dw, ULONG n);

/* A pixel of a colour buffer in VRAM as 0xRRGGBB (565 or 8888). */
ULONG Sim_Pixel(const struct Sim *s, ULONG gpu, ULONG pitch_px, ULONG format,
                ULONG x, ULONG y);

/* Write a colour buffer as a binary PPM. Returns 0 or -1. */
int Sim_WritePPM(const struct Sim *s, const char *path, ULONG gpu, ULONG pitch_px,
                 ULONG format, ULONG w, ULONG h);

#endif
