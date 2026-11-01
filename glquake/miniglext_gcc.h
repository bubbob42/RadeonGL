/*

MiniGL extensions - direct vertexbuffer construction with quake polys and interface for fake multitexturing

*/

//#include "quakedef.h"

#include <stddef.h>
#include <mgl/gl.h>

//#include "miniglext.h"


/******************

Fake multitexturing

*******************/

/*
 * MiniGLV3D port note (Phase L1): see miniglext.c's matching header comment
 * -- these are now thin loops over the public GL API instead of direct
 * MGLVertex struct pokes. This header is only pulled in by non-VBCC (GCC)
 * builds (see gl_rsurf.c/gl_warp.c's #ifdef __VBCC__ guard); vbcc builds use
 * miniglext.c/.h instead, so this file is not part of this project's actual
 * build path, but kept in sync for consistency.
 */

static void mglMtexTV23fv(GLcontext context, GLsizei stride, glpoly_t *p)
{
	int	count;
	GLfloat *v;

	v = p->verts[0];
	count = p->numverts;

	do
	{
		GLTexCoord2f(context, v[3], v[4]);
		GLMultiTexCoord2fARB(context, GL_TEXTURE0_ARB + 1, v[5], v[6]);
		GLVertex3fv(context, v);

		v+=stride;

	} while (--count);
}

/**********************************************************

These function allows an array of texcoords and verts to be
built directly in the vertexbuffer in 1 go

***********************************************************/

static void mglTV23fv(GLcontext context, GLsizei stride, glpoly_t *p)
{
	int	count;
	GLfloat *v;

	v = p->verts[0];
	count = p->numverts;

	do
	{
		GLTexCoord2f(context, v[3], v[4]);
		GLVertex3fv(context, v);

		v+=stride;

	} while (--count);
}

/*******************************

Same function, but for lightmaps

********************************/

static void mglTV23fvLM(GLcontext context, GLsizei stride, glpoly_t *p)
{
	int	count;
	GLfloat *v;

	v = p->verts[0];
	count = p->numverts;

	do
	{
		GLTexCoord2f(context, v[5], v[6]);
		GLVertex3fv(context, v);

		v+=stride;

	} while (--count);
}

// support for alpha surfaces - Cowcat

static void mglTV23fvLMalpha(GLcontext context, GLsizei stride, glpoly_t *p)
{
	int	count;
	GLfloat *v;

	v = p->verts[0];
	count = p->numverts;

	do
	{
		/* Original manually re-stamped context->CurrentColor into every
		 * vertex here -- GLVertex3fv/GLVertex4f already do this
		 * unconditionally now, so this is otherwise identical to
		 * mglTV23fvLM above. */
		GLTexCoord2f(context, v[5], v[6]);
		GLVertex3fv(context, v);

		v+=stride;

	} while (--count);
}
