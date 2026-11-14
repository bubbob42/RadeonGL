/*
 * backend.h -- what a minigl.library back end gives the shell (shell.c).
 *
 * The shell is the same for every back end: the census library (census.c)
 * and RadeonGL (rgl.c) link it unchanged.
 */

#ifndef RADEONGL_BACKEND_H
#define RADEONGL_BACKEND_H

#include <exec/types.h>
#include <exec/tasks.h>

/* The library's $VER line (the ROMTag's id string). */
extern const char Back_IdString[];

/* Once, from LibInit, after dos/graphics/intuition are open. */
void Back_Init(void);

/* From LibClose: a context the closing task still owns (it died or forgot
 * mglDeleteContext) is freed here, under Forbid -- no file I/O. */
void Back_ClientGone(struct Task *task);

/* From LibExpunge, before the bases close: no client is left. */
void Back_Expunge(void);

#endif /* RADEONGL_BACKEND_H */
