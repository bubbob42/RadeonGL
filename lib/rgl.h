/*
 * rgl.h -- what the generated dispatch table (build/rgl/rgl_dispatch.c)
 * needs from the RadeonGL back end (rgl.c).
 */

#ifndef RADEONGL_RGL_H
#define RADEONGL_RGL_H

#include <exec/types.h>

/* The table's currentContext points here; MGLD_mglCreateContext stores
 * the client's context into it. */
extern GLcontext Rgl_CurrentContext;

/* The current context's core, NULL without one (lib/rgllight.c). */
struct Core;
struct Core *Rgl_Core(void);

#endif /* RADEONGL_RGL_H */
