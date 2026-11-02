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

/* A fresh texture object's GL defaults. */
void  Tex_Defaults(struct CoreTex *t, ULONG name);

/* An object for name (created on first use), or NULL out of memory. */
struct CoreTex *Tex_Get(struct Core *c, ULONG name);

/* Errors that a call inside glBegin/glEnd must raise. */
#define IN_BEGIN(c) ((c)->prim != CORE_NO_PRIM)

/* Mark everything a texture unit change can touch. */
#define DIRTY_UNIT(c, u) ((c)->dirty |= (D_TEX0 << (u)) | D_COMBINE | D_PPCNTL | D_VTXFMT)

#endif /* CORE_COREINT_H */
