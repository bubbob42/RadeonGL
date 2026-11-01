#include "quakedef.h"
#include <mgl/mglmacros.h>

cvar_t	r_showbboxes = {"r_showbboxes","0"};

GLfloat	bboxcolor[4] = { 1, 1, 1, 0.2 };

/*
 * MiniGLV3D port note: originally poked the old mglmacros.h's file-local
 * mglvert pointer directly (same disease as miniglext.c's pre-Phase-L1
 * functions) -- that pointer no longer exists (see MiniGLV3D/gl/include/
 * mgl/mglmacros.h's own comment: gl.h's real macros already forward to
 * the correct GL*() functions, no direct struct access needed). This
 * macro never did anything beyond what glVertex3f already does (no
 * color/shading logic), so it's just a thin alias now.
 */
#define glVertex3fFlat(x,y,z) glVertex3f(x,y,z)


void R_EmitWirePoint(vec3_t origin)
{
	int size = 8;

	glBegin(GL_LINES);

	glVertex3f(origin[0] - size, origin[1], origin[2]);
	glVertex3f(origin[0] + size, origin[1], origin[2]);
	glVertex3f(origin[0], origin[1] - size, origin[2]);
	glVertex3f(origin[0], origin[1] + size, origin[2]);
	glVertex3f(origin[0], origin[1], origin[2] - size);
	glVertex3f(origin[0], origin[1], origin[2] + size);

	glEnd();	
}

void R_EmitWireBox(vec3_t mins, vec3_t maxs)
{
	glBegin(GL_QUAD_STRIP);

	glVertex3f (mins[0], mins[1], mins[2]);
	glVertex3f (mins[0], mins[1], maxs[2]);
	glVertex3f (maxs[0], mins[1], mins[2]);
	glVertex3f (maxs[0], mins[1], maxs[2]);
	glVertex3f (maxs[0], maxs[1], mins[2]);
	glVertex3f (maxs[0], maxs[1], maxs[2]);
	glVertex3f (mins[0], maxs[1], mins[2]);
	glVertex3f (mins[0], maxs[1], maxs[2]);
	glVertex3f (mins[0], mins[1], mins[2]);
	glVertex3f (mins[0], mins[1], maxs[2]);

	glEnd ();
}

void Box_Color_f (void)
{
	if(Cmd_Argc() == 1)
	{
		Con_Printf("usage: bboxcolor red green blue alpha\n");
		Con_Printf("actual color %.1f, %.1f, %.1f, %.1f\n", bboxcolor[0], bboxcolor[1], bboxcolor[2], bboxcolor[3]);
		return;
	}

	bboxcolor[0] = CLAMP(0.0, Q_atof(Cmd_Argv(1)), 1.0);
	bboxcolor[1] = CLAMP(0.0, Q_atof(Cmd_Argv(2)), 1.0);
	bboxcolor[2] = CLAMP(0.0, Q_atof(Cmd_Argv(3)), 1.0);
	bboxcolor[3] = CLAMP(0.0, Q_atof(Cmd_Argv(4)), 1.0);

}

void R_ShowBBoxes (void)
{
	int 		i;
	edict_t 	*ed;
	extern edict_t	*sv_player;
	vec3_t		mins, maxs;

	int mode = (int)r_showbboxes.value;

	if( /* mode == 0 || */ cl.maxclients > 1 || !sv.active)
		return;

	if(mode == 2)
		glDisable(GL_DEPTH_TEST);

	glEnable(GL_BLEND);
	//glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_CULL_FACE);
	//glColor4f (1, 1, 1, 0.2);
 
	glColor4fv (bboxcolor);

	for(i=0, ed = NEXT_EDICT(sv.edicts); i < sv.num_edicts; i++, ed = NEXT_EDICT(ed)) 
	{
		if(ed->free)
			continue; //


		if(ed == sv_player) 
			continue;


		if(ed->v.mins[0] == ed->v.maxs[0] && ed->v.mins[1] == ed->v.maxs[1] && ed->v.mins[2] == ed->v.maxs[2])
		{
			// point entity
			R_EmitWirePoint(ed->v.origin);

		}

		else
		{
			// box entity
			VectorAdd(ed->v.mins, ed->v.origin, mins);
			VectorAdd(ed->v.maxs, ed->v.origin, maxs);

			R_EmitWireBox (mins, maxs);
		}
		
	}

	if(mode == 2)
		glEnable(GL_DEPTH_TEST);

	glDisable(GL_BLEND);
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_CULL_FACE);
	//glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glColor3f (1,1,1);

}


#if 0
void mglTV23fvLines(GLcontext context, GLsizei stride, glpoly_t *p) // Cowcat
{
	
	int 	count, i;
	GLfloat *v;
	MGLVertex *vert; // , *vert2;

	v = p->verts[0];
	count = p->numverts;

	vert = &context->VertexBuffer[0];
	//vert2 = &context->VertexBuffer[0+1];

	context->VertexBufferPointer = count;

	//do
	for(i = 0; i < count ; i++)
	{
		vert->bx = (GLfloat)v[0];
		vert->by = (GLfloat)v[1];
		vert->bz = (GLfloat)v[2];

		//v+=stride;
		vert++;
		
		//
		vert->bx = (GLfloat)(v+stride)[0];
		vert->by = (GLfloat)(v+stride)[1];
		vert->bz = (GLfloat)(v+stride)[2];

		v+=stride;
		vert++;
		//

	} //while (--count);


	/*
	vert->bx = (GLfloat)v[0];
	vert->by = (GLfloat)v[1];
	vert->bz = (GLfloat)v[2];

	vert++;

	vert->bx = (GLfloat)(p->verts[0])[0];
	vert->by = (GLfloat)(p->verts[1])[1];
	vert->bz = (GLfloat)(p->verts[2])[2];
	*/

}
#endif

/*
 * MiniGLV3D port note: originally poked the old mglmacros.h's file-local
 * mglvert pointer directly (same disease as miniglext.c's pre-Phase-L1
 * functions) -- that pointer no longer exists (see MiniGLV3D/gl/include/
 * mgl/mglmacros.h's own comment: gl.h's real macros already forward to
 * the correct GL*() functions, no direct struct access needed). This
 * macro never did anything beyond what glVertex3f already does (no
 * color/shading logic), so it's just a thin alias now.
 */
#define glVertex3fFlat(x,y,z) glVertex3f(x,y,z)


void R_DrawLinePolys (msurface_t *s) // from Tomaz
{
	int	i;
	float	*v;
	glpoly_t *p;

	p = s->polys;
	v = p->verts[0];

	glDisable(GL_TEXTURE_2D);

	#if 0 // line_loop not implemented in amiga mgl
	
	glBegin(GL_LINE_LOOP);

	for (i=0 ; i < p->numverts ; i++, v+= VERTEXSIZE)
		glVertex3fv(v);
	
	#else

	// NPRQuake based idea - Cowcat

	glBegin(GL_LINES); 

	for (i=0 ; i < (p->numverts-1) ; i++, v+= VERTEXSIZE)
	{
		glVertex3fFlat( v[0], v[1], v[2]);
		glVertex3fFlat( (v+VERTEXSIZE)[0], (v+VERTEXSIZE)[1], (v+VERTEXSIZE)[2] );
		
	}

	// last line to first line (line_loop workaround)
	glVertex3fFlat( v[0], v[1], v[2]);
	glVertex3fFlat( (p->verts[0])[0], (p->verts[0])[1], (p->verts[0])[2] );

	glEnd();

	/*
	glBegin(GL_LINES);
	mglTV23fvLines(mini_CurrentContext, VERTEXSIZE, p);
	glEnd();
	*/

	glEnable(GL_TEXTURE_2D);

	#endif

}

