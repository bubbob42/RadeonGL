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
// r_surf.c: surface-related refresh code

#include "quakedef.h"

#include <mgl/mglmacros.h>

#ifdef MINIGL_SHARED_CLIENT
/* RadeonGL: the MiniGLV3D debug header is not in the client SDK; only its
 * D() macro is used here, and that is empty without -DDEBUG anyway. */
#define D(x)
#else
#include "../MiniGLV3D/backend/hw/v3d_debug.h"
#endif

#ifdef __VBCC__
#include "miniglext.h"	//optimized vertexbuffer construct
#else
#include "miniglext_gcc.h" // inline for gcc version - Cowcat 2017
#endif

/* REAL BUG FIX (MiniGLV3D white-texture investigation, 2026-08-03):
 * defaulted to "1" (on) in this Amiga fork, meaning R_DrawWorld's
 * once-per-frame R_DrawMultitextureBuffer() -> mglDrawMultitexBuffer(...,
 * GL_REPLACE) call ALWAYS fired, even though this port's real ARB
 * multitexture path (glActiveTextureARB/glTexEnvi/glMultiTexCoord2fARB)
 * works correctly on its own. That call hardcodes GL_REPLACE and applies
 * it directly to whatever texture happens to be bound at unit 1 at that
 * moment (leftover state from the last surface drawn that frame) --
 * hardware-confirmed (MiniGLV3D's own tex_SetEnv/MGLDrawMultitexBuffer
 * call logging) to permanently stick that ONE texture object's texenv
 * mode at REPLACE forever after (texenv mode is stored per-texture-
 * object, never reset per-frame), causing exactly the observed symptom:
 * a lightmap's own grayscale value renders directly (correct-looking
 * lighting gradient) with the base texture's own pattern completely
 * replaced away (flat white-ish). "Fake" 2-pass multitexture is meant as
 * a fallback for hardware without real ARB multitexture support -- this
 * port has real support and shouldn't run both mechanisms at once. */
cvar_t gl_fake_multitexture = {"gl_fake_multitexture","0", true};

extern void	R_DrawSkyBox();
extern qboolean fog;		// Cowcat
extern void	R_ClearSkyBox();

#ifndef GL_RGBA4
#define GL_RGBA4  0
#endif

int   skytexturenum;

extern qboolean r_dowarp; //waterwarp switch by surgeon

//extern GLfloat gl_debug;
extern int  gl_filter_min; //surgeon
extern int  gl_filter_max; //surgeon

int   lightmap_bytes;	// 1,2, or 4

#ifndef AMIGAOS
int   lightmap_textures;
#else

/*
** surgeon: we set a static texnum-base for lightmaps in
** order to use narrow-range multitexture-sorting for TMU1
** with MiniGL
*/
int   lightmap_textures = 1024;

#endif

#define BLOCK_WIDTH   128
#define BLOCK_HEIGHT  128

#ifdef LITFILES
qboolean eyecandy = false;
//unsigned blocklights[18*18*3]; //LordHavoc
unsigned blocklights[BLOCK_WIDTH*BLOCK_HEIGHT*3]; // new 2019 - Cowcat
#else
unsigned blocklights[18*18];
#endif


#define MAX_LIGHTMAPS 128 // was 64 2019 -Cowcat

//static int  active_lightmaps; /*surgeon:static*/

typedef struct glRect_s
{
	unsigned char l, t, w, h;

} glRect_t;

static	glpoly_t  *lightmap_polys[MAX_LIGHTMAPS];
static	qboolean  lightmap_modified[MAX_LIGHTMAPS];
static	glRect_t  lightmap_rectchange[MAX_LIGHTMAPS];
static	int	  allocated[MAX_LIGHTMAPS][BLOCK_WIDTH];

/*surgeon:static*/

// the lightmap texture data needs to be kept in
// main memory so texsubimage can update properly
byte	lightmaps[4*MAX_LIGHTMAPS*BLOCK_WIDTH*BLOCK_HEIGHT];

// For gl_texsort 0
static	msurface_t  *skychain = NULL;
static	msurface_t  *waterchain = NULL;

/*surgeon:static*/

static void R_RenderDynamicLightmaps (msurface_t *fa);

//static int last_lightmap_updated; // ezquake

extern mplane_t *mirror_plane;
extern mplane_t mirror_far_plane;
extern int mirror_clipside;
extern cvar_t	gl_showpolys;
extern cvar_t	gl_wireframe;
extern cvar_t	gl_wireonly;

#define PlaneDiff(point, plane) (((plane)->type < 3) ? (point)[(plane)->type] - (plane)->dist : DotProduct((point), (plane)->normal) - (plane)->dist)

//#define	GL_UNUSED_TEXTURE (~(GLuint)0)
//GLuint	currenttex = GL_UNUSED_TEXTURE; // to avoid unnecessary texture sets - test Cowcat

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

/*
===============
R_AddDynamicLights
===============
*/

void R_AddDynamicLights (msurface_t *surf)
{
	int		lnum;
	int		sd, td;
	float		dist, rad, minlight;
	vec3_t		impact, local;
	int		s, t;
	int		i;
	int		smax, tmax;
	mtexinfo_t	*tex;

	#ifdef LITFILES

	// LordHavoc: .lit support begin
	float		cred, cgreen, cblue, brightness;
	unsigned	*bl;
	// LordHavoc: .lit support end

	#endif

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;
	tex = surf->texinfo;
	
	for (lnum=0 ; lnum<MAX_DLIGHTS ; lnum++)
	{
		//if(cl_dlights[lnum].die < cl.time);
			//continue; // test 2019 - Cowcat (no dynamic light with this?)

		//if ( !(surf->dlightbits & (1<<lnum) ) ) continue;	// not lit by this light
		if ( !(surf->dlightbits[lnum >> 5] & (1U << (lnum & 31))) ) // extended lights - Cowcat 2019
			continue; 

		rad = cl_dlights[lnum].radius;
		dist = DotProduct (cl_dlights[lnum].origin, surf->plane->normal) - surf->plane->dist;
		//dist = PlaneDiff (cl_dlights[lnum].origin, surf->plane); // test if needed - Cowcat 2019
		rad -= fabs(dist);
		minlight = cl_dlights[lnum].minlight;

		if (rad < minlight)
			continue;

		minlight = rad - minlight;

		for (i=0 ; i<3 ; i++)
		{
			impact[i] = cl_dlights[lnum].origin[i] - surf->plane->normal[i] * dist;
		}

		local[0] = DotProduct (impact, tex->vecs[0]) + tex->vecs[0][3];
		local[1] = DotProduct (impact, tex->vecs[1]) + tex->vecs[1][3];

		local[0] -= surf->texturemins[0];
		local[1] -= surf->texturemins[1]; 

		#ifdef LITFILES

		if(eyecandy)
		{
			// LordHavoc: .lit support begin

			bl = blocklights;
			cred = cl_dlights[lnum].color[0] * 256.0f;
			cgreen = cl_dlights[lnum].color[1] * 256.0f;
			cblue = cl_dlights[lnum].color[2] * 256.0f;

			// LordHavoc: .lit support end

			for (t = 0 ; t < tmax ; t++)
			{
				td = local[1] - t*16;

				if (td < 0)
					td = -td;

				for (s=0 ; s < smax ; s++)
				{
					sd = local[0] - s*16;

					if (sd < 0)
						sd = -sd;

					if (sd > td)
						dist = sd + (td>>1);

					else
						dist = td + (sd>>1);

					if (dist < minlight)
					{
						brightness = rad - dist;
						bl[0] += (int)(brightness * cred);
						bl[1] += (int)(brightness * cgreen);
						bl[2] += (int)(brightness * cblue);
					}

					bl += 3;
				}
			}
		}

		else
		{

		#endif

			for (t = 0 ; t < tmax ; t++)
			{
				td = local[1] - t*16;

				if (td < 0)
					td = -td;

				for (s=0 ; s < smax ; s++)
				{
					sd = local[0] - s*16;

					if (sd < 0)
						sd = -sd;

					if (sd > td)
						dist = sd + (td>>1);

					else
						dist = td + (sd>>1);

					if (dist < minlight)
					{
						blocklights[t * smax + s] += (rad - dist)*256; 
					}
				}
			}

		#ifdef LITFILES

		}

		#endif
	}
}

/*
===============
R_BuildLightMap

Combine and scale multiple lightmaps into the 8.8 format in blocklights
===============
*/

#ifdef LITFILES

void R_BuildLightMapColor (msurface_t *surf, byte *dest, int stride)
{
	int		smax, tmax, t, i, j, size, maps, blocksize;
	byte		*lightmap;
	unsigned	scale, *bl;
	unsigned	r, g, b;	//surgeon
	UWORD		*short_dest;	//surgeon

	surf->cached_dlight = (surf->dlightframe == r_framecount);

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;
	size = smax*tmax;
	//blocksize = size * 3; // Cowcat 2019
	lightmap = surf->samples;

	// set to full bright if no light data
	//if (r_fullbright.value || 
	if(!cl.worldmodel->lightdata) // Cowcat 2019
	{
		bl = blocklights;

		for (i=0 ; i<size ; i++)
		{
			*bl++ = 255*256;
			*bl++ = 255*256;
			*bl++ = 255*256;
		}

		goto store;
	}

	// clear to no light
	#if 1

	bl = blocklights;

	for (i=0 ; i<size ; i++)
	{
		*bl++ = 0;
		*bl++ = 0;
		*bl++ = 0;
	}

	#else

	//memset (&blocklights[0], 0, size * 4 * sizeof(unsigned int));
	memset (blocklights, 0, size * 3 * sizeof(int)); // new 2019 - Cowcat

	#endif

	// add all the lightmaps 
	if (lightmap) 
	{
		for (maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
		{
			scale = d_lightstylevalue[surf->styles[maps]];
			surf->cached_light[maps] = scale;	// 8.8 fraction
			bl = blocklights;

			#if 1

			for (i=0; i<size; i++)
			{
				*bl++ += *lightmap++ * scale;
				*bl++ += *lightmap++ * scale;
				*bl++ += *lightmap++ * scale;
			}

			#else

			for (i=0; i<blocksize; i++)
			{
				*bl++ += lightmap[i] * scale;
			}

			lightmap += blocksize;

			#endif
		}
	}

	// add all the dynamic lights
	if (surf->dlightframe == r_framecount)
		R_AddDynamicLights (surf);

	// bound, invert, and shift

store:
	 switch (gl_lightmap_format)
	 {
		case GL_RGBA:
			stride -= (smax << 2);
			bl = blocklights;

			for (i=0 ; i < tmax ; i++, dest += stride)
			{
				for (j=0 ; j < smax ; j++)
				{
					t = *bl++ >> 7; if (t > 255) t = 255; *dest++ = t;
					t = *bl++ >> 7; if (t > 255) t = 255; *dest++ = t;
					t = *bl++ >> 7; if (t > 255) t = 255; *dest++ = t;
					*dest++ = 255;
				}
				// LordHavoc: .lit support end
			}

			break;

		case GL_ALPHA:
		case GL_LUMINANCE:
			bl = blocklights;

			for (i=0 ; i < tmax ; i++, dest += stride)
			{
				for (j=0 ; j < smax ; j++)
				{
					t = ((bl[0] + bl[1] + bl[2]) * 85) >> 15; // LordHavoc: basically / 3, but faster and combined with >> 7 shift down, note: actual number would be 85.3333...
					bl += 3;

					// LordHavoc: .lit support end
					if (t > 255)
						t = 255;

					dest[j] = t;
				}
			}

			break;

		case MGL_UNSIGNED_SHORT_4_4_4_4: //by Surgeon
			bl = blocklights;
			stride -= (smax << 1);

			for (i=0 ; i < tmax ; i++, dest += stride)
			{
				for (j=0 ; j < smax ; j++)
				{
					r = *bl++ >> 7; if (r > 255) r = 255;
					g = *bl++ >> 7; if (g > 255) g = 255;
					b = *bl++ >> 7; if (b > 255) b = 255;

					short_dest = (UWORD*)dest;
					*short_dest = ( (0xf0<<8) | ((r & 0xf0) << 4) | (g & 0xf0) | ((b & 0xf0) >> 4) );
					dest += 2;
				}
			}

			break;

		case MGL_UNSIGNED_SHORT_5_6_5: //by Surgeon
			bl = blocklights;
			stride -= (smax << 1);

			for (i=0 ; i < tmax ; i++, dest += stride)
			{
				for (j=0 ; j < smax ; j++)
				{
					r = *bl++ >> 7; if (r > 255) r = 255;
					g = *bl++ >> 7; if (g > 255) g = 255;
					b = *bl++ >> 7; if (b > 255) b = 255;

					short_dest = (UWORD*)dest;
					*short_dest = ( (UWORD)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | ((b & 0xf8) >> 3)) );
					dest += 2;
				}
			}

			break;

		default:
			Sys_Error ("Bad lightmap format");
	}
}

#endif

void R_BuildLightMap (msurface_t *surf, byte *dest, int stride)
{
	int		smax, tmax, t, i, j, size, maps;
	byte		*lightmap;
	unsigned	scale, *bl;
	UWORD		*short_dest; //surgeon

	surf->cached_dlight = (surf->dlightframe == r_framecount);

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;
	size = smax * tmax;

	lightmap = surf->samples;

	// set to full bright if no light data
	if (r_fullbright.value || !cl.worldmodel->lightdata)
	{
		for (i=0 ; i<size ; i++)
			blocklights[i] = 255*256;

		goto store;
	}

	// clear to no light
	for (i=0 ; i<size ; i++)
		blocklights[i] = 0;
	
	// add all the lightmaps
	if (lightmap)
	{
		for (maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
		{
			scale = d_lightstylevalue[surf->styles[maps]];
			surf->cached_light[maps] = scale;	// 8.8 fraction 

			for (i=0 ; i<size ; i++)					
				blocklights[i] += lightmap[i] * scale;

			lightmap += size;	// skip to next lightmap 
		}
	}

	// add all the dynamic lights
	if (surf->dlightframe == r_framecount)
		R_AddDynamicLights (surf);

	// bound, invert, and shift
store:
	switch (gl_lightmap_format)
	{
		case GL_RGBA:
			stride -= ( smax << 2 );
			bl = blocklights;

			for (i=0 ; i < tmax ; i++, dest += stride)
			{
				for (j=0 ; j < smax ; j++)
				{ 
					t = *bl++; 
					t >>= 7;

					if (t > 255)
						t = 0;

					else
						t = 255-t;

					dest[3] = t;
					dest += 4; 
				}
			}

			break;

		case GL_ALPHA:
		case GL_LUMINANCE:
			bl = blocklights;

			for (i=0 ; i < tmax ; i++, dest += stride)
			{
				for (j=0 ; j < smax ; j++)
				{
					t = *bl++;
					t >>= 7; 

					if (t > 255)
						t = 255;

					dest[j] = t;
				}
			}

			break;

		case MGL_UNSIGNED_SHORT_4_4_4_4: //by Surgeon
			/* BUG FIX, 2nd pass (2026-08-03): the first fix's comment said
			 * the intent was "write a real grayscale ARGB4444 value
			 * (R=G=B=brightness)" but the implementation kept the OLD
			 * `t = 255-t` inversion (appropriate for the ORIGINAL alpha-
			 * channel/subtractive-blend scheme this case used to belong
			 * to, GL_RGBA's dest[3] above still uses it for exactly that
			 * reason) plus an inverted overbright clamp (`t=0` instead of
			 * `t=255`) -- neither belongs in a direct RGB-modulate scheme.
			 * Net effect: well-lit surfaces (large t) produced a SMALL
			 * R=G=B value, and overbright areas clamped to solid black
			 * instead of white -- real hardware report: "crates are
			 * black and some lighting effects are incorrect" without
			 * -lm_RGB (MGL_UNSIGNED_SHORT_5_6_5, the case just below,
			 * never had either bug). Fixed to match that working case
			 * exactly: t used directly (no inversion), clamped to 255 on
			 * overbright, just packed into ARGB4444 instead of RGB565. */
			stride -= ( smax << 1);
			bl = blocklights;

			for (i=0 ; i < tmax ; i++, dest += stride)
			{
				for (j=0 ; j < smax ; j++)
				{
					t = *bl++;
					t >>= 7;

					if (t > 255)
						t = 255;

					short_dest = (UWORD*)dest;
					*short_dest = ( (UWORD)((0xf0<<8) | ((t & 0xf0) << 4) | (t & 0xf0) | ((t & 0xf0) >> 4)) );
					dest += 2;
				}
			}

			break;

		case MGL_UNSIGNED_SHORT_5_6_5: //by Surgeon
			stride -= ( smax << 1 );
			bl = blocklights;

			for (i=0 ; i < tmax ; i++, dest += stride)
			{
				for (j=0 ; j < smax ; j++)
				{
					t = *bl++;
					t >>= 7;

					if (t > 255)
						t = 255;

					short_dest = (UWORD*)dest;
					*short_dest = ( (UWORD)(((t & 0xf8) << 8) | ((t & 0xfc) << 3) | ((t & 0xf8) >> 3)) );
					dest += 2;
				}
			}

			break;

		default:
			Sys_Error ("Bad lightmap format");
	}
}


/*
===============
R_TextureAnimation

Returns the proper texture for a given time and base texture
===============
*/

texture_t *R_TextureAnimation (texture_t *base)
{
	int   reletive;
	int   count;

	if (currententity->frame)
	{
		if (base->alternate_anims)
			base = base->alternate_anims;
	}

	if (!base->anim_total)
		return base;

	reletive = (int)(cl.time*10) % base->anim_total;

	count = 0;  

	while (base->anim_min > reletive || base->anim_max <= reletive)
	{
		base = base->anim_next;

		if (!base)
			Sys_Error ("R_TextureAnimation: broken cycle");

		if (++count > 100)
			Sys_Error ("R_TextureAnimation: infinite cycle");
	}

	return base;
}


/*
=============================================================

  BRUSH MODELS

=============================================================
*/

extern	int   solidskytexture;
extern	int   alphaskytexture;
extern	float speedscale;   // for top sky and bottom sky

void DrawGLWaterPoly (glpoly_t *p);
void DrawGLWaterPolyLightmap (glpoly_t *p);

qboolean mtexenabled = false;

//extern void GL_SelectTexture (GLenum target); 

void GL_DisableMultitexture(void) 
{
	glDisable(GL_TEXTURE_2D);
	glActiveTextureARB(GL_TEXTURE0_ARB);
	mtexenabled = false;
}

void GL_EnableMultitexture(void) 
{
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glEnable(GL_TEXTURE_2D);
	mtexenabled = true;
}

/*
================
R_UploadLightmap

Sends the rows of lightmap page i rebuilt since its last upload; the page must be
bound on the active texture unit. 2026-09-14: the fake multitexture paths (the
default launch) never uploaded, so light styles and dynamic lights rebuilt the
lightmaps in memory and never reached the screen. The other paths upload the
same way inline.
================
*/
static void R_UploadLightmap (int i)
{
	glRect_t	*theRect;

	if (!lightmap_modified[i])
		return;

	lightmap_modified[i] = false;
	theRect = &lightmap_rectchange[i];

	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, theRect->t,
		BLOCK_WIDTH, theRect->h, gl_lightmap_format, GL_UNSIGNED_BYTE,
		lightmaps+(i* BLOCK_HEIGHT + theRect->t) *BLOCK_WIDTH*lightmap_bytes);

	theRect->l = BLOCK_WIDTH;
	theRect->t = BLOCK_HEIGHT;
	theRect->h = 0;
	theRect->w = 0;
}


/*
================
R_DrawSequentialPoly

Systems that have fast state and texture changes can
just do everything as it passes with no need to sort
================
*/

void R_DrawSequentialPoly (msurface_t *s)
{
	glpoly_t	*p;
	float		*v;
	int		i;
	texture_t	*t;
	vec3_t		nv;
	float		temp; // temp Cowcat
	glRect_t	*theRect;

	//
	// normal lightmaped poly
	//

	if (! (s->flags & (SURF_DRAWSKY|SURF_DRAWTURB|(r_dowarp ? SURF_UNDERWATER : 0)) ) ) 
	{
		R_RenderDynamicLightmaps(s);

		if (gl_mtexable)
		{
			p = s->polys;

			if(s->texinfo->texture->anim_total)
				t = R_TextureAnimation (s->texinfo->texture);

			else
				t = s->texinfo->texture;

			// Binds world to texture env 0:

			glActiveTextureARB(GL_TEXTURE0_ARB);
			GL_Bind (t->gl_texturenum);
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

			// Binds lightmap to texenv 1:

			GL_EnableMultitexture();
			GL_Bind (lightmap_textures + s->lightmaptexturenum);

			i = s->lightmaptexturenum;

			if (lightmap_modified[i])
			{
				lightmap_modified[i] = false;
				theRect = &lightmap_rectchange[i];

				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, theRect->t, 
					BLOCK_WIDTH, theRect->h, gl_lightmap_format, GL_UNSIGNED_BYTE,
					lightmaps+(i* BLOCK_HEIGHT + theRect->t) *BLOCK_WIDTH*lightmap_bytes);

				theRect->l = BLOCK_WIDTH;
				theRect->t = BLOCK_HEIGHT;
				theRect->h = 0;
				theRect->w = 0;
			}

			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); // unit 1

			glBegin(GL_POLYGON);
			v = p->verts[0];

			for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE)
			{
				glMultiTexCoord2fvARB (GL_TEXTURE0_ARB, (v+3));
				glMultiTexCoord2fvARB (GL_TEXTURE1_ARB, (v+5));
				glVertex3f (v[0], v[1], v[2]);
			}

			glEnd ();

			return;
		}

		else
		{
			p = s->polys;

			if(s->texinfo->texture->anim_total)
				t = R_TextureAnimation (s->texinfo->texture);

			else
				t = s->texinfo->texture;

			GL_Bind(t->gl_texturenum);

			#ifndef AMIGAOS

			glBegin (GL_POLYGON);
			v = p->verts[0];

			for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE)
			{
				glTexCoord2f (v[3], v[4]);
				glVertex3f (v[0], v[1], v[2]);
			}

			glEnd ();

			GL_Bind(lightmap_textures + s->lightmaptexturenum);

			glEnable (GL_BLEND);

			glBegin (GL_POLYGON);
			v = p->verts[0];

			for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE)
			{
				glTexCoord2f (v[5], v[6]);
				glVertex3f (v[0], v[1], v[2]);
			}

			glEnd ();

			glDisable (GL_BLEND);

			#else

			// fake single-pass multitexturing:

			if(gl_fake_multitexture.value || gl_force_fake_multitex)
			{
				glActiveTextureARB(GL_TEXTURE1_ARB);
				glEnable(GL_TEXTURE_2D);
				GL_Bind(lightmap_textures + s->lightmaptexturenum);
				R_UploadLightmap(s->lightmaptexturenum);
				glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

				glBegin(GL_POLYGON);
				mglMtexTV23fv(mini_CurrentContext, VERTEXSIZE, p);
				glEnd();

				glDisable(GL_TEXTURE_2D);
				glActiveTextureARB(GL_TEXTURE0_ARB);
			}

			else
			{
				glBegin(GL_POLYGON);
				mglTV23fv(mini_CurrentContext, VERTEXSIZE, p);
				glEnd();

				GL_Bind(lightmap_textures + s->lightmaptexturenum);

				glEnable (GL_BLEND);

				/* REAL BUG FIX (2026-08-05, -noextensions water/lightmap
				 * blend glitch): this 2nd (lightmap) pass never set its
				 * own blend function -- it relied on R_DrawMultitextureBuffer
				 * (gl_fake_multitexture path) having already configured the
				 * right factors, but that only runs when gl_fake_multitexture
				 * is true, and this branch is specifically the case where it
				 * ISN'T (gl_mtexable==FALSE, i.e. -noextensions, AND
				 * gl_fake_multitexture==0, its current default). Mirrors
				 * R_BlendLightmaps' own per-format logic exactly (same file,
				 * below) -- LUMINANCE/5_6_5 use a multiplicative blend
				 * (lightmap IS the color), ALPHA uses the lightmap's alpha
				 * channel directly, and the AmigaOS default (ARGB4444) falls
				 * through to the same normal alpha blend R_BlendLightmaps
				 * itself resets to before returning. */
				if (gl_lightmap_format == GL_LUMINANCE || gl_lightmap_format == MGL_UNSIGNED_SHORT_5_6_5)
					glBlendFunc (GL_ZERO, GL_SRC_COLOR);

				else if (gl_lightmap_format == GL_ALPHA)
					glBlendFunc (GL_ZERO, GL_SRC_ALPHA);

				else
					glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

				glBegin(GL_POLYGON);
				mglTV23fvLM(mini_CurrentContext, VERTEXSIZE, p);
				glEnd();

				glDisable (GL_BLEND);
			}

			#endif
		}

		return;
	}

	//
	// subdivided water surface warp
	//
	if (s->flags & SURF_DRAWTURB)
	{
		if(mtexenabled)
			GL_DisableMultitexture();

		GL_Bind (s->texinfo->texture->gl_texturenum);
		EmitWaterPolys (s);
		return;
	}

	//
	// subdivided sky warp
	//
	if (s->flags & SURF_DRAWSKY)
	{
		if(mtexenabled)
			GL_DisableMultitexture();

		GL_Bind (solidskytexture);
		speedscale = realtime*8;
		speedscale -= (int)speedscale & ~127;

		EmitSkyPolys (s);

		glEnable (GL_BLEND);

		GL_Bind (alphaskytexture);
		speedscale = realtime*16;
		speedscale -= (int)speedscale & ~127;

		EmitSkyPolys (s);

		glDisable (GL_BLEND);
		return;
	}

	//
	// underwater warped with lightmap
	//
	R_RenderDynamicLightmaps (s);

	/* REAL FIX (2026-08-05, -noextensions underwater "totally blue"
	 * artifact): the -noextensions fallback below (2-pass single-unit
	 * DrawGLWaterPoly+DrawGLWaterPolyLightmap) is untested/unproven --
	 * default mode never exercises it, since gl_mtexable forces this
	 * check true there. MiniGLV3D always supports real ARB
	 * multitexture in hardware regardless of -noextensions (that flag
	 * only controls glquake's OWN code-path choice, not actual driver
	 * capability) -- so just always take the proven, hardware-tested
	 * real-multitexture branch here instead of the untested fallback,
	 * exactly like the gl_texsort routing fix above. */
	if (GL_TRUE)
	{
		p = s->polys;

		t = R_TextureAnimation (s->texinfo->texture);

		glActiveTextureARB(GL_TEXTURE0_ARB);

		GL_Bind(t->gl_texturenum);
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

		GL_EnableMultitexture();
		GL_Bind(lightmap_textures + s->lightmaptexturenum);

		i = s->lightmaptexturenum;

		if (lightmap_modified[i])
		{
			lightmap_modified[i] = false;
			theRect = &lightmap_rectchange[i];

			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, theRect->t, 
				BLOCK_WIDTH, theRect->h, gl_lightmap_format, GL_UNSIGNED_BYTE,
				lightmaps+(i* BLOCK_HEIGHT + theRect->t) *BLOCK_WIDTH*lightmap_bytes);

			theRect->l = BLOCK_WIDTH;
			theRect->t = BLOCK_HEIGHT;
			theRect->h = 0;
			theRect->w = 0;
		}

		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); 

		glBegin (GL_TRIANGLE_FAN);
		v = p->verts[0];

		for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE)
		{
			temp = sin(v[2]*0.05+realtime); // Cowcat

			glMultiTexCoord2fvARB (GL_TEXTURE0_ARB, (v+3));
			glMultiTexCoord2fvARB (GL_TEXTURE1_ARB, (v+5));
			
			nv[0] = v[0] + 8*sin(v[1]*0.05+realtime)*temp;
			nv[1] = v[1] + 8*sin(v[0]*0.05+realtime)*temp;
			nv[2] = v[2];

			glVertex3fv (nv);
		}

		glEnd ();
	}

	else
	{
		p = s->polys;

		t = R_TextureAnimation (s->texinfo->texture);

		GL_Bind(t->gl_texturenum);

		DrawGLWaterPoly (p);

		GL_Bind(lightmap_textures + s->lightmaptexturenum);
		glEnable (GL_BLEND);

		/* REAL BUG FIX (2026-08-05, -noextensions water glitch): same
		 * missing-blend-function bug as R_DrawSequentialPoly's other
		 * !gl_mtexable lightmap-blend branch above (see that fix's own
		 * comment) -- this underwater-warped-with-lightmap branch also
		 * never set its own blend function, relying on whatever was left
		 * over from an unrelated previous draw. Same fix, same source
		 * (mirrors R_BlendLightmaps' own per-format logic). */
		if (gl_lightmap_format == GL_LUMINANCE || gl_lightmap_format == MGL_UNSIGNED_SHORT_5_6_5)
			glBlendFunc (GL_ZERO, GL_SRC_COLOR);

		else if (gl_lightmap_format == GL_ALPHA)
			glBlendFunc (GL_ZERO, GL_SRC_ALPHA);

		else
			glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		DrawGLWaterPolyLightmap (p);

		glDisable (GL_BLEND);
	}
}


/*
================
DrawGLWaterPoly

Warp the vertex coordinates
================
*/

void DrawGLWaterPoly (glpoly_t *p)
{
	int	i;
	float	*v, temp;
	vec3_t	nv;

	if(mtexenabled)
		GL_DisableMultitexture();

	glBegin (GL_TRIANGLE_FAN);

	v = p->verts[0];

	for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE)
	{
		temp = sin(v[2]*0.05+realtime); // Cowcat
		
		glTexCoord2f (v[3], v[4]);
		
		nv[0] = v[0] + 8*sin(v[1]*0.05+realtime) * temp;
		nv[1] = v[1] + 8*sin(v[0]*0.05+realtime) * temp; 
		nv[2] = v[2];

		glVertex3fv (nv);
	}

	glEnd ();
}

void DrawGLWaterPolyLightmap (glpoly_t *p)
{
	int	i;
	float	*v, temp;
	vec3_t	nv;

	if(mtexenabled)
		GL_DisableMultitexture();

	glBegin (GL_TRIANGLE_FAN);

	v = p->verts[0];

	for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE)
	{
		temp = sin(v[2]*0.05+realtime); // Cowcat

		glTexCoord2f (v[5], v[6]);

		nv[0] = v[0] + 8*sin(v[1]*0.05+realtime) * temp;
		nv[1] = v[1] + 8*sin(v[0]*0.05+realtime) * temp;
		nv[2] = v[2];
		
		glVertex3fv (nv);
	}

	glEnd ();
}

/*
================
DrawGLPoly
================
*/

#if 0 // not used in this Amiga version - Cowcat
void DrawGLPoly (glpoly_t *p)
{
	int   i;
	float *v;

	glBegin (GL_POLYGON);
	v = p->verts[0];

	for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE)
	{
		glTexCoord2f (v[3], v[4]);
		glVertex3f (v[0], v[1], v[2]);
	}

	glEnd ();
}
#endif 


#ifdef AMIGAOS

static void R_DrawMultitextureBuffer(void)
{
	GLenum src, dst;

	switch(gl_lightmap_format)
	{
		case GL_ALPHA:
			src = GL_ZERO;
			dst = GL_SRC_ALPHA;
			break;

		case GL_LUMINANCE:
		case MGL_UNSIGNED_SHORT_5_6_5:
			src = GL_ZERO;
			dst = GL_SRC_COLOR;
			break;

		default:

		#ifdef LITFILES

			if(eyecandy)
			{
				src = GL_ZERO;
				dst = GL_SRC_COLOR;
			}

			else
			{
		#endif
				src = GL_SRC_ALPHA;
				dst = GL_ONE_MINUS_SRC_ALPHA;
		#ifdef LITFILES

			}
		#endif

			break;
	}

	mglDrawMultitexBuffer(src, dst, GL_REPLACE);
}

#endif

/*
================
R_BlendLightmaps
================
*/

void R_BlendLightmaps (void)
{
	int		i;
	glpoly_t	*p;
	glRect_t	*theRect;
	int		fake_multitexture;

	if (r_fullbright.value)
		return;

	if (!gl_texsort.value)
		return;

	fake_multitexture = gl_force_fake_multitex ? 1 : (int)gl_fake_multitexture.value;

	if(fake_multitexture == 2)
	{
		return;
	}

	// there was no blending for warp water with fake_multitexture !! added dowarp check - Cowcat 2017

	if( !fake_multitexture || r_dowarp )
	{
		glDepthMask (0); // don't bother writing Z

		if (!r_lightmap.value)
			glEnable (GL_BLEND); // fix - Cowcat 2019

		#ifdef LITFILES

		if(eyecandy)
		{
			if (gl_lightmap_format == GL_ALPHA)
				glBlendFunc (GL_ZERO, GL_SRC_ALPHA);

			else
				glBlendFunc (GL_ZERO, GL_SRC_COLOR);
		}

		else
		{

		#endif
			if (gl_lightmap_format == GL_LUMINANCE || gl_lightmap_format == MGL_UNSIGNED_SHORT_5_6_5)
				glBlendFunc (GL_ZERO, GL_SRC_COLOR);

			else if (gl_lightmap_format == GL_ALPHA)
				glBlendFunc (GL_ZERO, GL_SRC_ALPHA);

		#ifdef LITFILES
		}
		#endif

		//if (!r_lightmap.value)
			//glEnable (GL_BLEND); // wrong place - Cowcat 2019

		glEnable(MGL_Z_OFFSET);
	}

	for (i=0 ; i<MAX_LIGHTMAPS ; i++)
	{
		p = lightmap_polys[i];

		if (!p)
			continue;

		GL_Bind(lightmap_textures+i);

		if (lightmap_modified[i])
		{
			#if 1

			lightmap_modified[i] = false;
			theRect = &lightmap_rectchange[i];

			//glPixelStorei(GL_UNPACK_ROW_LENGTH, BLOCK_WIDTH); // try to update minigl for this - Cowcat 2019

			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, theRect->t, 
				BLOCK_WIDTH, theRect->h, gl_lightmap_format, GL_UNSIGNED_BYTE,
				lightmaps + (i* BLOCK_HEIGHT + theRect->t) * BLOCK_WIDTH * lightmap_bytes);
			
			/*
			glTexSubImage2D(GL_TEXTURE_2D, 0, lightmap_bytes, theRect->t, // test 2019
				BLOCK_WIDTH, theRect->h, gl_lightmap_format, GL_UNSIGNED_BYTE,
				lightmaps + (i* BLOCK_HEIGHT + theRect->t) * BLOCK_WIDTH * lightmap_bytes);
			*/

			theRect->l = BLOCK_WIDTH;
			theRect->t = BLOCK_HEIGHT;
			theRect->h = 0;
			theRect->w = 0;
			
			//glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

			#else // from fodquake

			lightmap_modified[i] = false;
			theRect = &lightmap_rectchange[i];
			
			glPixelStorei(GL_UNPACK_ROW_LENGTH, BLOCK_WIDTH);

			glTexSubImage2D(GL_TEXTURE_2D, 0, theRect->l, theRect->t, 
				theRect->w, theRect->h, gl_lightmap_format, GL_UNSIGNED_BYTE,
				lightmaps + (((i * BLOCK_HEIGHT + theRect->t) * BLOCK_WIDTH) + theRect->l) * lightmap_bytes);

			glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

			theRect->l = BLOCK_WIDTH;
			theRect->t = BLOCK_HEIGHT;
			theRect->h = 0;
			theRect->w = 0;
			
			#endif
		}

		if(fake_multitexture && !r_dowarp)
			continue;  // if dowarp, we need WaterPolyLightmap, so go to the chain loop. - Cowcat

		for ( ; p ; p=p->chain)
		{
			if ((p->flags & SURF_UNDERWATER) && r_dowarp)
				DrawGLWaterPolyLightmap (p);

			else if(!fake_multitexture) // just do this for no fake_multitexture - Cowcat
			{
				#ifndef AMIGAOS

				glBegin (GL_POLYGON);
				v = p->verts[0];

				for (j=0 ; j<p->numverts ; j++, v+= VERTEXSIZE)
				{
					glTexCoord2f (v[5], v[6]);
					glVertex3f (v[0], v[1], v[2]);
				}

				glEnd ();

				#else

				glBegin(GL_POLYGON);
				mglTV23fvLM(mini_CurrentContext, VERTEXSIZE, p);
				glEnd();
				
				#endif
			}
		}
	}
	
	if(fake_multitexture && !r_dowarp) // Cowcat - dowarp check
		return;

	glDisable (MGL_Z_OFFSET);
	glDisable (GL_BLEND);

	#if 0
	#ifdef LITFILES

	if(eyecandy)
	{
		glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}

	else
	{
		if (gl_lightmap_format != GL_RGBA && gl_lightmap_format != MGL_UNSIGNED_SHORT_4_4_4_4)
			glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}

	#else

	if (gl_lightmap_format == GL_LUMINANCE || gl_lightmap_format == GL_ALPHA || gl_lightmap_format == MGL_UNSIGNED_SHORT_5_6_5)
		glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	#endif
	#endif

	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // just do this - Cowcat 2019

	glDepthMask (1);    // back to normal Z buffering
}



/*
================
R_RenderBrushPoly
================
*/

void R_RenderBrushPoly (msurface_t *fa)
{
	texture_t	*t;
	byte		*base;
	int		maps;
	glRect_t	*theRect;
	int		smax, tmax;

	c_brush_polys++;

	if (fa->flags & SURF_DRAWSKY)
	{
		// warp texture, no lightmaps
		EmitBothSkyLayers (fa);
		return;
	}

	t = R_TextureAnimation (fa->texinfo->texture);

	GL_BindCheck(t->gl_texturenum); //unit 0 - textures repeat - Cowcat 2019
	//currentpolytexture = t->gl_texturenum;

	if (fa->flags & SURF_DRAWTURB)
	{
		// warp texture, no lightmaps
		EmitWaterPolys (fa);
		return;
	}

	if ((fa->flags & SURF_UNDERWATER) && r_dowarp)
	{
		DrawGLWaterPoly (fa->polys);
	}

	else // DrawGLPoly (fa->polys)
	{
		/*
		if(fa->flags & SURF_DRAWFENCE)
		{
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			glEnable(GL_BLEND);
			glColor4f(1, 1 ,1, 0.5); // alpha //
			
		}
		*/

		if(gl_fake_multitexture.value || gl_force_fake_multitex)
		{
			glActiveTextureARB(GL_TEXTURE1_ARB);
			glEnable(GL_TEXTURE_2D);

			GL_Bind(lightmap_textures + fa->lightmaptexturenum); // bindcheck needed ? - Cowcat

			/* REAL BUG FIX (2026-08-03): this used to inherit whatever
			 * TexEnv[1] mode R_DrawMultitextureBuffer last set at the end
			 * of the PREVIOUS frame -- hardcoded GL_REPLACE there, meant
			 * for the original engine's unrelated deferred-buffer-flush
			 * semantics. On this driver, TexEnv[1]==GL_REPLACE dispatches
			 * to a real shader variant that outputs the lightmap texture
			 * ALONE, discarding the diffuse texture entirely (solid
			 * white/fullbright, since lightmaps are bright). MODULATE
			 * (diffuse * lightmap) is the actually-correct combine for
			 * lightmapping -- force it explicitly here instead of
			 * trusting leftover state. */
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

			glBegin(GL_POLYGON);
			mglMtexTV23fv(mini_CurrentContext, VERTEXSIZE, fa->polys);
			glEnd();

			glDisable(GL_TEXTURE_2D);
			glActiveTextureARB(GL_TEXTURE0_ARB);

			if(gl_fake_multitexture.value == 2)
				return; //don't modify lightmaps in any way
		}

		else
		{
			glBegin(GL_POLYGON);
			mglTV23fv(mini_CurrentContext, VERTEXSIZE, fa->polys);
			glEnd();
		}

		/*
		if(fa->flags & SURF_DRAWFENCE)
		{	
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			glDisable(GL_BLEND);
		}
		*/
	}

	// add the poly to the proper lightmap chain

	fa->polys->chain = lightmap_polys[fa->lightmaptexturenum];
	lightmap_polys[fa->lightmaptexturenum] = fa->polys;

	// check for lightmap modification
	for (maps = 0 ; maps < MAXLIGHTMAPS && fa->styles[maps] != 255 ; maps++)
	{
		if (d_lightstylevalue[fa->styles[maps]] != fa->cached_light[maps])
			goto dynamic;
	}

	// dynamic this frame or dynamic previously
	if (fa->dlightframe == r_framecount || fa->cached_dlight)
	{

	dynamic:
		if (r_dynamic.value)
		{
			lightmap_modified[fa->lightmaptexturenum] = true;
			theRect = &lightmap_rectchange[fa->lightmaptexturenum];

			if (fa->light_t < theRect->t)
			{
				if (theRect->h)
					theRect->h += theRect->t - fa->light_t;

				theRect->t = fa->light_t;
			}

			if (fa->light_s < theRect->l)
			{
				if (theRect->w)
					theRect->w += theRect->l - fa->light_s;

				theRect->l = fa->light_s;
			}

			smax = (fa->extents[0]>>4)+1;
			tmax = (fa->extents[1]>>4)+1;

			if ((theRect->w + theRect->l) < (fa->light_s + smax))
				theRect->w = (fa->light_s-theRect->l)+ smax;

			if ((theRect->h + theRect->t) < (fa->light_t + tmax))
				theRect->h = (fa->light_t-theRect->t)+ tmax;

			base = lightmaps + fa->lightmaptexturenum*lightmap_bytes*BLOCK_WIDTH*BLOCK_HEIGHT;
			//base += fa->light_t * BLOCK_WIDTH * lightmap_bytes + fa->light_s * lightmap_bytes; // test this - Cowcat 2019
			base += (fa->light_t * BLOCK_WIDTH + fa->light_s) * lightmap_bytes;

			#ifdef LITFILES

			if(eyecandy)
				R_BuildLightMapColor (fa, base, BLOCK_WIDTH*lightmap_bytes);

			else

			#endif
				R_BuildLightMap (fa, base, BLOCK_WIDTH*lightmap_bytes);	  
		}
	}
}	

/*
================
R_RenderDynamicLightmaps
Multitexture
================
*/

static void R_RenderDynamicLightmaps (msurface_t *fa)
{
	byte	  *base;
	int	  maps;
	glRect_t  *theRect;
	int	  smax, tmax;

	c_brush_polys++;

	if (fa->flags & ( SURF_DRAWSKY | SURF_DRAWTURB) )
		return;

	fa->polys->chain = lightmap_polys[fa->lightmaptexturenum];
	lightmap_polys[fa->lightmaptexturenum] = fa->polys;

	// check for lightmap modification
	for (maps = 0 ; maps < MAXLIGHTMAPS && fa->styles[maps] != 255 ; maps++) 
		if (d_lightstylevalue[fa->styles[maps]] != fa->cached_light[maps])
			goto dynamic;

	// dynamic this frame or dynamic previously
	if (fa->dlightframe == r_framecount || fa->cached_dlight)
	{

	 dynamic:
		if (r_dynamic.value)
		{
			lightmap_modified[fa->lightmaptexturenum] = true;
			theRect = &lightmap_rectchange[fa->lightmaptexturenum];

			if (fa->light_t < theRect->t)
			{
				if (theRect->h)
					theRect->h += theRect->t - fa->light_t;

				theRect->t = fa->light_t;
			}

			if (fa->light_s < theRect->l)
			{
				if (theRect->w)
					theRect->w += theRect->l - fa->light_s;

				theRect->l = fa->light_s;
			}

			smax = (fa->extents[0]>>4)+1;
			tmax = (fa->extents[1]>>4)+1;

			if ((theRect->w + theRect->l) < (fa->light_s + smax))
				theRect->w = (fa->light_s-theRect->l)+ smax;

			if ((theRect->h + theRect->t) < (fa->light_t + tmax))
				theRect->h = (fa->light_t-theRect->t)+ tmax;

			base = lightmaps + fa->lightmaptexturenum*lightmap_bytes*BLOCK_WIDTH*BLOCK_HEIGHT;
			//base += fa->light_t * BLOCK_WIDTH * lightmap_bytes + fa->light_s * lightmap_bytes; // test this - Cowcat 2019
			base += (fa->light_t * BLOCK_WIDTH + fa->light_s) * lightmap_bytes;

			#ifdef LITFILES

			if(eyecandy)
				R_BuildLightMapColor (fa, base, BLOCK_WIDTH*lightmap_bytes);

			else

			#endif
				R_BuildLightMap (fa, base, BLOCK_WIDTH*lightmap_bytes);
		}
	}
}


/*
================
R_MirrorChain
================
*/

void R_MirrorChain (msurface_t *s)
{
	if (mirror)	
		return;

	mirror = true;
	mirror_plane = s->plane;
}


#if 0
/*
================
R_DrawWaterSurfaces
================
*/
void R_DrawWaterSurfaces (void)
{
	int		i;
	msurface_t	*s;
	texture_t	*t;

	if (r_wateralpha.value == 1.0)
		return;

	//
	// go back to the world matrix
	//

	glLoadMatrixf (r_world_matrix);

	glEnable (GL_BLEND);
	glColor4f (1,1,1,r_wateralpha.value);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	for (i=0 ; i<cl.worldmodel->numtextures ; i++)
	{
		t = cl.worldmodel->textures[i];

		if (!t)
			continue;

		s = t->texturechain;

		if (!s)
			continue;

		if ( !(s->flags & SURF_DRAWTURB) )
			continue;

		// set modulate mode explicitly
		GL_Bind(t->gl_texturenum);

		for ( ; s ; s=s->texturechain)
			R_RenderBrushPoly (s);
  
		t->texturechain = NULL;
	}

	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glColor4f (1,1,1,1);
	glDisable (GL_BLEND);
}

#else

/*
================
R_DrawWaterSurfaces
================
*/

qboolean alphapass; // this is for EmitWaterPolys/gl_warp.c - Cowcat 2017

void R_DrawWaterSurfaces (void)
{
	int		i;
	msurface_t	*s;
	texture_t	*t;
	float		wateralpha;

	wateralpha = r_wateralpha.value;

	if (wateralpha == 1.0 && gl_texsort.value)
		return;

	//
	// go back to the world matrix
	//

	//glLoadMatrixf (r_world_matrix); // not needed - Cowcat 2019
	
	if (!gl_texsort.value)
	{
		if (!waterchain)
			return;

		if (wateralpha < 1.0)
		{
			glEnable (GL_BLEND);
			glColor4f (1, 1, 1, wateralpha);
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

			if( wateralpha < 0.9)
				glDepthMask(GL_FALSE);

			alphapass = true;
		}

		for ( s = waterchain ; s ; s=s->texturechain)
		{
			GL_Bind(s->texinfo->texture->gl_texturenum); // texture repeated, but few use no texsort - Cowcat 2019
			EmitWaterPolys (s);
		}
    
		waterchain = NULL;
	}

	else
	{
		if (wateralpha < 1.0 )
		{
			glEnable (GL_BLEND);
			glColor4f (1, 1, 1, wateralpha);
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

			if( wateralpha < 0.9)
				glDepthMask(GL_FALSE);

			alphapass = true;
		}

		for (i=0 ; i<cl.worldmodel->numtextures ; i++)
		{
			t = cl.worldmodel->textures[i];

			if (!t)
				continue;

			s = t->texturechain;

			if (!s)
				continue;

			if ( !(s->flags & SURF_DRAWTURB ) )
				continue;

			// set modulate mode explicitly

			/* ONE-TIME diagnostic (2026-08-05, -noextensions water/lava
			 * flat-fill investigation): checking whether the real texture
			 * handle is valid right before binding it for a water/lava
			 * surface. Read-only, no behavior change. Converted from
			 * mglv3d_log to D() (2026-09-02) -- silent in every normal
			 * build here (no file here compiles with -DDEBUG), kept in
			 * source for a future one-off debug build. */
			{
				static int s_water_diag_count = 0;
				extern int g_mglv3d_glquake_build_version;
				if (s_water_diag_count < 10)
				{
					D(("WATER TEX DIAG (build=%d): name='%s' gl_texturenum=%ld flags=%08lx gl_mtexable=%d\n",
						g_mglv3d_glquake_build_version, t->name, (long)t->gl_texturenum,
						(unsigned long)s->flags, (int)gl_mtexable));
					s_water_diag_count++;
				}
			}

			GL_Bind(t->gl_texturenum);

			for ( ; s ; s=s->texturechain)
			{
				EmitWaterPolys (s);
			}

			t->texturechain = NULL;
		}
	}
	
	if(wateralpha < 1.0)
	{	
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		glColor4f (1, 1, 1, 1);
		glDisable (GL_BLEND);

		if( wateralpha < 0.9)
			glDepthMask(GL_TRUE);

		alphapass = false;
	}
}

#endif

/*
================
DrawTextureChains
================
*/

void DrawTextureChains (void)
{
	int		i;
	msurface_t	*s;
	texture_t	*t;

	if (!gl_texsort.value)
	{
		if(mtexenabled)
			GL_DisableMultitexture();

		if (skychain)
		{
			R_DrawSkyChain(skychain);
			skychain = NULL;
		}

		return;
	} 

	for (i=0 ; i<cl.worldmodel->numtextures ; i++)
	{
		t = cl.worldmodel->textures[i];

		if (!t)
			continue;

		s = t->texturechain;

		if (!s)
			continue;
	
		if (i == skytexturenum)
			R_DrawSkyChain (s);

		// mirror stuff - Cowcat
		else if (i == mirrortexturenum && r_mirroralpha.value != 1.0 )
		{
			R_MirrorChain (s);
			continue;
		}

		else
		{
			// No translucency for teletransport/lava - Cowcat
			if ( (s->flags & SURF_DRAWTURB) && !(s->flags & (SURF_DRAWTELE | SURF_DRAWLAVA)) && r_wateralpha.value != 1.0 ) 
				continue; // draw translucent water later

			for ( ; s ; s=s->texturechain)
				R_RenderBrushPoly (s);
		}

		t->texturechain = NULL;
	}
}

/*
=============================================================

  WORLD BATCHES

=============================================================
*/

/* 2026-09-14. In the default launch (fake multitexture, so gl_texsort 0)
 * R_DrawSequentialPoly draws each visible world and brush-model surface on its
 * own: two texture binds, five state calls, glBegin(GL_POLYGON), three calls per
 * vertex and glEnd -- a GPU draw per surface. R_BatchSequentialPoly queues each of
 * those surfaces on the batch of its (texture, lightmap page) instead, and
 * R_FlushWorldBatches draws every batch with that state set once: its polygons
 * copied into one vertex array and drawn as fan-triangulated GL_TRIANGLES with
 * glDrawElements inside glLockArrays, MiniGLV3D's indexed path (the models'
 * path, GL_DrawAliasBatch), at most WORLDBATCH_MAX_VERTS vertices per draw, the
 * largest locked range that path takes.
 *
 * Hardware (static glquake v220): demo1 timedemo 79 -> 102 fps, surfaces
 * unchanged. The same triangles through glBegin(GL_TRIANGLES), the way Quake2's
 * world batching draws them (ref_gl_lib gl_rsurf.c), gave 99.
 *
 * Sky, water, warped underwater surfaces and every path other than fake
 * multitexture keep R_DrawSequentialPoly. A batch uploads its lightmap page when
 * it binds it (R_UploadLightmap), after the walk has rebuilt every surface on it. */

#define MAX_WORLDBATCHES	512
#define WORLDBATCH_MAX_VERTS	1024

typedef struct
{
	int		texnum;		// unit 0: the surface texture, animation resolved
	int		lmtex;		// unit 1: the lightmap page
	msurface_t	*chain;		// its surfaces, through texturechain
	int		next;		// 1 + the next batch in the same hash slot, 0 ends
} worldbatch_t;

static worldbatch_t	worldbatches[MAX_WORLDBATCHES];
static int		numworldbatches;
static int		worldbatchhash[256];		// 1 + the first batch in the slot, 0 empty
static float		*worldbatchverts;		// VERTEXSIZE floats per vertex
static unsigned short	*worldbatchindexes;

/*
================
R_AllocWorldBatches

The arrays a batch is copied into, allocated for the first batched surface.
Without them every surface is drawn on its own, as before.
================
*/
static qboolean R_AllocWorldBatches (void)
{
	static qboolean	failed;

	if (worldbatchverts)
		return true;

	if (failed)
		return false;

	worldbatchverts = malloc (WORLDBATCH_MAX_VERTS * VERTEXSIZE * sizeof(float));
	worldbatchindexes = malloc (WORLDBATCH_MAX_VERTS * 3 * sizeof(unsigned short));

	if (!worldbatchverts || !worldbatchindexes)
	{
		free (worldbatchverts);
		free (worldbatchindexes);
		worldbatchverts = NULL;
		worldbatchindexes = NULL;
		failed = true;
		return false;
	}

	return true;
}

/*
================
R_BatchSequentialPoly

Queues a surface that R_DrawSequentialPoly would draw through its fake
multitexture branch; draws anything else at once, as before.
================
*/
static void R_BatchSequentialPoly (msurface_t *s)
{
	texture_t	*t;
	int		slot, b;

	if (gl_mtexable || !(gl_fake_multitexture.value || gl_force_fake_multitex) ||
		(s->flags & (SURF_DRAWSKY|SURF_DRAWTURB|(r_dowarp ? SURF_UNDERWATER : 0))) ||
		!R_AllocWorldBatches ())
	{
		R_DrawSequentialPoly (s);
		return;
	}

	if (s->texinfo->texture->anim_total)
		t = R_TextureAnimation (s->texinfo->texture);

	else
		t = s->texinfo->texture;

	slot = (t->gl_texturenum * 31 + s->lightmaptexturenum) & 255;

	for (b = worldbatchhash[slot] ; b ; b = worldbatches[b-1].next)
		if (worldbatches[b-1].texnum == t->gl_texturenum && worldbatches[b-1].lmtex == s->lightmaptexturenum)
			break;

	if (!b)
	{
		if (numworldbatches == MAX_WORLDBATCHES)
		{
			R_DrawSequentialPoly (s);
			return;
		}

		b = ++numworldbatches;
		worldbatches[b-1].texnum = t->gl_texturenum;
		worldbatches[b-1].lmtex = s->lightmaptexturenum;
		worldbatches[b-1].chain = NULL;
		worldbatches[b-1].next = worldbatchhash[slot];
		worldbatchhash[slot] = b;
	}

	R_RenderDynamicLightmaps (s);

	s->texturechain = worldbatches[b-1].chain;
	worldbatches[b-1].chain = s;
}

/*
================
R_FlushWorldBatches

Draws and empties the batches R_BatchSequentialPoly queued.
================
*/
static void R_FlushWorldBatches (void)
{
	int		b;

	if (!numworldbatches)
		return;

	glVertexPointer (3, GL_FLOAT, VERTEXSIZE * sizeof(float), worldbatchverts);
	glTexCoordPointer (2, GL_FLOAT, VERTEXSIZE * sizeof(float), worldbatchverts + 3);
	glEnableClientState (GL_TEXTURE_COORD_ARRAY);
	glClientActiveTextureARB (GL_TEXTURE1_ARB);
	glTexCoordPointer (2, GL_FLOAT, VERTEXSIZE * sizeof(float), worldbatchverts + 5);
	glEnableClientState (GL_TEXTURE_COORD_ARRAY);
	glClientActiveTextureARB (GL_TEXTURE0_ARB);
	glEnableClientState (GL_VERTEX_ARRAY);

	for (b=0 ; b<numworldbatches ; b++)
	{
		msurface_t	*s;
		int		nv = 0, ni = 0;

		// the state R_DrawSequentialPoly's fake multitexture branch sets per surface,
		// after selecting unit 0: its underwater branch leaves unit 1 active
		glActiveTextureARB (GL_TEXTURE0_ARB);
		GL_Bind (worldbatches[b].texnum);
		glActiveTextureARB (GL_TEXTURE1_ARB);
		glEnable (GL_TEXTURE_2D);
		GL_Bind (lightmap_textures + worldbatches[b].lmtex);
		R_UploadLightmap (worldbatches[b].lmtex);	// every surface on the page is rebuilt by now
		glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

		for (s = worldbatches[b].chain ; s ; s = s->texturechain)
		{
			glpoly_t	*p = s->polys;
			int		n = p->numverts;
			int		j;

			if (n < 3)
				continue;

			if (n > WORLDBATCH_MAX_VERTS)
			{
				// more vertices than one draw holds: this polygon on its own
				glBegin (GL_POLYGON);
				mglMtexTV23fv (mini_CurrentContext, VERTEXSIZE, p);
				glEnd ();
				continue;
			}

			if (nv + n > WORLDBATCH_MAX_VERTS)
			{
				glLockArrays (0, nv);
				glDrawElements (GL_TRIANGLES, ni, GL_UNSIGNED_SHORT, worldbatchindexes);
				glUnlockArrays ();
				nv = ni = 0;
			}

			memcpy (worldbatchverts + nv * VERTEXSIZE, p->verts[0], n * VERTEXSIZE * sizeof(float));

			for (j=2 ; j<n ; j++)
			{
				worldbatchindexes[ni++] = nv;
				worldbatchindexes[ni++] = nv + j - 1;
				worldbatchindexes[ni++] = nv + j;
			}

			nv += n;
		}

		if (ni)
		{
			glLockArrays (0, nv);
			glDrawElements (GL_TRIANGLES, ni, GL_UNSIGNED_SHORT, worldbatchindexes);
			glUnlockArrays ();
		}

		glDisable (GL_TEXTURE_2D);
		glActiveTextureARB (GL_TEXTURE0_ARB);
	}

	// left on, the unit 1 array would be gathered for every later array draw
	glClientActiveTextureARB (GL_TEXTURE1_ARB);
	glDisableClientState (GL_TEXTURE_COORD_ARRAY);
	glClientActiveTextureARB (GL_TEXTURE0_ARB);
	glDisableClientState (GL_TEXTURE_COORD_ARRAY);
	glDisableClientState (GL_VERTEX_ARRAY);

	numworldbatches = 0;
	memset (worldbatchhash, 0, sizeof(worldbatchhash));
}

extern qboolean wireframe;
extern void R_DrawLinePolys (msurface_t *s);
#define DIST_EPSILON (0.03125)

/*
=================
R_DrawBrushModel
=================
*/

extern qboolean R_CullSphere (vec3_t centre, float radius); // gl_rmain.c
extern void R_PushDlights2(struct mnode_s *headnode);

void R_DrawBrushModel (entity_t *e)
{
	int		i;
	vec3_t		mins, maxs;
	msurface_t	*psurf;
	float		dot;
	mplane_t	*pplane;
	model_t		*clmodel;
	qboolean	rotated;

	currententity = e;
	currenttexture = -1;

	clmodel = e->model;

	#if 1

	if (e->angles[0] || e->angles[1] || e->angles[2])
	{
		rotated = true;

		for (i=0 ; i<3 ; i++)
		{
			mins[i] = e->origin[i] - clmodel->radius;
			maxs[i] = e->origin[i] + clmodel->radius;
		}
	}

	else
	{
		rotated = false;
		VectorAdd (e->origin, clmodel->mins, mins);
		VectorAdd (e->origin, clmodel->maxs, maxs);
	}

	if (R_CullBox (mins, maxs))
		return;

	#else // test 2019 - Cowcat

	if (e->angles[0] || e->angles[1] || e->angles[2])
	{
		rotated = true;

		if(R_CullSphere (e->origin, clmodel->radius))
			return;
	}

	else
	{
		rotated = false;
		VectorAdd (e->origin, clmodel->mins, mins);
		VectorAdd (e->origin, clmodel->maxs, maxs);

		if (R_CullBox (mins, maxs))
			return;
	}

	#endif

	//glColor3f (1,1,1); - not needed because of entity-sorting

	memset (lightmap_polys, 0, sizeof(lightmap_polys));

	VectorSubtract (r_refdef.vieworg, e->origin, modelorg);

	if (rotated)
	{
		vec3_t	temp, forward, right, up;

		VectorCopy (modelorg, temp);
		AngleVectors (e->angles, forward, right, up);
		modelorg[0] = DotProduct (temp, forward);
		modelorg[1] = -DotProduct (temp, right);
		modelorg[2] = DotProduct (temp, up);
	}

	psurf = &clmodel->surfaces[clmodel->firstmodelsurface];

	// calculate dynamic lighting for bmodel if it's not an
	// instanced model

	#if 0
	if (clmodel->firstmodelsurface != 0 && !gl_flashblend.value)
	{
		for (k=0 ; k<MAX_DLIGHTS ; k++)
		{
			if ((cl_dlights[k].die < cl.time) || (!cl_dlights[k].radius))
				continue;

			//R_MarkLights (&cl_dlights[k], 1<<k, clmodel->nodes + clmodel->hulls[0].firstclipnode);
			R_MarkLights (&cl_dlights[k], k, clmodel->nodes + clmodel->hulls[0].firstclipnode); // new 2019 - Cowcat
		}
	}

	#else // mh cleanup - Cowcat 2019

	if (clmodel->firstmodelsurface != 0 && !gl_flashblend.value)
		R_PushDlights2(clmodel->nodes + clmodel->hulls[0].firstclipnode);

	#endif

	glPushMatrix ();

	//e->origin[0] -= DIST_EPSILON;
	//e->origin[1] -= DIST_EPSILON;
	//e->origin[2] -= DIST_EPSILON;
	
	#if 0

	e->angles[0] = -e->angles[0]; // stupid quake bug
	R_RotateForEntity (e);
	e->angles[0] = -e->angles[0]; // stupid quake bug
	
	#else // Cowcat 2017

	//if(e->origin[0] || e->origin[1] || e->origin[2])
	{
		//Con_Printf("translate\n");
		glTranslatef (e->origin[0],  e->origin[1],  e->origin[2]);
	}

	if(rotated) // never met condition ?? - Cowcat
	{
		//Con_Printf("rotated true\n");
		glRotatefEXT(e->angles[1], GLROT_001);
		glRotatefEXT(e->angles[0], GLROT_010); // stupid quake bug
		glRotatefEXT(e->angles[2], GLROT_100);
	}
	#endif

	//e->origin[0] += DIST_EPSILON;
	//e->origin[1] += DIST_EPSILON;
	//e->origin[2] += DIST_EPSILON;

	//
	// draw texture
	//

	for (i=0 ; i<clmodel->nummodelsurfaces ; i++, psurf++)
	{
		// find which side of the node we are on

		pplane = psurf->plane;
		
		dot = DotProduct (modelorg, pplane->normal) - pplane->dist;
		//dot = PlaneDiff (modelorg, pplane); // test 2019 - Cowcat

		// draw the polygon
		if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
			(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
		{
			/*
			if(wireframe)
			{
				R_DrawLinePolys(psurf);
				continue;
			}
			*/

			if(gl_wireonly.value)
			{
				R_DrawLinePolys(psurf);
				continue;
			}

			if (gl_texsort.value)
			{
				R_RenderBrushPoly (psurf);
				//c_brush_polys++; // why here? - Cowcat
			}

			else
				R_BatchSequentialPoly (psurf);

			if(gl_showpolys.value)
				R_DrawLinePolys(psurf);
		}
	}

	R_FlushWorldBatches ();		// under this model's matrix

	R_BlendLightmaps ();

	glPopMatrix ();
}


/*
=============================================================

  WORLD MODEL

=============================================================
*/


/*
================
R_RecursiveWorldNode
================
*/

void R_RecursiveWorldNode (mnode_t *node)
{
	int		c, side;
	mplane_t	*plane; 
	msurface_t	*surf, **mark;
	mleaf_t		*pleaf;
	double		dot;

loc0:
	if (node->contents == CONTENTS_SOLID)
		return;	  // solid

	if (node->visframe != r_visframecount)
		return;

	if (R_CullBox (node->mins, node->maxs)) // was (node->minmaxs, node->minmaxs+3)
		return;

	// if a leaf node, draw stuff
	if (node->contents < 0)
	{
		pleaf = (mleaf_t *)node;

		mark = pleaf->firstmarksurface;
		c = pleaf->nummarksurfaces;

		if (c)
		{
			do
			{
				(*mark)->visframe = r_framecount;
				mark++;

			} while (--c);
		}

		// deal with model fragments in this leaf
		if (pleaf->efrags)
			R_StoreEfrags (&pleaf->efrags);

		return;
	}

	// node is just a decision point, so go down the apropriate sides

	// find which side of the node we are on
	plane = node->plane;
	dot = PlaneDiff(modelorg, plane);

	if (dot >= 0)
		side = 0;

	else
		side = 1;

	// recurse down the children, front side first
	// R_RecursiveWorldNode (node->children[side]);

	if(node->children[side]->contents != CONTENTS_SOLID && node->children[side]->visframe == r_visframecount)
	{	
		R_RecursiveWorldNode (node->children[side]);
	}

	// draw stuff

	c = node->numsurfaces;

	if (c)
	{
		surf = cl.worldmodel->surfaces + node->firstsurface;

		if (dot < 0 -BACKFACE_EPSILON)
			side = SURF_PLANEBACK;

		else if (dot > BACKFACE_EPSILON)
			side = 0;	

		for ( ; c ; c--, surf++)
		{
			if (surf->visframe != r_framecount)
				continue;

			// don't backface underwater surfaces, because they warp
			//surgeon: if no warping, then backface annyway

			if( !(surf->flags & (r_dowarp ? SURF_UNDERWATER : 0)) && ( (dot < 0) ^ !!(surf->flags & SURF_PLANEBACK)) )
				continue;   // wrong side
			
			/*
			if(wireframe)
			{
				R_DrawLinePolys(surf);
				continue;
			}
			*/

			if(gl_wireonly.value)
			{
				R_DrawLinePolys(surf);
				continue;
			}

			// if sorting by texture, just store it out
			if (gl_texsort.value)
			{
				if ( !mirror || surf->texinfo->texture != cl.worldmodel->textures[mirrortexturenum] )
				{
					surf->texturechain = surf->texinfo->texture->texturechain;
					surf->texinfo->texture->texturechain = surf;
				}
			}

			else if (surf->flags & SURF_DRAWSKY)
			{
				surf->texturechain = skychain;
				skychain = surf;
			}

			else if (surf->flags & SURF_DRAWTURB)
			{
				surf->texturechain = waterchain;
				waterchain = surf;
			}

			else
				R_BatchSequentialPoly (surf);

			if(gl_showpolys.value)
				R_DrawLinePolys(surf);
		}
	}

	// recurse down the back side

	#if 0

	R_RecursiveWorldNode (node->children[!side]);

	#else

	node = node->children[!side];

	goto loc0;

	#endif
}

extern qboolean r_skybox;

/*
=============
R_DrawWorld
=============
*/

void R_DrawWorld (void)
{
	entity_t	ent;

	memset (&ent, 0, sizeof(ent));
	ent.model = cl.worldmodel;

	VectorCopy (r_refdef.vieworg, modelorg);

	currententity = &ent;
	currenttexture = -1;

	glColor3f (1,1,1); // needed? Cowcat 2019

	memset (lightmap_polys, 0, sizeof(lightmap_polys));

	if(r_skybox)
		R_ClearSkyBox ();

	R_RecursiveWorldNode (cl.worldmodel->nodes);

	R_FlushWorldBatches ();		// the walk's queued surfaces, still ahead of the sky and water

	#if 0
	if(gl_wireframe.value)
	{
		wireframe = true;
		glDisable(GL_DEPTH_TEST);
		R_RecursiveWorldNode (cl.worldmodel->nodes);
		glEnable(GL_DEPTH_TEST);
		wireframe = false;
	}
	#endif

	DrawTextureChains ();
	
	R_BlendLightmaps ();

	/* REAL BUG FIX, 2nd pass (white-texture investigation, 2026-08-03):
	 * changing gl_fake_multitexture's compiled-in default (above, this
	 * file) to "0" was NOT sufficient by itself -- this cvar has
	 * `archive=true` (see cvar.h), meaning any PRIOR run's saved vars.rc
	 * (on the real Amiga's runtime filesystem, invisible from source)
	 * already has "gl_fake_multitexture 1" archived from before this fix
	 * existed, and silently overrides the new compiled default on every
	 * launch -- hardware-confirmed via MiniGLV3D's own new
	 * MGLDRAWMULTITEXBUFFER call-log (43 calls still logged after the
	 * cvar-default-only fix). `gl_mtexable` (gl_vidamiga.c) is a real
	 * hardware-capability flag, not a user-settable cvar, so it can't be
	 * silently overridden by a stale config file -- gating on it directly
	 * makes this robust regardless of any cvar/vars.rc state. */
	if((gl_fake_multitexture.value || gl_force_fake_multitex) && !gl_mtexable)
		R_DrawMultitextureBuffer();

	if(r_skybox)
	{
		if(fog) // Cowcat way
			glDisable(GL_FOG);

		R_DrawSkyBox ();

		if(fog)
			glEnable(GL_FOG);
	}
}


/*
===============
R_MarkLeaves
===============
*/

//extern byte *Mod_NoVisPVS(model_t *model);

void R_MarkLeaves (void)
{
	byte	*vis;
	mnode_t *node;
	int	i;
	byte	solid[4096]; 
	
	if (r_oldviewleaf == r_viewleaf && !r_novis.value)
		return;

	if(mirror)
		return;

	r_visframecount++;
	r_oldviewleaf = r_viewleaf;

	if ( r_novis.value )
	//if ( r_novis.value || r_viewleaf->contents == CONTENTS_SOLID || r_viewleaf->contents == CONTENTS_SKY ) // Cowcat 2019
	{
		vis = solid;
		memset (solid, 0xff, (cl.worldmodel->numleafs + 7) >>3);
		//vis = Mod_NoVisPVS(cl.worldmodel); // Cowcat 2019 - attempt to use realloc leafs QuakeSpasm source mod.
	}

	else
		vis = Mod_LeafPVS (r_viewleaf, cl.worldmodel);
    
	for (i=0 ; i<cl.worldmodel->numleafs ; i++)
	{
		if ( vis[i>>3] & (1 << (i&7)) )
		{
			node = (mnode_t *)&cl.worldmodel->leafs[i+1];

			do
			{
				if (node->visframe == r_visframecount)
					break;

				node->visframe = r_visframecount;
				node = node->parent;

			} while (node);
		}
	}
}


/*
=============================================================================

  LIGHTMAP ALLOCATION

=============================================================================
*/

// returns a texture number and the position inside it

int AllocBlock (int w, int h, int *x, int *y)
{
	int   i, j, best, best2, texnum;

	for (texnum=0 ; texnum<MAX_LIGHTMAPS ; texnum++)
	//for (texnum=last_lightmap_updated ; texnum<MAX_LIGHTMAPS ; texnum++, last_lightmap_updated++) // ezquake
	{
		best = BLOCK_HEIGHT;
		//best = BLOCK_HEIGHT + 1; // ezquake

		for (i=0 ; i<BLOCK_WIDTH - w ; i++)
		{
			best2 = 0;

			#if 1

			for (j=0 ; j<w ; j++)
			{
				if (allocated[texnum][i+j] >= best)
					break;

				if (allocated[texnum][i+j] > best2)
					best2 = allocated[texnum][i+j];
			}

			if (j == w)
			{
				// this is a valid spot
				*x = i;
				*y = best = best2;
			}

			#else // ezquake

			for (j=i ; j < i + w ; j++)
			{
				if (allocated[texnum][j] >= best)
				{	
					i = j;
					break;
				}

				if (allocated[texnum][j] > best2)
					best2 = allocated[texnum][j];
			}

			if (j == i + w)
			{
				// this is a valid spot
				*x = i;
				*y = best = best2;
			}

			#endif
		}

		if (best + h > BLOCK_HEIGHT)
			continue;

		for (i=0 ; i<w ; i++)
			allocated[texnum][*x + i] = best + h;

		return texnum;
	}

	//Sys_Error ("AllocBlock: full");
	Host_Error("AllocBlock: full, %i unable to find room for %i by %i lightmap", texnum, w, h); // new 2019 - Cowcat
	return 0;
}


mvertex_t *r_pcurrentvertbase;
model_t	  *currentmodel;
int nColinElim;

/*
================
BuildSurfaceDisplayList
================
*/

void BuildSurfaceDisplayList (msurface_t *fa) // modded Cowcat - Called only on level load
{	
	int	 i, lindex, lnumverts;		
	medge_t	 *pedges, *r_pedge;
	float	 *vec, s, t;
	glpoly_t *poly;

	// reconstruct the polygon
	pedges = currentmodel->edges;
	lnumverts = fa->numedges;

	//
	// draw texture
	//

	poly = Hunk_Alloc (sizeof(glpoly_t) + (lnumverts-4) * VERTEXSIZE*sizeof(float));
	poly->next = fa->polys;
	poly->flags = fa->flags; // don�t remove (issues with waterwarp !) - Cowcat
	fa->polys = poly;
	poly->numverts = lnumverts;

	for (i=0 ; i<lnumverts ; i++)
	{
		lindex = currentmodel->surfedges[fa->firstedge + i];

		if (lindex > 0)
		{
			r_pedge = &pedges[lindex];
			vec = r_pcurrentvertbase[r_pedge->v[0]].position;
		}

		else
		{
			r_pedge = &pedges[-lindex];
			vec = r_pcurrentvertbase[r_pedge->v[1]].position;
		}

		#if 1

		s = DotProduct (vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		//s /= fa->texinfo->texture->width;

		t = DotProduct (vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		//t /= fa->texinfo->texture->height;

		VectorCopy (vec, poly->verts[i]);
		poly->verts[i][3] = s / fa->texinfo->texture->width;
		poly->verts[i][4] = t / fa->texinfo->texture->height;

		//
		// lightmap texture coordinates
		//

		//s = DotProduct (vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s -= fa->texturemins[0];
		s += fa->light_s*16;
		s += 8;
		s /= BLOCK_WIDTH*16;	//fa->texinfo->texture->width;

		//t = DotProduct (vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t -= fa->texturemins[1];
		t += fa->light_t*16;
		t += 8;
		t /= BLOCK_HEIGHT*16;	//fa->texinfo->texture->height;

		poly->verts[i][5] = s;
		poly->verts[i][6] = t;

		#else

		s = DotProduct (vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s /= fa->texinfo->texture->width;

		t = DotProduct (vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t /= fa->texinfo->texture->height;

		VectorCopy (vec, poly->verts[i]);
		poly->verts[i][3] = s;
		poly->verts[i][4] = t;

		//
		// lightmap texture coordinates
		//

		s = DotProduct (vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s -= fa->texturemins[0];
		s += fa->light_s*16;
		s += 8;
		s /= BLOCK_WIDTH*16;	//fa->texinfo->texture->width;

		t = DotProduct (vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t -= fa->texturemins[1];
		t += fa->light_t*16;
		t += 8;
		t /= BLOCK_HEIGHT*16;	//fa->texinfo->texture->height;

		poly->verts[i][5] = s;
		poly->verts[i][6] = t;

		#endif
	}

	//
	// remove co-linear points - Ed
	//

	if (!gl_keeptjunctions.value && !(fa->flags & (r_dowarp ? SURF_UNDERWATER : 0)))
	{
		for (i = 0 ; i < lnumverts ; ++i)
		{
			vec3_t v1, v2;
			float  *prev, *this, *next;

			prev = poly->verts[(i + lnumverts - 1) % lnumverts];
			this = poly->verts[i];
			next = poly->verts[(i + 1) % lnumverts];

			VectorSubtract( this, prev, v1 );
			VectorNormalize( v1 );
			VectorSubtract( next, prev, v2 );
			VectorNormalize( v2 );

			// skip co-linear points
			#define COLINEAR_EPSILON 0.001

			if ((fabs( v1[0] - v2[0] ) <= COLINEAR_EPSILON) &&
				(fabs( v1[1] - v2[1] ) <= COLINEAR_EPSILON) && 
				(fabs( v1[2] - v2[2] ) <= COLINEAR_EPSILON))
			{
				int j;

				for (j = i + 1; j < lnumverts; ++j)
				{
					int k;

					for (k = 0; k < VERTEXSIZE; ++k)
						poly->verts[j - 1][k] = poly->verts[j][k];
				}

				--lnumverts;
				++nColinElim;
				// retry next vertex next time, which is now current vertex
				--i;
			}
		}
	}

	poly->numverts = lnumverts;
}

/*
========================
GL_CreateSurfaceLightmap
========================
*/

static void GL_CreateSurfaceLightmap (msurface_t *surf)
{
	int   smax, tmax;
	byte  *base;

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;

	surf->lightmaptexturenum = AllocBlock (smax, tmax, &surf->light_s, &surf->light_t);

	base = lightmaps + surf->lightmaptexturenum*lightmap_bytes*BLOCK_WIDTH*BLOCK_HEIGHT;
	base += (surf->light_t * BLOCK_WIDTH + surf->light_s) * lightmap_bytes;

	#ifdef LITFILES

	if(eyecandy)
		R_BuildLightMapColor (surf, base, BLOCK_WIDTH*lightmap_bytes);

	else
	#endif
		R_BuildLightMap (surf, base, BLOCK_WIDTH*lightmap_bytes);
}


/*
==================
GL_BuildLightmaps

Builds the lightmap texture
with all the surfaces from all brush models
==================
*/

void GL_BuildLightmaps (void) // only used at level change - cowcat 2019
{
	int	i, j;
	model_t *m;
	//extern qboolean lmf_isset;	//set in gl_vidamiga.c // ???
  
	memset (allocated, 0, sizeof(allocated));

	//last_lightmap_updated = 0; // ezquake

	r_framecount = 1;		// no dlightcache : was disabled by massimiliano!

	#ifndef AMIGAOS
	if (!lightmap_textures)
	{
		lightmap_textures = texture_extension_number;
		texture_extension_number += MAX_LIGHTMAPS;
	}
	#endif

	#ifdef LITFILES

	if (COM_CheckParm ("-litfiles"))
		eyecandy = true;

	#endif

	#ifndef AMIGAOS
	gl_lightmap_format = GL_RGBA;
	#else
	gl_lightmap_format = MGL_UNSIGNED_SHORT_4_4_4_4;
	#endif

	if (COM_CheckParm ("-lm_LUMINANCE"))
		gl_lightmap_format = GL_LUMINANCE;

	else if (COM_CheckParm ("-lm_ALPHA"))
		gl_lightmap_format = GL_ALPHA;

	else if (COM_CheckParm ("-lm_RGBA"))
		gl_lightmap_format = GL_RGBA;

	else if (COM_CheckParm ("-lm_RGB"))
		gl_lightmap_format = MGL_UNSIGNED_SHORT_5_6_5;

	switch (gl_lightmap_format)
	{
		case GL_RGBA:
			lightmap_bytes = 4;
			break;

		case MGL_UNSIGNED_SHORT_4_4_4_4:
		case MGL_UNSIGNED_SHORT_5_6_5:
			lightmap_bytes = 2;
			break;

		case GL_LUMINANCE:
		case GL_ALPHA:
			lightmap_bytes = 1;
			break;
	}

	for (j=1 ; j<MAX_MODELS ; j++)
	{
		m = cl.model_precache[j];

		if (!m)
			break;

		if (m->name[0] == '*')
			continue;

		r_pcurrentvertbase = m->vertexes;
		currentmodel = m;

		for (i=0 ; i<m->numsurfaces ; i++)
		{
			if ( m->surfaces[i].flags & SURF_DRAWTURB )
				continue;

			#ifndef QUAKE2
			if(!r_skybox)
				if ( m->surfaces[i].flags & SURF_DRAWSKY )
					continue;
			#endif

			GL_CreateSurfaceLightmap (m->surfaces + i);
			BuildSurfaceDisplayList (m->surfaces + i);
		}
	}

	if (gl_mtexable)
		glActiveTextureARB(GL_TEXTURE1_ARB);

	//
	// upload all lightmaps that were filled
	//

	for (i=0 ; i<MAX_LIGHTMAPS ; i++)
	{
		if (!allocated[i][0])
			break;	  // no more used

		lightmap_modified[i] = false;
		lightmap_rectchange[i].l = BLOCK_WIDTH;
		lightmap_rectchange[i].t = BLOCK_HEIGHT;
		lightmap_rectchange[i].w = 0;
		lightmap_rectchange[i].h = 0;

		GL_Bind(lightmap_textures + i);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexImage2D (GL_TEXTURE_2D, 0, gl_lightmap_format, BLOCK_WIDTH, BLOCK_HEIGHT, 0, 
			gl_lightmap_format, GL_UNSIGNED_BYTE, lightmaps+(i*BLOCK_WIDTH*BLOCK_HEIGHT*lightmap_bytes));
	}

	if (gl_mtexable)
		glActiveTextureARB(GL_TEXTURE0_ARB);
}

