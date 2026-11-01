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

// draw.c -- this is the only file outside the refresh that touches the
// vid buffer

#include "quakedef.h"
#include <mgl/mglmacros.h>

#define GL_COLOR_INDEX8_EXT 0x80E5

extern int  gl_lockmode;	/* for draw_disc */
extern qboolean ddisc;		/* for draw_disc */
    
//extern unsigned char d_15to8table[65536]; // not used - Cowcat

cvar_t	gl_nobind = {"gl_nobind", "0"};
cvar_t	gl_max_size = {"gl_max_size", "256"};
cvar_t	gl_picmip = {"gl_picmip", "0"};
cvar_t	gl_conalpha = {"gl_conalpha", "0.6", true}; // new 2017 - Cowcat
cvar_t	gl_conanim  = {"gl_conanim",  "1",   true}; // animated console backdrop, 2026

byte	*draw_chars;	// 8*8 graphic characters
qpic_t	*draw_disc;
qpic_t	*draw_backtile;

int	translate_texture;
int	char_texture;
extern	int texture_extension_number;

#if 0
static const byte crosshair_data[64] =
{
	0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff,
	0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff,
	0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};
#endif

typedef struct
{
	int	texnum;
	float	sl, tl, sh, th;

} glpic_t;

byte	conback_buffer[sizeof(qpic_t) + sizeof(glpic_t)];
qpic_t	*conback = (qpic_t *)&conback_buffer;

int	gl_lightmap_format = GL_RGBA;

#if 0
//these are replaced by defines in glquake.h
//amiga-version uses GL_RGB5_A1 for alpha textures
int	gl_solid_format = 3;
int	gl_alpha_format = 4;
#endif

/* 2026-09-05: restored to the stock GLQuake default now that MiniGLV3D
 * actually stores mip levels. GL_Upload32 has always BUILT the full chain
 * (see its miplevel loop) but the driver discarded every level above 0, so
 * asking for a mipmap filter here would have gained nothing. It does now.
 * gl_filter_max stays GL_LINEAR -- magnification has no mip levels to pick
 * between, and the modes[] table pairs them the same way. */
int	gl_filter_min = GL_LINEAR_MIPMAP_NEAREST;
int	gl_filter_max = GL_LINEAR;
int	texels;

typedef struct
{
	int		texnum;
	char		identifier[64];
	int		width, height;
	qboolean	mipmap;

	int		lhcsum;	// LordHavoc cache mismatch workaround - Cowcat 2019
	//unsigned int	flags; // test - Cowcat

} gltexture_t;

#define MAX_GLTEXTURES	2048 // was 1024 - Cowcat

gltexture_t gltextures[MAX_GLTEXTURES];

int numgltextures = 0; //surgeon: was not initialized to 0

/*
Surgeon: made a macro in glquake.h

void GL_Bind (int texnum)
{
	//if (gl_nobind.value)
		//texnum = char_texture;

	if (currenttexture == texnum)
		return;

	currenttexture = texnum;

	#ifdef _WIN32
	bindTexFunc (GL_TEXTURE_2D, texnum);
	#else
	glBindTexture(GL_TEXTURE_2D, texnum);
	#endif
}
*/

//#define USE_SCRAP 1

#ifdef USE_SCRAP

/*
=============================================================================

  scrap allocation

  Allocate all the little status bar obejcts into a single texture
  to crutch up stupid hardware / drivers

=============================================================================
*/

#define MAX_SCRAPS    2
#define BLOCK_WIDTH   256
#define BLOCK_HEIGHT  256

int	scrap_allocated[MAX_SCRAPS][BLOCK_WIDTH];
byte	scrap_texels[MAX_SCRAPS][BLOCK_WIDTH*BLOCK_HEIGHT];
int	scrap_texnum;
qboolean scrap_dirty = false;

static qboolean scrap_initialized = false; //surgeon

// returns a texture number and the position inside it

int Scrap_AllocBlock (int w, int h, int *x, int *y, qboolean alpha)
{
	int   i, j;
	int   best, best2;
	int   bestx;
	int   texnum;

	if(scrap_initialized == false)
	{
		for(i=0; i<BLOCK_WIDTH; i++)
		{
			scrap_allocated[0][i] = 0;
			scrap_allocated[1][i] = 0;
		}

		scrap_initialized = true;
	}

	if(alpha == true)
		texnum = 0;

	else
		texnum = 1;

	best = BLOCK_HEIGHT;

	for (i=0 ; i<BLOCK_WIDTH-w ; i++)
	{
		best2 = 0;

		for (j=0 ; j<w ; j++)
		{
			if (scrap_allocated[texnum][i+j] >= best)
				break;

			if (scrap_allocated[texnum][i+j] > best2)
				best2 = scrap_allocated[texnum][i+j];
		}

		if (j == w)
		{
			// this is a valid spot
			*x = i;
			*y = best = best2;
		}
	}

	if (best + h > BLOCK_HEIGHT)
		return -1;

	for (i=0 ; i<w ; i++)
	{
		scrap_allocated[texnum][*x + i] = best + h;
	}

	return texnum;
}

int scrap_uploads;

void Scrap_Upload (void)
{
	int   texnum;

	scrap_uploads++;

	GL_Bind(scrap_texnum);		//alpha texture
	GL_Upload8 (scrap_texels[texnum], BLOCK_WIDTH, BLOCK_HEIGHT, false, true);

	GL_Bind(scrap_texnum + 1);	//noalpha texture
	GL_Upload8 (scrap_texels[texnum], BLOCK_WIDTH, BLOCK_HEIGHT, false, false);

	scrap_dirty = false;
}

#endif


//=============================================================================
/* Support Routines */

typedef struct cachepic_s
{
	char	name[MAX_QPATH];
	qpic_t	pic;
	byte	padding[32];  // for appended glpic

} cachepic_t;

#define MAX_CACHED_PICS	  128

cachepic_t menu_cachepics[MAX_CACHED_PICS];
int	menu_numcachepics;

byte	menuplyr_pixels[4096];

int	pic_texels;
int	pic_count;

qpic_t *Draw_PicFromWad (char *name)
{
	qpic_t	*p;
	glpic_t *gl;

	p = W_GetLumpName (name);
	gl = (glpic_t *)p->data;

	#ifdef USE_SCRAP

	// load little ones into the scrap
	//surgeeon: is there a bug somewhere in scrap allocation ?

	if (p->width < 64 && p->height < 64)
	{
		int	x, y;
		int	i, j, k;
		int	texnum;
		int	s;
		qboolean alpha; //surgeon

		s = p->width * p->height;
		alpha = false;

		for(i=0; i<s; i++)
		{
			if(p->data[i] == 255)
			{
				alpha = true;
				break;
			}
		}

		texnum = Scrap_AllocBlock (p->width, p->height, &x, &y, alpha);

		if(texnum == -1)
			goto noscrap;

		scrap_dirty = true;

		k = 0;

		for (i=0 ; i<p->height ; i++)
		{
			for (j=0 ; j<p->width ; j++, k++)
			{
				scrap_texels[texnum][(y+i)*BLOCK_WIDTH + x + j] = p->data[k];
			}
		}

		gl->texnum = scrap_texnum + texnum;
		gl->sl = ((float)x+0.01)/(float)BLOCK_WIDTH;
		gl->sh = ((float)(x+p->width)-0.01)/(float)BLOCK_WIDTH;
		gl->tl = ((float)y+0.01)/(float)BLOCK_WIDTH;
		gl->th = ((float)(y+p->height)-0.01)/(float)BLOCK_WIDTH;

		pic_count++;
		pic_texels += p->width*p->height;

		return p;
	}

	else

	#endif
	{
		//noscrap:

		gl->texnum = GL_LoadPicTexture (p);
		gl->sl = 0.0;
		gl->sh = 1.0;
		gl->tl = 0.0;
		gl->th = 1.0;

		return p;
	}
}


/*
================
Draw_CachePic
================
*/

qpic_t *Draw_CachePic (char *path)
{
	cachepic_t	*pic;
	int		i;
	qpic_t		*dat;
	glpic_t		*gl;


	for (pic=menu_cachepics, i=0 ; i<menu_numcachepics ; pic++, i++)
	{
		if (!strcmp (path, pic->name))
			return &pic->pic;
	}

	if (menu_numcachepics == MAX_CACHED_PICS)
		Sys_Error ("menu_numcachepics == MAX_CACHED_PICS");

	menu_numcachepics++;
	strcpy (pic->name, path);

	//
	// load the pic from disk
	//

	dat = (qpic_t *)COM_LoadTempFile (path);
  
	if (!dat)
		Sys_Error ("Draw_CachePic: failed to load %s", path);

	SwapPic (dat);

	// HACK HACK HACK --- we need to keep the bytes for
	// the translatable player picture just for the menu
	// configuration dialog

	if (!strcmp (path, "gfx/menuplyr.lmp"))
		memcpy (menuplyr_pixels, dat->data, dat->width*dat->height);

	pic->pic.width = dat->width;
	pic->pic.height = dat->height;

	gl = (glpic_t *)pic->pic.data;
	gl->texnum = GL_LoadPicTexture (dat);
	gl->sl = 0;
	gl->sh = 1;
	gl->tl = 0;
	gl->th = 1;

	return &pic->pic;
}


void Draw_CharToConback (int num, byte *dest)
{
	int	row, col;
	byte	*source;
	int	drawline;
	int	x;

	row = num>>4;
	col = num&15;
	source = draw_chars + (row<<10) + (col<<3);

	drawline = 8;

	while (drawline--)
	{
		for (x=0 ; x<8 ; x++)
			if (source[x] != 255)
				dest[x] = 0x60 + source[x];

		source += 128;
		dest += 320;
	}
}

typedef struct
{
	char	*name;
	int	minimize, maximize;

} glmode_t;

glmode_t modes[] = 
{
	{"GL_NEAREST", GL_NEAREST, GL_NEAREST},
	{"GL_LINEAR", GL_LINEAR, GL_LINEAR},
	{"GL_NEAREST_MIPMAP_NEAREST", GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST},
	{"GL_LINEAR_MIPMAP_NEAREST", GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR},
	{"GL_NEAREST_MIPMAP_LINEAR", GL_NEAREST_MIPMAP_LINEAR, GL_NEAREST},
	{"GL_LINEAR_MIPMAP_LINEAR", GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR}
};

/*
===============
Draw_TextureMode_f
===============
*/

void Draw_TextureMode_f (void)
{
	int		i;
	gltexture_t	*glt;

	if (Cmd_Argc() == 1)
	{
		for (i=0 ; i< 6 ; i++)
			if (gl_filter_min == modes[i].minimize)
			{
				Con_Printf ("%s\n", modes[i].name);
				return;
			}

		Con_Printf ("current filter is unknown???\n");
		return;
	}

	for (i=0 ; i< 6 ; i++)
	{
		if (!Q_strcasecmp (modes[i].name, Cmd_Argv(1) ) )
			break;
	}

	if (i == 6)
	{
		Con_Printf ("bad filter name\n");
		return;
	}

	gl_filter_min = modes[i].minimize;
	gl_filter_max = modes[i].maximize;

	// change all the existing mipmap texture objects
	for (i=0, glt=gltextures ; i<numgltextures ; i++, glt++)
	{
		if (glt->mipmap)
		{
			GL_Bind (glt->texnum);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
		}
	}
}

/*
===============
Draw_Init
===============
*/

//int GL_LoadTexture (char *identifier, int width, int height, byte *data, qboolean mipmap, qboolean alpha, unsigned flags);

void Draw_Init (void)
{
	int	i;
	qpic_t	*cb;
	byte	*dest; // *src;
	int	x, y;
	char	ver[40];
	glpic_t *gl;
	int	start;
	byte	*ncdata;
	//int	f, fstep;
	int	maxsize;
	//GLint	max_size;

	Cvar_RegisterVariable (&gl_nobind);
	Cvar_RegisterVariable (&gl_max_size);
	Cvar_RegisterVariable (&gl_picmip);

	/*
	// 3dfx can only handle 256 wide textures
	if (!Q_strncasecmp ((char *)gl_renderer, "3dfx",4) || strstr((char *)gl_renderer, "Glide"))
		Cvar_Set ("gl_max_size", "256");
	*/

	// texture_max_size

	if ((i = COM_CheckParm("-maxsize")) != 0)
	{
		maxsize = Q_atoi(com_argv[i+1]);
		maxsize &= 0xff80;
		Cvar_SetValue("gl_max_size", maxsize);

		//gl_max_size.value = Q_atof(com_argv[i+1]);
		//if (gl_max_size.value < 128)	gl_max_size.value = 128;
		//if (gl_max_size.value > 1024) gl_max_size.value = 1024;
		//Cvar_Set ("gl_max_size", com_argv[i+1]);
		//printf("MAXSIZE:%f\n",gl_max_size.value);
	}  
	
	#if 0
	else // Cowcat 2019
	{
		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
		printf("MAXSIZE:%d\n",max_size);

		if(max_size < gl_max_size.value)
		{
			printf("Reducing gl_max_size from %d to %d\n", (int)gl_max_size.value, max_size);
			Cvar_Set("gl_max_size", va("%d", max_size));
		}

		printf("MAXSIZE:%f\n",gl_max_size.value);
	}
	#endif
	
	Cmd_AddCommand ("gl_texturemode", &Draw_TextureMode_f);

	//crosshair_pic = { 8, 8, 8, crosshair_data };
	//crosshair_texture = GL_LoadTexture ("crosshair", 128, 128, crosshair_pic, false, 0);

	// load the console background and the charset
	// by hand, because we need to write the version
	// string into the background before turning
	// it into a texture

	draw_chars = W_GetLumpName ("conchars");

	for (i=0 ; i< 256 * 64 ; i++)
		if (draw_chars[i] == 0)
			draw_chars[i] = 255;  // proper transparent color

	// now turn them into textures
	char_texture = GL_LoadTexture ("charset", 128, 128, draw_chars, false, true);

	start = Hunk_LowMark();

	cb = (qpic_t *)COM_LoadTempFile ("gfx/conback.lmp");
 
	if (!cb)
		Sys_Error ("Couldn't load gfx/conback.lmp");

	SwapPic (cb);

	// hack the version number directly into the pic

	#if defined(__linux__)
	sprintf (ver, "(Linux %2.2f, gl %4.2f) %4.2f", (float)LINUX_VERSION, (float)GLQUAKE_VERSION, (float)VERSION);
	#else
	sprintf (ver, "(Amiga %2.2f, MiniGL 1.2) %4.2f", (float)AMIGA_VERSION, (float)VERSION);
	#endif

	dest = cb->data + 320*186 + 320 - 11 - 8*strlen(ver);
	y = strlen(ver);

	for (x=0 ; x<y ; x++)
		Draw_CharToConback (ver[x], dest+(x<<3));

	conback->width = cb->width;
	conback->height = cb->height;
	ncdata = cb->data;

	//glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	//glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min); // 13/02/2000 changed: M.Tretene
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);

	gl = (glpic_t *)conback->data;
	//gl->texnum = GL_LoadTexture ("conback", conback->width, conback->height, ncdata, false, false); //  30/01/2000 modified: M.Tretene
	gl->texnum = GL_LoadTexture ("conback", conback->width, conback->height, ncdata, false, true);
	gl->sl = 0;
	gl->sh = 1;
	gl->tl = 0;
	gl->th = 1;
	conback->width = vid.width;
	conback->height = vid.height;

	// free loaded console
	Hunk_FreeToLowMark(start);

	// save a texture slot for translated picture
	translate_texture = texture_extension_number++;

	#ifdef USE_SCRAP
	// save slot for scraps
	scrap_texnum = texture_extension_number;
	texture_extension_number += MAX_SCRAPS;
	#endif

	//
	// get the other pics we need
	//

	//if (ddisc) // we need it or game crashes with pentagram invulnerability/Sbar_Draw - Cowcat 2019
	draw_disc = Draw_PicFromWad ("disc");

	draw_backtile = Draw_PicFromWad ("backtile");
	//shinytexture = Draw_PicFromWad ("textures/shiny");

	/*
	FILE *f;
	char name[64];
	char boxname[16] = "0";

	sprintf (name, "textures/shiny.pcx", boxname, 0);
	COM_FOpenFile (name, &f);

	if(f)
		return LoadPCX(f);
	*/
}


/*
================
Draw_Character

Draws one 8*8 graphics character with 0 being transparent.
It can be clipped to the top of the screen to allow the console to be
smoothly scrolled off.
================
*/

void Draw_Character (int x, int y, int num)
{	
	int	  row, col;
	float	  frow, fcol, size;

	if (num == 32)
		return; // space

	num &= 255;
  
	if (y <= -8)
		return; // totally off screen

	row = num>>4;
	col = num&15;

	frow = row*0.0625;
	fcol = col*0.0625;	
	size = 0.0625;
    
	GL_Bind (char_texture);

	#ifndef AMIGAOS
	glBegin (GL_QUADS);
	#else
	glBegin (GL_TRIANGLE_FAN);
	#endif
	
	glTexCoord2f (fcol, frow);
	glVertex2f (x, y);
	glTexCoord2f (fcol + size, frow);
	glVertex2f (x+8, y);
	glTexCoord2f (fcol + size, frow + size);
	glVertex2f (x+8, y+8);
	glTexCoord2f (fcol, frow + size);
	glVertex2f (x, y+8);

	glEnd ();
}


/*
================
Draw_String
================
*/

void Draw_String (int x, int y, char *str)
{

#if 0 //Surgeon: very inefficient switching blendmodes and binding same texture multiple times

	while (*str)
	{
		Draw_Character (x, y, *str);
		str++;
		x += 8;
	}
#else

	int	row, col;
	int	num;
	float	frow, fcol;
	float	fx, fy;
	const	float size = 0.0625;

	if (y <= -8)
		return;	    // totally off screen

	GL_Bind (char_texture);

	fy = (float)y;

	while (*str)
	{
		num = *str;

		if (num == 32)
			goto loc0;   // space

		num &= 255;
  
		row = num>>4;
		col = num&15;

		frow = (float)row*0.0625;
		fcol = (float)col*0.0625;

		fx = (float)x;

		#ifndef AMIGAOS
		glBegin (GL_QUADS);
		#else
		glBegin (GL_TRIANGLE_FAN);
		#endif

		glTexCoord2f (fcol, frow);
		glVertex2f (fx, fy);
		glTexCoord2f (fcol + size, frow);
		glVertex2f (fx+8.f, fy);
		glTexCoord2f (fcol + size, frow + size);
		glVertex2f (fx+8.f, fy+8.f);
		glTexCoord2f (fcol, frow + size);
		glVertex2f (fx, fy+8.f);

		glEnd ();

	loc0:
		str++;
		x += 8;
	}

#endif

}


/*
================
Draw_DebugChar

Draws a single character directly to the upper right corner of the screen.
This is for debugging lockups by drawing different chars in different parts
of the code.
================
*/

void Draw_DebugChar (char num)
{
}

/*
=============
Draw_AlphaPic
=============
*/

void Draw_AlphaPic (int x, int y, qpic_t *pic, float alpha)
{	
	glpic_t	 *gl;

	#ifdef USE_SCRAP
	if (scrap_dirty)
		Scrap_Upload ();
	#endif

	gl = (glpic_t *)pic->data;

	glDisable(GL_ALPHA_TEST);
	glEnable (GL_BLEND);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); // fix MH -

	glColor4f (1, 1, 1, alpha);
	GL_Bind (gl->texnum);

	glBegin (GL_TRIANGLE_FAN);

	glTexCoord2f (gl->sl, gl->tl);
	glVertex2f (x, y);
	glTexCoord2f (gl->sh, gl->tl);
	glVertex2f (x+pic->width, y);
	glTexCoord2f (gl->sh, gl->th);
	glVertex2f (x+pic->width, y+pic->height);
	glTexCoord2f (gl->sl, gl->th);
	glVertex2f (x, y+pic->height);

	glEnd ();

	glDisable (GL_BLEND);
	glEnable(GL_ALPHA_TEST);
	glColor4f (1,1,1,1);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE); // fix 
}


/*
=============
Draw_Pic
=============
*/

void Draw_Pic (int x, int y, qpic_t *pic)
{	
	glpic_t *gl;

	#ifdef USE_SCRAP
	if (scrap_dirty)
		Scrap_Upload ();
	#endif

	gl = (glpic_t *)pic->data;

	GL_Bind (gl->texnum);

	#ifndef AMIGAOS
	glBegin (GL_QUADS);
	#else
	glBegin (GL_TRIANGLE_FAN);
	#endif

	glTexCoord2f (gl->sl, gl->tl);
	glVertex2f (x, y);
	glTexCoord2f (gl->sh, gl->tl);
	glVertex2f (x+pic->width, y);
	glTexCoord2f (gl->sh, gl->th);
	glVertex2f (x+pic->width, y+pic->height);
	glTexCoord2f (gl->sl, gl->th);
	glVertex2f (x, y+pic->height);
	
	glEnd ();
}


/*
=============
Draw_TransPic
=============
*/

void Draw_TransPic (int x, int y, qpic_t *pic)
{	
	glpic_t *gl; //surgeon

	if (x < 0 || (unsigned)(x + pic->width) > vid.width || y < 0 || (unsigned)(y + pic->height) > vid.height)
	{
		Sys_Error ("Draw_TransPic: bad coordinates");
	}
    
	//Draw_Pic (x, y, pic);

	//surgeon: inlined
	gl = (glpic_t *)pic->data;
	GL_Bind (gl->texnum);

	#ifndef AMIGAOS
	glBegin (GL_QUADS);
	#else
	glBegin (GL_TRIANGLE_FAN);
	#endif

	float xf, yf;
	xf = x;
	yf = y;
	
	glTexCoord2f (gl->sl, gl->tl);
	glVertex2f (xf, yf);
	glTexCoord2f (gl->sh, gl->tl);
	glVertex2f (xf+pic->width, yf);
	glTexCoord2f (gl->sh, gl->th);
	glVertex2f (xf+pic->width, yf+pic->height);
	glTexCoord2f (gl->sl, gl->th);
	glVertex2f (xf, yf+pic->height);

	glEnd ();  
}


/*
=============
Draw_TransPicTranslate

Only used for the player color selection menu
=============
*/

void Draw_TransPicTranslate (int x, int y, qpic_t *pic, byte *translation)
{
	int	  v, u; // , c;
	unsigned  trans[64*64], *dest;
	byte	  *src;
	int	  p;

	GL_Bind (translate_texture);

	//c = pic->width * pic->height;

	dest = trans;

	for (v=0 ; v<64 ; v++, dest += 64)
	{
		src = &menuplyr_pixels[ ((v*pic->height)>>6) *pic->width];

		for (u=0 ; u<64 ; u++)
		{
			p = src[(u*pic->width)>>6];

			if (p == 255)
				dest[u] = p;

			else
				dest[u] = d_8to24table[translation[p]];
		}
	}

	glTexImage2D (GL_TEXTURE_2D, 0, gl_alpha_format, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, trans);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	#ifndef AMIGAOS
	glBegin (GL_QUADS);
	#else
	glBegin (GL_TRIANGLE_FAN);
	#endif

	glTexCoord2f (0, 0);
	glVertex2f (x, y);
	glTexCoord2f (1, 0);
	glVertex2f (x+pic->width, y);
	glTexCoord2f (1, 1);
	glVertex2f (x+pic->width, y+pic->height);
	glTexCoord2f (0, 1);
	glVertex2f (x, y+pic->height);
	glEnd ();
}


/*
================
Draw_ConsoleBackground

================
*/

/* ANIMATED BACKDROP, 2026-09-19. Cowcat wrote this in 2019 and switched it off
 * behind #if, with the note on its glMatrixMode line -- "minigl has to be updated
 * for this" -- because MiniGL had no GL_TEXTURE matrix mode. MiniGLV3D has one now,
 * so it runs, and the #if is gone: `gl_conanim` picks between the two at runtime.
 * Default 1 (animated); set it to 0 for the plain static backdrop. Archived, so
 * whichever you choose survives into the next session -- which is the point here,
 * unlike a measurement cvar, where that is a trap.
 *
 * The cvar is read ONCE into a local. Reading it twice would risk pushing the
 * texture matrix and not popping it if it changed mid-function, which would leak a
 * rotated matrix into everything drawn afterwards. */
void Draw_ConsoleBackground (int lines)
{
	static float xangle = 0, xfactor = .3f, xstep = .01f;

	int y = (vid.height * 3) >> 2;
	int animated = (gl_conanim.value != 0);

	if (animated)
	{
		glPushMatrix();
		glMatrixMode(GL_TEXTURE);
		glPushMatrix();
		glLoadIdentity();

		xangle += 1.0f;
		xfactor += -xstep;

		if(xfactor > 8 || xfactor < .3f)
			xstep = -xstep;

		glRotatef(xangle, 0, 0, 1);
		glScalef(xfactor, xfactor, xfactor);
	}

	if (lines > y || gl_conalpha.value == 1)
		Draw_Pic(0, lines - vid.height, conback);

	else
		Draw_AlphaPic (0, lines - vid.height, conback, gl_conalpha.value);

	if (animated)
	{
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
		glPopMatrix();
	}
}

#if 0

void Draw_CrossHair(void)
{
	int		x, y;
	unsigned char	*pColor;

	if(crosshair.value == 2)
	{
		x = scr_vrect.x + scr_vrect.width / 2 - 3 + cl_crossx.value;
		y = scr_vrect.y + scr_vrect.height / 2 - 3 + cl_crossy.value;

		glTexEnv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

		pColor = (unsigned char *)&d_8to24table[(byte)crosshaircolor.value];
		glColor4ubv(pColor);
		GL_Bind(crosshair_texture);

		glBegin(GL_TRIANGLE_FAN);
		glTexCoord2f (0, 0);
		glVertex2f (x - 4, y - 4);
		glTexCoord2f (1, 0);
		glVertex2f (x + 12, y - 4);
		glTexCoord2f (1, 1);
		glVertex2f (x + 12, y + 12);
		glTexCoord2f (0, 1);
		glVertex2f (x - 4, y + 12);
		glEnd ();

		glTexEnv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	}

	else
	{

	}
}
#endif

/*
=============
Draw_TileClear

This repeats a 64*64 tile graphic to fill the screen around a sized down
refresh window.
=============
*/

void Draw_TileClear (int x, int y, int w, int h)
{
	GL_Bind (*(int *)draw_backtile->data);

	#ifndef AMIGAOS
	glBegin (GL_QUADS);
	#else
	glBegin (GL_TRIANGLE_FAN);
	#endif

	glTexCoord2f ( x/64.0, y/64.0 );
	glVertex2f (x, y);
	glTexCoord2f ( (x+w)/64.0, y/64.0 );
	glVertex2f (x+w, y);
	glTexCoord2f ( (x+w)/64.0, (y+h)/64.0 );
	glVertex2f (x+w, y+h);
	glTexCoord2f ( x/64.0, (y+h)/64.0 );
	glVertex2f (x, y+h);

	glEnd ();
}


/*
=============
Draw_Fill

Fills a box of pixels with a single color
=============
*/

void Draw_Fill (int x, int y, int w, int h, int c)
{
	glDisable (GL_TEXTURE_2D);

	glColor3f (host_basepal[c*3]/255.0, host_basepal[c*3+1]/255.0, host_basepal[c*3+2]/255.0);

	#ifndef AMIGAOS
	glBegin (GL_QUADS);
	#else
	glBegin (GL_TRIANGLE_FAN);
	#endif

	glVertex2f (x, y);
	glVertex2f (x+w, y);
	glVertex2f (x+w, y+h);
	glVertex2f (x, y+h);

	glEnd ();
	glColor3f (1,1,1);
	glEnable (GL_TEXTURE_2D);
}

//=============================================================================

/*
================
Draw_FadeScreen

================
*/

void Draw_FadeScreen (void)
{
	glEnable (GL_BLEND);
	glDisable (GL_TEXTURE_2D);
	glColor4f (0, 0, 0, 0.5);

	#ifndef AMIGAOS
	glBegin (GL_QUADS);
	#else
	glBegin (GL_TRIANGLE_FAN);
	#endif

	glVertex2f (0, 0);
	glVertex2f (vid.width, 0);
	glVertex2f (vid.width, vid.height);
	glVertex2f (0, vid.height);

	glEnd ();
	glColor4f (1,1,1,1);

	glEnable (GL_TEXTURE_2D);
	glDisable (GL_BLEND);

	Sbar_Changed();
}

//=============================================================================


/*
================
Draw_BeginDisc

Draws the little blue disc in the corner of the screen.
Call before beginning any disc IO.
================
*/

void Draw_BeginDisc (void)
{
	if (!ddisc) // fix - Cowcat 2019
		return;
  
	if (!draw_disc)
		return;

	mglLockMode(MGL_LOCK_AUTOMATIC);

	glDrawBuffer  (GL_FRONT);  
	Draw_Pic (vid.width -24, 0, draw_disc);	 
	glDrawBuffer  (GL_BACK);

	mglLockMode(gl_lockmode);
}

/*

================
Draw_EndDisc

Erases the disc icon.
Call after completing any disc IO
================
*/
void Draw_EndDisc (void)
{
}

/*
================
GL_Set2D

Setup as if the screen was 320*200
================
*/

void GL_Set2D (void)
{
	/* BUG FIX, 1st pass (2026-08-03): R_DrawSequentialPoly's real-
	 * multitexture branch (gl_rsurf.c) never calls GL_DisableMultitexture()
	 * before returning, same class of "rely on a later surface to reset
	 * it" gap as everywhere else in that file. This is a real, harmless
	 * defensive fix worth keeping, but hardware-tested: it did NOT fix
	 * the reported font corruption -- a real hardware diagnostic log
	 * (MGLV3D_DIAG_MULTITEX_2D, draw.c) confirmed the broken text draws
	 * have mtex=0 the whole time, so multitexture was never actually the
	 * mechanism here. */
	if (mtexenabled)
		GL_DisableMultitexture();

	/* REAL BUG FIX, 2nd pass, confirmed via the same hardware diagnostic
	 * log: the broken text draws show smooth=1 -- MiniGLV3D's own
	 * alphatest dispatch (draw.c) requires `!smooth`, so ANY smooth-
	 * shaded draw can never use the alpha-test shader regardless of
	 * whether GL_ALPHA_TEST is enabled. glShadeModel is a real, PERSISTENT
	 * GL state (unlike blend/depth-test, which GL_Set2D already resets
	 * every frame) -- glquake defaults it to GL_FLAT once at init
	 * (gl_vidamiga.c) but R_RenderDlights/R_RenderGlows (gl_rlight.c)
	 * set it to GL_SMOOTH for their additive glow/light quads and never
	 * restore it afterward (unlike everything else they DO restore:
	 * color, blend, texture, depth mask) -- once any dynamic light or
	 * glow renders (nearly every real gameplay frame), shademodel stays
	 * stuck SMOOTH forever after, and Draw_Character's font quads
	 * inherit it, permanently losing alpha-test discard for their
	 * "transparent" background texels. Matches the user's report
	 * exactly: correct at boot (before any light/glow has ever
	 * rendered), permanently broken from the first one onward. Text
	 * never needs interpolated per-vertex color anyway, so forcing FLAT
	 * here is always safe. */
	glShadeModel (GL_FLAT);

	//glViewport (glx, gly, glwidth, glheight);  //(don�t touch!)
	glViewport (0, 0, glwidth, glheight);

	/* FIX (2026-08-23, MGL_FLATFAN full removal): AmigaOS now uses the
	 * exact same real glOrtho + normal-primitive path every other
	 * platform already used here -- see this project's own
	 * feedback_mgl_flatfan_recurring_fragility memory for why: FLATFAN
	 * bypassed the whole transform pipeline via raw window-space pixel
	 * coordinates, with three unenforced caller-side invariants that
	 * caused real bugs in every consumer that used it. R_PolyBlend
	 * (gl_rmain.c) already proved this exact glOrtho(0, vid.width,
	 * vid.height, 0, -99999, 99999) mapping works correctly on real
	 * hardware for AmigaOS (2026-08-08 fix) -- applying it here too,
	 * unconditionally, removes the bypass entirely instead of leaving
	 * it as a one-off exception. */
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity ();
	glOrtho	 (0, vid.width, vid.height, 0, -99999, 99999);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity ();

	glDisable (GL_DEPTH_TEST);
	glDisable (GL_CULL_FACE);
	glDisable (GL_BLEND);
	glEnable (GL_ALPHA_TEST);

	/* FIX (2026-08-23): MGL_PERSPECTIVE_MAPPING disable no longer
	 * needed -- that was specifically compensating for FLATFAN's own
	 * affine-vs-perspective-correct texture mapping mismatch. With a
	 * real orthographic projection, every 2D vertex has w=1 uniformly,
	 * so perspective-correct interpolation degenerates to the same
	 * result as affine anyway -- nothing to compensate for. */

	glColor4f (1,1,1,1);
}

//====================================================================

/*
================
GL_FindTexture
================
*/

int GL_FindTexture (char *identifier)
{
	int	i;
	gltexture_t *glt;

	for (i=0, glt=gltextures ; i<numgltextures ; i++, glt++)
	{
		if (!strcmp (identifier, glt->identifier))
			return gltextures[i].texnum;
	}

	return -1;
}

/*
================
GL_ResampleTexture
================
*/

void GL_ResampleTexture (unsigned *in, int inwidth, int inheight, unsigned *out, int outwidth, int outheight)
{
	int	  i, j;
	unsigned  *inrow;
	unsigned  frac, fracstep;

	fracstep = inwidth*0x10000/outwidth;

	for (i=0 ; i<outheight ; i++, out += outwidth)
	{
		inrow = in + inwidth*(i*inheight/outheight);
		frac = fracstep >> 1;

		for (j=0 ; j<outwidth ; j+=4)
		{
			out[j] = inrow[frac>>16];
			frac += fracstep;
			out[j+1] = inrow[frac>>16];
			frac += fracstep;
			out[j+2] = inrow[frac>>16];
			frac += fracstep;
			out[j+3] = inrow[frac>>16];
			frac += fracstep;
		}
	}
}

/*
================
GL_Resample8BitTexture -- JACK
================
*/

void GL_Resample8BitTexture (unsigned char *in, int inwidth, int inheight, unsigned char *out, int outwidth, int outheight)
{
	int	  i, j;
	unsigned  char *inrow;
	unsigned  frac, fracstep;

	fracstep = inwidth*0x10000/outwidth;

	for (i=0 ; i<outheight ; i++, out += outwidth)
	{
		inrow = in + inwidth*(i*inheight/outheight);
		frac = fracstep >> 1;

		for (j=0 ; j<outwidth ; j+=4)
		{
			out[j] = inrow[frac>>16];
			frac += fracstep;
			out[j+1] = inrow[frac>>16];
			frac += fracstep;
			out[j+2] = inrow[frac>>16];
			frac += fracstep;
			out[j+3] = inrow[frac>>16];
			frac += fracstep;
		}
	}
}


/*
================
GL_MipMap

Operates in place, quartering the size of the texture
================
*/

void GL_MipMap (byte *in, int width, int height)
{
	int   i, j;
	byte  *out;

	width <<=2;
	height >>= 1;
	out = in;
  
	for (i=0 ; i<height ; i++, in+=width)
	{
		for (j=0 ; j<width ; j+=8, out+=4, in+=8)
		{
			out[0] = (in[0] + in[4] + in[width+0] + in[width+4])>>2;
			out[1] = (in[1] + in[5] + in[width+1] + in[width+5])>>2;
			out[2] = (in[2] + in[6] + in[width+2] + in[width+6])>>2;
			out[3] = (in[3] + in[7] + in[width+3] + in[width+7])>>2;
		}
	}
}

/*
================
GL_MipMap8Bit

Mipping for 8 bit textures
================
*/

#if 0 // not used - Cowcat
void GL_MipMap8Bit (byte *in, int width, int height)
{
	int		i, j;
	unsigned short	r,g,b;
	byte		*out, *at1, *at2, *at3, *at4;

	//width <<=2;
	height >>= 1;
	out = in;

	for (i=0 ; i<height ; i++, in+=width)
	{
		for (j=0 ; j<width ; j+=2, out+=1, in+=2)
		{
			at1 = (byte *) (d_8to24table + in[0]);
			at2 = (byte *) (d_8to24table + in[1]);
			at3 = (byte *) (d_8to24table + in[width+0]);
			at4 = (byte *) (d_8to24table + in[width+1]);

			r = (at1[0]+at2[0]+at3[0]+at4[0]); r>>=5;
			g = (at1[1]+at2[1]+at3[1]+at4[1]); g>>=5;
			b = (at1[2]+at2[2]+at3[2]+at4[2]); b>>=5;

			out[0] = d_15to8table[(r<<0) + (g<<5) + (b<<10)];
		}
	}
}
#endif

/*
===============
GL_Upload32
===============
*/

void GL_Upload32 (unsigned *data, int width, int height, qboolean mipmap, qboolean alpha)
{
	int	samples;
	static	unsigned  scaled[1024*512]; // [512*256];
	int	scaled_width, scaled_height;

	for (scaled_width = 1 ; scaled_width < width ; scaled_width<<=1)
	;

	for (scaled_height = 1 ; scaled_height < height ; scaled_height<<=1)
	;

	scaled_width >>= (int)gl_picmip.value;
	scaled_height >>= (int)gl_picmip.value;

	if (scaled_width > gl_max_size.value)
		scaled_width = gl_max_size.value;

	if (scaled_height > gl_max_size.value)
		scaled_height = gl_max_size.value;

	if (scaled_width * scaled_height > sizeof(scaled)/4)
		Sys_Error ("GL_LoadTexture: too big");

	samples = alpha ? gl_alpha_format : gl_solid_format;

#if 0
	if (mipmap)
		gluBuild2DMipmaps (GL_TEXTURE_2D, samples, width, height, GL_RGBA, GL_UNSIGNED_BYTE, trans);

	else if (scaled_width == width && scaled_height == height)
		glTexImage2D (GL_TEXTURE_2D, 0, samples, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, trans);

	else
	{
		gluScaleImage (GL_RGBA, width, height, GL_UNSIGNED_BYTE, trans,
		scaled_width, scaled_height, GL_UNSIGNED_BYTE, scaled);
		glTexImage2D (GL_TEXTURE_2D, 0, samples, scaled_width, scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, scaled);
	}
#else

	texels += scaled_width * scaled_height;

	if (scaled_width == width && scaled_height == height)
	{
		if (!mipmap)
		{
			glTexImage2D (GL_TEXTURE_2D, 0, samples, scaled_width, scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
			goto done;
		}

		memcpy (scaled, data, width*height*4);
	}

	else
		GL_ResampleTexture (data, width, height, scaled, scaled_width, scaled_height);

	glTexImage2D (GL_TEXTURE_2D, 0, samples, scaled_width, scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, scaled);

	if (mipmap)
	{
		int   miplevel;

		miplevel = 0;

		while (scaled_width > 1 || scaled_height > 1)
		{
			GL_MipMap ((byte *)scaled, scaled_width, scaled_height);
			scaled_width >>= 1;
			scaled_height >>= 1;

			if (scaled_width < 1)
				scaled_width = 1;

			if (scaled_height < 1)
				scaled_height = 1;

			miplevel++;
			glTexImage2D (GL_TEXTURE_2D, miplevel, samples, scaled_width, scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, scaled);
		}
	}

	done: ;
#endif

	if (mipmap)
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
	}

	else
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_max);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
	}
}

#if 0
void GL_Upload8_EXT (byte *data, int width, int height,	 qboolean mipmap, qboolean alpha) 
{
	int		i, s;
	qboolean	noalpha;
	int		p;
	static unsigned j;
	int		samples;
	static		unsigned char scaled[1024*512]; // [512*256];
	int		scaled_width, scaled_height;

	s = width*height;
	// if there are no transparent pixels, make it a 3 component
	// texture even if it was specified as otherwise

	/*
	if (alpha)
	{
		noalpha = true;

		for (i=0 ; i<s ; i++)
		{
			if (data[i] == 255)
				noalpha = false;
		}

		if (alpha && noalpha)
			alpha = false;
	}
	*/

	for (scaled_width = 1 ; scaled_width < width ; scaled_width<<=1)
	;

	for (scaled_height = 1 ; scaled_height < height ; scaled_height<<=1)
	;

	scaled_width >>= (int)gl_picmip.value;
	scaled_height >>= (int)gl_picmip.value;

	if (scaled_width > gl_max_size.value)
		scaled_width = gl_max_size.value;

	if (scaled_height > gl_max_size.value)
		scaled_height = gl_max_size.value;

	if (scaled_width * scaled_height > sizeof(scaled))
		Sys_Error ("GL_LoadTexture: too big");

	samples = 1; // alpha ? gl_alpha_format : gl_solid_format;

	texels += scaled_width * scaled_height;

	if (scaled_width == width && scaled_height == height)
	{
		if (!mipmap)
		{
			glTexImage2D (GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT, scaled_width, scaled_height, 0, GL_COLOR_INDEX , GL_UNSIGNED_BYTE, data);
			goto done;
		}

		memcpy (scaled, data, width*height);
	}

	else
		GL_Resample8BitTexture (data, width, height, scaled, scaled_width, scaled_height);

	glTexImage2D (GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT, scaled_width, scaled_height, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, scaled);

	if (mipmap)
	{
		int   miplevel;

		miplevel = 0;

		while (scaled_width > 1 || scaled_height > 1)
		{
			GL_MipMap8Bit ((byte *)scaled, scaled_width, scaled_height);
			scaled_width >>= 1;
			scaled_height >>= 1;

			if (scaled_width < 1)
				scaled_width = 1;

			if (scaled_height < 1)
				scaled_height = 1;

			miplevel++;
			glTexImage2D (GL_TEXTURE_2D, miplevel, GL_COLOR_INDEX8_EXT, scaled_width, scaled_height, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, scaled);
		}
	}

	done: ;


	if (mipmap)
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
	}

	else
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_max);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
	}
}
#endif

/*
===============
GL_Upload8
===============
*/

void GL_Upload8 (byte *data, int width, int height, qboolean mipmap, qboolean alpha)
{
	static unsigned trans[640*480];	  // FIXME, temporary
	int		i, s;
	qboolean	noalpha;
	int		p;

	s = width*height;
	
	// if there are no transparent pixels, make it a 3 component
	// texture even if it was specified as otherwise

	if (alpha)
	{
		noalpha = true;

		for (i=0 ; i<s ; i++)
		{
			p = data[i];

			if (p == 255)
				noalpha = false;

			trans[i] = d_8to24table[p];
		}

		if (alpha && noalpha)
			alpha = false;
	}

	else
	{
		if (s&3)
			Sys_Error ("GL_Upload8: s&3");

		for (i=0 ; i<s ; i+=4)
		{
			trans[i]   = d_8to24table[data[i]];
			trans[i+1] = d_8to24table[data[i+1]];
			trans[i+2] = d_8to24table[data[i+2]];
			trans[i+3] = d_8to24table[data[i+3]];
		}
	}

#ifndef AMIGAOS

	if (VID_Is8bit() && !alpha && (data != scrap_texels[0]))
	{
		GL_Upload8_EXT (data, width, height, mipmap, alpha);
		return;
	}
#endif

	GL_Upload32 (trans, width, height, mipmap, alpha);
}

/*
================
GL_LoadTexture
================
*/

#if 0
void AlphaEdgeFix (byte *data, int width, int height)
{
	int	i, j, n = 0, b, c[3] = {0,0,0}, lastrow, thisrow, nextrow, lastpix, thispix, nextpix;
	byte	*dest = data;

	for(i = 0; i < height; i++)
	{
		lastrow = width * 4 * ((i == 0) ? height-1 : i-1);
		thisrow = width * 4 * i;
		nextrow = width * 4 * ((i == height-1) ? 0 : i+1);

		for(j = 0; j < width; j++, dest += 4)
		{
			if(dest[3])
				continue;

			lastpix = 4 * ((j == 0) ? width-1 : j-1);
			thispix = 4 * j;
			nextpix = 4 * ((j == width-1) ? 0 : j+1);

			b = lastrow + lastpix; if (data[b+3]) {c[0] +=data[b]; c[1] +=data[b+1]; c[2] +=data[b+2]; n++;}
			b = thisrow + lastpix; if (data[b+3]) {c[0] +=data[b]; c[1] +=data[b+1]; c[2] +=data[b+2]; n++;}
			b = nextrow + lastpix; if (data[b+3]) {c[0] +=data[b]; c[1] +=data[b+1]; c[2] +=data[b+2]; n++;}
			b = lastrow + thispix; if (data[b+3]) {c[0] +=data[b]; c[1] +=data[b+1]; c[2] +=data[b+2]; n++;}
			b = nextrow + thispix; if (data[b+3]) {c[0] +=data[b]; c[1] +=data[b+1]; c[2] +=data[b+2]; n++;}
			b = lastrow + nextpix; if (data[b+3]) {c[0] +=data[b]; c[1] +=data[b+1]; c[2] +=data[b+2]; n++;}
			b = thisrow + nextpix; if (data[b+3]) {c[0] +=data[b]; c[1] +=data[b+1]; c[2] +=data[b+2]; n++;}
			b = nextrow + nextpix; if (data[b+3]) {c[0] +=data[b]; c[1] +=data[b+1]; c[2] +=data[b+2]; n++;}

			if(n)
			{
				dest[0] = (byte)(c[0]/n);
				dest[1] = (byte)(c[1]/n);
				dest[2] = (byte)(c[2]/n);

				n = c[0] = c[1] = c[2] = 0;
			}
		}
	}
}
#endif

#if 0

int GL_LoadTexture (char *identifier, int width, int height, byte *data, qboolean mipmap, qboolean alpha)
{
	int		i;
	gltexture_t	*glt;
  
	// see if the texture is already present
	//if (identifier != NULL)  //--- null identifier fucks wos permedia
  
	if (identifier[0])
	{
		for (i=0, glt=gltextures ; i<numgltextures ; i++, glt++)
		{
			if (!strcmp (identifier, glt->identifier))
			{
				if (width != glt->width || height != glt->height)
					Sys_Error ("GL_LoadTexture: cache mismatch");

				return gltextures[i].texnum;
			}
		}
	}
  
	glt = &gltextures[numgltextures];
	numgltextures++;

	strcpy (glt->identifier, identifier);
	glt->texnum = texture_extension_number;
	glt->width = width;
	glt->height = height;
	glt->mipmap = mipmap;

	GL_Bind(texture_extension_number );
		
	GL_Upload8 (data, width, height, mipmap, alpha);

	texture_extension_number++;

	return texture_extension_number-1;
}

#else

int lhcsumtable[256];

int GL_LoadTexture (char *identifier, int width, int height, byte *data, qboolean mipmap, qboolean alpha)
{
	int		i, p, s;
	gltexture_t	*glt;
	int		lhcsum;

	for(i = 0; i<256 ; i++)
	{
		lhcsumtable[i] = i + 1;
	}

	s = width * height;
	lhcsum = 0;

	for(i = 0; i<s ; i++)
	{
		lhcsum += (lhcsumtable[data[i] & 255]++);
	}

	if (identifier[0])
	{
		for (i=0, glt=gltextures ; i<numgltextures ; i++, glt++)
		{
			if (!strcmp (identifier, glt->identifier))
			{
				if ( lhcsum != glt->lhcsum || width != glt->width || height != glt->height)
				{
					Con_DPrintf ("GL_LoadTexture: cache mismatch, replacing old texture\n");
					goto GL_LoadTexture_Setup;
				}

				return glt->texnum;
			}
		}
	}

	if(numgltextures >= MAX_GLTEXTURES)
		Sys_Error("GL_LoadTexture: cache full, max is %i textures", MAX_GLTEXTURES);

	glt = &gltextures[numgltextures];
	numgltextures++;
	strcpy (glt->identifier, identifier);
	glt->texnum = texture_extension_number;
	texture_extension_number++;

GL_LoadTexture_Setup:
	glt->lhcsum = lhcsum;
	glt->width = width;
	glt->height = height;
	glt->mipmap = mipmap;

	if(cls.state != ca_dedicated)
	{
		GL_Bind(glt->texnum);
		
		GL_Upload8 (data, width, height, mipmap, alpha);
	}

	return glt->texnum;
}

#endif

/*
================
GL_LoadPicTexture
================
*/

int GL_LoadPicTexture (qpic_t *pic)
{
	return GL_LoadTexture ("", pic->width, pic->height, pic->data, false, true);
}


/****************************************/

/*
static GLenum oldtarget = GL_TEXTURE0_ARB;

void GL_SelectTexture (GLenum target) 
{
	if (!gl_mtexable)
		return;

	glActiveTextureARB(target);

	if (target == oldtarget) 
		return;

	cnttextures[oldtarget - GL_TEXTURE0_ARB] = currenttexture;
	currenttexture = cnttextures[target - GL_TEXTURE0_ARB];
	oldtarget = target;
}
*/
