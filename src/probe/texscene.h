/*
 * texscene.h -- the textured scene of the Tri probe (SCENE=TEX).
 *
 * Four 128 x 128 quads cover the 256 x 256 target, each texture 4 x 4 with
 * NEAREST filtering, so one texel is a 32 x 32 block and every check pixel
 * (a block's centre) has exactly one right value:
 *
 *   top left      RGBA, stored ARGB4444 (values n * 17 survive 4 bits)  REPLACE
 *   top right     RGB, stored RGB565 (values 0 and 255 survive)         REPLACE
 *   bottom left   LUMINANCE, stored I8                                  REPLACE
 *   bottom right  the RGB texture on unit 0 times a 2 x 2 LUMINANCE
 *                 lightmap (0 / 255) on unit 1, MODULATE: GLQuake's
 *                 multitexture path
 *
 * What it answers on the card: texture fetch from VRAM, the texel byte
 * order (texels stored byte-reversed by the 68k, tex_swap), the three
 * formats, texture coordinates passed through TCL, two units combined.
 * Shared with test_sim like triscene.c; pure C89 on the GL core.
 */

#ifndef PROBE_TEXSCENE_H
#define PROBE_TEXSCENE_H

#include "triscene.h"

#define TEX_NPIXELS 20

/* Draw the scene with c (a TRI_W x TRI_H ARGB8888 target). */
void TexScene_Draw(struct Core *c);

/* The check pixels, computed from the same texel functions the scene
 * uploads. Fills out[0 .. TEX_NPIXELS - 1]. */
void TexScene_Pixels(struct TriPixel *out);

#endif /* PROBE_TEXSCENE_H */
