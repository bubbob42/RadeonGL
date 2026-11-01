// from TomazQuake

#include "quakedef.h"

#include <mgl/mglmacros.h>
#include "miniglext.h"

qboolean mirror;
int 	mirrortexturenum;
mplane_t *mirror_plane;
//mplane_t mirror_far_plane;
cvar_t	r_mirroralpha = {"r_mirroralpha","1"};

//extern r_projection_matrix[16];
//extern r_modelview_matrix[16];

float r_base_world_matrix[16];
//float r_base_projection_matrix[16];
//float r_base_modelview_matrix[16];

//float r_original_projection_matrix[16];
//float r_original_modelview_matrix[16];

extern void R_RenderScene();
//extern void R_SetFrustum(float fovx, float fovy);

//int mirror_clipside;

/*
 * MiniGLV3D port note (Phase L1): see miniglext.c's matching header comment
 * -- reimplemented as a thin loop over the public GL API instead of a
 * direct MGLVertex struct poke. The manual per-vertex CurrentColor copy is
 * no longer needed: GLVertex3fv/GLVertex4f already stamp CurrentColor into
 * every new vertex unconditionally.
 */
static void mglTV23fvalpha(GLcontext context, GLsizei stride, glpoly_t *p) // Cowcat
{
	int 	count;
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


void Mirror_Scale (void)
{
	if (mirror_plane->normal[2])
		glScalef (1, -1, 1);

	else
		glScalef (-1, 1, 1);

	glCullFace(GL_BACK);
}

#define PlaneDiff(point, plane) (((plane)->type < 3) ? (point)[(plane)->type] - (plane)->dist : DotProduct((point), (plane)->normal) - (plane)->dist)

#if 1

void R_Mirror (void)
{
	float		d;
	msurface_t	*s;
	entity_t	*ent;
	glpoly_t 	*p;
	float		mirror_alpha;
	int		cl_oldvisedicts;
	//texture_t	*t;

	vec3_t oldvieworg;
	vec3_t oldviewangles;
	vec3_t oldvpn;

	r_drawviewmodel.value = 0; // don�t draw weapon - Cowcat

	memcpy (r_base_world_matrix, r_world_matrix, sizeof(r_base_world_matrix));
	
	cl_oldvisedicts = cl_numvisedicts;

	// this is yourself on window - Cowcat
	ent = &cl_entities[cl.viewentity];

	if (cl_numvisedicts < MAX_VISEDICTS)
	{
		cl_visedicts[cl_numvisedicts] = ent;
		cl_numvisedicts++;
	}
	
	VectorCopy(r_refdef.vieworg, oldvieworg);
	VectorCopy(r_refdef.viewangles, oldviewangles);
	VectorCopy(vpn, oldvpn);

	mirror_alpha = r_mirroralpha.value * (1.0 / 2.5);

	s = cl.worldmodel->textures[mirrortexturenum]->texturechain;

	for ( ; s ; s=s->texturechain)
	{	
		mirror_plane = s->plane;

		d = PlaneDiff(oldvieworg, mirror_plane);  // workaround for rooms with more than one mirror ?
		//d = DotProduct (oldvieworg, mirror_plane->normal) - mirror_plane->dist;

		//VectorCopy(mirror_plane->normal, mirror_far_plane.normal);

		VectorMA (oldvieworg, -2*d, mirror_plane->normal, r_refdef.vieworg);
		
		d = DotProduct (oldvpn, mirror_plane->normal);
		VectorMA (oldvpn, -2*d, mirror_plane->normal, vpn);

		r_refdef.viewangles[0] = -asin (vpn[2])/M_PI*180;
		r_refdef.viewangles[1] = atan2 (vpn[1], vpn[0])/M_PI*180;
		r_refdef.viewangles[2] = -oldviewangles[2];
		
		
		gldepthmin = 0.5;
		gldepthmax = 1;
		glDepthRange (gldepthmin, gldepthmax);
		glDepthFunc (GL_LEQUAL);

		R_RenderScene ();  
		//R_DrawWaterSurfaces(); // now in RenderScene
		
		gldepthmin = 0;
		gldepthmax = 0.5;
		glDepthRange (gldepthmin, gldepthmax);
		glDepthFunc (GL_LEQUAL);
		

		// actual texture mirror draw

		#if 1

		// blend on top
		glEnable (GL_BLEND);
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

		glMatrixMode(GL_PROJECTION);
	
		if (mirror_plane->normal[2])
			glScalef (1,-1,1);

		else
			glScalef (-1,1,1);

		glCullFace(GL_FRONT);
		
		glMatrixMode(GL_MODELVIEW);

		glLoadMatrixf (r_base_world_matrix);
		
		glColor4f (1, 1, 1, mirror_alpha);

		GL_Bind(s->texinfo->texture->gl_texturenum);

		p = s->polys;

		#if 0

		float v = p->verts[0];

		glBegin(GL_POLYGON);

		for(i=0 ; i<p->numverts; i++, v+=VERTEXSIZE)
		{
			glTexCoord2fv(&v[3]);
			glVertex3fv(v);
		}
		
		glEnd();

		#else // Cowcat

		glBegin(GL_POLYGON);
		mglTV23fvalpha(mini_CurrentContext, VERTEXSIZE, p);
		glEnd();

		#endif

		#endif
		
	}

	glDisable (GL_BLEND);
	
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glColor4f (1,1,1,1);

	cl.worldmodel->textures[mirrortexturenum]->texturechain = NULL;

	cl_numvisedicts = cl_oldvisedicts;
	
	r_drawviewmodel.value = 1; // enable weapon again - Cowcat

}

#else

void R_Mirror (void)
{
	float		d;
	msurface_t	*s;
	entity_t	*ent;
	glpoly_t 	*p;
	float		*v, mirror_alpha;
	int		i, cl_oldvisedicts;
	texture_t	*t;

	vec3_t oldvieworg;
	vec3_t oldviewangles;
	vec3_t oldvpn;

	r_drawviewmodel.value = 0; // don�t draw weapon - Cowcat

	//save
	//glGetFloatv (GL_PROJECTION_MATRIX, r_original_projection_matrix);
	//glGetFloatv (GL_MODELVIEW_MATRIX, r_original_modelview_matrix);

	//memcpy (r_base_world_matrix, r_world_matrix, sizeof(r_base_world_matrix));

	memcpy (r_base_projection_matrix, r_projection_matrix, sizeof(r_base_projection_matrix));
	memcpy (r_base_modelview_matrix, r_modelview_matrix, sizeof(r_base_modelview_matrix));

	cl_oldvisedicts = cl_numvisedicts;

	// this is yourself on window - Cowcat
	ent = &cl_entities[cl.viewentity];

	if (cl_numvisedicts < MAX_VISEDICTS)
	{
		cl_visedicts[cl_numvisedicts] = ent;
		cl_numvisedicts++;
	}
	
	VectorCopy(r_refdef.vieworg, oldvieworg);
	VectorCopy(r_refdef.viewangles, oldviewangles);
	VectorCopy(vpn, oldvpn);

	mirror_alpha = r_mirroralpha.value * (1.0 / 2.5);

	s = cl.worldmodel->textures[mirrortexturenum]->texturechain;

	for ( ; s ; s=s->texturechain)
	{	
		mirror_plane = s->plane;

		d = PlaneDiff(oldvieworg, mirror_plane);  // workaround for rooms with more than one mirror ?
		//d = DotProduct (oldvieworg, mirror_plane->normal) - mirror_plane->dist;

		//VectorCopy(mirror_plane->normal, mirror_far_plane.normal);

		VectorMA (oldvieworg, -2*d, mirror_plane->normal, r_refdef.vieworg);
	
		d = DotProduct (oldvpn, mirror_plane->normal);
		VectorMA (oldvpn, -2*d, mirror_plane->normal, vpn);

		r_refdef.viewangles[0] = -asin (vpn[2])/M_PI*180;
		r_refdef.viewangles[1] = atan2 (vpn[1], vpn[0])/M_PI*180;
		r_refdef.viewangles[2] = -oldviewangles[2];
		
		

		// actual texture mirror draw

		#if 1

		glMatrixMode(GL_PROJECTION);
		
		//glLoadMatrixf (r_base_projection_matrix); ////
	
		if (mirror_plane->normal[2])
			glScalef (1,-1,1);

		else
			glScalef (-1,1,1);

		glGetFloatv (GL_PROJECTION_MATRIX, r_base_projection_matrix);
		

		glMatrixMode(GL_MODELVIEW);

		//glLoadMatrixf (r_base_modelview_matrix); ////
		glLoadIdentity();

		glRotatefEXTs(-1.f, 0.f, GLROT_100);
		glRotatefEXTs( 1.f, 0.f, GLROT_001);
		glRotatefEXT (-r_refdef.viewangles[2], GLROT_100);
		glRotatefEXT (-r_refdef.viewangles[0], GLROT_010);
		glRotatefEXT (-r_refdef.viewangles[1], GLROT_001);
		glTranslatef (-r_refdef.vieworg[0], -r_refdef.vieworg[1], -r_refdef.vieworg[2]);

		glGetFloatv (GL_PROJECTION_MATRIX, r_base_modelview_matrix);

		//R_SetFrustum (360, 360); //

		// blend on top
		glEnable (GL_BLEND);
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

		glCullFace(GL_BACK);
		
		gldepthmin = 0.5;
		gldepthmax = 1;
		glDepthRange (gldepthmin, gldepthmax);
		glDepthFunc (GL_LEQUAL);

		R_RenderScene ();  
		
		gldepthmin = 0;
		gldepthmax = 0.5;
		glDepthRange (gldepthmin, gldepthmax);
		glDepthFunc (GL_LEQUAL);
		
		
		//glCullFace(GL_FRONT);

		glColor4f (1, 1, 1, mirror_alpha);

		GL_Bind(s->texinfo->texture->gl_texturenum);
		
		p = s->polys;
		v = p->verts[0];
		
		#if 0
		glBegin(GL_POLYGON);

		for(i=0 ; i<p->numverts; i++, v+=VERTEXSIZE)
		{
			glTexCoord2fv(&v[3]);
			glVertex3fv(v);
		}
		
		glEnd();

		#else // Cowcat

		glBegin(GL_POLYGON);
		mglTV23fvalpha(mini_CurrentContext, VERTEXSIZE, p);
		glEnd();

		#endif

		#endif
		
	}

	glDisable (GL_BLEND);
	
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glColor4f (1,1,1,1);

	cl.worldmodel->textures[mirrortexturenum]->texturechain = NULL;

	cl_numvisedicts = cl_oldvisedicts;
	
	r_drawviewmodel.value = 1; // enable weapon again - Cowcat

	// restore
	//glMatrixMode(GL_PROJECTION);
	//glLoadMatrixf (r_original_projection_matrix);
	//glMatrixMode(GL_MODELVIEW);
	//glLoadMatrixf (r_original_modelview_matrix);
}

#endif