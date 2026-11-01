/*
MiniGL extensions
*/

#include <mgl/gl.h>

#ifndef MGL_EXT_H
#define MGL_EXT_H

extern void mglTV23fv(GLcontext context, GLsizei stride, glpoly_t *p);
extern void mglTV23fvLM(GLcontext context, GLsizei stride, glpoly_t *p);
extern void mglTV23fvLMalpha(GLcontext context, GLsizei stride, glpoly_t *p); // Cowcat
extern void mglMtexTV23fv(GLcontext context, GLsizei stride, glpoly_t *p);

#endif
