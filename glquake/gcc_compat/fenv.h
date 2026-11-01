/*
 * Minimal GCC-compatible fenv.h for m68k-amigaos-gcc/clib2.
 *
 * gl_vidamiga.c's GL_Init() (real, always-live code -- the fesetenv() call
 * fires unconditionally on every build under #if !defined(__PPC__), it's
 * not Permedia-specific dead code, only *which* rounding mode gets picked
 * is) uses vbcc's own fenv.h macro names (FE_LDBLPREC/FE_TOWARDZERO/
 * FE_TONEAREST) and fesetenv(). clib2's real fenv.h is a genuinely
 * different, standards-conforming header that doesn't define
 * FE_LDBLPREC at all (that's a vbcc-specific rounding-*precision*
 * extension, not part of any real ISO C99 fenv.h) -- so gl_vidamiga.c
 * fails to compile under GCC+clib2 with "'FE_LDBLPREC' undeclared"
 * unless this header's directory is put on the include path ahead of
 * clib2's own fenv.h.
 *
 * Only fesetenv() and the 3 macros gl_vidamiga.c actually uses are
 * provided (grepped the whole file for fenv_t/FE_/fe* identifiers to
 * confirm nothing else is needed) -- not a full fenv.h reimplementation.
 * Values and the FPU write copied verbatim from vbcc's own fenv.h
 * (C:\amiga-dev\vbcc_target_m68k-amigaos\targets\m68k-amigaos\include\fenv.h)
 * so the bit patterns landing in fpcr are identical either way.
 */
#ifndef _MGLV3D_GCC_COMPAT_FENV_H
#define _MGLV3D_GCC_COMPAT_FENV_H

typedef unsigned long fenv_t;

#define FE_TONEAREST  0x00
#define FE_TOWARDZERO 0x10
#define FE_DOWNWARD   0x20
#define FE_UPWARD     0x30

#define FE_LDBLPREC 0x00
#define FE_FLTPREC  0x40
#define FE_DBLPREC  0x80

static __inline__ int fesetenv(const fenv_t *envp)
{
	__asm__ __volatile__ ("fmove.l %0,%%fpcr" :: "d"(*envp));
	return 0;
}

#endif
