/*

MiniGL extensions - direct vertexbuffer construction with quake polys and interface for fake multitexturing

*/

#include "quakedef.h"

#include <stddef.h>
#include <mgl/gl.h>

#include "miniglext.h"


/******************

Fake multitexturing

*******************/


/*
 * MiniGLV3D port note (Phase L1): these 5 functions used to poke MiniGL's
 * internal MGLVertex struct fields directly (vert->v.u/v.v, vert->tcoord.s/t,
 * vert->v.color.r/g/b/a) as a bulk-fill shortcut. Those fields were renamed
 * or dropped by the MiniGLV3D port (see STATUS.md), so this is now a thin
 * loop over the public GL API instead: GLTexCoord2f/GLMultiTexCoord2fARB set
 * the texcoord slot(s) about to be consumed by the following GLVertex3fv
 * call (same "texcoord before vertex" contract as any real GL app), and
 * GLVertex3fv itself advances VertexBufferPointer and stamps
 * context->CurrentColor into the new vertex unconditionally -- the manual
 * per-vertex color copy the alpha-surface variants used to do here is now
 * redundant and has been dropped. Callers already bracket these with
 * glBegin(GL_POLYGON)/glEnd() (glBegin resets VertexBufferPointer to 0), so
 * no direct VertexBuffer/VertexBufferPointer access is needed here at all.
 */

void mglMtexTV23fv(GLcontext context, GLsizei stride, glpoly_t *p)
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

void mglTV23fv(GLcontext context, GLsizei stride, glpoly_t *p)
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

void mglTV23fvLM(GLcontext context, GLsizei stride, glpoly_t *p)
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

void mglTV23fvLMalpha(GLcontext context, GLsizei stride, glpoly_t *p)
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
		 * mglTV23fvLM above. Kept as a separate function to match the
		 * header's public API and the call sites' own naming. */
		GLTexCoord2f(context, v[5], v[6]);
		GLVertex3fv(context, v);

		v+=stride;

	} while (--count);
}
