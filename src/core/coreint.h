/*
 * coreint.h -- helpers shared by the core's files only.
 */

#ifndef CORE_COREINT_H
#define CORE_COREINT_H

#include "core.h"

/* floor(f) whichever way the compiler converts float to integer */
LONG  Core_Floor(double f);
/* [0, 1] -> 0..255, rounded, clamped */
ULONG Core_Unit255(float f);

/* Every texture the next draw reads is in use this epoch (a draw may
 * reuse a texture whose registers were emitted frames ago). */
void  Core_MarkTextures(struct Core *c, ULONG units);

/* The R200 primitive for a GL mode (a line loop is drawn as a strip). */
ULONG Vtx_HwPrim(ULONG mode);

/* The open packet is full in the middle of a primitive: close what can be
 * drawn, reopen with what the rest needs. Not static, so the compiler
 * keeps it out of the per-vertex path (inlined, it made every vertex save
 * eleven registers and a 148-byte frame). */
void  Vtx_Split(struct Core *c);

/* The upload cache forgets everything (array.c). */
void  UCache_Clear(struct Core *c);

/* The longest prefix of n vertices that is a whole primitive of mode. */
ULONG Vtx_Whole(ULONG mode, ULONG n);

/* Wait for the GPU if texture t was drawn in the current epoch. */
void  Core_TexHazard(struct Core *c, struct CoreTex *t);

/* A fresh texture object's GL defaults. */
void  Tex_Defaults(struct CoreTex *t, ULONG name);

/* An object for name (created on first use), or NULL out of memory. */
struct CoreTex *Tex_Get(struct Core *c, ULONG name);

/* light_dirty bits beside the lights' own (0..7) */
#define LD_MATERIAL 0x100UL
#define LD_MODEL    0x200UL
#define LD_ALL      0x3ffUL

/* GL's initial lighting state (light.c, from Core_Init). */
void  Light_Defaults(struct Core *c);
/* R2E_TclDefaults wrote the lighting controls: their shadow takes the
 * defaults, and the lights, materials and model go out again (light.c). */
void  Light_Reset(struct Core *c);
/* The TCL lighting state that D_LIGHT asks for (hwstate.c's emit). */
void  Light_Emit(struct Core *c);
/* The modelview and its inverse transpose for the TCL, when lighting
 * needs them (hwstate.c's emit, with D_MATRIX). */
void  Light_Matrices(struct Core *c);

/* Texgen (texgen.c): GL's defaults; the registers as R2E_TclDefaults left
 * them; the TCL's texgen state and matrices when D_TEXGEN asks (returns 1
 * when the units the TCL computes changed: the vertex format follows). */
void  Texgen_Defaults(struct Core *c);
void  Texgen_Forget(struct Core *c);
ULONG Texgen_Update(struct Core *c);
/* Fog (fog.c): defaults; the shadows forgotten at D_INIT; the TCL's fog
 * mode bits for UCP_VERT_BLEND_CTL; what D_FOG asks for */
void  Fog_Defaults(struct Core *c);
void  Fog_Forget(struct Core *c);
void  Fog_Emit(struct Core *c);
/* c->vnorm from lighting and the sphere maps (texgen.c) */
void  Core_UpdateVnorm(struct Core *c);

/* RB3D_PLANEMASK for glColorMask's bits in a colour format (hwstate.c). */
ULONG Core_PlaneMask(ULONG format, ULONG m);

/* Errors that a call inside glBegin/glEnd must raise. */
#define IN_BEGIN(c) ((c)->prim != CORE_NO_PRIM)

/* Mark everything a texture unit change can touch. */
#define DIRTY_UNIT(c, u) ((c)->dirty |= (D_TEX0 << (u)) | D_COMBINE | D_PPCNTL)

#endif /* CORE_COREINT_H */
