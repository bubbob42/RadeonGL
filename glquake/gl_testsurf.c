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
#include "miniglext.h"	//optimized vertexbuffer construct

cvar_t gl_fake_multitexture = {"gl_fake_multitexture","1", true};

extern void	R_DrawSkyBox();
//extern cvar_t gl_fog;
extern qboolean fog; 		// Cowcat
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

#ifndef AMIGA
int   lightmap_textures;
#else

/*
** surgeon: we set a static texnum-base for lightmaps in
** order to use narrow-range multitexture-sorting for TMU1
** with MiniGL
*/
int   lightmap_textures = 1024;
#endif

#ifdef LITFILES
qboolean eyecandy = false;
unsigned blocklights[18*18*3]; //LordHavoc
#else
unsigned blocklights[18*18];
#endif

#define BLOCK_WIDTH   128
#define BLOCK_HEIGHT  128

#define MAX_LIGHTMAPS 64

static	int  active_lightmaps; /*surgeon:static*/

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

void R_RenderDynamicLightmaps (msurface_t *fa);

//void DrawGLPoly(glpoly_t *p); //

//static int last_lightmap_updated; // ezquake


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
		if ( !(surf->dlightbits & (1<<lnum) ) )
			continue;		// not lit by this light

		rad = cl_dlights[lnum].radius;
		dist = DotProduct (cl_dlights[lnum].origin, surf->plane->normal) - surf->plane->dist;
		rad -= fabs(dist);
		minlight = cl_dlights[lnum].minlight;

		if (rad < minlight)
			continue;

		minlight = rad - minlight;

		for (i=0 ; i<3 ; i++)
		{
			impact[i] = cl_dlights[lnum].origin[i] - surf->plane->normal[i]*dist;
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

			for (t = 0 ; t<tmax ; t++)
			{
				td = local[1] - t*16;

				if (td < 0)
					td = -td;

				for (s=0 ; s<smax ; s++)
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

						bl[0] += (brightness * cred);
						bl[1] += (brightness * cgreen);
						bl[2] += (brightness * cblue);
					}

					bl += 3;
				}
			}

		}

		else
		{
		#endif

			for (t = 0 ; t<tmax ; t++)
			{
				td = local[1] - t*16;

				if (td < 0)
					td = -td;

				for (s=0 ; s<smax ; s++)
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
						blocklights[t*smax + s] += (rad - dist)*256; 
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
	int		smax, tmax;
	int		t;
	int		i, j, size;
	byte		*lightmap;
	unsigned	scale;
	int		maps;
	int		lightadj[4];
	unsigned	*bl;
	unsigned	r,g,b;		//surgeon
	UWORD		*short_dest;	//surgeon

	surf->cached_dlight = (surf->dlightframe == r_framecount);

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;
	size = smax*tmax;
	lightmap = surf->samples;

	// set to full bright if no light data
	if (r_fullbright.value || !cl.worldmodel->lightdata)
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
	bl = blocklights;

	for (i=0 ; i<size ; i++)
	{
		*bl++ = 0;
		*bl++ = 0;
		*bl++ = 0;
	}

 
	// add all the lightmaps 
	if (lightmap) 
	{ 
		for (maps = 0;maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++) 
		{ 
			scale = d_lightstylevalue[surf->styles[maps]]; 
			surf->cached_light[maps] = scale;	// 8.8 fraction 
			bl = blocklights;

			for (i=0; i<size; i++)
			{ 
				*bl++ += *lightmap++ * scale;
				*bl++ += *lightmap++ * scale;
				*bl++ += *lightmap++ * scale;
			}
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

			for (i=0 ; i<tmax ; i++, dest += stride)
			{
				for (j=0 ; j<smax ; j++)
				{
					t = *bl++ >> 7; if (t > 255) t = 255;
					*dest++ = t;
					t = *bl++ >> 7; if (t > 255) t = 255;
					*dest++ = t;
					t = *bl++ >> 7; if (t > 255) t = 255;
					*dest++ = t;
					*dest++ = 255;
				}
				// LordHavoc: .lit support end
			}

			break;

		case GL_ALPHA:
		case GL_LUMINANCE:
			bl = blocklights;

			for (i=0 ; i<tmax ; i++, dest += stride)
			{
				for (j=0 ; j<smax ; j++)
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

			for (i=0 ; i<tmax ; i++, dest += stride)
			{
				for (j=0 ; j<smax ; j++)
				{
					r = *bl++ >> 7; if (r > 255) r = 255;
					g = *bl++ >> 7; if (g > 255) g = 255;
					b = *bl++ >> 7; if (b > 255) b = 255;

					short_dest = (UWORD*)dest;
					*short_dest = ((0xf0<<8) | ((r & 0xf0) << 4) | (g&0xf0) | ((b&0xf0)>>4));
					dest += 2;
				}
			}

			break;

		case MGL_UNSIGNED_SHORT_5_6_5: //by Surgeon
			bl = blocklights;
			stride -= (smax << 1);

			for (i=0 ; i<tmax ; i++, dest += stride)
			{
				for (j=0 ; j<smax ; j++)
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
	int		smax, tmax;
	int		t;
	int		i, j, size;
	byte		*lightmap;
	unsigned	scale;
	int		maps;
	int		lightadj[4];
	unsigned	*bl;
	UWORD		*short_dest; //surgeon

	surf->cached_dlight = (surf->dlightframe == r_framecount);

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;
	size = smax*tmax;
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
		for (maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
		{
			scale = d_lightstylevalue[surf->styles[maps]];
			surf->cached_light[maps] = scale;	// 8.8 fraction 

			for (i=0 ; i<size ; i++)					
				blocklights[i] += lightmap[i] * scale;

			lightmap += size;			// skip to next lightmap 
		}

	// add all the dynamic lights
	if (surf->dlightframe == r_framecount)
		R_AddDynamicLights (surf);

	// bound, invert, and shift
store:
	switch (gl_lightmap_format)
	{
		case GL_RGBA:
			stride -= (smax<<2);
			bl = blocklights;

			for (i=0 ; i<tmax ; i++, dest += stride)
			{
				for (j=0 ; j<smax ; j++)
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

			for (i=0 ; i<tmax ; i++, dest += stride)
			{
				for (j=0 ; j<smax ; j++)
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
			stride -= (smax<<1);
			bl = blocklights;

			for (i=0 ; i<tmax ; i++, dest += stride)
			{
				for (j=0 ; j<smax ; j++)
				{
					t = *bl++;
					t >>= 7;

					if (t > 255)
						t = 0;

					else	  
						t = (255-t) & 0xf0;

					dest[0] = t;	  

					dest += 2;
				}
			}

			break;

		case MGL_UNSIGNED_SHORT_5_6_5: //by Surgeon
			stride -= (smax<<1);
			bl = blocklights;

			for (i=0 ; i<tmax ; i++, dest += stride)
			{
				for (j=0 ; j<smax ; j++)
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


void R_UploadLightMap (int lightmapnum)
{
	glRect_t *theRect;

	lightmap_modified[lightmapnum] = false;
	theRect = &lightmap_rectchange[lightmapnum];

	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, theRect->t, BLOCK_WIDTH, theRect->h, gl_lightmap_format, GL_UNSIGNED_BYTE,
		lightmaps + (lightmapnum * BLOCK_HEIGHT + theRect->t) * BLOCK_WIDTH * lightmap_bytes);


	theRect->l = BLOCK_WIDTH;
	theRect->t = BLOCK_HEIGHT;
	theRect->h = 0;
	theRect->w = 0;
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
	vec3_t		nv, dir;
	float		ss, ss2, length;
	float		s1, t1, temp; // temp Cowcat
	//glRect_t	*theRect;

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
			{	/*
				lightmap_modified[i] = false;
				theRect = &lightmap_rectchange[i];

				#if 1
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, theRect->t, 
					BLOCK_WIDTH, theRect->h, gl_lightmap_format, GL_UNSIGNED_BYTE,
					lightmaps+(i* BLOCK_HEIGHT + theRect->t) *BLOCK_WIDTH*lightmap_bytes);

				#else

				glPixelStorei(GL_UNPACK_ROW_LENGTH, BLOCK_WIDTH);

				glTexSubImage2D(GL_TEXTURE_2D, 0, theRect->l, theRect->t, 
				theRect->w, theRect->h, gl_lightmap_format, GL_UNSIGNED_BYTE,
				lightmaps+(i + theRect->t) * BLOCK_WIDTH * lightmap_bytes + theRect->l * lightmap_bytes);

				glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

				#endif

				theRect->l = BLOCK_WIDTH;
				theRect->t = BLOCK_HEIGHT;
				theRect->h = 0;
				theRect->w = 0;
				*/

				R_UploadLightMap(i);
			}

			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);  //unit 1

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

			GL_Bind (t->gl_texturenum);

			#ifndef AMIGA

			glBegin (GL_POLYGON);
			v = p->verts[0];

			for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE)
			{
				glTexCoord2f (v[3], v[4]);
				glVertex3f (v[0], v[1], v[2]);
			}

			glEnd ();

			GL_Bind (lightmap_textures + s->lightmaptexturenum);

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

			if(gl_fake_multitexture.value)
			{
				glActiveTextureARB(GL_TEXTURE1_ARB);
				glEnable(GL_TEXTURE_2D);
				GL_Bind(lightmap_textures + s->lightmaptexturenum);

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

				GL_Bind (lightmap_textures + s->lightmaptexturenum);

				glEnable (GL_BLEND);

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

	if (gl_mtexable)
	{
		p = s->polys;

		t = R_TextureAnimation (s->texinfo->texture);

		glActiveTextureARB(GL_TEXTURE0_ARB);

		GL_Bind (t->gl_texturenum);
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

		GL_EnableMultitexture();
		GL_Bind (lightmap_textures + s->lightmaptexturenum);

		i = s->lightmaptexturenum;

		if (lightmap_modified[i])
		{
			/*
			lightmap_modified[i] = false;
			theRect = &lightmap_rectchange[i];

			#if 1
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, theRect->t, 
				BLOCK_WIDTH, theRect->h, gl_lightmap_format, GL_UNSIGNED_BYTE,
				lightmaps+(i* BLOCK_HEIGHT + theRect->t) *BLOCK_WIDTH*lightmap_bytes);

			#else

			glPixelStorei(GL_UNPACK_ROW_LENGTH, BLOCK_WIDTH);

			glTexSubImage2D(GL_TEXTURE_2D, 0, theRect->l, theRect->t, 
				theRect->w, theRect->h, gl_lightmap_format, GL_UNSIGNED_BYTE,
				lightmaps+(i + theRect->t) * BLOCK_WIDTH * lightmap_bytes + theRect->l * lightmap_bytes);

			glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

			#endif

			theRect->l = BLOCK_WIDTH;
			theRect->t = BLOCK_HEIGHT;
			theRect->h = 0;
			theRect->w = 0;
			*/

			R_UploadLightMap(i);
		}

		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); 

		glBegin (GL_TRIANGLE_FAN);
		v = p->verts[0];

		for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE)
		{
			temp = sin(v[2]*0.05+realtime);

			glMultiTexCoord2fvARB (GL_TEXTURE0_ARB, (v+3));
			glMultiTexCoord2fvARB (GL_TEXTURE1_ARB, (v+5));

			nv[0] = v[0] + 8*sin(v[1]*0.05+realtime)*temp; //sin(v[2]*0.05+realtime);
			nv[1] = v[1] + 8*sin(v[0]*0.05+realtime)*temp; //sin(v[2]*0.05+realtime);
			nv[2] = v[2];

			glVertex3fv (nv);
		}

		glEnd ();

	}

	else
	{
		p = s->polys;

		t = R_TextureAnimation (s->texinfo->texture);

		GL_Bind (t->gl_texturenum);

		DrawGLWaterPoly (p);

		GL_Bind (lightmap_textures + s->lightmaptexturenum);
		glEnable (GL_BLEND);

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
		//temp = sin(v[2]*0.05+realtime);

		glTexCoord2f (v[3], v[4]);

		nv[0] = v[0] + 8*sin(v[1]*0.05+realtime)* sin(v[2]*0.05+realtime);
		nv[1] = v[1] + 8*sin(v[0]*0.05+realtime)* sin(v[2]*0.05+realtime);
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
		//temp = sin(v[2]*0.05+realtime);

		glTexCoord2f (v[5], v[6]);

		nv[0] = v[0] + 8*sin(v[1]*0.05+realtime)* sin(v[2]*0.05+realtime);
		nv[1] = v[1] + 8*sin(v[0]*0.05+realtime)* sin(v[2]*0.05+realtime);
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

#ifdef AMIGA

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
	int		i, j;
	glpoly_t	*p;
	float		*v;
	glRect_t	*theRect;
	int		fake_multitexture;

	if (r_fullbright.value)
		return;

	if (!gl_texsort.value)
		return;

	fake_multitexture = (int)gl_fake_multitexture.value;

	if(fake_multitexture == 2)
	{
		return;
	}

	if(!fake_multitexture)
	{
		glDepthMask (0);    // don't bother writing Z

		#ifdef LITFILES
		if(eyecandy)
		{
			if (gl_lightmap_format == GL_ALPHA)
			{
				glBlendFunc (GL_ZERO, GL_SRC_ALPHA);
			}

			else
			{
				glBlendFunc (GL_ZERO, GL_SRC_COLOR);
			} 
		}

		else
		{
		#endif
			if (gl_lightmap_format == GL_LUMINANCE || gl_lightmap_format == MGL_UNSIGNED_SHORT_5_6_5)
			{
				glBlendFunc (GL_ZERO, GL_SRC_COLOR);
			}

			else if (gl_lightmap_format == GL_ALPHA)
			{
				glBlendFunc (GL_ZERO, GL_SRC_ALPHA);
			}
		#ifdef LITFILES
		}
		#endif

		if (!r_lightmap.value)
		{
			glEnable (GL_BLEND);
		}

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

			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, theRect->t, 
				BLOCK_WIDTH, theRect->h, gl_lightmap_format, GL_UNSIGNED_BYTE,
				lightmaps+(i* BLOCK_HEIGHT + theRect->t) *BLOCK_WIDTH*lightmap_bytes);


			theRect->l = BLOCK_WIDTH;
			theRect->t = BLOCK_HEIGHT;
			theRect->h = 0;
			theRect->w = 0;

			#else

			R_UploadLightMap(i);

			#endif
		}

		if(fake_multitexture)
			continue;

		for ( ; p ; p=p->chain)
		{

			if ((p->flags & SURF_UNDERWATER) && r_dowarp)
				DrawGLWaterPolyLightmap (p);

			else
			{
				#ifndef AMIGA

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
		
		//p = NULL; // maybe?
	}


	if(fake_multitexture)
	{
		return;
	}

	glDisable (MGL_Z_OFFSET);
	glDisable (GL_BLEND);

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

	glDepthMask (1);    // back to normal Z buffering
}



/*
================
R_RenderBrushPoly
================
*/

void R_RenderBrushPoly (msurface_t *fa)
{
	texture_t		*t;
	byte			*base;
	register int		maps;
	register glRect_t	*theRect;
	int			smax, tmax;
	//qboolean		lm_static;

	c_brush_polys++;

	if (fa->flags & SURF_DRAWSKY)
	{
		// warp texture, no lightmaps
		EmitBothSkyLayers (fa);
		return;
	}

	t = R_TextureAnimation (fa->texinfo->texture);

	GL_Bind (t->gl_texturenum);	//unit 0

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
		if(gl_fake_multitexture.value)
		{
			glActiveTextureARB(GL_TEXTURE1_ARB);
			glEnable(GL_TEXTURE_2D);
			GL_Bind(lightmap_textures + fa->lightmaptexturenum);

			glBegin(GL_POLYGON);
			mglMtexTV23fv(mini_CurrentContext, VERTEXSIZE, fa->polys);
			glEnd();

			glDisable(GL_TEXTURE_2D);
			glActiveTextureARB(GL_TEXTURE0_ARB);

			if(gl_fake_multitexture.value == 2)
				return;	//don't modify lightmaps in any way
		}

		else
		{
			glBegin(GL_POLYGON);
			mglTV23fv(mini_CurrentContext, VERTEXSIZE, fa->polys);
			glEnd();
		}
	}

	// add the poly to the proper lightmap chain

	fa->polys->chain = lightmap_polys[fa->lightmaptexturenum];
	lightmap_polys[fa->lightmaptexturenum] = fa->polys;

	// check for lightmap modification
	for (maps = 0 ; maps < MAXLIGHTMAPS && fa->styles[maps] != 255 ; maps++) 
		if (d_lightstylevalue[fa->styles[maps]] != fa->cached_light[maps])
			goto dynamic;
  
	if (fa->dlightframe == r_framecount // dynamic this frame
		|| fa->cached_dlight)	  // dynamic previously
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
				theRect->w = (fa->light_s-theRect->l)+smax;

			if ((theRect->h + theRect->t) < (fa->light_t + tmax))
				theRect->h = (fa->light_t-theRect->t)+tmax;

			base = lightmaps + fa->lightmaptexturenum*lightmap_bytes*BLOCK_WIDTH*BLOCK_HEIGHT;
			//base += fa->light_t * BLOCK_WIDTH * lightmap_bytes + fa->light_s * lightmap_bytes;
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
void R_RenderDynamicLightmaps (msurface_t *fa)
{
	//texture_t *t;
	byte	  *base;
	int	  maps;
	glRect_t  *theRect;
	int	  smax, tmax;

	c_brush_polys++; 

	if (fa->flags & ( SURF_DRAWSKY | SURF_DRAWTURB) )  // mmm ?? - Cowcat
		return;

	fa->polys->chain = lightmap_polys[fa->lightmaptexturenum];
	lightmap_polys[fa->lightmaptexturenum] = fa->polys;

	// check for lightmap modification
	for (maps = 0 ; maps < MAXLIGHTMAPS && fa->styles[maps] != 255 ; maps++) 
		if (d_lightstylevalue[fa->styles[maps]] != fa->cached_light[maps])
			goto dynamic;

	if (fa->dlightframe == r_framecount // dynamic this frame
		|| fa->cached_dlight)	  // dynamic previously
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
				theRect->w = (fa->light_s-theRect->l)+smax;

			if ((theRect->h + theRect->t) < (fa->light_t + tmax))
				theRect->h = (fa->light_t-theRect->t)+tmax;

			base = lightmaps + fa->lightmaptexturenum*lightmap_bytes*BLOCK_WIDTH*BLOCK_HEIGHT;
			//base += fa->light_t * BLOCK_WIDTH * lightmap_bytes + fa->light_s * lightmap_bytes;
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
		GL_Bind (t->gl_texturenum);

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

qboolean watersurfpass = false;

void R_DrawWaterSurfaces (void)
{
	int		i;
	msurface_t	*s;
	texture_t	*t;
	float		wateralpha;

	/*
	if (r_wateralpha.value == 1.0 && gl_texsort.value)
		return;
	*/

	wateralpha = r_wateralpha.value;

	if (wateralpha == 1.0 && gl_texsort.value)
		return;


	//
	// go back to the world matrix
	//

	glLoadMatrixf (r_world_matrix);


	if (!gl_texsort.value)
	{
		if (!waterchain)
			return;

		//if (r_wateralpha.value < 1.0)
		if (wateralpha < 1.0)
		{
			glEnable (GL_BLEND);
			//glColor4f (1,1,1, r_wateralpha.value);
			glColor4f (1, 1, 1, wateralpha);
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

			if( wateralpha < 0.9)
				glDepthMask(GL_FALSE);

			watersurfpass = true;
		}

		for ( s = waterchain ; s ; s=s->texturechain)
		{
			GL_Bind (s->texinfo->texture->gl_texturenum);
			EmitWaterPolys (s);
		}
    
		waterchain = NULL;
	}

	else
	{
		//if (r_wateralpha.value < 1.0)
		if (wateralpha < 1.0)
		{
			glEnable (GL_BLEND);
			//glColor4f (1,1,1, r_wateralpha.value);
			glColor4f (1, 1, 1, wateralpha);
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

			if( wateralpha < 0.9)
				glDepthMask(GL_FALSE);

			watersurfpass = true;
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
      
			GL_Bind (t->gl_texturenum);

			for ( ; s ; s=s->texturechain)
				EmitWaterPolys (s);
      
			t->texturechain = NULL;
		}

	}

	//if (r_wateralpha.value < 1.0)
	if (wateralpha < 1.0)
	{	
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		glColor4f (1, 1, 1, 1);
		glDisable (GL_BLEND);

		if( wateralpha < 0.9)
			glDepthMask(GL_TRUE);

		watersurfpass = false;
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
	int   		i;
	msurface_t  	*s;
	texture_t 	*t;


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

		// here is the mirror stuff - Cowcat
		else if (i == mirrortexturenum && r_mirroralpha.value != 1.0)
		{
			R_MirrorChain (s);
			continue;
		}
	
		else
		{
			if ((s->flags & SURF_DRAWTURB) && r_wateralpha.value != 1.0)
				continue; // draw translucent water later

			for ( ; s ; s=s->texturechain)
				R_RenderBrushPoly (s);

		}

		t->texturechain = NULL;
	}
}



/*
=================
R_DrawBrushModel
=================
*/

//extern void R_RotateForEntity(vec3_t origin, vec3_t angles);

#if 0
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

void R_DrawBrushModel (entity_t *e)
{
	int		j, k;
	vec3_t		mins, maxs;
	int		i, numsurfaces;
	msurface_t	*psurf;
	float		dot;
	mplane_t	*pplane;
	model_t		*clmodel;
	qboolean	rotated;

	currententity = e;
	currenttexture = -1;

	clmodel = e->model;


	if (e->angles[0] || e->angles[1] || e->angles[2])
	{
		rotated = true;

		for (i=0 ; i<3 ; i++)
		{
			mins[i] = e->origin[i] - clmodel->radius;
			maxs[i] = e->origin[i] + clmodel->radius;
		}
		

		//if(R_CullSphere(e->origin, clmodel->radius))
			//return;
	}

	else
	{
		rotated = false;
		VectorAdd (e->origin, clmodel->mins, mins);
		VectorAdd (e->origin, clmodel->maxs, maxs);

		//if (R_CullBox (mins, maxs))
			//return;
	}


	if (R_CullBox (mins, maxs))
		return;

	// glColor3f (1,1,1); - not needed because of entity-sorting

	memset (lightmap_polys, 0, sizeof(lightmap_polys));

	VectorSubtract (r_refdef.vieworg, e->origin, modelorg);

	if (rotated)
	{
		vec3_t	temp;
		vec3_t	forward, right, up;

		VectorCopy (modelorg, temp);
		AngleVectors (e->angles, forward, right, up);
		modelorg[0] = DotProduct (temp, forward);
		modelorg[1] = -DotProduct (temp, right);
		modelorg[2] = DotProduct (temp, up);
	}

	psurf = &clmodel->surfaces[clmodel->firstmodelsurface];

	// calculate dynamic lighting for bmodel if it's not an
	// instanced model

	if (clmodel->firstmodelsurface != 0 && !gl_flashblend.value)
	{
		for (k=0 ; k<MAX_DLIGHTS ; k++)
		{
			if ((cl_dlights[k].die < cl.time) || (!cl_dlights[k].radius))
				continue;

			R_MarkLights (&cl_dlights[k], 1<<k, clmodel->nodes + clmodel->hulls[0].firstclipnode);
		}
	}

	glPushMatrix ();

	#if 1

	e->angles[0] = -e->angles[0]; // stupid quake bug
	R_RotateForEntity (e);
	e->angles[0] = -e->angles[0]; // stupid quake bug

	#else

	glTranslatef (e->origin[0],  e->origin[1],  e->origin[2]);

	#ifdef AMIGA
	glRotatefEXT(e->angles[1], GLROT_001);
	glRotatefEXT(e->angles[0], GLROT_010);
	glRotatefEXT(e->angles[2], GLROT_100);
	#else
	glRotatef (e->angles[1],  0, 0, 1);
	glRotatef (e->angles[0],  0, 1, 0);
	glRotatef (e->angles[2],  1, 0, 0);
	#endif

	#endif


	//
	// draw texture
	//

	for (i=0 ; i<clmodel->nummodelsurfaces ; i++, psurf++)
	{
		// find which side of the node we are on
		pplane = psurf->plane;
		
		#if 1
		dot = DotProduct (modelorg, pplane->normal) - pplane->dist;

		#else

		//dot = PlaneDiff(modelorg, pplane);

		if (pplane->type < 3)
			dot = modelorg[pplane->type] - pplane->dist;

		else
			dot = DotProduct (modelorg, pplane->normal) - pplane->dist; 
		

		#endif

		// draw the polygon
		if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
			(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
		{
			if (gl_texsort.value)
			{
				R_RenderBrushPoly (psurf);
			}

			else
				R_DrawSequentialPoly (psurf);
		}
	}

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
	int		i, c, side, *pindex;
	vec3_t		acceptpt, rejectpt;
	mplane_t	*plane;
	msurface_t	*surf, **mark;
	mleaf_t		*pleaf;
	double		d, dot;
	vec3_t		mins, maxs;

loc0:

	if (node->contents == CONTENTS_SOLID)
		return;	  // solid

	if (node->visframe != r_visframecount)
		return;


	if (R_CullBox (node->minmaxs, node->minmaxs+3))
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

	switch (plane->type)
	{
		case PLANE_X:
			dot = modelorg[0] - plane->dist;
			break;

		case PLANE_Y:
			dot = modelorg[1] - plane->dist;
			break;

		case PLANE_Z:
			dot = modelorg[2] - plane->dist;
			break;

		default:
			dot = DotProduct (modelorg, plane->normal) - plane->dist;
			break;
	}

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

			if(!(surf->flags & (r_dowarp ? SURF_UNDERWATER : 0)) && ( (dot < 0) ^ !!(surf->flags & SURF_PLANEBACK)) )
				continue;   // wrong side

			// if sorting by texture, just store it out
			if (gl_texsort.value)
			{
				if (!mirror || surf->texinfo->texture != cl.worldmodel->textures[mirrortexturenum])
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
				R_DrawSequentialPoly (surf);
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
	int		i;

	memset (&ent, 0, sizeof(ent));
	ent.model = cl.worldmodel;

	VectorCopy (r_refdef.vieworg, modelorg);

	currententity = &ent;
	currenttexture = -1;

	glColor3f (1,1,1);

	memset (lightmap_polys, 0, sizeof(lightmap_polys));


	if(r_skybox)
		R_ClearSkyBox ();

	
	R_RecursiveWorldNode (cl.worldmodel->nodes);

	DrawTextureChains ();
	
	R_BlendLightmaps ();

	if(gl_fake_multitexture.value)
		R_DrawMultitextureBuffer();

	if(r_skybox)
	{
		//if (gl_fog.value)
		if(fog) // Cowcat way
			glDisable(GL_FOG);

		R_DrawSkyBox ();

		//if (gl_fog.value)
		if(fog)
			glEnable(GL_FOG);
	}
}


/*
===============
R_MarkLeaves
===============
*/
void R_MarkLeaves (void)
{
	byte	*vis;
	mnode_t *node;
	int	i;
	byte	solid[4096];

	if (r_oldviewleaf == r_viewleaf && !r_novis.value)
		return;
  
	if (mirror)
		return;

	r_visframecount++;
	r_oldviewleaf = r_viewleaf;

	if (r_novis.value)
	{
		vis = solid;
		memset (solid, 0xff, (cl.worldmodel->numleafs+7)>>3);
	}

	else
		vis = Mod_LeafPVS (r_viewleaf, cl.worldmodel);
    
	for (i=0 ; i<cl.worldmodel->numleafs ; i++)
	{
		if (vis[i>>3] & (1<<(i&7)))
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
			{	// this is a valid spot
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
			{	// this is a valid spot
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

	Sys_Error ("AllocBlock: full");
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
void BuildSurfaceDisplayList (msurface_t *fa) // modded Cowcat 
{	
	int	 i, lindex, lnumverts, s_axis, t_axis, vertpage;		
	medge_t	 *pedges, *r_pedge;
	float	 *vec, s, t;
	glpoly_t *poly;

	// reconstruct the polygon
	pedges = currentmodel->edges;
	lnumverts = fa->numedges;
	vertpage = 0;

	//
	// draw texture
	//
	poly = Hunk_Alloc (sizeof(glpoly_t) + (lnumverts-4) * VERTEXSIZE*sizeof(float));
	poly->next = fa->polys;
	poly->flags = fa->flags; // don´t remove now (issues with waterwarp !) - Cowcat
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
			float  f;

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
void GL_CreateSurfaceLightmap (msurface_t *surf)
{
	int   smax, tmax;
	byte  *base;

	if (surf->flags & (SURF_DRAWSKY|SURF_DRAWTURB))
		return;

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
void GL_BuildLightmaps (void)
{
	int	i, j;
	model_t *m;
	extern	qboolean lmf_isset;	//set in gl_vidamiga.c
  
	memset (allocated, 0, sizeof(allocated));

	//last_lightmap_updated = 0; // ezquake

	r_framecount = 1;		// no dlightcache : was disabled by massimiliano!

	#ifndef AMIGA
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

	#ifndef AMIGA
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
			//GL_CreateSurfaceLightmap (m->surfaces + i); //down Cowcat

			if ( m->surfaces[i].flags & SURF_DRAWTURB )
				continue;

			#ifndef QUAKE2
			if(!r_skybox)
				if ( m->surfaces[i].flags & SURF_DRAWSKY )
					continue;
			#endif


			GL_CreateSurfaceLightmap (m->surfaces + i); // Cowcat
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

