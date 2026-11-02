/*
 * census.h -- the census back end of minigl.library.
 *
 * A minigl.library (dispatch ABI 5) that draws nothing and counts what a
 * client asks for: every dispatch slot, and per frame the primitives,
 * vertices and the bytes they would cost as packed R200 vertices, the
 * texture uploads, vertex-array traffic, state and matrix changes. One CSV
 * line per frame (mglSwitchDisplay ends a frame) and the per-slot totals
 * when the context is deleted. docs/census.md says what the columns mean
 * and how tools/census.py turns them into bus bytes.
 *
 * Output: <prefix>.csv and <prefix>-slots.csv, prefix from the variable
 * MiniGLCensus (default "PROGDIR:minigl-census"). MiniGLCensusExt
 * replaces the GL_EXTENSIONS string (default: what PiStorm3D 29.1
 * reports, so a client takes the paths it takes there).
 *
 * Compiled WITHOUT -msasregparm: clients call the table with GCC's default
 * stack convention.
 */

#ifndef RADEONGL_CENSUS_H
#define RADEONGL_CENSUS_H

#include <exec/types.h>

/* The table's currentContext points here; MGLD_mglCreateContext stores the
 * client's context into it. */
extern GLcontext Cen_CurrentContext;

/* Counts one call of a dispatch slot. */
void Cen_Hit(ULONG slot);

/* The shell's hooks (Back_Init, Back_ClientGone, Back_IdString). */
#include "backend.h"

#endif /* RADEONGL_CENSUS_H */
