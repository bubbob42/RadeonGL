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
// r_misc.c

#include "quakedef.h"

//extern void GL_Upload8_EXT (byte *data, int width, int height,  qboolean mipmap, qboolean alpha); 

extern void R_InitBubble(); //sincostab for lightballs

//extern qboolean VID_Is8bit();
extern void R_LoadSkys();

cvar_t r_litfiles = {"r_litfiles", "0", true};	//surgeon
cvar_t r_flames = {"r_flames", "1", true};	//surgeon

/*
==================
R_InitTextures
==================
*/
void  R_InitTextures (void)
{
	int   x, y, m;
	byte  *dest;

	// create a simple checkerboard texture for the default
	r_notexture_mip = Hunk_AllocName (sizeof(texture_t) + 16*16+8*8+4*4+2*2, "notexture");
  
	r_notexture_mip->width = r_notexture_mip->height = 16;
	r_notexture_mip->offsets[0] = sizeof(texture_t);
	r_notexture_mip->offsets[1] = r_notexture_mip->offsets[0] + 16*16;
	r_notexture_mip->offsets[2] = r_notexture_mip->offsets[1] + 8*8;
	r_notexture_mip->offsets[3] = r_notexture_mip->offsets[2] + 4*4;
  
	for (m=0 ; m<4 ; m++)
	{
		dest = (byte *)r_notexture_mip + r_notexture_mip->offsets[m];

		for (y=0 ; y< (16>>m) ; y++)
			for (x=0 ; x< (16>>m) ; x++)
			{
				if (  (y< (8>>m) ) ^ (x< (8>>m) ) )
					*dest++ = 0;

				else
					*dest++ = 0xff;
			}
	} 
}


byte  dottexture[8][8] =
{
	{0,1,1,0,0,0,0,0},
	{1,1,1,1,0,0,0,0},
	{1,1,1,1,0,0,0,0},
	{0,1,1,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
};

byte  quadtexture[8][8] =
{
	{1,1,1,1,0,0,0,0},
	{1,1,1,1,0,0,0,0},
	{1,1,1,1,0,0,0,0},
	{1,1,1,1,0,0,0,0},
	{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
};

extern qboolean quadtex;

void R_InitParticleTexture (void)
{
	int   x,y;
	byte  data[8][8][4];

	//
	// particle texture
	//

	particletexture = texture_extension_number++;
	GL_Bind(particletexture);

	for (x=0 ; x<8 ; x++)
	{
		for (y=0 ; y<8 ; y++)
		{
			data[y][x][0] = 255;
			data[y][x][1] = 255;
			data[y][x][2] = 255;

			if (!quadtex)
				data[y][x][3] = dottexture[x][y]*255;

			else
				data[y][x][3] = quadtexture[x][y]*255;
		}
	}

	

	glTexImage2D (GL_TEXTURE_2D, 0, gl_alpha_format, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, (GLint)GL_MODULATE);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	
}

/*
===============
R_Envmap_f

Grab six views for environment mapping tests
===============
*/
#if 0
void R_Envmap_f (void)
{
/*					   30/01/2000 removed: M.Tretene
	byte  buffer[256*256*4];
	char  name[1024];

	glDrawBuffer  (GL_FRONT);
	glReadBuffer  (GL_FRONT);
	envmap = true;

	r_refdef.vrect.x = 0;
	r_refdef.vrect.y = 0;
	r_refdef.vrect.width = 256;
	r_refdef.vrect.height = 256;

	r_refdef.viewangles[0] = 0;
	r_refdef.viewangles[1] = 0;
	r_refdef.viewangles[2] = 0;
	GL_BeginRendering (&glx, &gly, &glwidth, &glheight);
	R_RenderView ();
	glReadPixels (0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
	COM_WriteFile ("env0.rgb", buffer, sizeof(buffer));   

	r_refdef.viewangles[1] = 90;
	GL_BeginRendering (&glx, &gly, &glwidth, &glheight);
	R_RenderView ();
	glReadPixels (0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
	COM_WriteFile ("env1.rgb", buffer, sizeof(buffer));   

	r_refdef.viewangles[1] = 180;
	GL_BeginRendering (&glx, &gly, &glwidth, &glheight);
	R_RenderView ();
	glReadPixels (0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
	COM_WriteFile ("env2.rgb", buffer, sizeof(buffer));   

	r_refdef.viewangles[1] = 270;
	GL_BeginRendering (&glx, &gly, &glwidth, &glheight);
	R_RenderView ();
	glReadPixels (0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
	COM_WriteFile ("env3.rgb", buffer, sizeof(buffer));   
	
	r_refdef.viewangles[0] = -90;
	r_refdef.viewangles[1] = 0;
	GL_BeginRendering (&glx, &gly, &glwidth, &glheight);
	R_RenderView ();
	glReadPixels (0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
	COM_WriteFile ("env4.rgb", buffer, sizeof(buffer));   

	r_refdef.viewangles[0] = 90;
	r_refdef.viewangles[1] = 0;
	GL_BeginRendering (&glx, &gly, &glwidth, &glheight);
	R_RenderView ();
	glReadPixels (0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
	COM_WriteFile ("env5.rgb", buffer, sizeof(buffer));   

	envmap = false;
	glDrawBuffer  (GL_BACK);
	glReadBuffer  (GL_BACK);
	GL_EndRendering ();
*/  
}
#endif

#ifdef TRIGTABLES
extern void MY_SINCOS_Init(void);
#endif

void Fog_FogCommand_f(void)
{
	// dummy
}

/*
===============
R_Init
===============
*/

extern void ChangeSkyName_f();
extern void qgl_InitArrays();

extern cvar_t gl_finish;

cvar_t r_skybox_format = {"r_skybox_format","1", true};
//extern cvar_t gl_fake_multitexture; //Surgeon
//extern cvar_t gl_glows; //Surgeon
extern cvar_t r_cullworld;//Surgeon
//extern cvar_t r_vertexarrays;//Surgeon
//extern cvar_t r_cull_aggressive;//Surgeon
extern cvar_t r_model_maxdist;//Surgeon
extern cvar_t r_elim_areasize; //Surgeon
extern cvar_t r_interpolations; //Surgeon
//extern cvar_t r_particle_hack; //Surgeon - Cowcat

cvar_t r_pos = {"r_pos","0"}; // Cowcat 2019

// Cowcat
extern cvar_t r_showbboxes;
extern cvar_t r_stereo;
extern cvar_t r_stereodepth;
extern void Box_Color_f (void);

extern cvar_t gl_showpolys;
extern cvar_t gl_wireframe;
extern cvar_t gl_wireonly;
extern cvar_t gl_farclip; // 2019
//

extern void R_InitParticles (void);
extern void R_ClearParticles (void);

void R_Init (void)
{ 
	extern byte *hunk_base;
 
	Cmd_AddCommand ("timerefresh", R_TimeRefresh_f);  
	//Cmd_AddCommand ("envmap", R_Envmap_f);  
	Cmd_AddCommand ("pointfile", R_ReadPointFile_f);  

	Cmd_AddCommand ("r_skybox", ChangeSkyName_f);  

	//Cvar_RegisterVariable (&r_flames);
	//Cvar_RegisterVariable (&r_skybox_format);
	//Cvar_RegisterVariable (&r_litfiles);
	//Cvar_RegisterVariable (&r_particle_hack); // not now
	//Cvar_RegisterVariable (&gl_glows);
	//Cvar_RegisterVariable (&gl_fake_multitexture);
	//Cvar_RegisterVariable (&r_interpolations); // now on host.c - Cowcat 2019
	//Cvar_RegisterVariable (&r_elim_areasize);
	Cvar_RegisterVariable (&r_model_maxdist);
	//Cvar_RegisterVariable (&r_cull_aggressive);
	//Cvar_RegisterVariable (&r_vertexarrays);
	Cvar_RegisterVariable (&r_cullworld);
	Cvar_RegisterVariable (&r_norefresh);
	Cvar_RegisterVariable (&r_lightmap);
	Cvar_RegisterVariable (&r_fullbright);
	Cvar_RegisterVariable (&r_drawentities);
	Cvar_RegisterVariable (&r_drawviewmodel);
	//Cvar_RegisterVariable (&r_shadows); // now on host.c - Cowcat
	Cvar_RegisterVariable (&r_mirroralpha);
	//Cvar_RegisterVariable (&r_wateralpha); // now on host.c - Cowcat
	//Cvar_RegisterVariable (&r_waterwarp); //by surgeon - on host.c - Cowcat
	Cvar_RegisterVariable (&r_dynamic);
	Cvar_RegisterVariable (&r_novis);
	Cvar_RegisterVariable (&r_speeds);

	Cvar_RegisterVariable (&r_pos); // Cowcat 2019

	Cvar_RegisterVariable (&gl_finish);
	Cvar_RegisterVariable (&gl_clear);
	Cvar_RegisterVariable (&gl_texsort);

	/* REAL FIX (2026-08-05, -noextensions water smear investigation):
	 * this only forced gl_texsort off for real multitexture, so
	 * -noextensions (gl_mtexable false) fell through to the
	 * texture-chain draw path (DrawTextureChains/R_BlendLightmaps),
	 * a whole separate code path from default's R_DrawSequentialPoly
	 * -- and the one where the water smearing/duplication bug actually
	 * lives. Route -noextensions through the SAME sequential path
	 * default uses (already patched for the !gl_mtexable fallback
	 * blend cases) instead of the buggy chain path. */
	if (gl_mtexable || gl_force_fake_multitex)
		Cvar_SetValue ("gl_texsort", 0);

	Cvar_RegisterVariable (&gl_cull);
	Cvar_RegisterVariable (&gl_smoothmodels);
	Cvar_RegisterVariable (&gl_affinemodels);
	Cvar_RegisterVariable (&gl_polyblend);
	Cvar_RegisterVariable (&gl_flashblend);
	Cvar_RegisterVariable (&gl_playermip);
	Cvar_RegisterVariable (&gl_nocolors);

	Cvar_RegisterVariable (&gl_keeptjunctions);
	Cvar_RegisterVariable (&gl_reporttjunctions);

	Cvar_RegisterVariable (&gl_doubleeyes);
	//Cvar_RegisterVariable (&gl_fog); // now on host.c - Cowcat

	Cvar_RegisterVariable (&r_showbboxes);
	Cvar_RegisterVariable (&r_stereo);
	Cvar_RegisterVariable (&r_stereodepth);
	Cvar_RegisterVariable (&gl_showpolys);
	Cvar_RegisterVariable (&gl_wireframe);
	Cvar_RegisterVariable (&gl_wireonly);
 
	Cmd_AddCommand ("bboxcolor", Box_Color_f);

	qgl_InitArrays (); //Surgeon
	R_InitBubble(); //Surgeon

	#ifdef TRIGTABLES
	MY_SINCOS_Init(); //Surgeon (mathlib.c and gl_rsurf.c)
	#endif

	R_InitParticles ();
	R_InitParticleTexture ();

	#ifdef GLTEST
	Test_Init ();
	#endif

	playertextures = texture_extension_number;
	texture_extension_number += 16;

	Cmd_AddCommand("fog", Fog_FogCommand_f); // annoying - Cowcat 2019
}

/*
===============
R_TranslatePlayerSkin

Translates a skin texture by the per-player color lookup
===============
*/

void R_TranslatePlayerSkin (int playernum)
{
	//surgeon:
	extern qboolean mtexenabled;
	int		top, bottom;
	byte		translate[256];
	unsigned	translate32[256];
	int		i, j, s;
	model_t		*model;
	aliashdr_t	*paliashdr;
	byte		*original;
	unsigned	pixels[512*256], *out;
	unsigned	scaled_width, scaled_height;
	int		inwidth, inheight;
	byte		*inrow;
	unsigned	frac, fracstep;
	extern byte	**player_8bit_texels_tbl;

	if(mtexenabled)
		GL_DisableMultitexture();

	top = cl.scores[playernum].colors & 0xf0;
	bottom = (cl.scores[playernum].colors &15)<<4;

	for (i=0 ; i<256 ; i++)
		translate[i] = i;

	for (i=0 ; i<16 ; i++)
	{
		if (top < 128)	// the artists made some backwards ranges.  sigh.
			translate[TOP_RANGE+i] = top+i;

		else
			translate[TOP_RANGE+i] = top+15-i;
	
		if (bottom < 128)
			translate[BOTTOM_RANGE+i] = bottom+i;

		else
			translate[BOTTOM_RANGE+i] = bottom+15-i;
	}

	//
	// locate the original skin pixels
	//
	currententity = &cl_entities[1+playernum];
	model = currententity->model;

	if (!model)
		return;	  // player doesn't have a model yet

	if (model->type != mod_alias)
		return; // only translate skins on alias models

	paliashdr = (aliashdr_t *)Mod_Extradata (model);
	s = paliashdr->skinwidth * paliashdr->skinheight;

	if (currententity->skinnum < 0 || currententity->skinnum >= paliashdr->numskins)
	{
		Con_Printf("(%d): Invalid player skin #%d\n", playernum, currententity->skinnum);
		original = (byte *)paliashdr + paliashdr->texels[0];
	}

	else
		original = (byte *)paliashdr + paliashdr->texels[currententity->skinnum];

	if (s & 3)
		Sys_Error ("R_TranslateSkin: s&3");

	inwidth = paliashdr->skinwidth;
	inheight = paliashdr->skinheight;

	// because this happens during gameplay, do it fast
	// instead of sending it through gl_upload 8
	GL_Bind(playertextures + playernum);

	#if 0

	byte  translated[320*200];

	for (i=0 ; i<s ; i+=4)
	{
		translated[i] = translate[original[i]];
		translated[i+1] = translate[original[i+1]];
		translated[i+2] = translate[original[i+2]];
		translated[i+3] = translate[original[i+3]];
	}


	// don't mipmap these, because it takes too long
	GL_Upload8 (translated, paliashdr->skinwidth, paliashdr->skinheight, false, false, true);

	#else

	scaled_width = gl_max_size.value < 512 ? gl_max_size.value : 512;
	scaled_height = gl_max_size.value < 256 ? gl_max_size.value : 256;

	// allow users to crunch sizes down even more if they want
	scaled_width >>= (int)gl_playermip.value;
	scaled_height >>= (int)gl_playermip.value;

	#if 0  /* cowcat no 8bit video */

	if (VID_Is8bit())
	{	// 8bit texture upload
		byte *out2;

		out2 = (byte *)pixels;
		memset(pixels, 0, sizeof(pixels));
		fracstep = inwidth*0x10000/scaled_width;

		for (i=0 ; i<scaled_height ; i++, out2 += scaled_width)
		{
			inrow = original + inwidth*(i*inheight/scaled_height);
			frac = fracstep >> 1;

			for (j=0 ; j<scaled_width ; j+=4)
			{
				out2[j] = translate[inrow[frac>>16]];
				frac += fracstep;
				out2[j+1] = translate[inrow[frac>>16]];
				frac += fracstep;
				out2[j+2] = translate[inrow[frac>>16]];
				frac += fracstep;
				out2[j+3] = translate[inrow[frac>>16]];
				frac += fracstep;
			}
		}	

		GL_Upload8_EXT ((byte *)pixels, scaled_width, scaled_height, false, false);
		return;
	}

	#endif

	for (i=0 ; i<256 ; i++)
		translate32[i] = d_8to24table[translate[i]];

	out = pixels;
	fracstep = inwidth*0x10000/scaled_width;

	for (i=0 ; i<scaled_height ; i++, out += scaled_width)
	{
		inrow = original + inwidth*(i*inheight/scaled_height);
		frac = fracstep >> 1;

		for (j=0 ; j<scaled_width ; j++, frac += fracstep )
		{
			out[j] = translate32[inrow[frac>>16]];
		}

	}

	glTexImage2D (GL_TEXTURE_2D, 0, gl_solid_format, scaled_width, scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); // (glint)
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	#endif
}


extern qboolean r_skybox;

/*
===============
R_NewMap
===============
*/
void R_NewMap (void)
{
	int		i;
	texture_t	*tex; //
  
	for (i=0 ; i<256 ; i++)
		d_lightstylevalue[i] = 264;   // normal light value

	memset (&r_worldentity, 0, sizeof(r_worldentity));
	r_worldentity.model = cl.worldmodel;

	// clear out efrags in case the level hasn't been reloaded
	// FIXME: is this one short?
	for (i=0 ; i<cl.worldmodel->numleafs ; i++)
		cl.worldmodel->leafs[i].efrags = NULL;
      
	r_viewleaf = NULL;
	R_ClearParticles ();

	GL_BuildLightmaps ();

	// identify sky texture
	skytexturenum = -1;
	mirrortexturenum = -1;

	for (i=0 ; i<cl.worldmodel->numtextures ; i++)
	{
		
		tex = cl.worldmodel->textures[i];

		if(!tex)
			continue;

		if (!Q_strncmp(tex->name,"sky",3) )
			skytexturenum = i;

		if (!Q_strncmp(tex->name,"window02_1",10) ) 
			mirrortexturenum = i;

		else if (!Q_strncmp(tex->name,"mirror_",7) ) // mirror textures - Cowcat
			mirrortexturenum = i;

		//else if (!Q_strncmp(tex->name,"star_teleport",13) ) // mirror textures - Cowcat
			//mirrortexturenum = i;

		tex->texturechain = NULL;
		
	}

	//#ifdef QUAKE2
	if(r_skybox)
		R_LoadSkys ();
	//#endif
}


/*
====================
R_TimeRefresh_f

For program optimization
====================
*/
void R_TimeRefresh_f (void)
{
	int	i;
	float	start, stop, time;
	int	startangle;
	vrect_t vr;

	glDrawBuffer (GL_FRONT);
	glFinish ();

	start = Sys_FloatTime ();

	for (i=0 ; i<128 ; i++)
	{
		r_refdef.viewangles[1] = i/128.0*360.0;
		R_RenderView ();
	}

	glFinish ();
	stop = Sys_FloatTime ();
	time = stop-start;
	Con_Printf ("%f seconds (%f fps)\n", time, 128/time);

	glDrawBuffer (GL_BACK);
	GL_EndRendering ();
}

void D_FlushCaches (void)
{
}


