/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// r_main.c

//Surgeon: notes regarding model-flatshading:
//precalc dotproducts are within the range [0.70;1.99]
//I have selected 1.20 as constant flatshade multiplier


#include "quakedef.h"

#include <mgl/mglmacros.h>

cvar_t r_interpolations = {"r_interpolations","0", true}; // now true - Cowcat 2019

//surgeon:
extern qboolean mtexenabled;

//surgeon: some constants 
#define MPIMUL (M_PI*180)
#define MPIDIV (M_PI/360)

extern void R_DrawWaterSurfaces (void);
extern void R_DrawParticles (void);
extern void R_DrawParticles_Classic(void); // Cowcat
extern void R_RenderBrushPoly (msurface_t *fa);
extern void V_CalcBlend (void);
//extern void RotatePointAroundVector( vec3_t dst, const vec3_t dir, const vec3_t point, float degrees );
extern void RotatePointAroundVectorNEW( vec3_t dst, vec3_t dst2, const vec3_t dir, const vec3_t point, float degrees );

extern qboolean SV_RecursiveHullCheck (hull_t *hull, int num, float p1f, float p2f, vec3_t p1, vec3_t p2, trace_t *trace);

extern void Mirror_Scale(); // 
extern void R_Mirror(void); // 

entity_t r_worldentity;

qboolean r_cache_thrash;	// compatability

vec3_t	  modelorg, r_entorigin;
entity_t  *currententity;

int	r_visframecount;	// bumped when going to a new PVS
int	r_framecount;		// used for dlight push checking

mplane_t  frustum[4]; //

int	c_brush_polys, c_alias_polys;

//qboolean  envmap;		// true during envmap command capture - was disabled Cowcat

int	currenttexture = -1;	// to avoid unnecessary texture sets

int	cnttextures[2] = {-1, -1};     // cached

int	particletexture;	// little dot for particles
int	playertextures;		// up to 16 color translated skins

extern	mplane_t  *mirror_plane;	//
extern	qboolean  mirror;

qboolean r_dowarp = true;	//waterwarp switch by SuRgEoN
//qboolean smoothmodels = true; // Cowcat 2019

//
// view origin
//

vec3_t	vup;
vec3_t	vpn;
vec3_t	vright;
vec3_t	r_origin;

float r_world_matrix[16];

//
// screen size info
//

refdef_t  r_refdef;

mleaf_t	  *r_viewleaf, *r_oldviewleaf;

texture_t *r_notexture_mip;

int	d_lightstylevalue[256];		// 8.8 fraction of base light value

void	R_MarkLeaves (void);

cvar_t	r_elim_areasize = {"r_elim_areasize","0.50", true};
cvar_t	r_cullworld = {"r_cullworld","0"};	//Surgeon
cvar_t	r_norefresh = {"r_norefresh","0"};
cvar_t	r_drawentities = {"r_drawentities","1"};
cvar_t	r_drawviewmodel = {"r_drawviewmodel","1"};
cvar_t	r_speeds = {"r_speeds","0"};
cvar_t	r_fullbright = {"r_fullbright","0"};
cvar_t	r_lightmap = {"r_lightmap","0"};
cvar_t	r_shadows = {"r_shadows","0", true}; // true Cowcat
cvar_t	r_wateralpha = {"r_wateralpha","1", true}; // true Cowcat
cvar_t	r_waterwarp = {"r_waterwarp","1", true}; //by surgeon - now true -Cowcat
cvar_t	r_dynamic = {"r_dynamic","1"};
cvar_t	r_novis = {"r_novis","0"};

cvar_t	gl_finish = {"gl_finish","0"};
cvar_t	gl_clear = {"gl_clear","0"};
cvar_t	gl_cull = {"gl_cull","1"};
cvar_t	gl_texsort = {"gl_texsort","1"};
cvar_t	gl_smoothmodels = {"gl_smoothmodels","1"};
cvar_t	gl_affinemodels = {"gl_affinemodels","0"};
cvar_t	gl_polyblend = {"gl_polyblend","1"};
cvar_t	gl_flashblend = {"gl_flashblend","1"};
cvar_t	gl_playermip = {"gl_playermip","0"};
cvar_t	gl_nocolors = {"gl_nocolors","0"};
cvar_t	gl_keeptjunctions = {"gl_keeptjunctions","0"};
cvar_t	gl_reporttjunctions = {"gl_reporttjunctions","0"};
cvar_t	gl_doubleeyes = {"gl_doubleeyes", "1"};

extern void R_Fog_BeginFrame(void);
extern qboolean fog;

extern cvar_t r_showbboxes;
extern void R_ShowBBoxes();

cvar_t	gl_glows = {"gl_glows", "1", true};

cvar_t	gl_classic_particles = {"gl_classic_particles","0", true}; // Cowcat
cvar_t	cl_gunfov = {"cl_gunfov","0", true}; // ROOk
cvar_t	shadowalpha = {"shadowalpha","1.0", true}; // Cowcat
cvar_t	gl_farclip = {"gl_farclip","4096", true}; // 16384 max - Cowcat 2019

extern	cvar_t	gl_ztrick;
extern	qboolean gl_warp;

#ifdef AMIGAOS
cvar_t	r_cull_aggressive = {"r_cull_aggressive","0", true};
cvar_t	r_vertexarrays = {"r_vertexarrays","1", true};
cvar_t	r_model_maxdist = {"r_model_maxdist","2048"};
static	ULONG r_mod_maxdist;	//default 4096 units
#endif

cvar_t	r_stereo = {"r_stereo","0"};
cvar_t	r_stereodepth = {"r_stereodepth","128"};
qboolean stereoview = false;

cvar_t	gl_showpolys = {"gl_showpolys","0"};
cvar_t	gl_wireframe = {"gl_wireframe","0"};
cvar_t	gl_wireonly = {"gl_wireonly","0"};
qboolean wireframe = false;

float	r_shadow_matrix[16]; // Cowcat

float	entalpha;	// johnfitz
float	r_fovx, r_fovy; // johnfitz - rendering fov may be different for r_stereo

//#define ALIGNED16 __attribute__((aligned(16))) // Cowcat

typedef struct qglarray_s
//typedef struct __attribute__((packed, aligned(4))) qglarray_s
{
	float	x,y,z;
	float	s,t,w;		// w is for MiniGL

	#ifdef OPTIMIZE_060
	int	xi,yi,zi;	// MiniGL has fast integer pipeline
	#endif

	ULONG	c;		// rgba packed
	//float	s,t,w;		// w is for MiniGL
	//float	_pad_;

} qglarray_t;

static qglarray_t *global_qglarray = NULL;

void qgl_InitArrays(void)
{
	int i;

	global_qglarray = malloc(sizeof(qglarray_t)*4096);

	#define maxlight 0xffffffff

	for (i=0; i<4096; i++)
	{
		global_qglarray[i].w = 1.0;
		global_qglarray[i].c = maxlight;
	}
}

void qgl_FreeArrays(void)
{
	free(global_qglarray);
}

static void qgl_EnableArray()
{
	#if 0

	glEnableClientState(GL_VERTEX_ARRAY);

	#ifdef OPTIMIZE_060

	if( r_interpolations.value )
	{
		glVertexPointer(3, GL_FLOAT, sizeof(qglarray_t), &global_qglarray->x);
	}

	else
	{
		glVertexPointer(3, GL_INT, sizeof(qglarray_t), &global_qglarray->xi);
	}

	#else

	glVertexPointer(3, GL_FLOAT, sizeof(qglarray_t), &global_qglarray->x);

	#endif 

	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glTexCoordPointer(3, GL_FLOAT, sizeof(qglarray_t), &global_qglarray->s);
	
	if(gl_smoothmodels.value)
	//if(smoothmodels)
	{
		glEnableClientState(GL_COLOR_ARRAY);
		glColorPointer(4, MGL_UBYTE_ARGB, sizeof(qglarray_t), &global_qglarray->c);
	}

	#else // new Cowcat

	glTexCoordPointer(3, GL_FLOAT, sizeof(qglarray_t), &global_qglarray->s); // 3 for w coord - test only with 2 - Cowcat
 	//glTexCoordPointer(2, GL_FLOAT, sizeof(qglarray_t), &global_qglarray->texture);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);

	if(gl_smoothmodels.value)
	{
		glColorPointer(4, MGL_UBYTE_ARGB, sizeof(qglarray_t), &global_qglarray->c);
		glEnableClientState(GL_COLOR_ARRAY);
	}

	#ifdef OPTIMIZE_060

	if( r_interpolations.value )
	{
		glVertexPointer(3, GL_FLOAT, sizeof(qglarray_t), &global_qglarray->x);
	}

	else
	{
		glVertexPointer(3, GL_INT, sizeof(qglarray_t), &global_qglarray->xi);
	}

	#else

	glVertexPointer(3, GL_FLOAT, sizeof(qglarray_t), &global_qglarray->x);
	//glVertexPointer(3, GL_FLOAT, sizeof(qglarray_t), &global_qglarray->vert);

	#endif

	glEnableClientState(GL_VERTEX_ARRAY);

	#endif
}

static void qgl_DisableArray()
{
	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);
}


#ifndef FASTBOPS //made a macro in mathlib.h
/*
=================
R_CullBox

Returns true if the box is completely outside the frustom
=================
*/
qboolean R_CullBox (vec3_t mins, vec3_t maxs)
{
	int   i;

	for (i=0 ; i<4 ; i++)
		if (BoxOnPlaneSide (mins, maxs, &frustum[i]) == 2)
			return true;

	return false;
}
#endif



void R_RotateForEntity (entity_t *e, int shadow)
//void R_RotateForEntity (entity_t *e)
{
	glTranslatef (e->origin[0],  e->origin[1],  e->origin[2]);

#ifdef AMIGAOS
	glRotatefEXT(e->angles[1], GLROT_001);
	
	//if(r_shadows.value && e->model->type == mod_alias)
		//glGetFloatv (GL_MODELVIEW_MATRIX, r_shadow_matrix);

	if(shadow == 0)
	{
		glRotatefEXT(-e->angles[0], GLROT_010);
		//ZOID: fixed z angle
		glRotatefEXT(e->angles[2], GLROT_100);
	}
#else
	glRotatef (e->angles[1],  0, 0, 1);
	glRotatef (-e->angles[0],  0, 1, 0);
	glRotatef (e->angles[2],  1, 0, 0);
#endif

}

static float shadow_angle;

void R_RotateInterpolatedForEntity (entity_t *e, int shadow)
//void R_RotateInterpolatedForEntity (entity_t *e) // modded 2019 - Cowcat
{
	float	blend, timepassed;
	vec3_t	d;
	int	i;

	// positional interpolation

	//timepassed = realtime - e->translate_start_time;
	timepassed = cl.time - e->translate_start_time;
	
	if (e->translate_start_time == 0 || timepassed > 1)
	{
		//e->translate_start_time = realtime;
		e->translate_start_time = cl.time;
		VectorCopy (e->origin, e->origin1);
		VectorCopy (e->origin, e->origin2);
	}

	if (!VectorCompare (e->origin, e->origin2))
	{
		//e->translate_start_time = realtime;
		e->translate_start_time = cl.time;
		VectorCopy (e->origin2, e->origin1);
		VectorCopy (e->origin,	e->origin2);
		blend = 0;
	}

	else
	{
		//blend = timepassed / 0.1;
		//blend = ( timepassed * 10 ) * r_interpolations.value; 
		blend = timepassed * 10;

		if (cl.paused || blend > 1)
			blend = 1;
	}

	VectorSubtract (e->origin2, e->origin1, d);

	glTranslatef ( e->origin1[0] + (blend * d[0]), e->origin1[1] + (blend * d[1]), e->origin1[2] + (blend * d[2]));

	// orientation interpolation (Euler angles, yuck!)

	//timepassed = realtime - e->rotate_start_time;
	timepassed = cl.time - e->rotate_start_time;

	if (e->rotate_start_time == 0 || timepassed > 1)
	{
		//e->rotate_start_time = realtime;
		e->rotate_start_time = cl.time;
		VectorCopy (e->angles, e->angles1);
		VectorCopy (e->angles, e->angles2);
	}

	if (!VectorCompare (e->angles, e->angles2))
	{
		//e->rotate_start_time = realtime;
		e->rotate_start_time = cl.time;
		VectorCopy (e->angles2, e->angles1);
		VectorCopy (e->angles,	e->angles2);
		blend = 0;
	}

	else
	{
		//blend = timepassed / 0.1;
		//blend = ( timepassed * 10 ) * r_interpolations.value;
		blend = timepassed * 10;

		if (cl.paused || blend > 1)
			blend = 1;
	}

	VectorSubtract (e->angles2, e->angles1, d);

	// always interpolate along the shortest path
	for (i = 0; i < 3; i++) 
	{
		if (d[i] > 180)
		{
			d[i] -= 360;
		}

		else if (d[i] < -180)
		{
			d[i] += 360;
		}
	}

	glRotatefEXT ( shadow_angle = e->angles1[1] + ( blend * d[1]), GLROT_001); // shadow_angle - Cowcat 2019

	//if(r_shadows.value && e->model->type == mod_alias)
		//glGetFloatv (GL_MODELVIEW_MATRIX, r_shadow_matrix);

	if( shadow == 0)
	{
		glRotatefEXT (-e->angles1[0] + (-blend * d[0]),	 GLROT_010);
		glRotatefEXT ( e->angles1[2] + ( blend * d[2]),	 GLROT_100);
	}
}


/*
=============================================================

  SPRITE MODELS

=============================================================
*/

/*
================
R_GetSpriteFrame
================
*/

mspriteframe_t *R_GetSpriteFrame (entity_t *currententity)
{
	msprite_t	*psprite;
	mspritegroup_t	*pspritegroup;
	mspriteframe_t	*pspriteframe;
	int		i, numframes, frame;
	float		*pintervals, fullinterval, targettime, time;

	psprite = currententity->model->cache.data;
	frame = currententity->frame;

	if ((frame >= psprite->numframes) || (frame < 0))
	{
		Con_Printf ("R_DrawSprite: no such frame %d\n", frame);
		frame = 0;
	}

	if (psprite->frames[frame].type == SPR_SINGLE)
	{
		pspriteframe = psprite->frames[frame].frameptr;
	}

	else
	{
		pspritegroup = (mspritegroup_t *)psprite->frames[frame].frameptr;
		pintervals = pspritegroup->intervals;
		numframes = pspritegroup->numframes;
		fullinterval = pintervals[numframes-1];

		time = cl.time + currententity->syncbase;

		// when loading in Mod_LoadSpriteGroup, we guaranteed all interval values
		// are positive, so we don't have to worry about division by 0

		targettime = time - ((int)(time / fullinterval)) * fullinterval;

		for (i=0 ; i<(numframes-1) ; i++)
		{
			if (pintervals[i] > targettime)
				break;
		}

		pspriteframe = pspritegroup->frames[i];
	}

	return pspriteframe;
}


/*
=================
R_DrawSpriteModel

=================
*/

void R_DrawSpriteModel (entity_t *e)
{
	vec3_t		point, v_forward, v_right, v_up;
	mspriteframe_t	*frame;
	msprite_t	*psprite;
	float		*up, *right;
	

	// don't even bother culling, because it's just a single
	// polygon without a surface cache

	frame = R_GetSpriteFrame (e);
	psprite = currententity->model->cache.data;

	if (psprite->type == SPR_ORIENTED)
	{
		//Con_Printf("SPR_ORIENTED\n");

		// bullet marks on walls
		AngleVectors (currententity->angles, v_forward, v_right, v_up);
		up = v_up;
		right = v_right;
	}

	else
	{
		//Con_Printf("SPR_normal\n");
		// normal sprite
		up = vup;
		right = vright;
	}

	//if(mtexenabled)
		//GL_DisableMultitexture(); // already disabled

	GL_Bind(frame->gl_texturenum);

	glColor3f(1,1,1);
	glEnable(GL_ALPHA_TEST);
	glBegin (GL_QUADS);

#if 1 //VectorMA inlined
	
	point[0] = e->origin[0] + frame->down*up[0];
	point[1] = e->origin[1] + frame->down*up[1];
	point[2] = e->origin[2] + frame->down*up[2];
	point[0] += frame->left*right[0];
	point[1] += frame->left*right[1];
	point[2] += frame->left*right[2];

	glTexCoord2f (0, 1);
	glVertex3fv (point);

	point[0] = e->origin[0] + frame->up*up[0];
	point[1] = e->origin[1] + frame->up*up[1];
	point[2] = e->origin[2] + frame->up*up[2];
	point[0] += frame->left*right[0];
	point[1] += frame->left*right[1];
	point[2] += frame->left*right[2];

	glTexCoord2f (0, 0);
	glVertex3fv (point);

	point[0] = e->origin[0] + frame->up*up[0];
	point[1] = e->origin[1] + frame->up*up[1];
	point[2] = e->origin[2] + frame->up*up[2];
	point[0] += frame->right*right[0];
	point[1] += frame->right*right[1];
	point[2] += frame->right*right[2];

	glTexCoord2f (1, 0);
	glVertex3fv (point);

	point[0] = e->origin[0] + frame->down*up[0];
	point[1] = e->origin[1] + frame->down*up[1];
	point[2] = e->origin[2] + frame->down*up[2];
	point[0] += frame->right*right[0];
	point[1] += frame->right*right[1];
	point[2] += frame->right*right[2];

	glTexCoord2f (1, 1);
	glVertex3fv (point);

#else

	glTexCoord2f (0, 1);
	VectorMA (e->origin, frame->down, up, point);
	VectorMA (point, frame->left, right, point);
	glVertex3fv (point);

	glTexCoord2f (0, 0);
	VectorMA (e->origin, frame->up, up, point);
	VectorMA (point, frame->left, right, point);
	glVertex3fv (point);

	glTexCoord2f (1, 0);
	VectorMA (e->origin, frame->up, up, point);
	VectorMA (point, frame->right, right, point);
	glVertex3fv (point);

	glTexCoord2f (1, 1);
	VectorMA (e->origin, frame->down, up, point);
	VectorMA (point, frame->right, right, point);
	glVertex3fv (point);

#endif
 
	glEnd ();
	glDisable(GL_ALPHA_TEST);
}


/*
=============================================================

  ALIAS MODELS

=============================================================
*/

/* moved to r_part.c
#define NUMVERTEXNORMALS  162

float r_avertexnormals[NUMVERTEXNORMALS][3] = {
#include "anorms.h"
};
*/

vec3_t	shadevector;

float	shadelight, ambientlight;

// precalculated dot products for quantized angles
#define SHADEDOT_QUANT 16
//surgeon: static
static float r_avertexnormal_dots[SHADEDOT_QUANT][256] =
#include "anorm_dots.h"
;

static float *shadedots = r_avertexnormal_dots[0];

int	lastposenum;
int	lastposenum0; // new Cowcat 2019

#ifdef LITFILES
extern	vec3_t lightcolor;
extern	qboolean eyecandy;
extern	int R_LightPointColor (vec3_t p);
#endif


/*
=============
GL_DrawAliasFrame
=============
*/

void GL_DrawAliasBlendedFrame (aliashdr_t *paliashdr, int pose1, int pose2, float blend) // 2019 - cowcat
{ 
	#ifdef LITFILES
	float		r, g, b, a; // alpha
	#endif

	float		l;
	trivertx_t	*verts1, *verts2;
	int		*order, count;
	vec3_t		d;
	qboolean	lerping;
	float		iblend;

	//qboolean	smoothmodels = true; // Cowcat 2019
	a = entalpha; // johnfitz

	lastposenum0 = pose1;
	lastposenum = pose2;

	verts1 = (trivertx_t *)((byte *)paliashdr + paliashdr->posedata);
	verts2 = verts1;
	verts1 += pose1 * paliashdr->poseverts; 

	//if(pose1 != pose2)
	{
		verts2 += pose2 * paliashdr->poseverts;
		//lerping = true;
		iblend = 1.0f - blend;
		//if(pose1 != pose2)
		//Con_Printf("no lerping!\n");
	}

	//else
	{
		//lerping = false;

		//if(currententity != &cl.viewent)
			//Con_Printf("no lerping!\n");
	}

	order = (int *)((byte *)paliashdr + paliashdr->commands); 

	#ifndef LITFILES

	if(!gl_smoothmodels.value) //Surgeon 
	{
		l = shadelight * 1.20;

		if (l > 255)
			l = 255.f;

		l *= 1.f / 255.f;

		glColor3f(l,l,l);
	}

	#else

	if(!gl_smoothmodels.value) //Surgeon
	//if(!smoothmodels)
	{
		if(eyecandy)
		{
			r = (1.2/255.0)*lightcolor[0]; if(r > 1.0) r = 1.0;
			g = (1.2/255.0)*lightcolor[1]; if(g > 1.0) g = 1.0;
			b = (1.2/255.0)*lightcolor[2]; if(b > 1.0) b = 1.0;

			//glColor3f(r,g,b);
			glColor4f(r, g, b, a);
		}

		else
		{
			l = shadelight * 1.20;

			if (l > 255)
				l = 255.f;

			l *= 1.f / 255.f;

			//glColor3f(l,l,l);
			glColor4f(l, l, l, a);
		}

		//smoothmodels = false; // Cowcat 2019
	}

	#endif

	while (1) 
	{ 
		// get the vertex count and primitive type 
		count = *order++;
 
		if (!count) 
			break;	// done

		if (count <  0) 
		{ 
			count = -count; 
			glBegin (GL_TRIANGLE_FAN); 
		}
 
		else
		{ 
			glBegin (GL_TRIANGLE_STRIP); 
		}

		if(gl_smoothmodels.value)
		//if(smoothmodels) // Cowcat 2019
		{
			/*
			const trivertx_t *lightvert;
			const trivertx_t *verts0;
			verts0 = ((trivertx_t *)((byte *)paliashdr + paliashdr->posedata)) + paliashdr->poseverts * paliashdr->numverts;
	
			lightvert = (blend < 0.5f) ? verts0 : verts1;
			*/

			do
			{
				// texture coordinates come from the draw list 
				glTexCoord2f (((float *)order)[0], ((float *)order)[1]); 
				order += 2;

				#ifndef LITFILES

				l = shadedots[verts->lightnormalindex] * shadelight;

				if(l>255)
					l=255; //clamp

				l *= 1.0 / 255.0;

				glColor3f (l, l, l);

				#else

				if(eyecandy)
				{
					#if 1

					l = shadedots[verts1->lightnormalindex] * (1.0/255.0);

					r = l*lightcolor[0]; if(r > 1.0) r = 1.0;
					g = l*lightcolor[1]; if(g > 1.0) g = 1.0;
					b = l*lightcolor[2]; if(b > 1.0) b = 1.0;

					//glColor3f(r,g,b);
					glColor4f (r, g, b, a);

					#else // test Cowcat - 2019

					//d[0] = shadedots[verts2->lightnormalindex] - shadedots[verts1->lightnormalindex];
					//l = (1.0/255.0) * (shadedots[verts1->lightnormalindex] + (blend * d[0]));
					l = (1.0/255.0) * ( shadedots[verts1->lightnormalindex] * iblend + shadedots[verts2->lightnormalindex] * blend );

					r = l*lightcolor[0]; if(r > 1.0) r = 1.0;
					g = l*lightcolor[1]; if(g > 1.0) g = 1.0;
					b = l*lightcolor[2]; if(b > 1.0) b = 1.0;

					//glColor3f(r,g,b);
					glColor4f (r, g, b, a);

					#endif
				}

				else
				{
					#if 1

					l = shadedots[verts1->lightnormalindex] * shadelight;

					if(l>255)
						l=255; //clamp

					l *= 1.0 / 255.0;

					//glColor3f (l, l, l);
					glColor4f (l, l, l, a);

					#else // test Cowcat 2019
					
					//d[0] = shadedots[verts2->lightnormalindex] - shadedots[verts1->lightnormalindex];
					//l = shadelight * (shadedots[verts1->lightnormalindex] + (blend * d[0]));
					l = shadedots[lightvert->lightnormalindex] * shadelight;

					if(l>255)
						l=255; //clamp

					l *= 1.0 / 255.0;

					//glColor3f (l, l, l);
					glColor4f (l, l, l, a);

					#endif
				}

				#endif

				#if 1

				// normals and vertexes come from the frame list
				/*
				VectorSubtract(verts2->v, verts1->v, d);

				glVertex3f ( verts1->v[0] + (blend * d[0]), 
					verts1->v[1] + (blend * d[1]),	
					verts1->v[2] + (blend * d[2]) );
				*/

				glVertex3f ( verts1->v[0] * iblend + verts2->v[0] * blend, 
					verts1->v[1] * iblend + verts2->v[1] * blend,	
					verts1->v[2] * iblend + verts2->v[2] * blend );

				verts1++; 
				verts2++;
				
				#else

				if(lerping)
				{
					/*
					VectorSubtract(verts2->v, verts1->v, d);

					glVertex3f ( verts1->v[0] + (blend * d[0]), 
						verts1->v[1] + (blend * d[1]),	
						verts1->v[2] + (blend * d[2]) );
					*/

					glVertex3f ( verts1->v[0] * iblend + verts2->v[0] * blend, 
						verts1->v[1] * iblend + verts2->v[1] * blend,	
						verts1->v[2] * iblend + verts2->v[2] * blend );

					verts1++; 
					verts2++;
				}

				else
				{
					glVertex3f ( verts1->v[0], verts1->v[1], verts1->v[2] );
					verts1++;
					
				}

				#endif

			} while (--count);
		}

		else
		{
			do
			{
				// texture coordinates come from the draw list 
				glTexCoord2f (((float *)order)[0], ((float *)order)[1]); 
				order += 2; 

				/*
				VectorSubtract(verts2->v, verts1->v, d);

				glVertex3f ( verts1->v[0] + (blend * d[0]), 
					verts1->v[1] + (blend * d[1]),	
					verts1->v[2] + (blend * d[2]) );
				*/

				#if 1

				glVertex3f ( verts1->v[0] * iblend + verts2->v[0] * blend, 
					verts1->v[1] * iblend + verts2->v[1] * blend,	
					verts1->v[2] * iblend + verts2->v[2] * blend );

				verts1++; 
				verts2++;

				#else

				if(lerping)
				{
					glVertex3f ( verts1->v[0] * iblend + verts2->v[0] * blend, 
					verts1->v[1] * iblend + verts2->v[1] * blend,	
					verts1->v[2] * iblend + verts2->v[2] * blend );

					verts1++;
					verts2++;
				}
				
				else
				{
					glVertex3f ( verts1->v[0], verts1->v[1], verts1->v[2] );
					verts1++;
				}

				#endif
 
			} while (--count); 
		}

		glEnd (); 
	} 
}


void GL_DrawAliasFrame (aliashdr_t *paliashdr, int posenum)
{
	#ifdef LITFILES
	float		r, g, b, a; // alpha
	#endif

	float		l;
	trivertx_t	*verts;
	int		*order, count;

	a = entalpha; // johnfitz

	lastposenum = posenum;

	verts = (trivertx_t *)((byte *)paliashdr + paliashdr->posedata);
	verts += posenum * paliashdr->poseverts;
	order = (int *)((byte *)paliashdr + paliashdr->commands);

	#ifndef LITFILES

	if(!gl_smoothmodels.value) //Surgeon 
	{
		l = shadelight * 1.20;

		if (l > 255)
			l = 255.f;

		l *= 1.f / 255.f;

		glColor3f(l,l,l);
	}

	#else

	if(!gl_smoothmodels.value) //Surgeon
	//if(!smoothmodels)
	{
		if(eyecandy)
		{
			r = (1.2/255.0)*lightcolor[0]; if(r > 1.0) r = 1.0;
			g = (1.2/255.0)*lightcolor[1]; if(g > 1.0) g = 1.0;
			b = (1.2/255.0)*lightcolor[2]; if(b > 1.0) b = 1.0;

			//glColor3f(r, g, b);
			glColor4f(r, g, b, a);
		}

		else
		{
			l = shadelight * 1.20;

			if (l > 255)
				l = 255.f;

			l *= 1.f / 255.f;

			//glColor3f(l, l, l);
			glColor4f(l, l, l, a);
		}
	}

	#endif

	while (1)
	{
		// get the vertex count and primitive type
		count = *order++;

		if (!count)
			break;	  // done

		if (count < 0)
		{
			count = -count;
			glBegin (GL_TRIANGLE_FAN);
		}

		else
		{
			glBegin (GL_TRIANGLE_STRIP);
		}

		// Surgeon: HACK - flatshading implemented - works fine

		if (gl_smoothmodels.value)
		//if (smoothmodels) // Cowcat 2019
		{
			do
			{
				// texture coordinates come from the draw list
				glTexCoord2f (((float *)order)[0], ((float *)order)[1]); 
				order += 2;

				// normals and vertexes come from the frame list

				#ifndef LITFILES

				l = shadedots[verts->lightnormalindex] * shadelight;

				//printf("%f\n", l); //for debugging

				if(l>255)
					l=255; //clamp

				l *= 1.0/255.0;

				glColor3f (l, l, l);

				#else

				if(eyecandy)
				{
					l = shadedots[verts->lightnormalindex]*(1.0/255.0);

					r = l*lightcolor[0]; if(r > 1.0) r = 1.0;
					g = l*lightcolor[1]; if(g > 1.0) g = 1.0;
					b = l*lightcolor[2]; if(b > 1.0) b = 1.0;

					//glColor3f(r,g,b);
					glColor4f (r, g, b, a);
				}

				else
				{
					l = shadedots[verts->lightnormalindex] * shadelight;

					if(l>255)
						l=255; //clamp

					l *= 1.0 / 255.0;

					//glColor3f (l, l, l);
					glColor4f (l, l, l, a);
				}

				#endif

				glVertex3f (verts->v[0], verts->v[1], verts->v[2]);

				verts++;

			} while (--count);
		}

		else //moved glColor outside loop
		{
			do
			{
				// texture coordinates come from the draw list
				glTexCoord2f (((float *)order)[0], ((float *)order)[1]); 
				order += 2;

				glVertex3f (verts->v[0], verts->v[1], verts->v[2]);

				verts++;

			} while (--count);
		}

		glEnd ();
	}
}



#if 0
static GLsizei fcount[256];
static GLsizei scount[256];
static GLint   ffirst[256];
static GLint   sfirst[256];
#endif

#if 0 // Cowcat - not used due to alpha support

#ifndef __PPC__
/*
** Surgeon: used for vertexarrays
** Table of 256 unsigned longs mimmicking the rgba ubytes
*/

const ULONG lighttab[256] = {
#include "whitelight.h"
};
#endif

#endif

void GL_DrawAliasArrayFrame (aliashdr_t *paliashdr, int posenum)
{
	#ifdef LITFILES
	ULONG		r, g, b, a; // alpha
	#endif
	float		alpha;	// johnfitz

	float		l;
	trivertx_t	*verts;
	int		*order;
	int		count;
	int		vcount;
	GLenum		prim;
	qglarray_t	*lva;
	
	#if 0
	int fans, strips;
	fans = 0;
	strips = 0;
	vcount = 0;
	lva = global_qglarray;
	#endif

	alpha = entalpha; // johnfitz

	lastposenum = posenum;

	verts = (trivertx_t *)((byte *)paliashdr + paliashdr->posedata);
	verts += posenum * paliashdr->poseverts;
	order = (int *)((byte *)paliashdr + paliashdr->commands);

	#ifndef LITFILES

	if(!gl_smoothmodels.value) //Surgeon
	{
		l = shadelight * 1.20;

		if (l > 255)
			l = 255.f;

		l *= 1.f / 255.f;

		glColor3f(l,l,l);
	}

	#else

	if(!gl_smoothmodels.value) //Surgeon
	//if(!smoothmodels)
	{
		if(eyecandy)
		{
			float fr, fg, fb;

			fr = (1.2/255.0)*lightcolor[0]; if(fr > 1.0) fr = 1.0;
			fg = (1.2/255.0)*lightcolor[1]; if(fg > 1.0) fg = 1.0;
			fb = (1.2/255.0)*lightcolor[2]; if(fb > 1.0) fb = 1.0;

			//glColor3f(fr, fg, fb);
			glColor4f(fr, fg, fb, alpha);
		}

		else
		{
			l = shadelight * 1.20;

			if (l > 255)
				l = 255.f;

			l *= 1.f / 255.f;

			//glColor3f(l, l, l);
			glColor4f(l, l, l, alpha);
		}
	}

	#endif
	
	if(alpha < 1)
		a = ((GLuint)(alpha*255.f))<<24; // from Q2 Amiga :)

	else
		a = 0xFF000000;

	while (1)
	{
		// get the vertex count and primitive type
		count = *order++;

		if (!count)
			break;	  // done

		#if 1

		if (count < 0)
		{
			count = -count;
			prim = GL_TRIANGLE_FAN;
			//vcount = count;
			//lva = global_qglarray;
		}

		else
		{
			prim = GL_TRIANGLE_STRIP;
			//vcount = count;
			//lva = global_qglarray;
		}

		vcount = count;
		lva = &global_qglarray[0];
		
		#else

		if (count < 0)
		{
			count = -count;
			ffirst[fans] = vcount;
			fcount[fans] = count;
			vcount += count;
			fans++;
		}

		else
		{
			sfirst[strips] = vcount;
			scount[strips] = count;
			strips++;
			vcount += count;
		}

		#endif

		// Surgeon: HACK - flatshading implemented - works fine

		if (gl_smoothmodels.value)
		//if(smoothmodels) // Cowcat 2019
		{
			ULONG c;
			ULONG argb = a; // added

			do
			{
				// texture coordinates come from the draw list
				lva->s = ((float *)order)[0];
				lva->t = ((float *)order)[1];

				order += 2;

				// normals and vertexes come from the frame list

				#ifndef LITFILES

				c = (ULONG)(shadedots[verts->lightnormalindex] * shadelight);

				if(c>255)
					c=255; //clamp

				#ifndef __PPC__
				lva->c = lighttab[c];
				#else
				lva->c = 0xFF000000 | (c<<16) | (c<<8) | c;
				#endif

				#else

				if(eyecandy)
				{
					l = shadedots[verts->lightnormalindex];

					r = (ULONG)(lightcolor[0]*l);
					g = (ULONG)(lightcolor[1]*l);
					b = (ULONG)(lightcolor[2]*l);

					if(r > 255) r = 255;
					if(g > 255) g = 255;
					if(b > 255) b = 255;

					//lva->c = (0xFF000000 | (r<<16) | (g<<8) | b);
					lva->c = (argb | (r<<16) | (g<<8) | b);
				}

				else
				{
					c = (ULONG)(shadedots[verts->lightnormalindex] * shadelight);

					if(c>255)
						c=255; //clamp

					//#ifndef __PPC__
					//lva->c = lighttab[c]; // solution for m68k ???
	
					//#else
					//lva->c = 0xFF000000 | (c<<16) | (c<<8) | c;
					lva->c = argb | (c<<16) | (c<<8) | c;
					
					//#endif
				}

				#endif

				#ifdef OPTIMIZE_060
				lva->xi = verts->v[0];
				lva->yi = verts->v[1];
				lva->zi = verts->v[2];
				#else
				lva->x = (float)verts->v[0];
				lva->y = (float)verts->v[1];
				lva->z = (float)verts->v[2];
				#endif

				lva++;
				verts++;

			} while (--count);
		}

		else //glColor moved outside loop
		{
			do
			{
				// texture coordinates come from the draw list
				lva->s = ((float *)order)[0];
				lva->t = ((float *)order)[1];

				order += 2;

				#ifdef OPTIMIZE_060
				lva->xi = verts->v[0];
				lva->yi = verts->v[1];
				lva->zi = verts->v[2];
				#else
				lva->x = (float)verts->v[0];
				lva->y = (float)verts->v[1];
				lva->z = (float)verts->v[2];
				#endif

				lva++;
				verts++;

			} while (--count);
		}

		#if 0
		if(r_vertexarrays.value == 2)
		{
			UWORD index[256];

			for (count=0; count<vcount; count++)
				index[count] = count;
  
			glDrawElements(prim, vcount, GL_UNSIGNED_SHORT, (void *)index);
		}

		else
		#endif

		glDrawArrays(prim, 0, vcount);

	}

	#if 0
	if(fans)
	{
		glMultiDrawArrays(GL_TRIANGLE_FAN, ffirst, fcount, fans);
	}

	if(strips)
	{
		glMultiDrawArrays(GL_TRIANGLE_STRIP, sfirst, scount, strips);
	}
	#endif
}

void GL_DrawAliasArrayBlendedFrame (aliashdr_t *paliashdr, int pose1, int pose2, float blend) // modded 2019 - Cowcat
{
	#ifdef LITFILES
	ULONG		r, g, b, a; // added a
	#endif
	float		alpha;	// johnfitz

	GLenum		primitive;
	ULONG		vcount;
	qglarray_t	*lva;
	float		l;
	trivertx_t	*verts1, *verts2; 
	int		*order, count;
	vec3_t		d;

	alpha = entalpha;	// johnfitz

	lastposenum0 = pose1;
	lastposenum = pose2;

	verts1 = (trivertx_t *)((byte *)paliashdr + paliashdr->posedata);
	verts2 = verts1;

	verts1 += pose1 * paliashdr->poseverts; 
	verts2 += pose2 * paliashdr->poseverts; 

	order = (int *)((byte *)paliashdr + paliashdr->commands); 

	#ifndef LITFILES

	if(!gl_smoothmodels.value) //Surgeon 
	{
		l = shadelight * 1.20;

		if (l > 255)
			l = 255.f;

		l *= 1.f / 255.f;

		glColor3f(l,l,l);
	}

	#else

	if(!gl_smoothmodels.value) //Surgeon
	//if(!smoothmodels)
	{
		if(eyecandy)
		{
			float fr, fg, fb;

			fr = (1.2/255.0)*lightcolor[0]; if(fr > 1.0) fr = 1.0;
			fg = (1.2/255.0)*lightcolor[1]; if(fg > 1.0) fg = 1.0;
			fb = (1.2/255.0)*lightcolor[2]; if(fb > 1.0) fb = 1.0;

			//glColor3f(fr, fg, fb);
			glColor4f (fr, fg, fb, alpha);
		}

		else
		{
			l = shadelight * 1.20;

			if (l > 255)
				l = 255.f;

			l *= 1.f / 255.f;

			//glColor3f(l, l, l);
			glColor4f (l, l, l, alpha);
		}
	}

	#endif

	if(alpha < 1)
		a = ((GLuint)(alpha*255.f))<<24; // from Q2 Amiga :)

	else
		a = 0xFF000000;

	while (1)
	{
		// get the vertex count and primitive type
		count = *order++;

		if (!count)
			break;	  // done

		if (count < 0)
		{
			count = -count;
			primitive = GL_TRIANGLE_FAN;
		}

		else
		{
			primitive = GL_TRIANGLE_STRIP;
		}

		lva = &global_qglarray[0];
		vcount = count;

		// Surgeon: HACK - flatshading implemented - works fine

		if (gl_smoothmodels.value)
		//if (smoothmodels) // Cowcat 2019
		{
			ULONG c;
			ULONG argb = a;

			do
			{
				// texture coordinates come from the draw list
				lva->s = ((float *)order)[0];
				lva->t = ((float *)order)[1];

				order += 2;

				// normals and vertexes come from the frame list

				#ifndef LITFILES

				c = (ULONG)(shadedots[verts1->lightnormalindex] * shadelight);

				if(c>255)
					c=255; //clamp

				#ifndef __PPC__
				lva->c = lighttab[c];
				#else
				lva->c = 0xFF000000 | (c<<16) | (c<<8) | c;
				#endif

				#else

				if(eyecandy)
				{
					l = shadedots[verts1->lightnormalindex];

					r = (ULONG)(lightcolor[0]*l);
					g = (ULONG)(lightcolor[1]*l);
					b = (ULONG)(lightcolor[2]*l);
 
					if(r > 255) r = 255;
					if(g > 255) g = 255;
					if(b > 255) b = 255;

					//lva->c = (0xFF000000 | (r<<16) | (g<<8) | b);
					lva->c = argb | (r<<16) | (g<<8) | b; // 
				}

				else
				{
					c = (ULONG)(shadedots[verts1->lightnormalindex] * shadelight);
					//d[0] = shadedots[verts2->lightnormalindex] - shadedots[verts1->lightnormalindex];
					//c = (ULONG) shadelight * (shadedots[verts1->lightnormalindex] + (blend * d[0]));

					if(c>255)
						c=255; //clamp

					//#ifndef __PPC__
					//lva->c = lighttab[c];
					//#else
					//lva->c = 0xFF000000 | (c<<16) | (c<<8) | c;
					lva->c = argb | (c<<16) | (c<<8) | c; // 
					//#endif
				}

				#endif

				VectorSubtract(verts2->v, verts1->v, d);

				lva->x = verts1->v[0] + (blend * d[0]);
				lva->y = verts1->v[1] + (blend * d[1]);
				lva->z = verts1->v[2] + (blend * d[2]);
		
				lva++;
				verts1++;
				verts2++;

			} while (--count);
		}

		else // glColor moved outside loop
		{
			do
			{
				// texture coordinates come from the draw list
				lva->s = ((float *)order)[0];
				lva->t = ((float *)order)[1];

				order += 2;

				VectorSubtract(verts2->v, verts1->v, d);

				lva->x = verts1->v[0] + (blend * d[0]);
				lva->y = verts1->v[1] + (blend * d[1]);
				lva->z = verts1->v[2] + (blend * d[2]);
		
				lva++;
				verts1++;
				verts2++;

			} while (--count);
		}

		glDrawArrays(primitive, 0, vcount);
	}
}


/*
=============
GL_DrawAliasBatch

2026-09-14. Draws pose1, or pose1 blended toward pose2 when lerp is set, through
the model's triangle list (gl_mesh.c GL_BuildAliasBatch). Each chunk's vertices
go into global_qglarray, where qgl_EnableArray's pointers already look, and are
drawn with one glDrawElements(GL_TRIANGLES) inside glLockArrays. Positions,
colours and texture coordinates are the ones GL_DrawAliasArrayFrame and
GL_DrawAliasArrayBlendedFrame compute, once per distinct vertex instead of once
per strip vertex.
=============
*/
static void GL_DrawAliasBatch (aliashdr_t *paliashdr, int pose1, int pose2, float blend, qboolean lerp)
{
	ULONG			a;
	float			alpha;	// johnfitz
	float			l;
	trivertx_t		*verts1, *verts2;
	aliasbatchchunk_t	*chunk;
	short			*bverts;
	float			*bst;
	unsigned short		*bindexes;
	int			c, i;

	alpha = entalpha;	// johnfitz

	if (lerp)
	{
		lastposenum0 = pose1;
		lastposenum = pose2;
	}

	else
		lastposenum = pose1;

	verts1 = (trivertx_t *)((byte *)paliashdr + paliashdr->posedata);
	verts2 = verts1 + pose2 * paliashdr->poseverts;
	verts1 += pose1 * paliashdr->poseverts;

	chunk = (aliasbatchchunk_t *)((byte *)paliashdr + paliashdr->batchchunks);
	bverts = (short *)((byte *)paliashdr + paliashdr->batchverts);
	bst = (float *)((byte *)paliashdr + paliashdr->batchst);
	bindexes = (unsigned short *)((byte *)paliashdr + paliashdr->batchindexes);

	#ifndef LITFILES

	if(!gl_smoothmodels.value) //Surgeon
	{
		l = shadelight * 1.20;

		if (l > 255)
			l = 255.f;

		l *= 1.f / 255.f;

		glColor3f(l,l,l);
	}

	#else

	if(!gl_smoothmodels.value) //Surgeon
	{
		if(eyecandy)
		{
			float fr, fg, fb;

			fr = (1.2/255.0)*lightcolor[0]; if(fr > 1.0) fr = 1.0;
			fg = (1.2/255.0)*lightcolor[1]; if(fg > 1.0) fg = 1.0;
			fb = (1.2/255.0)*lightcolor[2]; if(fb > 1.0) fb = 1.0;

			glColor4f(fr, fg, fb, alpha);
		}

		else
		{
			l = shadelight * 1.20;

			if (l > 255)
				l = 255.f;

			l *= 1.f / 255.f;

			glColor4f(l, l, l, alpha);
		}
	}

	#endif

	if(alpha < 1)
		a = ((GLuint)(alpha*255.f))<<24; // from Q2 Amiga :)

	else
		a = 0xFF000000;

	for (c=0 ; c<paliashdr->numbatchchunks ; c++, chunk++)
	{
		short		*bv = bverts + chunk->firstvert;
		float		*st = bst + chunk->firstvert * 2;
		qglarray_t	*lva = &global_qglarray[0];

		if (lerp)
		{
			for (i=0 ; i<chunk->numverts ; i++, lva++, st += 2)
			{
				trivertx_t	*v1 = verts1 + bv[i];
				trivertx_t	*v2 = verts2 + bv[i];
				vec3_t		d;

				lva->s = st[0];
				lva->t = st[1];

				VectorSubtract(v2->v, v1->v, d);

				lva->x = v1->v[0] + (blend * d[0]);
				lva->y = v1->v[1] + (blend * d[1]);
				lva->z = v1->v[2] + (blend * d[2]);
			}
		}

		else
		{
			for (i=0 ; i<chunk->numverts ; i++, lva++, st += 2)
			{
				trivertx_t	*v1 = verts1 + bv[i];

				lva->s = st[0];
				lva->t = st[1];

				lva->x = (float)v1->v[0];
				lva->y = (float)v1->v[1];
				lva->z = (float)v1->v[2];
			}
		}

		// lit from pose1's normals, as both frame functions do
		if (gl_smoothmodels.value)
		{
			lva = &global_qglarray[0];

			#ifdef LITFILES
			if(eyecandy)
			{
				for (i=0 ; i<chunk->numverts ; i++, lva++)
				{
					ULONG	r, g, b;

					l = shadedots[verts1[bv[i]].lightnormalindex];

					r = (ULONG)(lightcolor[0]*l);
					g = (ULONG)(lightcolor[1]*l);
					b = (ULONG)(lightcolor[2]*l);

					if(r > 255) r = 255;
					if(g > 255) g = 255;
					if(b > 255) b = 255;

					lva->c = a | (r<<16) | (g<<8) | b;
				}
			}

			else
			#endif
			{
				for (i=0 ; i<chunk->numverts ; i++, lva++)
				{
					ULONG	lit = (ULONG)(shadedots[verts1[bv[i]].lightnormalindex] * shadelight);

					if(lit > 255)
						lit = 255; //clamp

					lva->c = a | (lit<<16) | (lit<<8) | lit;
				}
			}
		}

		glLockArrays(0, chunk->numverts);
		glDrawElements(GL_TRIANGLES, chunk->numindexes, GL_UNSIGNED_SHORT, bindexes + chunk->firstindex);
		glUnlockArrays();
	}
}


/*
=============
GL_DrawAliasShadow
=============
*/
extern	vec3_t	lightspot;

void GL_DrawAliasBlendedShadow (aliashdr_t *paliashdr, int pose1, int pose2, entity_t *e, trace_t downtrace)
{
	int		*order, count;
	trivertx_t	*verts1, *verts2;
	vec3_t		point1;
	vec3_t		point2;
	vec3_t		d;	
	float		lheight;
	float		s1, c1, an;
	float		blend, iblend;
	//qboolean	lerping;

	float shadescale = 0.707106781187; // 1 / sqrt(2)

	blend = (cl.time - e->frame_start_time) / e->frame_interval;

	if (blend > 1 )
		blend = 1;

	lheight = e->origin[2] - lightspot[2];

	verts1 = (trivertx_t *)((byte *)paliashdr + paliashdr->posedata);
	verts2 = verts1;
	verts1 += pose1 * paliashdr->poseverts;

	//if(pose1 != pose2)
	{
		verts2 += pose2 * paliashdr->poseverts;
		iblend = 1.0f - blend;

		//lerping = true;
		//Con_Printf("no lerping!\n");
	}

	//else
	{
		//lerping = false;
		//Con_Printf("no lerping!\n");
	}

	//an = e->angles[1] / 180 * M_PI;
	an = shadow_angle / 180 * M_PI; // Cowcat
	c1 = cos(an);
	s1 = sin(an);

	shadevector[0] = c1 * shadescale;
	shadevector[1] = -s1 * shadescale;
	shadevector[2] = shadescale;
	
	order = (int *)((byte *)paliashdr + paliashdr->commands);	
	
	while (1)
	{
		// get the vertex count and primitive type
		count = *order++;

		if (!count)
			break;	  // done

		if (count < 0)
		{
			count = -count;
			glBegin (GL_TRIANGLE_FAN);
		}

		else
		{
			glBegin (GL_TRIANGLE_STRIP);
		}

		do
		{	
			order += 2;

		//if(lerping)
		{
			// normals and vertexes come from the frame list
			point1[0] = verts1->v[0] * paliashdr->scale[0] + paliashdr->scale_origin[0];
			point1[1] = verts1->v[1] * paliashdr->scale[1] + paliashdr->scale_origin[1];
			point1[2] = verts1->v[2] * paliashdr->scale[2] + paliashdr->scale_origin[2];

			point1[0] -= shadevector[0] * (point1[2] + lheight);
			point1[1] -= shadevector[1] * (point1[2] + lheight);

			point2[0] = verts2->v[0] * paliashdr->scale[0] + paliashdr->scale_origin[0];
			point2[1] = verts2->v[1] * paliashdr->scale[1] + paliashdr->scale_origin[1];
			point2[2] = verts2->v[2] * paliashdr->scale[2] + paliashdr->scale_origin[2];

			point2[0] -= shadevector[0] * (point2[2] + lheight);
			point2[1] -= shadevector[1] * (point2[2] + lheight);

			#if 1

			/*
			VectorSubtract(point2, point1, d);

			point1[0] = point1[0] + (blend * d[0]);
			point1[1] = point1[1] + (blend * d[1]);
			point1[2] = point1[2] + (blend * d[2]);
			*/
			
			point1[0] = point1[0] * iblend + point2[0] * blend;
			point1[1] = point1[1] * iblend + point2[1] * blend;
			point1[2] = point1[2] * iblend + point2[2] * blend;
			
			point1[2] = -(e->origin[2] - downtrace.endpos[2]);
		
			point1[2] += ((point1[1] * (s1 * downtrace.plane.normal[0])) -
				(point1[0] * (c1 * downtrace.plane.normal[0])) -
				(point1[0] * (s1 * downtrace.plane.normal[1])) -
				(point1[1] * (c1 * downtrace.plane.normal[1]))) +
				((1.0 - downtrace.plane.normal[2]) * 20) + 0.2 ;

			glVertex3fv (point1);

			#else // Cowcat

			d[0] = point1[0] + blend * (point2[0] - point1[0]);
			d[1] = point1[1] + blend * (point2[1] - point1[1]);
			d[2] = point1[2] + blend * (point2[2] - point1[2]);

			d[2] = -(e->origin[2] - downtrace.endpos[2]);
		
			d[2] += ((d[1] * (s1 * downtrace.plane.normal[0])) -
				(d[0] * (c1 * downtrace.plane.normal[0])) -
				(d[0] * (s1 * downtrace.plane.normal[1])) -
				(d[1] * (c1 * downtrace.plane.normal[1]))) +
				((1.0 - downtrace.plane.normal[2]) * 20) + 0.2 ;

			glVertex3fv (d);

			#endif

			verts1++;
			verts2++;
		}

		#if 0
		else
		{
			// normals and vertexes come from the frame list
			point1[0] = verts1->v[0] * paliashdr->scale[0] + paliashdr->scale_origin[0];
			point1[1] = verts1->v[1] * paliashdr->scale[1] + paliashdr->scale_origin[1];
			point1[2] = verts1->v[2] * paliashdr->scale[2] + paliashdr->scale_origin[2];

			point1[0] -= shadevector[0] * (point1[2] + lheight);
			point1[1] -= shadevector[1] * (point1[2] + lheight);

			point1[2] = -(e->origin[2] - downtrace.endpos[2]);
		
			point1[2] += ((point1[1] * (s1 * downtrace.plane.normal[0])) -
				(point1[0] * (c1 * downtrace.plane.normal[0])) -
				(point1[0] * (s1 * downtrace.plane.normal[1])) -
				(point1[1] * (c1 * downtrace.plane.normal[1]))) +
				((1.0 - downtrace.plane.normal[2]) * 20) + 0.2 ;

			glVertex3fv (point1);

			verts1++;
		}
		#endif

		} while (--count);

		glEnd ();
	}
}

void GL_DrawAliasShadow (aliashdr_t *paliashdr, int posenum, entity_t *e, trace_t downtrace)
{
	int		*order;
	trivertx_t	*verts;
	vec3_t		point;		
	float		lheight;
	int		count;	
	float		s1, c1, an;

	float shadescale = 0.707106781187; // 1 / sqrt(2)

	lheight = e->origin[2] - lightspot[2];

	verts = (trivertx_t *)((byte *)paliashdr + paliashdr->posedata);
	verts += posenum * paliashdr->poseverts;

	order = (int *)((byte *)paliashdr + paliashdr->commands);	
	
	an = e->angles[1] / 180 * M_PI;
	c1 = cos(an);
	s1 = sin(an);

	shadevector[0] = c1 * shadescale;
	shadevector[1] = -s1 * shadescale;
	shadevector[2] = shadescale;

	//if (!r_vertexarrays.value)
	{
		while (1)
		{
			// get the vertex count and primitive type
			count = *order++;

			if (!count)
				break;	  // done

			if (count < 0)
			{
				count = -count;
				glBegin (GL_TRIANGLE_FAN);
			}

			else
			{
				glBegin (GL_TRIANGLE_STRIP);
			}

			do
			{	
				order += 2;
	
				// normals and vertexes come from the frame list
				point[0] = verts->v[0] * paliashdr->scale[0] + paliashdr->scale_origin[0];
				point[1] = verts->v[1] * paliashdr->scale[1] + paliashdr->scale_origin[1];
				point[2] = verts->v[2] * paliashdr->scale[2] + paliashdr->scale_origin[2];
	
				point[0] -= shadevector[0] * (point[2] + lheight);
				point[1] -= shadevector[1] * (point[2] + lheight);

				point[2] = - (e->origin[2] - downtrace.endpos[2]);

				point[2] += ((point[1] * (s1 * downtrace.plane.normal[0])) -
					     (point[0] * (c1 * downtrace.plane.normal[0])) -
					     (point[0] * (s1 * downtrace.plane.normal[1])) -
					     (point[1] * (c1 * downtrace.plane.normal[1]))) +
					     ((1.0 - downtrace.plane.normal[2]) * 20) + 0.2 ;

				glVertex3fv (point);

				verts++;

			} while (--count);

			glEnd ();
		}
	}

	#if 0 // didn�t work ok with vertexarrays (clipping issues & not fading in fog) - Cowcat 2019 - (maybe missing disable texcoords?)
	else
	{
		GLenum	primitive;
		ULONG	vcount;
		qglarray_t *lva;
		
		while (1)
		{
			count = *order++;

			if (!count)
				break;	  // done

			if (count < 0)
			{
				count = -count;
				primitive = GL_TRIANGLE_FAN;
				
			}

			else
			{
				primitive = GL_TRIANGLE_STRIP;
				
			}

			vcount = count;
			lva = &global_qglarray[0];
			//ULONG a,r,g,b;

			do
			{
				order += 2;
				
				/*
				//c = (ULONG)(shadedots[verts->lightnormalindex] * shadelight);
				r = 155;
				g = 200;
				b = 20;
				a = ((GLuint)(shadowalpha.value*255.f))<<24;

				lva->c = a | (r<<16) | (g<<8) | b;
				*/

				#ifdef OPTIMIZE_060
				
				lva->xi = verts->v[0] * paliashdr->scale[0] + paliashdr->scale_origin[0];
				lva->yi = verts->v[1] * paliashdr->scale[1] + paliashdr->scale_origin[1];
				lva->zi = verts->v[2] * paliashdr->scale[2] + paliashdr->scale_origin[2];

				lva->xi -= shadevector[0] * (lva->zi + lheight);
				lva->yi -= shadevector[1] * (lva->zi + lheight);

				lva->zi = - (e->origin[2] - downtrace.endpos[2]);

				lva->zi += ((lva->yi * (s1 * downtrace.plane.normal[0])) -
					    (lva->xi * (c1 * downtrace.plane.normal[0])) -
					    (lva->xi * (s1 * downtrace.plane.normal[1])) -
					    (lva->yi * (c1 * downtrace.plane.normal[1]))) +
					    ((1.0 - downtrace.plane.normal[2]) * 20) + 0.2 ;
				#else

				lva->x = verts->v[0] * paliashdr->scale[0] + paliashdr->scale_origin[0];
				lva->y = verts->v[1] * paliashdr->scale[1] + paliashdr->scale_origin[1];
				lva->z = verts->v[2] * paliashdr->scale[2] + paliashdr->scale_origin[2];

				lva->x -= shadevector[0] * (lva->z + lheight);
				lva->y -= shadevector[1] * (lva->z + lheight);

				lva->z = - (e->origin[2] - downtrace.endpos[2]);

				lva->z += ((lva->y * (s1 * downtrace.plane.normal[0])) -
					   (lva->x * (c1 * downtrace.plane.normal[0])) -
					   (lva->x * (s1 * downtrace.plane.normal[1])) -
					   (lva->y * (c1 * downtrace.plane.normal[1]))) +
					   ((1.0 - downtrace.plane.normal[2]) * 20) + 0.2 ;

				#endif

				lva++;
				verts++;

			} while (--count);

			glDrawArrays(primitive, 0, vcount);
		}
	}

	#endif
}

/*
=================
R_SetupAliasFrame

=================
*/

void R_SetupAliasInterpolatedFrame (int frame, aliashdr_t *paliashdr, entity_t *e)  // Tomaz - new Cowcat 2019
{ 
	int	pose, numposes; 
	float	blend;

	if ((frame >= paliashdr->numframes) || (frame < 0))
	{ 
		Con_DPrintf ("R_AliasSetupFrame: no such frame %d\n", frame); 
		frame = 0; 
	} 

	pose = paliashdr->frames[frame].firstpose; 
	numposes = paliashdr->frames[frame].numposes; 

	if (numposes > 1) 
	{ 
		e->frame_interval = paliashdr->frames[frame].interval; 
		pose += (int)(cl.time / e->frame_interval) % numposes; 
	} 

	else
		e->frame_interval = 0.1;

	if (e->pose2 != pose) 
	{ 
		//e->frame_start_time = realtime;
		e->frame_start_time = cl.time;
		e->pose1 = e->pose2;
		e->pose2 = pose;
		blend = 0;
	}

	else
	{
		//blend = ((realtime - e->frame_start_time) * r_interpolations.value ) / e->frame_interval;
		//blend = (realtime - e->frame_start_time) / e->frame_interval;
		blend = (cl.time - e->frame_start_time) / e->frame_interval;

		if(cl.paused || blend > 1)
			blend =	 1;
	}

	if (paliashdr->numbatchchunks)
	{
		if( blend == 0 )
			GL_DrawAliasBatch (paliashdr, e->pose1, e->pose1, 0, false);

		else
			GL_DrawAliasBatch (paliashdr, e->pose1, e->pose2, blend, true);
	}

	else if (!r_vertexarrays.value)
	{
		if( blend == 0 )
			GL_DrawAliasFrame (paliashdr, e->pose1);

		else
			GL_DrawAliasBlendedFrame (paliashdr, e->pose1, e->pose2, blend );

	}

	else
	{
		if( blend == 0 )
			GL_DrawAliasArrayFrame (paliashdr, e->pose1 ); // new test - Cowcat

		else
			GL_DrawAliasArrayBlendedFrame (paliashdr, e->pose1, e->pose2, blend );
	}
} 

void R_SetupAliasFrame (int frame, aliashdr_t *paliashdr)
{
	int	  pose, numposes;
	float	  interval;

	if ((frame >= paliashdr->numframes) || (frame < 0))
	{
		Con_DPrintf ("R_AliasSetupFrame: no such frame %d\n", frame);
		frame = 0;
	}

	pose = paliashdr->frames[frame].firstpose;
	numposes = paliashdr->frames[frame].numposes;

	if (numposes > 1)
	{
		interval = paliashdr->frames[frame].interval;
		pose += (int)(cl.time / interval) % numposes;
	}

	if (paliashdr->numbatchchunks)
	{
		GL_DrawAliasBatch (paliashdr, pose, pose, 0, false);
	}

	else if ( !r_vertexarrays.value )
	{
		GL_DrawAliasFrame (paliashdr, pose);
	}

	else
	{
	#if 0
		if (r_vertexarrays.value > 2)
			GL_DrawAliasTrisFrame (paliashdr, pose);
		else
	#endif
		GL_DrawAliasArrayFrame (paliashdr, pose);
	
	}
}



/*
=================
R_DrawAliasModel

=================
*/

#ifdef GLOWEFFECTS
extern void R_AddGlow (entity_t *e); //gl_rlight.c
extern void R_RenderGlows (void);
#endif

#define DEG2RAD( a ) ( a * M_PI ) / 180.0F

#define MOD_NOSHADOW	256
#define MOD_GLOWS	512
#define MOD_FULLBRIGHT	1024

#if 0 // Cowcat

#define PlaneDiff(point, plane) (((plane)->type < 3) ? (point)[(plane)->type] - (plane)->dist : DotProduct((point), (plane)->normal) - (plane)->dist)

qboolean R_CullSphere (vec3_t centre, float radius)
{
	int i;
	mplane_t *p;

	for(i=0, p = frustum; i < 4; i++, p++)
		if(PlaneDiff(centre, p) <= -radius)
			return true;

	return false;
}

#endif

#define GL_BindCheck(texnum)	\
	if (currenttexture != (texnum)) \
	{				\
		currenttexture = (texnum);	\
		glBindTexture(GL_TEXTURE_2D, currenttexture);	\
	}	\

#if 0
void GL_BindTest(int texnum)
{
	if(currenttexture == texnum)
	{
		Con_Printf("Same texture %d!\n", texnum);
		return;
	}

	currenttexture = texnum;
	glBindTexture(GL_TEXTURE_2D, currenttexture);
	//Con_Printf("texnum %d\n", texnum);
}
#endif

extern float chase_alpha;
//#define ENTALPHA_DECODE2(a) (((a)==ENTALPHA_DEFAULT)?1.0f:((float)(a)-1)*(float)0.003937007874)

void R_DrawAliasModel (entity_t *e) // modded - Cowcat
{
	#ifdef LITFILES
	extern vec3_t	lightcolor;
	#endif

	int		i, lnum, anim;
	vec3_t		dist;
	float		add;
	model_t		*clmodel;
	vec3_t		mins, maxs;
	aliashdr_t	*paliashdr;

	float		clipdist, scale;
	vec3_t		cvec, mod_orig;
	float		distfromplane, reference;
	float		d_forward, d_right, d_up;
	float		viewfield, vd_ratio;

	extern	cvar_t scr_fov;

	clmodel = e->model;

	VectorAdd (e->origin, clmodel->mins, mins);
	VectorAdd (e->origin, clmodel->maxs, maxs);
	
	#ifdef FASTBOPS
	//Surgeon: bugfix
	if (e->model->flags & EF_ROTATE)
	{
		mins[2] += 16;
		maxs[2] += 16;
	}
	#endif
	
	#if 0 // Cowcat 2019 - slow here, test it when full boundbox code is done

	if(e != &cl.viewent)
	{
		if(e->angles[0] || e->angles[1] || e->angles[2])
		{
			if(R_CullSphere(e->origin, clmodel->radius))
				return;
		}
	}	

	else
	#endif

	if (R_CullBox (mins, maxs))
		return;

	//Surgeon: far culling
	cvec[0] = r_refdef.vieworg[0] - e->origin[0];
	cvec[1] = r_refdef.vieworg[1] - e->origin[1];

	if (e->model->flags & EF_ROTATE)
		cvec[2] = r_refdef.vieworg[2] - e->origin[2] + 32;

	else
		cvec[2] = r_refdef.vieworg[2] - e->origin[2];

	clipdist = (cvec[0]*cvec[0] + cvec[1]*cvec[1] + cvec[2]*cvec[2]);

	if (clipdist > r_mod_maxdist)
		return;

	//optional aggressive frustum culling

	if(r_cull_aggressive.value && (clipdist > 256))
	{
		mod_orig[0] = e->origin[0];
		mod_orig[1] = e->origin[1];
		
		if (e->model->flags & EF_ROTATE)
		{
			mod_orig[2] = e->origin[2] + 32;
		}

		else
		{
			mod_orig[2] = e->origin[2];
		}

		distfromplane = DotProduct(mod_orig, vpn);
		reference = DotProduct(r_origin, vpn); 
		d_forward = distfromplane - reference;

		if(d_forward < 0.0)
			return;

		else
		{
			viewfield = scr_fov.value;

			if(viewfield > 90.0)
				vd_ratio = viewfield / (90.0 - (viewfield - 90.0));

			else
				vd_ratio = viewfield * (1.0/90.0);

			distfromplane = DotProduct(mod_orig, vright);
			reference = DotProduct(r_origin, vright); 

			d_right = fabs(distfromplane - reference);	

			d_right /= d_forward;

			if(d_right > vd_ratio)
				return;

			distfromplane = DotProduct(mod_orig, vup);
			reference = DotProduct(r_origin, vup); 

			d_up = fabs(distfromplane - reference); 

			d_up /= d_forward;

			if(d_up > vd_ratio * 0.75)
				return;
		}
	}

	VectorCopy (e->origin, r_entorigin);
	VectorSubtract (r_origin, r_entorigin, modelorg);

	//if (!chase_active.value)
		//entalpha = ENTALPHA_DECODE(e->alpha); // johnfitz
	
	if( currententity == &cl_entities[cl.viewentity] ) // alpha workaround for chasecam - Cowcat 2019
		entalpha = chase_alpha;

	else
		entalpha = ENTALPHA_DECODE(e->alpha); // johnfitz

	if(entalpha == 0) // new 2019 - Cowcat
		goto cleanup;

	if(entalpha < 1) // johnfitz
	{
		//glDepthMask(GL_FALSE);
		glEnable(GL_BLEND);
		//Con_Printf("alpha entity\n");
	}

	//
	// get lighting information
	//

	#ifdef LITFILES
	if(!eyecandy)
	{
	#endif
		//if ( clmodel->modhint == MOD_FLAME )
		if ( clmodel->flags & MOD_FULLBRIGHT )
		{
			ambientlight = shadelight = 255; // JPG 3.02 says 255 - was 256 - Cowcat
		}

		else
		{
			ambientlight = shadelight = (float)R_LightPoint (e->origin);

			// always give the gun some light
			if (e == &cl.viewent && ambientlight < 24)
				ambientlight = shadelight = 24;

			for (lnum=0 ; lnum<MAX_DLIGHTS ; lnum++)
			{
				if (cl_dlights[lnum].die < cl.time || !cl_dlights[lnum].radius)
					continue; // test 2019 - Cowcat

				//if (cl_dlights[lnum].die >= cl.time)
				{
					VectorSubtract (e->origin, cl_dlights[lnum].origin, dist);
					add = cl_dlights[lnum].radius - Length(dist);

					if (add > 0)
					{
						ambientlight += add;
						//ZOID models should be affected by dlights as well
						shadelight += add;
					}
				}
			}

			// clamp lighting so it doesn't overbright as much
			if (ambientlight > 128)
				ambientlight = 128;

			if (ambientlight + shadelight > 192)
				shadelight = 192 - ambientlight;

			// ZOID: never allow players to go totally black
			i = currententity - cl_entities;

			#if 1

			if (i >= 1 && i <= cl.maxclients)
			{
				if (ambientlight < 8)
					ambientlight = shadelight = 8;
			}

			#else

			if ( clmodel->modhint == MOD_PLAYER )
			{
				if (ambientlight < 8)
					ambientlight = shadelight = 8;
			}

			#endif
		}

		//shadelight = shadelight / 200.0;
		shadelight *= 1.275; //use ubytes unstead
		
	#ifdef LITFILES
	}

	else
	{
		//if ( clmodel->modhint == MOD_FLAME )
		if ( clmodel->flags & MOD_FULLBRIGHT)
		{
			lightcolor[0] = lightcolor[1] = lightcolor[2] = 255; // 256 / 200; - new Cowcat 2019
		}

		else
		{
			R_LightPointColor(e->origin); // LordHavoc: lightcolor is all that matters from this 

			if (e == &cl.viewent) 
			{ 
				if (lightcolor[0] < 24) lightcolor[0] = 24; 
				if (lightcolor[1] < 24) lightcolor[1] = 24; 
				if (lightcolor[2] < 24) lightcolor[2] = 24; 
			} 

			for (lnum=0 ; lnum < MAX_DLIGHTS ; lnum++)
			{ 
				if (cl_dlights[lnum].die < cl.time || !cl_dlights[lnum].radius)
					continue; // test 2019 - Cowcat

				//if (cl_dlights[lnum].die >= cl.time)
				{ 
					VectorSubtract (e->origin, cl_dlights[lnum].origin, dist); 

					add = cl_dlights[lnum].radius - Length(dist); 
 
					if (add > 0) 
					{ 
						lightcolor[0] += add * cl_dlights[lnum].color[0]; 
						lightcolor[1] += add * cl_dlights[lnum].color[1]; 
						lightcolor[2] += add * cl_dlights[lnum].color[2]; 
					}
				}
			}

			// ZOID: never allow players to go totally black 
			i = e - cl_entities;
			
			if (i >= 1 && i <= cl.maxclients) 
			{ 
				if (lightcolor[0] < 8) lightcolor[0] = 8; 
				if (lightcolor[1] < 8) lightcolor[1] = 8; 
				if (lightcolor[2] < 8) lightcolor[2] = 8; 
			} 

			VectorScale(lightcolor, 1.275, lightcolor); 

			// clamp lighting so it doesn't overbright
			
			if(lightcolor[0] > 255.0) lightcolor[0] = 255.0;
			if(lightcolor[1] > 255.0) lightcolor[1] = 255.0;
			if(lightcolor[2] > 255.0) lightcolor[2] = 255.0;
		}
	}
	#endif

	shadedots = r_avertexnormal_dots[((int)(e->angles[1] * (SHADEDOT_QUANT / 360.0))) & (SHADEDOT_QUANT - 1)];

	//
	// locate the proper data
	//
	paliashdr = (aliashdr_t *)Mod_Extradata (e->model);
	c_alias_polys += paliashdr->numtris;

	//
	// draw all the triangles
	//

	//if(mtexenabled)
		//GL_DisableMultitexture(); // already disabled - Cowcat

	glPushMatrix ();

	#if 0
	if(r_interpolations.value && e != &cl.viewent)
		R_RotateInterpolatedForEntity (e);

	else
		R_RotateForEntity (e);

	#else

	if( (e == &cl.viewent) || (clmodel->modhint == MOD_FLAME) || (r_interpolations.value == 0) )
		//R_RotateForEntity (e);
		R_RotateForEntity (e, false);

	else
		//R_RotateInterpolatedForEntity (e);
		R_RotateInterpolatedForEntity (e, false);

	#endif
	
	if ( clmodel->modhint == MOD_EYES  && gl_doubleeyes.value)
	{
		glTranslatef (paliashdr->scale_origin[0], paliashdr->scale_origin[1], paliashdr->scale_origin[2] - (22 + 8));
		// double size of eyes, since they are really hard to see in gl
		glScalef (paliashdr->scale[0]*2, paliashdr->scale[1]*2, paliashdr->scale[2]*2);
	}

	else
	{
		if ( (e == &cl.viewent) && (cl_gunfov.value) )
		{
			if (scr_fov.value <= 90)
				goto nogunfov; //scale = 1.0f;

			else
				scale = 1.0f / tan(DEG2RAD(scr_fov.value / 2)); // Phoenix

			glTranslatef (paliashdr->scale_origin[0]*scale, paliashdr->scale_origin[1], paliashdr->scale_origin[2]);
			glScalef (paliashdr->scale[0] * scale, paliashdr->scale[1], paliashdr->scale[2]);
		}

		else
		{

		nogunfov:
			glTranslatef (paliashdr->scale_origin[0], paliashdr->scale_origin[1], paliashdr->scale_origin[2]);
			glScalef (paliashdr->scale[0], paliashdr->scale[1], paliashdr->scale[2]);
		}
	}

	anim = (int)(cl.time*10) & 3;

	GL_BindCheck(paliashdr->gl_texturenum[e->skinnum][anim]); // textures repeat - Cowcat 2019

	// we can't dynamically colormap textures, so they are cached
	// seperately for the players.	Heads are just uncolored.
	if (e->colormap != vid.colormap && !gl_nocolors.value)
	{
		i = e - cl_entities;

		if (i >= 1 && i <= cl.maxclients /* && !strcmp (currententity->model->name, "progs/player.mdl") */)
			GL_Bind(playertextures - 1 + i);
	}

	/* Unconditional GL_MODULATE/ShadeModel assertion (2026-08-17,
	 * hardware-confirmed): the shadow-blob draw below sets GL_REPLACE
	 * and GL_FLAT; without resetting them here before every alias-model
	 * draw (not just when r_shadows is on), a leaked GL_REPLACE state
	 * from an earlier draw makes the next alias model (e.g. the weapon)
	 * render fullbright/pure-white regardless of lighting -- MiniGLV3D's
	 * `combined` dispatch treats REPLACE specially and substitutes pure
	 * white to fake it through a modulate-only shader. Was briefly
	 * gated behind r_shadows again during the "New Game" Guru
	 * bisection (ruled out -- RemoveLink in common.c was the real
	 * crash cause) and should have been restored unconditional right
	 * after; left mistakenly reverted until now. */
	if (gl_smoothmodels.value)
		glShadeModel (GL_SMOOTH);

	else
		glShadeModel (GL_FLAT); //SuRgEoN

	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	if ( r_interpolations.value && clmodel->modhint != MOD_FLAME )
		R_SetupAliasInterpolatedFrame (e->frame, paliashdr, e);

	else
		R_SetupAliasFrame (e->frame, paliashdr);

	glPopMatrix ();

	#ifdef GLOWEFFECTS
	if(gl_glows.value)
	{
		if(clmodel->flags & MOD_GLOWS)
			R_AddGlow (e);
	}
	#endif

	// shadows - Cowcat

#ifndef MGLV3D_FORCE_NOSHADOW
	if( r_shadows.value && e != &cl.viewent && !(clmodel->flags & MOD_NOSHADOW) && r_refdef.vieworg[2] - lightspot[2] + 1 > 0 )
	{
		trace_t downtrace;
		vec3_t	downmove;

		VectorCopy (e->origin, downmove);
		downmove[2] -= 4096;
		memset (&downtrace, 0, sizeof(downtrace));
		downtrace.fraction = 1;
	
		SV_RecursiveHullCheck (cl.worldmodel->hulls, 0, 0, 1, e->origin, downmove, &downtrace);
					
		if(downtrace.fraction < 1) //R00k
		{
			glPushMatrix ();
	
			//
			//if(r_interpolations.value && e->pose1 != e->pose2 )
			if(r_interpolations.value && lastposenum0 != lastposenum )
				R_RotateInterpolatedForEntity(e, true);
		
			else
				R_RotateForEntity(e, true);
			//

			//glLoadMatrixf(r_shadow_matrix); // bypass transformations - R_Rotate (Realm engine idea - Cowcat)

			glDisable (GL_TEXTURE_2D);
			glEnable (GL_BLEND);
			
			if (r_shadows.value == 2)
				{ glColor4f(0.0f, 0.0f, 0.0f, 0.5f); } //keep those {} - macros are in use !! - Cowcat

			else
				{ glColor4f( 0.0f, 0.0f, 0.0f, shadowalpha.value - ((mins[2]-downtrace.endpos[2])/60) ); }

			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			glShadeModel (GL_FLAT);
				
			/*
			glEnable(GL_STENCIL_TEST);
			glStencilFunc(GL_EQUAL, 1, 2);
			glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
			*/

			//glDepthFunc(GL_NOTEQUAL);

			//if(r_interpolations.value && e->pose1 != e->pose2 )
			if(r_interpolations.value && lastposenum0 != lastposenum )
				//GL_DrawAliasBlendedShadow (paliashdr, lastposenum0, lastposenum, e, downtrace);
				GL_DrawAliasBlendedShadow (paliashdr, e->pose1, e->pose2, e, downtrace);

			else
				GL_DrawAliasShadow (paliashdr, lastposenum, e, downtrace);
				
			glEnable (GL_TEXTURE_2D);
			glDisable (GL_BLEND);

			/*
			glDisable(GL_STENCIL_TEST);
			*/

			//glDepthFunc(GL_LEQUAL);

			glColor4f (1,1,1,1);
			glPopMatrix ();
		}
	}
#endif

cleanup:
	if(entalpha < 1 ) // johnfitz
	{
		//glDepthMask(GL_TRUE);
		glDisable(GL_BLEND);
	}
}


//==================================================================================

void R_DrawViewModel (void);

/*
=============
R_DrawEntitiesOnList
=============
*/

/*
#if 0

void SortEntitiesByTransparency(void)
{
	int		i, j;
	entity_t	*tmp;
	qboolean	istransparent = false;

	for(i = 0; i < cl_numvisedicts; i++)
	{
		currententity = cl_visedicts[i];

		if (currententity->model->type != mod_alias)
				continue;

		if(ENTALPHA_DECODE(currententity->alpha) < 1)
			istransparent = true;

		else
			istransparent = false;
		
		if( istransparent )
		{
			for( j = cl_numvisedicts - i; j > i ; j--)
			{
				if(!istransparent)
				{
					tmp = cl_visedicts[i];
					cl_visedicts[i] = cl_visedicts[j];
					cl_visedicts[j] = tmp;
					break;
				}

				else
					continue;
			}

			if (j == i)
				return;
		}
	}
}

#else

void SortEntitiesByTransparency(void)
{
	int		i, j;
	entity_t	*tmp;

	for(i = 0; i < cl_numvisedicts; i++)
	{
		if( cl_visedicts[i]->alpha < 1 && cl_visedicts[i]->alpha > 0 )
		{
			for( j = cl_numvisedicts - i; j > i ; j--)
			{
				if(cl_visedicts[i]->alpha == 1)
				{
					tmp = cl_visedicts[i];
					cl_visedicts[i] = cl_visedicts[j];
					cl_visedicts[j] = tmp;
					break;
				}

				else
					continue;
			}

			if (j == i)
				return;
		}
	}
}

#endif
*/

void R_DrawEntitiesOnList (int routine)
{
	int	i;
  
	if (!r_drawentities.value)
		return;

	if(routine == 1)
	{
		for (i=0; i<cl_numvisedicts; i++)
		{
			currententity = cl_visedicts[i];

			if (currententity->model->type != mod_brush)
				continue;

			R_DrawBrushModel(currententity);

			/*
			if(gl_wireframe.value)
			{
				wireframe = true;
				glDisable(GL_DEPTH_TEST);
				R_DrawBrushModel(currententity);
				glEnable(GL_DEPTH_TEST);
				wireframe = false;
			}
			*/
		}

		return;
	}

	if(routine == 2)
	{
		// draw sprites seperately, because of alpha blending

		if(mtexenabled)
			GL_DisableMultitexture();

		if (gl_affinemodels.value)
		#ifndef AMIGAOS
			glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
		#else
			glDisable(MGL_PERSPECTIVE_MAPPING);
		#endif

		glDisable(GL_CULL_FACE);

		//glDepthMask(GL_FALSE);
	
		for (i=0; i<cl_numvisedicts; i++)
		{
			currententity = cl_visedicts[i];

			if (currententity->model->type != mod_sprite)
				continue;

			R_DrawSpriteModel(currententity);
		}

		//glDepthMask(GL_TRUE);

		if(gl_cull.value)
			glEnable(GL_CULL_FACE); // Cowcat

		if (gl_smoothmodels.value)
		//if(smoothmodels)
			glShadeModel (GL_SMOOTH);

		qgl_EnableArray(); // every alias model draws through them now (GL_DrawAliasBatch)

		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

		for (i=0; i<cl_numvisedicts; i++)
		{
			currententity = cl_visedicts[i];

			if (currententity->model->type != mod_alias)
				continue;

			//if(ENTALPHA_DECODE(currententity->alpha) < 1)
				//continue;

			if (currententity == &cl_entities[cl.viewentity] && chase_active.value)
			{
				currententity->angles[0] *= 0.3; //
			}
			
			R_DrawAliasModel(currententity);
		}

		#if 0
		for (i=0; i<cl_numvisedicts; i++)
		{
			currententity = cl_visedicts[i];

			if (currententity->model->type != mod_alias)
				continue;

			if(ENTALPHA_DECODE(currententity->alpha) == 1)
				continue;

			if (currententity == &cl_entities[cl.viewentity] && chase_active.value)
			{
				currententity->angles[0] *= 0.3; //
			}
			
			R_DrawAliasModel(currententity);
		}
		#endif

		if(fog)
			glDisable(GL_FOG); // hack : workaround for weapon bug with fog + varrays - Cowcat

		R_DrawViewModel ();

		qgl_DisableArray();
			
		
		if (gl_affinemodels.value)
		#ifndef AMIGAOS
			glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
		#else
			glEnable(MGL_PERSPECTIVE_MAPPING);
		#endif

		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

		return;
	}
}

extern	cvar_t	scr_fov; //Surgeon

/*
=============
R_DrawViewModel
=============
*/
void R_DrawViewModel (void)
{
	#if 0

	if (!r_drawviewmodel.value)
		return;

	//  if(r_framecount < 2) //surgeon
	//	return;

	if (scr_fov.value > 170) //surgeon: like soft renderer (was 90 - Cowcat)
		return;

	if (chase_active.value)
		return;

	//if (envmap)
		//return; // Cowcat

	//if (!r_drawentities.value)
		//return; // Cowcat

	if (cl.items & IT_INVISIBILITY)
		return;

	if (cl.stats[STAT_HEALTH] <= 0)
		return;

	currententity = &cl.viewent;

	if (!currententity->model)
		return;

	#else

	currententity = &cl.viewent;

	if ( !r_drawviewmodel.value || chase_active.value || cl.items & IT_INVISIBILITY ||
		cl.stats[STAT_HEALTH] <= 0 || !currententity->model)
		return;

	#endif

	// hack the depth range to prevent view model from poking into walls
	glDepthRange (gldepthmin, gldepthmin + 0.3 * (gldepthmax-gldepthmin));
	R_DrawAliasModel (currententity);
	glDepthRange (gldepthmin, gldepthmax);
}


/*
============
R_PolyBlend
============
*/

#ifdef AMIGAOS //surgeon: MGL_FLATFAN is fastest method..

void R_PolyBlend (void)
{
	if (!gl_polyblend.value || !v_blend[3] )
		return;

	if(mtexenabled)
		GL_DisableMultitexture();

	glDisable (GL_ALPHA_TEST);
	glDisable (GL_DEPTH_TEST);
	glDisable (GL_TEXTURE_2D);
	glEnable (GL_BLEND);
	/* REAL BUG FIX (2026-08-05, -noextensions underwater-tint/water
	 * glitch): this full-screen overlay (damage flash AND, critically,
	 * the underwater color tint via v_blend/V_CalcBlend) never set its
	 * own blend function -- same missing-glBlendFunc bug class already
	 * found+fixed twice elsewhere this session (gl_rsurf.c's lightmap-
	 * blend fallback paths). Since this runs EVERY frame whenever
	 * underwater/any blend flash is active, relying on whatever blend
	 * func some unrelated earlier draw happened to leave set explains
	 * the jagged/banded wall texture seen underwater specifically under
	 * -noextensions (quake25 vs quake26 comparison) -- a wrong blend
	 * func combining the tint with the scene incorrectly produces
	 * banding, not a clean tint. */
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	/* TEST (soft-hang investigation, 2026-08-03): real hardware confirmed
	 * this exact draw (untextured, GL_SMOOTH shaded, GL_BLEND enabled,
	 * GL_DEPTH_TEST disabled) triggers the render-timeout hang -- and
	 * that it goes through real fixed-function hardware blend (`be`),
	 * not the software (`swblend`) shader path, specifically BECAUSE
	 * swblend's own dispatch condition excludes smooth-shaded draws.
	 * Forcing GL_FLAT here routes this draw through swblend instead
	 * (same visual result -- swblend implements the identical SRC_ALPHA/
	 * ONE_MINUS_SRC_ALPHA LERP -- just a different GPU-side mechanism),
	 * to test whether avoiding `be` for this one draw is enough to avoid
	 * the hang without disabling blend anywhere in the driver. Restored
	 * to GL_SMOOTH immediately after -- this project's own default
	 * shading mode, expected everywhere else. */
	glShadeModel (GL_FLAT);

	glColor4fv (v_blend);

	/* REAL BUG FIX (2026-08-08, "sprinkle" artifact investigation):
	 * MGL_FLATFAN was ruled out at the dispatch level (draw.c's three
	 * use_clip_space call sites all correctly distinguish it from normal
	 * primitives), but this project's own AMIGAOS branch is the ONLY
	 * platform variant that relies on MGL_FLATFAN's window-space-bypass
	 * transform at all (GL_Set2D's #ifndef AMIGAOS block, gl_draw.c,
	 * confirms every other platform just uses a normal glOrtho + regular
	 * primitives for 2D drawing instead) -- meaning MGL_FLATFAN's own
	 * transform path (v_Transform's special branch, draw.c) is comparatively
	 * untested next to the standard ModelView*Projection pipeline every
	 * other draw call in this entire driver already goes through
	 * constantly. Rather than keep chasing a bug somewhere in that
	 * bypass path, sidestep it here: temporarily swap in a screen-space
	 * orthographic projection (same mapping GL_Set2D's own non-AmigaOS
	 * branch uses) and draw this one quad as a normal GL_QUADS primitive
	 * instead -- R_PolyBlend only runs once per frame (unlike per-glyph
	 * text), so the extra matrix push/pop here costs nothing measurable. */
	glMatrixMode (GL_PROJECTION);
	glPushMatrix ();
	glLoadIdentity ();
	glOrtho (0, vid.width, vid.height, 0, -99999, 99999);
	glMatrixMode (GL_MODELVIEW);
	glPushMatrix ();
	glLoadIdentity ();

	/* The quad must span the WHOLE ortho range: the ortho maps onto the
	 * viewport that is current here, which is the 3D view (R_SetupGL), not
	 * the screen. Until 2026-09-11 it stopped at vid.height - sb_lines, which
	 * was only right while SCR_CalcRefdef's workaround made the 3D viewport
	 * the full screen; with stock geometry that left the bottom sb_lines
	 * rows OF THE VIEW untinted. Whatever the viewport is, full or
	 * letterboxed, this now fills it. */
	glBegin (GL_QUADS);

	glVertex2f (0, 0);
	glVertex2f (vid.width, 0);
	glVertex2f (vid.width, vid.height);
	glVertex2f (0, vid.height);

	glEnd ();

	glMatrixMode (GL_PROJECTION);
	glPopMatrix ();
	glMatrixMode (GL_MODELVIEW);
	glPopMatrix ();

	glShadeModel (GL_SMOOTH);

	glColor4f (1, 1, 1, 1);

	glDisable (GL_BLEND);
	glEnable (GL_TEXTURE_2D);

	/* REAL BUG FIX (2026-08-08, "sprinkle" artifact investigation): this
	 * function disables GL_DEPTH_TEST above (needed for the full-screen
	 * blend quad itself) but never re-enabled it here -- GL_ALPHA_TEST's
	 * own re-enable was even left commented out. Anything drawn after
	 * this point, whether later this same frame or before R_SetupGL's
	 * own unconditional glEnable(GL_DEPTH_TEST) runs again next frame,
	 * could render without depth testing -- consistent with small,
	 * sharp-edged world-geometry fragments showing through specifically
	 * during pickup/damage/underwater blend flashes (this function only
	 * runs at all when v_blend[3] is nonzero, i.e. exactly those
	 * moments). */
	glEnable (GL_DEPTH_TEST);
	glEnable (GL_ALPHA_TEST);
}

#else

void R_PolyBlend (void)
{
	if (!gl_polyblend.value)
		return;

	if (!v_blend[3])
		return;
  
	if(mtexenabled)
		GL_DisableMultitexture();
	
	glEnable (GL_BLEND);
	glDisable (GL_DEPTH_TEST);
	glDisable (GL_TEXTURE_2D);

	glLoadIdentity ();

	glRotatef (-90,	 1, 0, 0);	// put Z going up
	glRotatef (90,	0, 0, 1);	// put Z going up

	glColor4fv (v_blend);

	glBegin (GL_QUADS);

	glVertex3f (10, 100, 100);
	glVertex3f (10, -100, 100);
	glVertex3f (10, -100, -100);
	glVertex3f (10, 100, -100);

	glEnd ();

	glDisable (GL_BLEND);
	glEnable (GL_TEXTURE_2D);
	//glEnable (GL_ALPHA_TEST);
}

#endif


#ifndef FASTBOPS
int SignbitsForPlane (mplane_t *out)
{
	int bits, j;

	// for fast box on planeside test

	bits = 0;

	for (j=0 ; j<3 ; j++)
	{
		if (out->normal[j] < 0)
			bits |= 1<<j;
	}

	return bits;
}
#endif


void R_SetFrustum (float fovx, float fovy)
{
	//float vpnrx, vpnry; //SuRgEoN

	if(stereoview)
		fovx += 10; // stereo
	
	RotatePointAroundVectorNEW( frustum[0].normal, frustum[1].normal, vup, vpn, -(90-fovx * 0.5) ); 
	RotatePointAroundVectorNEW( frustum[2].normal, frustum[3].normal, vright, vpn, 90-fovy * 0.5 );

	#ifndef FASTBOPS

	int i;

	for (i=0 ; i<4 ; i++)
	{
		frustum[i].type = PLANE_ANYZ;
		frustum[i].dist = DotProduct (r_origin, frustum[i].normal);
		frustum[i].signbits = SignbitsForPlane (&frustum[i]);
	#else

	frustum[0].type = PLANE_ANYZ;
	frustum[1].type = PLANE_ANYZ;
	frustum[2].type = PLANE_ANYZ;
	frustum[3].type = PLANE_ANYZ;

	frustum[0].dist = DotProduct (r_origin, frustum[0].normal);
	frustum[1].dist = DotProduct (r_origin, frustum[1].normal);
	frustum[2].dist = DotProduct (r_origin, frustum[2].normal);
	frustum[3].dist = DotProduct (r_origin, frustum[3].normal);

	BoxOnPlaneSideClassify(&frustum[0]);
	BoxOnPlaneSideClassify(&frustum[1]);
	BoxOnPlaneSideClassify(&frustum[2]);
	BoxOnPlaneSideClassify(&frustum[3]);

	#endif

	#ifndef FASTBOPS

	}

	#endif
}

/*
===============
R_SetupFrame
===============
*/

void R_SetupFrame (void)
{
	r_mod_maxdist = (ULONG)r_model_maxdist.value;
	r_mod_maxdist *= r_mod_maxdist;

	// don't allow cheats in multiplayer
	if (cl.maxclients > 1)
		//Cvar_Set ("r_fullbright", "0");
		r_fullbright.value = 0;

	r_dowarp = (r_waterwarp.value != 0); // r_waterwarp switch enable by SuRgEoN

	R_AnimateLight ();

	r_framecount++;

	// build the transformation matrix for the given view angles
	VectorCopy (r_refdef.vieworg, r_origin);
	AngleVectors (r_refdef.viewangles, vpn, vright, vup);

	// current viewleaf
	r_oldviewleaf = r_viewleaf;
	r_viewleaf = Mod_PointInLeaf (r_origin, cl.worldmodel);

	if(!mirror)
	{
		V_SetContentsColor (r_viewleaf->contents);
		V_CalcBlend ();
	}

	r_fovx = r_refdef.fov_x; // johnfitz
	r_fovy = r_refdef.fov_y; //

	r_cache_thrash = false;

	c_brush_polys = 0;
	c_alias_polys = 0;
}


float frustum_skew = 0.0; // stereo

void MYgluPerspective( GLdouble fovy, GLdouble aspect, GLdouble zNear) // , GLdouble zFar )
{
	GLdouble xmin, xmax, ymin, ymax;

	//ymax = zNear * tan( fovy * M_PI / 360.0 );

	ymax = zNear * tan( fovy * MPIDIV);
	ymin = -ymax;

	xmax = ymax * aspect;
	xmin = -xmax;

	//glFrustum( xmin + frustum_skew, xmax + frustum_skew, ymin, ymax, zNear, zFar );
	glFrustum( xmin + frustum_skew, xmax + frustum_skew, ymin, ymax, zNear, gl_farclip.value );
}

/*
=============
R_SetupGL
=============
*/

void R_SetupGL (void)
{
	float	screenaspect;
	//float	yfov;
	extern	int glwidth, glheight;
	int	x, x2, y2, y, w, h;

	//surgeon: constant optimization
	int	glhght,glwdth;		//note these will typically be constant

	//
	// set up viewpoint
	//

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity ();

	glhght = glheight/vid.height;
	glwdth = glwidth/vid.width;

	x = r_refdef.vrect.x * glwdth;
	x2 = (r_refdef.vrect.x + r_refdef.vrect.width) * glwdth;
	y = (vid.height-r_refdef.vrect.y) * glhght;
	y2 = (vid.height - (r_refdef.vrect.y + r_refdef.vrect.height)) * glhght;

/*
	x = r_refdef.vrect.x * glwidth/vid.width;
	x2 = (r_refdef.vrect.x + r_refdef.vrect.width) * glwidth/vid.width;
	y = (vid.height-r_refdef.vrect.y) * glheight/vid.height;
	y2 = (vid.height - (r_refdef.vrect.y + r_refdef.vrect.height)) * glheight/vid.height;
*/

	// fudge around because of frac screen scale
	if (x > 0)
		x--;

	if (x2 < glwidth)
		x2++;

	if (y2 < 0)
		y2--;

	if (y < glheight)
		y++;

	w = x2 - x;
	h = y - y2;

	// no envmap tests - Cowcat
/*
	if (envmap)
	{
		x = y2 = 0;
		w = h = 256;
	}
*/

/*
	if ((h + y2) == vid.height)
	{
		y2 = 0;	  //  30/01/2000 added: M.Tretene
	}

	Surgeon: this causes offset to always be 0 for viewsizes less than fullscreen! - causes the console/sbar bug!
*/

	glViewport ((glx + x), (gly + y2), w, h);

	//glGetFloatv(GL_PROJECTION_MATRIX, r_projection_matrix); // save for mirrors - test 

	//Surgeon: take vid.aspect into account
	screenaspect = (float)r_refdef.vrect.width/r_refdef.vrect.height*vid.aspect;

	if(chase_active.value) // R00k
		//MYgluPerspective (r_refdef.fov_y,  screenaspect,  1,	4096);
		MYgluPerspective (r_fovy,  screenaspect,  1); //,  4096);

	else
		//MYgluPerspective (r_refdef.fov_y,  screenaspect,  4,	4096);
		MYgluPerspective (r_fovy,  screenaspect,  4); //,  4096);

	if (mirror)
	{
		/*
		if (mirror_plane->normal[2])
			glScalef (1, -1, 1);

		else
			glScalef (-1, 1, 1);

		glCullFace(GL_BACK);
		*/

		Mirror_Scale();
	}

	else
	{
		glCullFace(GL_FRONT);
	}

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity ();

#ifdef AMIGAOS
	
	glRotatefEXTs(-1.f, 0.f, GLROT_100);
	glRotatefEXTs( 1.f, 0.f, GLROT_001);
	glRotatefEXT (-r_refdef.viewangles[2], GLROT_100);
	glRotatefEXT (-r_refdef.viewangles[0], GLROT_010);
	glRotatefEXT (-r_refdef.viewangles[1], GLROT_001);

#else
	glRotatef (-90, 1, 0, 0);    // put Z going up
	glRotatef (90, 0, 0, 1);     // put Z going up
	glRotatef (-r_refdef.viewangles[2],  1, 0, 0);
	glRotatef (-r_refdef.viewangles[0],  0, 1, 0);
	glRotatef (-r_refdef.viewangles[1],  0, 0, 1);
#endif
		
	/* GCC -O2 miscompile (2026-08-20, found via file-list + intra-file +
	 * intra-function bisection, then GCC-vs-GCC -O0/-O2 disassembly
	 * comparison -- see project_minigl_v3d_gcc_update_render_regression
	 * memory): the inline `-r_refdef.vieworg[N]` triple-negation,
	 * passed directly as this call's 3 float arguments, made -O2
	 * generate `suba.l a0,a0` (unconditionally zeroing a0) followed by
	 * `fneg.s N(a0),fp0` for each component -- reading raw absolute
	 * addresses 20/24/28 instead of r_refdef.vieworg, with no
	 * relocation anywhere loading a0 with r_refdef's real address
	 * (confirmed: -O0 loads each component via its own absolute-
	 * addressed move.l, each individually relocated to _r_refdef;
	 * -O2 has no such load at all).
	 *
	 * Two intermediate attempts, both reverified via disassembly and
	 * both insufficient: plain explicit temporaries (RemoveLink's own
	 * fix in common.c) still produced the identical suba.l/fneg.s
	 * pattern -- the optimizer sees through plain temps and
	 * reconstructs the same fused computation. `volatile` on the
	 * temps forced a real store for the RESULT but not the READ --
	 * suba.l a0,a0 still showed up feeding the value in.
	 *
	 * What actually works: a `volatile float *` POINTER, assigned once
	 * from r_refdef.vieworg, then dereferenced per component. Verified
	 * in the actual compiled object: suba.l a0,a0 is gone; the
	 * address now comes from `lea ...,a4` carrying a real
	 * RELOC32 -> _r_refdef, and a4 is correctly preserved across the
	 * function (movem.l d2-d7/a2-a4). 20(a4)/24(a4)/28(a4) resolve to
	 * _r_refdef+0x48/0x4c/0x50 -- exactly the offsets -O0's own
	 * (already-correct) codegen uses for vieworg[0..2]. */
	{
		volatile float *vo = r_refdef.vieworg;
		float neworg0 = -vo[0];
		float neworg1 = -vo[1];
		float neworg2 = -vo[2];
		glTranslatef (neworg0, neworg1, neworg2);
	}

	//if(mirror) - no,no Cowcat - trying to fix mirrors
		glGetFloatv (GL_MODELVIEW_MATRIX, r_world_matrix); // needed for mirror - Cowcat 2019

	//
	// set drawing parms
	//

	if (gl_cull.value)
		glEnable(GL_CULL_FACE);

	else
		glDisable(GL_CULL_FACE);

	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glEnable(GL_DEPTH_TEST);

#ifdef AMIGAOS //was disabled for 2D
	glEnable(MGL_PERSPECTIVE_MAPPING);
#endif

}

/*
================
R_RenderScene

r_refdef must be set before the first call
================
*/

void R_RenderScene (void)
{
	R_SetupFrame ();

	R_SetFrustum (r_fovx, r_fovy);
	//R_ExtractFrustum();

	R_SetupGL ();

	R_MarkLeaves ();		// done here so we know if we're in water

	//clamp triangle elimination area: (vertexarrays)
	
	#ifdef AMIGAOS
	
	if(r_elim_areasize.value > 10.0)
		Cvar_SetValue("r_elim_areasize", 10.0);

	else if (r_elim_areasize.value < 0.5)
		Cvar_SetValue("r_elim_areasize", 0.5);

	mglMinTriArea(r_elim_areasize.value);

	#endif

	if(!r_cullworld.value && !r_novis.value)
		glDisable(GL_CULL_FACE);

	glShadeModel(GL_FLAT);		//SuRgEoN

	glColor4f(1,1,1,1);		// brushmodels, world and sprites are drawn before alias models

	//SortEntitiesByTransparency(); //

	R_DrawWorld ();			// adds static entities to the list

	/* 2026-09-14: brush models after the world, stock GLQuake's order. Drawn
	 * first, as this port did, they ran before the world walk added the static
	 * entities to the list, so static brush entities (func_illusionary) were
	 * never drawn. */
	glColor4f(1,1,1,1);
	R_DrawEntitiesOnList (1);	// brushmodels

	S_ExtraUpdate ();		// don't let sound get messed up if going slow
 
	//if(gl_cull.value)
		//glEnable(GL_CULL_FACE); // GL_CULL_FACE is disabled/enabled when DrawEntitiesOnList(2) - Cowcat

	//smoothmodels = (gl_smoothmodels.value != 0); // ala SuRgEoN - Cowcat 2019

	R_DrawEntitiesOnList (2);

	glShadeModel(GL_SMOOTH);	//SuRgEoN

	#ifdef GLOWEFFECTS
	if(gl_glows.value)
		R_RenderGlows ();
	#endif

	R_RenderDlights ();

	glDisable(GL_CULL_FACE);	//Surgeon: particle cullung is a waste of time

	if(!gl_classic_particles.value)
		R_DrawParticles ();

	else
		R_DrawParticles_Classic(); // Cowcat

	if(gl_cull.value)
		glEnable(GL_CULL_FACE);

	#ifdef GLTEST
		Test_Draw ();
	#endif

	R_DrawWaterSurfaces (); // was in RenderView (needed here for stereoview & mirrors) - Cowcat
}

/*
=============
R_Clear
=============
*/

void R_Clear (void)
{
	#if 1

	GLbitfield clearbits;

	clearbits = GL_DEPTH_BUFFER_BIT;

	//glClearColor(0, 0, 0, 1);
	//Surgeon - minimize gl_keeptjunctions 0 errors

	if (r_mirroralpha.value != 1.0)
	{
		if (gl_clear.value || gl_wireframe.value)
			clearbits |= GL_COLOR_BUFFER_BIT;

		gldepthmin = 0;
		gldepthmax = 0.5;
		glDepthFunc (GL_LEQUAL);
	}

	else if (gl_ztrick.value)
	{
		static int trickframe;

		if (gl_clear.value || gl_wireframe.value)
			glClear (GL_COLOR_BUFFER_BIT);

		trickframe++;

		if (trickframe & 1)
		{
			gldepthmin = 0;
			gldepthmax = 0.49999;
			glDepthFunc (GL_LEQUAL);
		}

		else
		{
			gldepthmin = 1;
			gldepthmax = 0.5;
			glDepthFunc (GL_GEQUAL);
		}

		goto dr;
	}

	else
	{
		/* gl_wireframe forces the colour clear the same way gl_wireonly
		 * already does, and for the same reason: without it the tile is
		 * loaded from the previous frame and last frame's edges stay under
		 * this frame's, so the lines smear. The CVAR is left alone -- turning
		 * wireframe off restores whatever the user set, which on this
		 * renderer is worth keeping at 1 anyway (not clearing costs a
		 * full-screen tile load). */
		if (gl_clear.value || gl_wireonly.value || gl_wireframe.value)
			clearbits |= GL_COLOR_BUFFER_BIT;

		gldepthmin = 0;
		gldepthmax = 1;
		glDepthFunc (GL_LEQUAL);
	}

	//if(r_viewleaf && r_viewleaf->contents == CONTENTS_SOLID)
		//glClearColor(0.01f, 0.01f, 0.01f, 1.0f);

	//else
		glClearColor(0, 0, 0, 1);

	glClear(clearbits);

dr:
	glDepthRange (gldepthmin, gldepthmax);

	/*
	if(r_shadows.value)
	{
		glClearStencil(GL_TRUE);
		glClear(GL_STENCIL_BUFFER_BIT);
	}
	*/

	#else // test Cowcat 2019

	GLbitfield clearbits;

	clearbits = GL_DEPTH_BUFFER_BIT;

	if (gl_clear.value)
		clearbits |= GL_COLOR_BUFFER_BIT;
	
	glClear(clearbits);

	gldepthmin = 0;
	gldepthmax = 1;
	glDepthFunc (GL_LEQUAL);

	glDepthRange (gldepthmin, gldepthmax);

	#endif
}


/*
================
R_RenderView

r_refdef must be set before the first call
================
*/

extern cvar_t r_pos; // Cowcat 2019

void R_RenderView (void)
{
	double	time1 = 0, time2;
	//GLfloat colors[4] = {(GLfloat) 0.0, (GLfloat) 0.0, (GLfloat) 1, (GLfloat) 0.20}; // 30/01/2000 modified: M.Tretene

	if (r_norefresh.value)
		return;

	if (!r_worldentity.model || !cl.worldmodel)
		Sys_Error ("R_RenderView: NULL worldmodel");

	if (r_speeds.value)
	{
		glFinish ();
		time1 = Sys_FloatTime ();
		c_brush_polys = 0;
		c_alias_polys = 0;
	}

	mirror = false;
	
	if (gl_finish.value)
		glFinish ();

	R_Clear();

	/* Set AFTER R_Clear, so a scissored clear still draws its quad filled and
	 * only the scene itself is outlined. Restored at the end of this function.
	 * 1 outlines the world, 2 reduces it to the boundary vertices -- the same
	 * decomposition either way, so 2 exercises GL_POINT on the locked/indexed
	 * path the world really uses. */
	if (gl_wireframe.value)
		glPolygonMode (GL_FRONT_AND_BACK,
		               gl_wireframe.value >= 2 ? GL_POINT : GL_LINE);

	// render normal view
	
	R_Fog_BeginFrame(); // Cowcat

	#if 1
	//johnfitz -- stereo rendering -- full of hacky goodness
	if (r_stereo.value)
	{
		stereoview = true;

		float eyesep = CLAMP(-8.0f, r_stereo.value, 8.0f);
		float fdepth = CLAMP(32.0f, r_stereodepth.value, 1024.0f);

		AngleVectors (r_refdef.viewangles, vpn, vright, vup);

		//render left eye (red)
		glColorMask(1, 0, 0, 1);
		VectorMA (r_refdef.vieworg, -0.5f * eyesep, vright, r_refdef.vieworg);
		frustum_skew = 0.5 * eyesep * 4 / fdepth;
		srand((int) (cl.time * 1000)); //sync random stuff between eyes

		R_RenderScene ();

		//render right eye (cyan)
		//glClear (GL_DEPTH_BUFFER_BIT); 
		R_Clear();			 // Cowcat

		glColorMask(0, 1, 1, 1);
		VectorMA (r_refdef.vieworg, 1.0f * eyesep, vright, r_refdef.vieworg);
		frustum_skew = -frustum_skew;
		srand((int) (cl.time * 1000)); //sync random stuff between eyes

		R_RenderScene ();
		
		//restore
		glColorMask(1, 1, 1, 1);
		VectorMA (r_refdef.vieworg, -0.5f * eyesep, vright, r_refdef.vieworg);
		
		frustum_skew = 0.0f;

		stereoview = false;
	}
	else
	#endif
	{
		R_RenderScene ();
	}

	//R_DrawWaterSurfaces (); // now in R_RenderScene

	if(mirror)
		R_Mirror ();
	
	//  More fog code right here :)
	
	if (fog) // Cowcat
	{	
		glDisable(GL_FOG);
		fog = false;
	}

	//R_Fog_EndFrame(); // naaa...

	//  End of all fog code...

	// render mirror view // moved up - Cowcat

	if(r_showbboxes.value)
		R_ShowBBoxes();

	R_PolyBlend ();

	if(r_pos.value)
	{
		Con_Printf("x %i y %i z %i (pitch %i yaw %i roll %i)\n",
			(int)cl_entities[cl.viewentity].origin[0],
			(int)cl_entities[cl.viewentity].origin[1],
			(int)cl_entities[cl.viewentity].origin[2],
			(int)cl.viewangles[PITCH],
			(int)cl.viewangles[YAW],
			(int)cl.viewangles[ROLL]);
	}
			
	else if (r_speeds.value)
	{
		//glFinish ();
		time2 = Sys_FloatTime ();
		Con_Printf ("%3i ms  %4i wpoly %4i epoly\n", (int)((time2-time1)*1000), c_brush_polys, c_alias_polys);
	}

	/* Back to GL_FILL before the 2D pass: the console, HUD and menu are drawn
	 * after this and must not come out as outlines. */
	if (gl_wireframe.value)
		glPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
}

