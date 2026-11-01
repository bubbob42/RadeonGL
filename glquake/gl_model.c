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
// models.c -- model loading and caching

// models are the only shared resource between a client and server running
// on the same machine.

#include "quakedef.h"

model_t *loadmodel;
char	loadname[32]; // for hunk tags

void	Mod_LoadSpriteModel (model_t *mod, void *buffer);
void	Mod_LoadBrushModel (model_t *mod, void *buffer);
void	Mod_LoadAliasModel (model_t *mod, void *buffer);
model_t *Mod_LoadModel (model_t *mod, qboolean crash);

//byte	mod_novis[MAX_MAP_LEAFS/8];
static byte	*mod_novis;		// new 2019 - cowcat
static int	mod_novis_capacity;	//

static byte	*mod_decompressed;	//
static int	mod_decompressed_capacity; //


#define MAX_MOD_KNOWN 2048	// was 512 - Johnfitz
model_t mod_known[MAX_MOD_KNOWN];
int	mod_numknown;

cvar_t	gl_subdivide_size = {"gl_subdivide_size", "1024", false};

/*
===============
Mod_Init
===============
*/

void Mod_Init (void)
{
	Cvar_RegisterVariable (&gl_subdivide_size);
	//memset (mod_novis, 0xff, sizeof(mod_novis)); // disabled 2019 - Cowcat - now in NoVisPVS
}

/*
===============
Mod_Init

Caches the data if needed
===============
*/

void *Mod_Extradata (model_t *mod)
{
	void  *r;
  
	r = Cache_Check (&mod->cache);

	if (r)
		return r;

	Mod_LoadModel (mod, true);
  
	if (!mod->cache.data)
		Sys_Error ("Mod_Extradata: caching failed");

	return mod->cache.data;
}

/*
===============
Mod_PointInLeaf
===============
*/

#if 1

mleaf_t *Mod_PointInLeaf (vec3_t p, model_t *model)
{
	mnode_t	 *node;
	float	 d;
	mplane_t *plane;
  
	if (!model || !model->nodes)
		Sys_Error ("Mod_PointInLeaf: bad model");

	node = model->nodes;

	while (1)
	{
		if (node->contents < 0)
			return (mleaf_t *)node;

		plane = node->plane;

		/* 
		if (plane->type < 3)
			d = p[plane->type] - plane->dist;

		else
		*/
			
		d = DotProduct (p, plane->normal) - plane->dist;

		if (d > 0)
			node = node->children[0];

		else
			node = node->children[1];

	}
  
	//return NULL;	// never reached
}

#else // LordHavoc

mleaf_t *Mod_PointInLeaf (vec3_t p, model_t *model)
{
	mnode_t	 *node;
	
	node = model->nodes;

	do
	{
		node = node->children [ (node->plane->type < 3 ? p[node->plane->type] : DotProduct (p, node->plane->normal)) < node->plane->dist ];

	} while (node->contents == 0);

	return (mleaf_t *)node;

}
#endif


/*
===================
Mod_DecompressVis
===================
*/

byte *Mod_DecompressVis (byte *in, model_t *model)
{
	//static byte decompressed[MAX_MAP_LEAFS/8]; // disabled - Cowcat 2019
	int	c;
	byte	*out;
	int	row;

	row = (model->numleafs+7)>>3;

	if( mod_decompressed == NULL || row > mod_decompressed_capacity ) // new 2019 - Cowcat
	{
		mod_decompressed_capacity = row;
		mod_decompressed = (byte *) realloc (mod_decompressed, mod_decompressed_capacity);

		if(!mod_decompressed)
			Sys_Error("Mod_DecompressVis: Realloc() failed on %d bytes", mod_decompressed_capacity);
	}

	//out = decompressed;
	out = mod_decompressed; // new 2019

	if (!in)
	{
		// no vis info, so make all visible
		while (row)
		{
			*out++ = 0xff;
			row--;
		}
   
		//return decompressed;
		return mod_decompressed; // new	 2019
	}

	do
	{
		if (*in)
		{
			*out++ = *in++;
			continue;
		}
  
		c = in[1];
		in += 2;

		while (c)
		{
			*out++ = 0;
			c--;
		}

	} //while (out - decompressed < row);
	while (out - mod_decompressed < row); // new 2019

	//return decompressed;
	return mod_decompressed; // new 2019
}

byte *Mod_NoVisPVS (model_t *model)  // new 2019 - Cowcat
{
	int pvsbytes;

	pvsbytes = (model->numleafs+7)>>3;

	if( mod_novis == NULL || pvsbytes > mod_novis_capacity )
	{
		mod_novis_capacity = pvsbytes;
		mod_novis = (byte *) realloc (mod_novis, mod_novis_capacity);

		if(!mod_novis)
			Sys_Error("Mod_NoVisPVS: Realloc() failed on %d bytes", mod_novis_capacity);

		memset (mod_novis, 0xff, mod_novis_capacity);
	}

	return mod_novis;
}

byte *Mod_LeafPVS (mleaf_t *leaf, model_t *model)
{
	if (leaf == model->leafs)
		//return mod_novis;
		return Mod_NoVisPVS(model); // new 2019 - Cowcat

	return Mod_DecompressVis (leaf->compressed_vis, model);
}


/*
===================
Mod_ClearAll
===================
*/

void Mod_ClearAll (void)
{
	int	i;
	model_t *mod;
  
	for (i=0 , mod=mod_known ; i<mod_numknown ; i++, mod++)
		if (mod->type != mod_alias)
			mod->needload = true;
}

/*
==================
Mod_FindName

==================
*/

model_t *Mod_FindName (char *name)
{
	int	i;
	model_t *mod;
  
	if (!name[0])
		Sys_Error ("Mod_FindName: NULL name"); // was ForName
    
	//
	// search the currently loaded models
	//

	for (i=0 , mod=mod_known ; i<mod_numknown ; i++, mod++)
		if (!strcmp (mod->name, name) )
			break;
      
	if (i == mod_numknown)
	{
		if (mod_numknown == MAX_MOD_KNOWN)
			Sys_Error ("mod_numknown == MAX_MOD_KNOWN");

		strcpy (mod->name, name);
		mod->needload = true;
		mod_numknown++;
	}

	return mod;
}

/*
==================
Mod_TouchModel

==================
*/

void Mod_TouchModel (char *name)
{
	model_t *mod;
  
	mod = Mod_FindName (name);
  
	if (!mod->needload)
	{
		if (mod->type == mod_alias)
			Cache_Check (&mod->cache);
	}
}

/*
==================
Mod_LoadModel

Loads a model into the cache
==================
*/

model_t *Mod_LoadModel (model_t *mod, qboolean crash) // modded as qspasm/mark v
{
	void	*d;
	byte	*buf;		// was unsigned
	byte	stackbuf[1024]; // avoid dirtying the cache heap
	int	mod_type;	// test

	if (!mod->needload)
	{
		if (mod->type == mod_alias)
		{
			d = Cache_Check (&mod->cache);

			if (d)
				return mod;
		}

		else
			return mod;   // not cached at all
	}

	//
	// because the world is so huge, load it one piece at a time
	//

	if (!crash)
	{
  
	}
  
	//
	// load the file
	//

	buf = (byte *)COM_LoadStackFile (mod->name, stackbuf, sizeof(stackbuf)); // was (unsigned *)

	if (!buf)
	{
		if (crash)
			Sys_Error ("Mod_LoadModel: %s not found", mod->name); // was NumForName

		return NULL;
	}
  
	//
	// allocate a new model
	//

	COM_FileBase (mod->name, loadname, sizeof(loadname)); // sizeof Cowcat
  
	loadmodel = mod;

	//
	// fill it in
	//

	// call the apropriate loader
	mod->needload = false;

	mod->modhint = 0; // needed ?? test Cowcat
  
	mod_type = (buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24)); //

	//switch (LittleLong(*(unsigned *)buf))
	switch(mod_type)
	{
		case IDPOLYHEADER:
			Mod_LoadAliasModel (mod, buf);
			break;
    
		case IDSPRITEHEADER:
			Mod_LoadSpriteModel (mod, buf);
			break;
  
		default:
			Mod_LoadBrushModel (mod, buf);
			break;
	}

	return mod;
}

/*
==================
Mod_ForName

Loads in a model for the given name
==================
*/

model_t *Mod_ForName (char *name, qboolean crash)
{
	model_t *mod;
	mod = Mod_FindName (name);
  
	return Mod_LoadModel (mod, crash);
}


/*
===============================================================================

	  BRUSHMODEL LOADING

===============================================================================
*/

byte  *mod_base;

/*
=================
Mod_LoadTextures
=================
*/

void Mod_LoadTextures (lump_t *l)
{
	int		i, j, num, max, altmax; // pixels;
	miptex_t	*mt;
	texture_t	*tx, *tx2;
	texture_t	*anims[10];
	texture_t	*altanims[10];
	dmiptexlump_t	*m;

	if (!l->filelen)
	{
		loadmodel->textures = NULL;
		return;
	}

	m = (dmiptexlump_t *)(mod_base + l->fileofs);
  
	m->nummiptex = LittleLong (m->nummiptex);
  
	loadmodel->numtextures = m->nummiptex;

	//tx = Hunk_Alloc(loadmodel->numtextures * sizeof(texture_t)); // mh !! NEW !!!!!!!!!!!

	loadmodel->textures = Hunk_AllocName (m->nummiptex * sizeof(*loadmodel->textures) , loadname);
	//loadmodel->textures = Hunk_Alloc(loadmodel->numtextures * sizeof (texture_t *));

	for (i=0 ; i<m->nummiptex ; i++)
	{
		m->dataofs[i] = LittleLong(m->dataofs[i]);

		if (m->dataofs[i] == -1)
			continue;

		mt = (miptex_t *)((byte *)m + m->dataofs[i]);
		mt->width = LittleLong (mt->width);
		mt->height = LittleLong (mt->height);

		for (j=0 ; j<MIPLEVELS ; j++)
			mt->offsets[j] = LittleLong (mt->offsets[j]);
    
		#if 0
		// HACK HACK

		if(!strcmp(mt->name, "shot1sid") && mt->width == 32 && mt->height == 32 && CRC_Block((byte *)(mt + 1), mt->width * mt->height) == 65393)
		{
			memcpy (mt + 1, (byte *)(mt + 1) + 32 * 31, 32);
		}

		#endif

		if ( (mt->width & 15) || (mt->height & 15) )
			Sys_Error ("Texture %s is not 16 aligned", mt->name);

		//pixels = mt->width*mt->height/64*85; // if memory saving -> disable

		//tx = Hunk_AllocName (sizeof(texture_t) + pixels, loadname );
		tx = Hunk_AllocName (sizeof(texture_t), loadname ); // memory saving mh tutorial 2017 //  !!!!

		loadmodel->textures[i] = tx;

		memcpy (tx->name, mt->name, sizeof(tx->name)); 
		tx->width = mt->width;
		tx->height = mt->height;

		for (j=0 ; j<MIPLEVELS ; j++)
			tx->offsets[j] = mt->offsets[j] + sizeof(texture_t) - sizeof(miptex_t);

		// the pixels immediately follow the structures
		//memcpy ( tx+1, mt+1, pixels); // if memory saving -> disable
    

		if (!Q_strncasecmp(mt->name,"sky",3)) // was strncmp, changed to match qbsp
			//R_InitSky (tx); 
			R_InitSky(mt); // memory saving

		else
		{
			//texture_mode = GL_LINEAR_MIPMAP_NEAREST; //_LINEAR;
			//texture_mode = GL_LINEAR;
			//tx->gl_texturenum = GL_LoadTexture (mt->name, tx->width, tx->height, (byte *)(tx+1), true, false);
			tx->gl_texturenum = GL_LoadTexture (mt->name, tx->width, tx->height, (byte *)(mt+1), true, false); // - memory saving mh
			//texture_mode = GL_LINEAR;
		}
	}

	//
	// sequence the animations
	//

	for (i=0 ; i<m->nummiptex ; i++)
	{
		tx = loadmodel->textures[i];

		if (!tx || tx->name[0] != '+')
			continue;

		if (tx->anim_next)
			continue; // allready sequenced

		// find the number of frames in the animation
		memset (anims, 0, sizeof(anims));
		memset (altanims, 0, sizeof(altanims));

		max = tx->name[1];
		altmax = 0;

		if (max >= 'a' && max <= 'z')
			max -= 'a' - 'A';

		if (max >= '0' && max <= '9')
		{
			max -= '0';
			altmax = 0;
			anims[max] = tx;
			max++;
		}

		else if (max >= 'A' && max <= 'J')
		{
			altmax = max - 'A';
			max = 0;
			altanims[altmax] = tx;
			altmax++;
		}

		else
			Sys_Error ("Bad animating texture %s", tx->name);

		for (j=i+1 ; j<m->nummiptex ; j++)
		{
			tx2 = loadmodel->textures[j];

			if (!tx2 || tx2->name[0] != '+')
				continue;

			if (strcmp (tx2->name+2, tx->name+2))
				continue;

			num = tx2->name[1];

			if (num >= 'a' && num <= 'z')
				num -= 'a' - 'A';

			if (num >= '0' && num <= '9')
			{
				num -= '0';
				anims[num] = tx2;

				if (num+1 > max)
					max = num + 1;
			}

			else if (num >= 'A' && num <= 'J')
			{
				num = num - 'A';
				altanims[num] = tx2;

				if (num+1 > altmax)
					altmax = num+1;
			}

			else
				Sys_Error ("Bad animating texture %s", tx->name);
		}
    
		#define ANIM_CYCLE  2
		// link them all together

		for (j=0 ; j<max ; j++)
		{
			tx2 = anims[j];

			if (!tx2)
				Sys_Error ("Missing frame %i of %s",j, tx->name);

			tx2->anim_total = max * ANIM_CYCLE;
			tx2->anim_min = j * ANIM_CYCLE;
			tx2->anim_max = (j+1) * ANIM_CYCLE;
			tx2->anim_next = anims[ (j+1)%max ];

			if (altmax)
				tx2->alternate_anims = altanims[0];
		}

		for (j=0 ; j<altmax ; j++)
		{
			tx2 = altanims[j];

			if (!tx2)
				Sys_Error ("Missing frame %i of %s",j, tx->name);

			tx2->anim_total = altmax * ANIM_CYCLE;
			tx2->anim_min = j * ANIM_CYCLE;
			tx2->anim_max = (j+1) * ANIM_CYCLE;
			tx2->anim_next = altanims[ (j+1)%altmax ];

			if (max)
				tx2->alternate_anims = anims[0];
		}
	}
}

#ifndef LITFILES
/*
=================
Mod_LoadLighting
=================
*/

void Mod_LoadLighting (lump_t *l)
{
	if (!l->filelen)
	{
		loadmodel->lightdata = NULL;
		return;
	}

	loadmodel->lightdata = Hunk_AllocName ( l->filelen, loadname);	
	memcpy (loadmodel->lightdata, mod_base + l->fileofs, l->filelen);
}

#else

extern cvar_t r_litfiles;

void Mod_LoadLighting (lump_t *l)
{
	// LordHavoc: .lit support begin

	int	i;
	byte	*in, *out, *data, d;
	char	litfilename[1024];

	if(!COM_CheckParm("-litfiles"))
	{
		if (!l->filelen)
		{
			loadmodel->lightdata = NULL;
			return;
		}

		loadmodel->lightdata = Hunk_AllocName ( l->filelen, loadname);	
		memcpy (loadmodel->lightdata, mod_base + l->fileofs, l->filelen);

		return;
	}

	loadmodel->lightdata = NULL;

	// LordHavoc: check for a .lit file
	strcpy(litfilename, loadmodel->name);
	COM_StripExtension(litfilename, litfilename);
	strcat(litfilename, ".lit");

	if(!r_litfiles.value) //Surgeon
		data = 0;

	else
		data = (byte*) COM_LoadHunkFile (litfilename);

	if (data)
	{
		if (data[0] == 'Q' && data[1] == 'L' && data[2] == 'I' && data[3] == 'T')
		{
			i = LittleLong(((int *)data)[1]);

			if (i == 1)
			{
				Con_DPrintf("%s loaded", litfilename);
				loadmodel->lightdata = data + 8;
				return;
			}

			else
				Con_Printf("Unknown .lit file version (%d)\n", i);
		}

		else
			Con_Printf("Corrupt .lit file (old version?), ignoring\n");
	 }

	// LordHavoc: no .lit found, expand the white lighting data to color

	if (!l->filelen)
		return;

	loadmodel->lightdata = Hunk_AllocName ( l->filelen*3, litfilename );
	in = loadmodel->lightdata + l->filelen*2; // place the file at the end, so it will not be overwritten until the very last write
	out = loadmodel->lightdata;
	memcpy (in, mod_base + l->fileofs, l->filelen);

	for (i = 0; i < l->filelen; i++)
	{
		d = *in++;
		*out++ = d;
		*out++ = d;
		*out++ = d;
	}

	// LordHavoc: .lit support end
}

#endif

/*
=================
Mod_LoadVisibility
=================
*/

void Mod_LoadVisibility (lump_t *l)
{
	if (!l->filelen)
	{
		loadmodel->visdata = NULL;
		return;
	}

	loadmodel->visdata = Hunk_AllocName ( l->filelen, loadname);  
	memcpy (loadmodel->visdata, mod_base + l->fileofs, l->filelen);
}


/*
=================
Mod_LoadEntities
=================
*/

void Mod_LoadEntities (lump_t *l)
{
	if (!l->filelen)
	{
		loadmodel->entities = NULL;
		return;
	}

	loadmodel->entities = Hunk_AllocName ( l->filelen, loadname); 
	memcpy (loadmodel->entities, mod_base + l->fileofs, l->filelen);
}


/*
=================
Mod_LoadVertexes
=================
*/

void Mod_LoadVertexes (lump_t *l)
{
	dvertex_t *in;
	mvertex_t *out;
	int	  i, count;

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
		Sys_Error ("Mod_LoadVertexes: funny lump size in %s",loadmodel->name);

	count = l->filelen / sizeof(*in);
	out = Hunk_AllocName ( count*sizeof(*out), loadname); 

	loadmodel->vertexes = out;
	loadmodel->numvertexes = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		out->position[0] = LittleFloat (in->point[0]);
		out->position[1] = LittleFloat (in->point[1]);
		out->position[2] = LittleFloat (in->point[2]);
	}
}

/*
=================
Mod_LoadSubmodels
=================
*/

void Mod_LoadSubmodels (lump_t *l)
{
	dmodel_t  *in;
	dmodel_t  *out;
	int	  i, j, count;

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
		Sys_Error ("Mod_LoadSubmodels: funny lump size in %s",loadmodel->name);

	count = l->filelen / sizeof(*in);
	out = Hunk_AllocName ( count*sizeof(*out), loadname); 

	loadmodel->submodels = out;
	loadmodel->numsubmodels = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			// spread the mins / maxs by a pixel
			out->mins[j] = LittleFloat (in->mins[j]) - 1;
			out->maxs[j] = LittleFloat (in->maxs[j]) + 1;
			out->origin[j] = LittleFloat (in->origin[j]);
		}

		for (j=0 ; j<MAX_MAP_HULLS ; j++)
			out->headnode[j] = LittleLong (in->headnode[j]);

		out->visleafs = LittleLong (in->visleafs);
		out->firstface = LittleLong (in->firstface);
		out->numfaces = LittleLong (in->numfaces);
	}

	// johnfitz -- check world visleafs -- adapted from bjp
	out = loadmodel->submodels;

	//if (out->visleafs > MAX_MAP_LEAFS) // not used now - 2019 Cowcat 
		//Sys_Error ("Mod_LoadSubmodels: too many visleafs (%d, max = %d) in %s", out->visleafs, MAX_MAP_LEAFS, loadmodel->name); 

	if (out->visleafs > 8192)
		Con_DPrintf ("%i visleafs exceeds standard limit of 8192.\n", out->visleafs);

	//johnfitz
	
}

/*
=================
Mod_LoadEdges
=================
*/

void Mod_LoadEdges (lump_t *l, int bsp2)
{
	medge_t *out;
	int	i, count;

	if (bsp2)
	{
		dledge_t *in = (dledge_t *)(mod_base + l->fileofs);

		if (l->filelen % sizeof(*in))
			Sys_Error ("Mod_LoadEdges: funny lump size in %s",loadmodel->name);

		count = l->filelen / sizeof(*in);
		out = (medge_t *) Hunk_AllocName ( (count + 1) * sizeof(*out), loadname);

		loadmodel->edges = out;
		loadmodel->numedges = count;

		for ( i=0 ; i<count ; i++, in++, out++)
		{
			out->v[0] = LittleLong(in->v[0]);
			out->v[1] = LittleLong(in->v[1]);
		}
	}

	else
	{
		dsedge_t *in = (dsedge_t *)(mod_base + l->fileofs);

		if (l->filelen % sizeof(*in))
			Sys_Error ("Mod_LoadEdges: funny lump size in %s",loadmodel->name);

		count = l->filelen / sizeof(*in);
		out = (medge_t *) Hunk_AllocName ( (count + 1) * sizeof(*out), loadname); 

		loadmodel->edges = out;
		loadmodel->numedges = count;

		for ( i=0 ; i<count ; i++, in++, out++)
		{
			out->v[0] = (unsigned short)LittleShort(in->v[0]); // jf
			out->v[1] = (unsigned short)LittleShort(in->v[1]); // jf
		}
	}
}

/*
=================
Mod_LoadTexinfo
=================
*/

void Mod_LoadTexinfo (lump_t *l)
{
	texinfo_t	*in;
	mtexinfo_t	*out;
	int		i, j, count;
	int		miptex;
	float		len1, len2;

	in = (texinfo_t *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
		Sys_Error ("Mod_LoadTexinfo: funny lump size in %s",loadmodel->name);

	count = l->filelen / sizeof(*in);
	out = (mtexinfo_t *)Hunk_AllocName ( count*sizeof(*out), loadname);

	loadmodel->texinfo = out;
	loadmodel->numtexinfo = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		//for (j=0 ; j<8 ; j++)
			//out->vecs[0][j] = LittleFloat (in->vecs[0][j]);
		
		for (j=0 ; j<4 ; j++) // fix Cowcat 2019
		{
			out->vecs[0][j] = LittleFloat (in->vecs[0][j]);
			out->vecs[1][j] = LittleFloat (in->vecs[1][j]);
		}

		len1 = Length (out->vecs[0]);
		len2 = Length (out->vecs[1]);
		len1 = (len1 + len2)/2;

		if (len1 < 0.32)
			out->mipadjust = 4;

		else if (len1 < 0.49)
			out->mipadjust = 3;

		else if (len1 < 0.99)
			out->mipadjust = 2;

		else
			out->mipadjust = 1;

		#if 0
			if (len1 + len2 < 0.001)
				out->mipadjust = 1;   // don't crash

			else
				out->mipadjust = 1 / floor( (len1+len2)/2 + 0.1 );
		#endif

		miptex = LittleLong (in->miptex);
		out->flags = LittleLong (in->flags);
  
		if (!loadmodel->textures)
		{
			out->texture = r_notexture_mip; // checkerboard texture
			out->flags = 0;
		}

		else
		{
			if (miptex >= loadmodel->numtextures)
				Sys_Error ("miptex >= loadmodel->numtextures");

			out->texture = loadmodel->textures[miptex];

			if (!out->texture)
			{
				out->texture = r_notexture_mip; // texture not found
				out->flags = 0;
			}
		}
	}
}

/*
================
CalcSurfaceExtents

Fills in s->texturemins[] and s->extents[]
================
*/

void CalcSurfaceExtents (msurface_t *s)
{
	float		mins[2], maxs[2], val;
	int		i, j, e;
	mvertex_t	*v;
	mtexinfo_t	*tex;
	int		bmins[2], bmaxs[2];

	mins[0] = mins[1] = 999999;
	maxs[0] = maxs[1] = -99999; // +9 ? - Cowcat 2019

	tex = s->texinfo;
  
	for (i=0 ; i<s->numedges ; i++)
	{
		e = loadmodel->surfedges[s->firstedge+i];

		if (e >= 0)
			v = &loadmodel->vertexes[loadmodel->edges[e].v[0]];

		else
			v = &loadmodel->vertexes[loadmodel->edges[-e].v[1]];
    
		for (j=0 ; j<2 ; j++)
		{
			val = v->position[0] * tex->vecs[j][0] + v->position[1] * tex->vecs[j][1] +
				v->position[2] * tex->vecs[j][2] + tex->vecs[j][3];

			if (val < mins[j])
				mins[j] = val;

			if (val > maxs[j])
				maxs[j] = val;
		}
	}

	for (i=0 ; i<2 ; i++)
	{ 
		bmins[i] = floor(mins[i]/16);
		bmaxs[i] = ceil(maxs[i]/16);

		s->texturemins[i] = bmins[i] * 16;
		s->extents[i] = (bmaxs[i] - bmins[i]) * 16;

		if ( !(tex->flags & TEX_SPECIAL) && s->extents[i] > 2000 ) // johnfitz	-- was 512 /* 256 */ ) - 2000 - Cowcat 2019
			Sys_Error ("Bad surface extents");
	}
}


/*
================
Mod_PolyForUnlitSurface -- johnfitz -- creates polys for unlightmapped surfaces (sky and water)

TODO: merge this into BuildSurfaceDisplayList?
================
*/

#if 0
void Mod_PolyForUnlitSurface (msurface_t *fa)
{
	vec3_t		verts[64];
	int		numverts, i, lindex;
	float		*vec;
	texture_t	*t;
	glpoly_t	*poly;
	float		texscale;

	if (fa->flags & (SURF_DRAWTURB | SURF_DRAWSKY))
		texscale = (1.0/128.0); //warp animation repeats every 128
	else
		texscale = (1.0/32.0); //to match r_notexture_mip

	// convert edges back to a normal polygon
	numverts = 0;

	for (i=0 ; i<fa->numedges ; i++)
	{
		lindex = loadmodel->surfedges[fa->firstedge + i];

		if (lindex > 0)
			vec = loadmodel->vertexes[loadmodel->edges[lindex].v[0]].position;

		else
			vec = loadmodel->vertexes[loadmodel->edges[-lindex].v[1]].position;

		VectorCopy (vec, verts[numverts]);
		numverts++;
	}

	//create the poly
	poly = Hunk_Alloc (sizeof(glpoly_t) + (numverts-4) * VERTEXSIZE*sizeof(float));
	poly->next = NULL;
	fa->polys = poly;
	poly->numverts = numverts;

	for (i=0, vec=(float *)verts; i<numverts; i++, vec+= 3)
	{
		VectorCopy (vec, poly->verts[i]);
		poly->verts[i][3] = DotProduct(vec, fa->texinfo->vecs[0]) * texscale;
		poly->verts[i][4] = DotProduct(vec, fa->texinfo->vecs[1]) * texscale;
	}
}
#endif


/*
=================
Mod_CalcSurfaceBounds -- johnfitz -- calculate bounding box for per-surface frustum culling
=================
*/

#if 0
void Mod_CalcSurfaceBounds (msurface_t *s)
{
	int		i, e, j;
	mvertex_t	*v;

	s->mins[0] = s->mins[1] = s->mins[2] = 9999;
	s->maxs[0] = s->maxs[1] = s->maxs[2] = -9999;

	for (i=0 ; i<s->numedges ; i++)
	{
		e = loadmodel->surfedges[s->firstedge+i];

		if (e >= 0)
			v = &loadmodel->vertexes[loadmodel->edges[e].v[0]];

		else
			v = &loadmodel->vertexes[loadmodel->edges[-e].v[1]];

		
		for(j = 0; j < 3; j++)
		{
			if(s->mins[j] > v->position[j])
				s->mins[j] = v->position[j];

			if(s->maxs[j] < v->position[j])
				s->maxs[j] = v->position[j];
		}
	
	}
}
#endif

extern qboolean r_skybox;

/*
=================
Mod_LoadFaces
=================
*/

#define ISTELETEX(x) ((x)[1] == 't' && (x)[2] == 'e' && (x)[3] == 'l' && (x)[4] == 'e' )
#define ISLAVATEX(x) ((x)[1] == 'l' && (x)[2] == 'a' && (x)[3] == 'v' && (x)[4] == 'a' )

void Mod_LoadFaces (lump_t *l, qboolean bsp2)
{
	dsface_t	*ins;
	dlface_t	*inl;
	msurface_t	*out;
	int		i, count, surfnum, lofs;
	int		planenum, side, texinfon;

	if (bsp2)
	{
		ins = NULL;
		inl = (dlface_t *)(mod_base + l->fileofs);

		if (l->filelen % sizeof(*inl))
			Sys_Error ("Mod_LoadFaces: funny lump size in %s", loadmodel->name);
		
		count = l->filelen / sizeof(*inl);
	}

	else
	{
		ins = (dsface_t *)(mod_base + l->fileofs);
		inl = NULL;

		if (l->filelen % sizeof(*ins))
			Sys_Error ("Mod_LoadFaces: funny lump size in %s", loadmodel->name);

		count = l->filelen / sizeof(*ins);
	}

	out = (msurface_t *)Hunk_AllocName ( count*sizeof(*out), loadname);

	//johnfitz
	if(count > 32767 && !bsp2)
		Con_DPrintf("%i faces exceeds standard limit of 32767.\n", count);
	//
	
	loadmodel->surfaces = out;
	loadmodel->numsurfaces = count;

	for ( surfnum=0 ; surfnum<count ; surfnum++, out++)
	{
		if (bsp2)
		{
			out->firstedge = LittleLong(inl->firstedge);
			out->numedges = LittleLong(inl->numedges); 
			planenum = LittleLong(inl->planenum);
			side = LittleLong(inl->side);
			texinfon = LittleLong(inl->texinfo);

			for (i=0 ; i<MAXLIGHTMAPS ; i++)
				out->styles[i] = inl->styles[i];

			lofs = LittleLong(inl->lightofs);
			inl++;
		}

		else
		{
			out->firstedge = LittleLong(ins->firstedge);
			out->numedges = LittleShort(ins->numedges);    
			planenum = LittleShort(ins->planenum);
			side = LittleShort(ins->side);
			texinfon = LittleShort(ins->texinfo);

			for (i=0 ; i<MAXLIGHTMAPS ; i++)
				out->styles[i] = ins->styles[i];

			lofs = LittleLong(ins->lightofs);
			ins++;
		}

		out->flags = 0;

		if (side)
			out->flags |= SURF_PLANEBACK;	  

		out->plane = loadmodel->planes + planenum;

		out->texinfo = loadmodel->texinfo + texinfon;

		CalcSurfaceExtents (out);

		//Mod_CalcSurfaceBounds (out); // johnfitz - for per-surface frustum culling

		if (lofs == -1)
			out->samples = NULL;

		else
		#ifndef LITFILES

			out->samples = loadmodel->lightdata + lofs;

		#else
			if(COM_CheckParm("-litfiles"))
			{
				out->samples = loadmodel->lightdata + (lofs * 3);
			}

			else
			{
				out->samples = loadmodel->lightdata + lofs;
			}

			// LordHavoc
		#endif
    
		// set the drawing flags flag
    
		if (!Q_strncasecmp(out->texinfo->texture->name,"sky",3))  // sky - was strncmp, match qbsp
		{
			out->flags |= (SURF_DRAWSKY | SURF_DRAWTILED);

			//#ifndef QUAKE2
			if(!r_skybox)
				GL_SubdivideSurface (out);  // cut up polygon for warps
				//Mod_PolyForUnlitSurface (out); // jf
			//#endif

			continue;
		}

		//surgeon: target for optimizations    
		//if (!Q_strncmp(out->texinfo->texture->name,"*",1))	// turbulent
		if (out->texinfo->texture->name[0] == '*') // LordHavoc
		{
			out->flags |= (SURF_DRAWTURB | SURF_DRAWTILED);

			if (ISTELETEX(out->texinfo->texture->name))
				out->flags |= SURF_DRAWTELE;

			else if (ISLAVATEX(out->texinfo->texture->name))
				out->flags |= SURF_DRAWLAVA; 

			for (i=0 ; i<2 ; i++)
			{
				out->extents[i] = 16384;
				out->texturemins[i] = -8192;
			}

			GL_SubdivideSurface (out);  // cut up polygon for warps
			//Mod_PolyForUnlitSurface (out); // jf
			continue;
		}

		if(out->texinfo->texture->name[0] == '{') // ericw -- fence textures
		{
			out->flags |= SURF_DRAWFENCE;
			//Con_Printf("Surf_drawfence\n");
		}
	}
}


/*
=================
Mod_SetParent
=================
*/

void Mod_SetParent (mnode_t *node, mnode_t *parent)
{
	node->parent = parent;

	if (node->contents < 0)
		return;

	Mod_SetParent (node->children[0], node);
	Mod_SetParent (node->children[1], node);
}

/*
=================
Mod_LoadNodes
=================
*/

void Mod_LoadNodes_S (lump_t *l)
{
	int	  i, j, count, p;
	dsnode_t  *in;
	mnode_t	  *out;

	in = (dsnode_t *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
		Sys_Error ("Mod_LoadNodes: funny lump size in %s", loadmodel->name);

	count = l->filelen / sizeof(*in);
	out = (mnode_t *)Hunk_AllocName ( count*sizeof(*out), loadname);

	//johnfitz
	if(count > 32767)
		Con_DPrintf("%i nodes exceeds standard limit of 32767.\n", count);
	//
	
	loadmodel->nodes = out;
	loadmodel->numnodes = count;

	for (i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			//out->minmaxs[j] = LittleShort (in->mins[j]); // maybe those ? - Cowcat 2019
			//out->minmaxs[3+j] = LittleShort (in->maxs[j]);
			out->mins[j] = LittleShort (in->mins[j]);
			out->maxs[j] = LittleShort (in->maxs[j]);
		}
  
		p = LittleLong(in->planenum);
		out->plane = loadmodel->planes + p;
		out->firstsurface = (unsigned short)LittleShort (in->firstface); // jf
		out->numsurfaces = (unsigned short)LittleShort (in->numfaces); // jf
    
		for (j=0 ; j<2 ; j++)
		{	
			// johnfitz - adapted darkplaces - handle nodes > 32
			p = (unsigned short)LittleShort (in->children[j]);

			if(p < count)
				out->children[j] = loadmodel->nodes + p;

			else
			{
				p = 65535 - p; //

				if(p < loadmodel->numleafs)
					out->children[j] = (mnode_t *)(loadmodel->leafs + p);
				
				else
				{
					Con_Printf("Mod_LoadNodes: invalid leaf index %i\n",p , loadmodel->numleafs);
					out->children[j] = (mnode_t *)(loadmodel->leafs);
				}
			}
			//
		}	
	}
}

void Mod_LoadNodes_L2 (lump_t *l)
{
	int	  i, j, count, p;
	dl2node_t *in;
	mnode_t	  *out;

	in = (dl2node_t *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
		Sys_Error ("Mod_LoadNodes: funny lump size in %s", loadmodel->name);

	count = l->filelen / sizeof(*in);
	out = (mnode_t *)Hunk_AllocName ( count*sizeof(*out), loadname);
	
	loadmodel->nodes = out;
	loadmodel->numnodes = count;

	for (i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			//out->minmaxs[j] = LittleShort (in->mins[j]); // maybe those ? - Cowcat 2019
			//out->minmaxs[3+j] = LittleShort (in->maxs[j]);
			out->mins[j] = LittleFloat (in->mins[j]);
			out->maxs[j] = LittleFloat (in->maxs[j]);
		}
  
		p = LittleLong(in->planenum);
		out->plane = loadmodel->planes + p;
		out->firstsurface = LittleLong (in->firstface);
		out->numsurfaces = LittleLong (in->numfaces);
    
		for (j=0 ; j<2 ; j++)
		{	
			// johnfitz - adapted darkplaces - handle nodes > 32
			p = LittleLong (in->children[j]);

			if(p > 0 && p < count)
				out->children[j] = loadmodel->nodes + p;

			else
			{
				p = 0xffffffff - p; //

				if( p >= 0 && p < loadmodel->numleafs)
					out->children[j] = (mnode_t *)(loadmodel->leafs + p);
				
				else
				{
					Con_Printf("Mod_LoadNodes: invalid leaf index %i\n",p , loadmodel->numleafs);
					out->children[j] = (mnode_t *)(loadmodel->leafs);
				}
			}
			//
		}	
	}
}

void Mod_LoadNodes (lump_t *l, int bsp2)
{
	if (bsp2 == 2)
		Mod_LoadNodes_L2(l);

	else
		Mod_LoadNodes_S(l);

	Mod_SetParent (loadmodel->nodes, NULL); // sets nodes and leafs
}

/*
=================
Mod_LoadLeafs
=================
*/

void Mod_ProcessLeafs_S (dsleaf_t *in, int filelen)
{
	mleaf_t	  *out;
	int	  i, j, count, p;

	if (filelen % sizeof(*in))
		Sys_Error ("Mod_LoadLeafs: funny lump size in %s", loadmodel->name);

	count = filelen / sizeof(*in);
	out = (mleaf_t *)Hunk_AllocName ( count*sizeof(*out), loadname);
 
	//johnfitz
	if(count > 32767)
		Host_Error("Mod_LoadLeafs: %i leafs exceeds limit of 32767.\n", count);
	//

	loadmodel->leafs = out;
	loadmodel->numleafs = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			//out->minmaxs[j] = LittleShort (in->mins[j]);
			//out->minmaxs[3+j] = LittleShort (in->maxs[j]);
			out->mins[j] = LittleShort (in->mins[j]);
			out->maxs[j] = LittleShort (in->maxs[j]);
		}

		p = LittleLong(in->contents);

		out->contents = p;
		out->firstmarksurface = loadmodel->marksurfaces + (unsigned short)LittleShort(in->firstmarksurface); //jf
		out->nummarksurfaces = (unsigned short)LittleShort(in->nummarksurfaces); // jf
    
		p = LittleLong(in->visofs);

		if (p == -1)
			out->compressed_vis = NULL;

		else
			out->compressed_vis = loadmodel->visdata + p;

		out->efrags = NULL;
    
		for (j=0 ; j<4 ; j++)
			out->ambient_sound_level[j] = in->ambient_level[j];

		// gl underwater warp
		if (out->contents != CONTENTS_EMPTY)
		{
			for (j=0 ; j<out->nummarksurfaces ; j++)
			{
				// Baker
				msurface_t **surf = &out->firstmarksurface[j];

				if(!surf || !(*surf) || !(*surf)->texinfo || !(*surf)->texinfo->texture)
					continue;
				//

				out->firstmarksurface[j]->flags |= SURF_UNDERWATER;
			}
		}
	}
}

void Mod_ProcessLeafs_L2 (dl2leaf_t *in, int filelen)
{
	mleaf_t	  *out;
	int	  i, j, count, p;

	if (filelen % sizeof(*in))
		Sys_Error ("Mod_LoadLeafs: funny lump size in %s", loadmodel->name);

	count = filelen / sizeof(*in);
	out = (mleaf_t *)Hunk_AllocName ( count*sizeof(*out), loadname);
	
	loadmodel->leafs = out;
	loadmodel->numleafs = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			//out->minmaxs[j] = LittleFloat (in->mins[j]);
			//out->minmaxs[3+j] = LittleFloat (in->maxs[j]);
			out->mins[j] = LittleFloat (in->mins[j]);
			out->maxs[j] = LittleFloat (in->maxs[j]);
		}

		p = LittleLong(in->contents);

		out->contents = p;
		out->firstmarksurface = loadmodel->marksurfaces + LittleLong(in->firstmarksurface);
		out->nummarksurfaces = LittleLong(in->nummarksurfaces);
    
		p = LittleLong(in->visofs);

		if (p == -1)
			out->compressed_vis = NULL;

		else
			out->compressed_vis = loadmodel->visdata + p;

		out->efrags = NULL;
    
		for (j=0 ; j<4 ; j++)
			out->ambient_sound_level[j] = in->ambient_level[j];

		// gl underwater warp
		if (out->contents != CONTENTS_EMPTY)
		{
			for (j=0 ; j<out->nummarksurfaces ; j++)
			{
				// Baker
				msurface_t **surf = &out->firstmarksurface[j];

				if(!surf || !(*surf) || !(*surf)->texinfo || !(*surf)->texinfo->texture)
					continue;
				//

				out->firstmarksurface[j]->flags |= SURF_UNDERWATER;
			}
		}
	}
}


void Mod_LoadLeafs (lump_t *l, int bsp2)
{
	void *in = (void *)(mod_base + l->fileofs);

	if(bsp2 == 2)
		Mod_ProcessLeafs_L2 ((dl2leaf_t*)in, l->filelen);

	else
		Mod_ProcessLeafs_S ((dsleaf_t*)in, l->filelen);
}

/*
=================
Mod_LoadClipnodes
=================
*/

void Mod_LoadClipnodes (lump_t *l, qboolean bsp2)
{
	dsclipnode_t	*ins;
	dlclipnode_t	*inl; 
	mclipnode_t	*out; // johnfitz was dclipnode_t
	int		i, count;
	hull_t		*hull;

	if(bsp2)
	{
		ins = NULL;
		inl = (dlclipnode_t *)(mod_base + l->fileofs);

		if (l->filelen % sizeof(*inl))
			Sys_Error ("Mod_LoadClipnodes: funny lump size in %s", loadmodel->name);

		count = l->filelen / sizeof(*inl);
	}
	
	else
	{
		ins = (dsclipnode_t *)(mod_base + l->fileofs);
		inl = NULL;

		if (l->filelen % sizeof(*ins))
			Sys_Error ("Mod_LoadClipnodes: funny lump size in %s", loadmodel->name);

		count = l->filelen / sizeof(*ins);

	}

	out = (mclipnode_t *) Hunk_AllocName ( count*sizeof(*out), loadname);
	
	//johnfitz
	if(count > 32767 && !bsp2)
		Con_DPrintf("%i clipnodes exceeds standard limit of 32767.\n", count);
	//
	
	loadmodel->clipnodes = out;
	loadmodel->numclipnodes = count;

	//Player hull
	hull = &loadmodel->hulls[1];
	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = loadmodel->planes;
	hull->clip_mins[0] = -16;
	hull->clip_mins[1] = -16;
	hull->clip_mins[2] = -24;
	hull->clip_maxs[0] = 16;
	hull->clip_maxs[1] = 16;
	hull->clip_maxs[2] = 32;

	//Monster hull
	hull = &loadmodel->hulls[2];
	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = loadmodel->planes;
	hull->clip_mins[0] = -32;
	hull->clip_mins[1] = -32;
	hull->clip_mins[2] = -24;
	hull->clip_maxs[0] = 32;
	hull->clip_maxs[1] = 32;
	hull->clip_maxs[2] = 64;

	if (bsp2)
	{
		for (i=0 ; i<count ; i++, out++, inl++)
		{
			out->planenum = LittleLong(inl->planenum);

			//johnfitz -- bounds check
			if (out->planenum < 0 || out->planenum >= loadmodel->numplanes)
				Host_Error ("Mod_LoadClipnodes: planenum out of bounds");
			//johnfitz

			out->children[0] = LittleLong(inl->children[0]);
			out->children[1] = LittleLong(inl->children[1]);
		}
	}

	else
	{
		for (i=0 ; i<count ; i++, out++, ins++)
		{
			out->planenum = LittleLong(ins->planenum);
		
			//johnfitz -- bounds check
			if (out->planenum < 0 || out->planenum >= loadmodel->numplanes)
				Host_Error ("Mod_LoadClipnodes: planenum out of bounds");
			//johnfitz

			//johnfitz -- support clipnodes > 32k
			out->children[0] = (unsigned short)LittleShort(ins->children[0]);
			out->children[1] = (unsigned short)LittleShort(ins->children[1]);

			if (out->children[0] >= count)
				out->children[0] -= 65536;

			if (out->children[1] >= count)
				out->children[1] -= 65536;
			//johnfitz
		}
	}
}

/*
=================
Mod_MakeHull0

Deplicate the drawing hull structure as a clipping hull
=================
*/

void Mod_MakeHull0 (void)
{
	mnode_t		*in, *child;
	mclipnode_t	*out;		// jf was dclipnode_t
	int		i, j, count;
	hull_t		*hull;
  
	hull = &loadmodel->hulls[0];  
  
	in = loadmodel->nodes;
	count = loadmodel->numnodes;
	out = Hunk_AllocName (count*sizeof(*out), loadname); 

	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = loadmodel->planes;

	for (i=0 ; i<count ; i++, out++, in++)
	{
		out->planenum = in->plane - loadmodel->planes;

		for (j=0 ; j<2 ; j++)
		{
			child = in->children[j];

			if (child->contents < 0)
				out->children[j] = child->contents;

			else
				out->children[j] = child - loadmodel->nodes;
		}
	}
}

/*
=================
Mod_LoadMarksurfaces
=================
*/

void Mod_LoadMarksurfaces (lump_t *l, int bsp2)
{ 
	int		i, j, count;
	msurface_t	**out;

	if (bsp2)
	{
		unsigned int *in = (unsigned int *)(mod_base + l->fileofs);

		if (l->filelen % sizeof(*in))
			Host_Error ("Mod_LoadMarksurfaces: funny lump size in %s", loadmodel->name);

		count = l->filelen / sizeof(*in);
		out = (msurface_t **)Hunk_AllocName ( count*sizeof(*out), loadname);

		loadmodel->marksurfaces = out;
		loadmodel->nummarksurfaces = count;

		for ( i=0 ; i<count ; i++)
		{
			j = LittleLong(in[i]);

			if (j >= loadmodel->numsurfaces)
				Host_Error ("Mod_ParseMarksurfaces: bad surface number");

			out[i] = loadmodel->surfaces + j;
		}

	}

	else
	{
		short *in = (short *)(mod_base + l->fileofs);

		if (l->filelen % sizeof(*in))
			Host_Error ("Mod_LoadMarksurfaces: funny lump size in %s", loadmodel->name);

		count = l->filelen / sizeof(*in);
		out = (msurface_t **)Hunk_AllocName ( count*sizeof(*out), loadname); 

		loadmodel->marksurfaces = out;
		loadmodel->nummarksurfaces = count;

		//johnfitz
		if(count > 32767)
			Con_DPrintf("%i marksurfaces exceeds standard limit of 32767.\n", count);
		//

		for ( i=0 ; i<count ; i++)
		{
			j = (unsigned short)LittleShort(in[i]); // jf

			if (j >= loadmodel->numsurfaces)
				Sys_Error ("Mod_ParseMarksurfaces: bad surface number");

			out[i] = loadmodel->surfaces + j;
		}
	}
}

/*
=================
Mod_LoadSurfedges
=================
*/

void Mod_LoadSurfedges (lump_t *l)
{ 
	int   i, count;
	int   *in, *out;
  
	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
		Sys_Error ("Mod_LoadSurfedges: funny lump size in %s",loadmodel->name);

	count = l->filelen / sizeof(*in);
	out = Hunk_AllocName ( count*sizeof(*out), loadname); 

	loadmodel->surfedges = out;
	loadmodel->numsurfedges = count;

	for ( i=0 ; i<count ; i++)
		out[i] = LittleLong (in[i]);
}


/*
=================
Mod_LoadPlanes
=================
*/

void Mod_LoadPlanes (lump_t *l)
{
	int		i, j;
	mplane_t	*out;
	dplane_t	*in;
	int		count;
	#ifndef FASTBOPS
	int		bits;
  	#endif

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
		Sys_Error ("Mod_LoadPlanes: funny lump size in %s", loadmodel->name);

	count = l->filelen / sizeof(*in);
	out = Hunk_AllocName ( count*2*sizeof(*out), loadname); 
  
	loadmodel->planes = out;
	loadmodel->numplanes = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		#ifndef FASTBOPS
		bits = 0;
		#endif

		for (j=0 ; j<3 ; j++)
		{
			out->normal[j] = LittleFloat (in->normal[j]);

			#ifndef FASTBOPS

			if (out->normal[j] < 0)
				bits |= 1<<j;
			#endif

		}

		out->dist = LittleFloat (in->dist);
		out->type = LittleLong (in->type);

		#ifndef FASTBOPS

		out->signbits = bits;

		#else

		BoxOnPlaneSideClassify(out);

		#endif

	}
}

/*
=================
RadiusFromBounds
=================
*/

float RadiusFromBounds (vec3_t mins, vec3_t maxs)
{
	int	i;
	vec3_t	corner;

	for (i=0 ; i<3 ; i++)
	{
		corner[i] = fabs(mins[i]) > fabs(maxs[i]) ? fabs(mins[i]) : fabs(maxs[i]);
	}

	return Length (corner);
}

/*
=================
Mod_LoadBrushModel
=================
*/

void Mod_LoadBrushModel (model_t *mod, void *buffer)
{
	int		i, j;
	int		bsp2;
	dheader_t	*header;
	dmodel_t	*bm;
	//float		radius; // jf

	loadmodel->type = mod_brush;
  
	header = (dheader_t *)buffer;

	mod->bspversion = LittleLong (header->version);

	switch (mod->bspversion) // new 2019 - Cowcat
	{
		case BSPVERSION:
			bsp2 = false;
			//printf("bsp1\n");
			break;

		case BSP2VERSION_BSP2:
			bsp2 = 2;
			//printf("bsp2\n");
			break;

		default:
			Sys_Error ("Mod_LoadBrushModel: %s has wrong version number (%i should be %i)", mod->name, mod->bspversion, BSPVERSION);
			break;
	}

	// swap all the lumps
	mod_base = (byte *)header;

	for (i=0 ; i<sizeof(dheader_t)/4 ; i++)
		((int *)header)[i] = LittleLong ( ((int *)header)[i]);

	// load into heap

	Mod_LoadVertexes (&header->lumps[LUMP_VERTEXES]);
	Mod_LoadEdges (&header->lumps[LUMP_EDGES], bsp2);
	Mod_LoadSurfedges (&header->lumps[LUMP_SURFEDGES]);
	Mod_LoadTextures (&header->lumps[LUMP_TEXTURES]);
	Mod_LoadLighting (&header->lumps[LUMP_LIGHTING]);
	Mod_LoadPlanes (&header->lumps[LUMP_PLANES]);
	Mod_LoadTexinfo (&header->lumps[LUMP_TEXINFO]);
	Mod_LoadFaces (&header->lumps[LUMP_FACES], bsp2);
	Mod_LoadMarksurfaces (&header->lumps[LUMP_MARKSURFACES], bsp2);
	Mod_LoadVisibility (&header->lumps[LUMP_VISIBILITY]);
	Mod_LoadLeafs (&header->lumps[LUMP_LEAFS], bsp2);
	Mod_LoadNodes (&header->lumps[LUMP_NODES], bsp2);
	Mod_LoadClipnodes (&header->lumps[LUMP_CLIPNODES], bsp2);
	Mod_LoadEntities (&header->lumps[LUMP_ENTITIES]);
	Mod_LoadSubmodels (&header->lumps[LUMP_MODELS]);

	Mod_MakeHull0 ();
  
	mod->numframes = 2;   // regular and alternate animation
  
	//
	// set up the submodels (FIXME: this is confusing)
	//

	for (i=0 ; i<mod->numsubmodels ; i++)
	{
		bm = &mod->submodels[i];

		mod->hulls[0].firstclipnode = bm->headnode[0];

		for (j=1 ; j<MAX_MAP_HULLS ; j++)
		{
			mod->hulls[j].firstclipnode = bm->headnode[j];
			mod->hulls[j].lastclipnode = mod->numclipnodes-1;
		}
    
		mod->firstmodelsurface = bm->firstface;
		mod->nummodelsurfaces = bm->numfaces;

		VectorCopy (bm->maxs, mod->maxs);
		VectorCopy (bm->mins, mod->mins);

		mod->radius = RadiusFromBounds (mod->mins, mod->maxs);
		
		//johnfitz
		#if 0
		radius = RadiusFromBounds (mod->mins, mod->maxs); // jf
		mod->rmaxs[0] = mod->rmaxs[1] = mod->rmaxs[2] = mod->ymaxs[0] = mod->ymaxs[1] = mod->ymaxs[2] = radius;
		mod->rmins[0] = mod->rmins[1] = mod->rmins[2] = mod->ymins[0] = mod->ymins[1] = mod->ymins[2] = -radius;
		
		//johnfitz -- correct physics cullboxes so that outlying clip brushes on doors and stuff are handled right
		if(i > 0 || strcmp(mod->name, sv.modelname) !=0 )
		{
			VectorCopy (mod->maxs, mod->clipmaxs);
			VectorCopy (mod->mins, mod->clipmins);

			//Mod_BoundsFromClipNode (mod, 1, mod->hulls[1].firstclipnode); // (disabled for now becuase it fucks up on rotating models)
		}
		#endif

		mod->numleafs = bm->visleafs;

		if (i < mod->numsubmodels-1)
		{
			// duplicate the basic information
			char  name[10];

			sprintf (name, "*%i", i+1);
			loadmodel = Mod_FindName (name);
			*loadmodel = *mod;
			strcpy (loadmodel->name, name);
			mod = loadmodel;
		}
	}
}

/*
==============================================================================

ALIAS MODELS

==============================================================================
*/

aliashdr_t  *pheader;

stvert_t    stverts[MAXALIASVERTS]; // org 1024 - fitz 2000
mtriangle_t triangles[MAXALIASTRIS]; // org 2048

// a pose is a single set of vertexes.	a frame may be an animating sequence of poses
trivertx_t  *poseverts[MAXALIASFRAMES];
int	    posenum;

#if 1 // Cowcat
// proquake fix bounding boxes - !!
byte aliasbboxmins[3], aliasbboxmaxs[3];
extern qboolean fixboundbox;

#define min(a,b) (((a) < (b)) ? (a) : (b))
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif


/*
=================
Mod_LoadAliasFrame
=================
*/

void *Mod_LoadAliasFrame (void * pin, maliasframedesc_t *frame)
{
	trivertx_t	*pinframe;
	int		i;
	daliasframe_t	*pdaliasframe;
  
	pdaliasframe = (daliasframe_t *)pin;

	strcpy (frame->name, pdaliasframe->name);
	frame->firstpose = posenum;
	frame->numposes = 1;

	for (i=0 ; i<3 ; i++)
	{
		// these are byte values, so we don't have to worry about endianness
		frame->bboxmin.v[i] = pdaliasframe->bboxmin.v[i];
		frame->bboxmin.v[i] = pdaliasframe->bboxmax.v[i];

		if(fixboundbox)
		{
			aliasbboxmins[i] = min (frame->bboxmin.v[i], aliasbboxmins[i]); //
			aliasbboxmaxs[i] = max (frame->bboxmax.v[i], aliasbboxmaxs[i]); //
		}
	}

	pinframe = (trivertx_t *)(pdaliasframe + 1);

	poseverts[posenum] = pinframe;
	posenum++;

	pinframe += pheader->numverts;

	return (void *)pinframe;
}


/*
=================
Mod_LoadAliasGroup
=================
*/

void *Mod_LoadAliasGroup (void * pin, maliasframedesc_t *frame)
{
	daliasgroup_t		*pingroup;
	int			i, numframes;
	daliasinterval_t	*pin_intervals;
	void			*ptemp;
  
	pingroup = (daliasgroup_t *)pin;

	numframes = LittleLong (pingroup->numframes);

	frame->firstpose = posenum;
	frame->numposes = numframes;

	for (i=0 ; i<3 ; i++)
	{
		// these are byte values, so we don't have to worry about endianness
		frame->bboxmin.v[i] = pingroup->bboxmin.v[i];
		frame->bboxmin.v[i] = pingroup->bboxmax.v[i];

		if(fixboundbox)
		{
			aliasbboxmins[i] = min (frame->bboxmin.v[i], aliasbboxmins[i]); //
			aliasbboxmaxs[i] = max (frame->bboxmax.v[i], aliasbboxmaxs[i]); //
		}
	}

	pin_intervals = (daliasinterval_t *)(pingroup + 1);

	frame->interval = LittleFloat (pin_intervals->interval);

	pin_intervals += numframes;

	ptemp = (void *)pin_intervals;

	for (i=0 ; i<numframes ; i++)
	{
		poseverts[posenum] = (trivertx_t *)((daliasframe_t *)ptemp + 1);
		posenum++;

		ptemp = (trivertx_t *)((daliasframe_t *)ptemp + 1) + pheader->numverts;
	}

	return ptemp;
}

//=========================================================

/*
=================
Mod_FloodFillSkin

Fill background pixels so mipmapping doesn't have haloes - Ed
=================
*/

typedef struct
{
	short	x, y;

} floodfill_t;

extern unsigned d_8to24table[];

// must be a power of 2
#define FLOODFILL_FIFO_SIZE 0x1000
#define FLOODFILL_FIFO_MASK (FLOODFILL_FIFO_SIZE - 1)

#define FLOODFILL_STEP( off, dx, dy ) \
{ \
	if (pos[off] == fillcolor) \
	{ \
		pos[off] = 255; \
		fifo[inpt].x = x + (dx), fifo[inpt].y = y + (dy); \
		inpt = (inpt + 1) & FLOODFILL_FIFO_MASK; \
	} \
	else if (pos[off] != 255) fdc = pos[off]; \
}

void Mod_FloodFillSkin( byte *skin, int skinwidth, int skinheight )
{
	byte		fillcolor = *skin;	// assume this is the pixel to fill
	floodfill_t	fifo[FLOODFILL_FIFO_SIZE];
	int		inpt = 0, outpt = 0;
	int		filledcolor = -1;
	int		i;

	if (filledcolor == -1)
	{
		filledcolor = 0;
		// attempt to find opaque black

		for (i = 0; i < 256; ++i)
			if (d_8to24table[i] == (255 << 0)) // alpha 1.0
			{
				filledcolor = i;
				break;
			}
	}

	// can't fill to filled color or to transparent color (used as visited marker)
	if ((fillcolor == filledcolor) || (fillcolor == 255))
	{
		//printf( "not filling skin from %d to %d\n", fillcolor, filledcolor );
		return;
	}

	fifo[inpt].x = 0, fifo[inpt].y = 0;
	inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;

	while (outpt != inpt)
	{
		int	x = fifo[outpt].x, y = fifo[outpt].y;
		int	fdc = filledcolor;
		byte	*pos = &skin[x + skinwidth * y];

		outpt = (outpt + 1) & FLOODFILL_FIFO_MASK;

		if (x > 0)
			FLOODFILL_STEP( -1, -1, 0 );

		if (x < skinwidth - 1)
			FLOODFILL_STEP( 1, 1, 0 );

		if (y > 0)
			FLOODFILL_STEP( -skinwidth, 0, -1 );

		if (y < skinheight - 1)
			FLOODFILL_STEP( skinwidth, 0, 1 );

		skin[x + skinwidth * y] = fdc;
	}
}

/*
===============
Mod_LoadAllSkins
===============
*/

void *Mod_LoadAllSkins (int numskins, daliasskintype_t *pskintype)
{
	int			i, j, k;
	char			name[32]; // MAX_QPATH ? - CHECK Cowcat 2019
	int			s;
	byte			*skin;
	byte			*texels;
	daliasskingroup_t	*pinskingroup;
	int			groupskins;
	daliasskininterval_t	*pinskinintervals;
  
	skin = (byte *)(pskintype + 1);

	if (numskins < 1 || numskins > MAX_SKINS)
		Sys_Error ("Mod_LoadAliasModel: Invalid # of skins: %d\n", numskins);

	s = pheader->skinwidth * pheader->skinheight;

	for (i=0 ; i<numskins ; i++)
	{
		if (pskintype->type == ALIAS_SKIN_SINGLE)
		{
			Mod_FloodFillSkin( skin, pheader->skinwidth, pheader->skinheight );

			// save 8 bit texels for the player model to remap
			//   if (!strcmp(loadmodel->name,"progs/player.mdl")) {

			texels = Hunk_AllocName(s, loadname);
			pheader->texels[i] = texels - (byte *)pheader;
			memcpy (texels, (byte *)(pskintype + 1), s);

			//    }

			sprintf (name, "%s_%i", loadmodel->name, i);

			pheader->gl_texturenum[i][0] =
			pheader->gl_texturenum[i][1] =
			pheader->gl_texturenum[i][2] =
			pheader->gl_texturenum[i][3] =
			GL_LoadTexture (name, pheader->skinwidth, pheader->skinheight, (byte *)(pskintype + 1), true, false);

			pskintype = (daliasskintype_t *)((byte *)(pskintype+1) + s);
		}

		else
		{
			// animating skin group.  yuck.
			pskintype++;
			pinskingroup = (daliasskingroup_t *)pskintype;
			groupskins = LittleLong (pinskingroup->numskins);
			pinskinintervals = (daliasskininterval_t *)(pinskingroup + 1);

			pskintype = (void *)(pinskinintervals + groupskins);

			for (j=0 ; j<groupskins ; j++)
			{
				Mod_FloodFillSkin( skin, pheader->skinwidth, pheader->skinheight );

				if (j == 0)
				{
					texels = Hunk_AllocName(s, loadname);
					pheader->texels[i] = texels - (byte *)pheader;
					memcpy (texels, (byte *)(pskintype), s);
				}

				sprintf (name, "%s_%i_%i", loadmodel->name, i,j);

				pheader->gl_texturenum[i][j&3] = 
				GL_LoadTexture (name, pheader->skinwidth, pheader->skinheight, (byte *)(pskintype), true, false);

				pskintype = (daliasskintype_t *)((byte *)(pskintype) + s);
			}

			k = j;

			for (/* */; j < 4; j++)
				pheader->gl_texturenum[i][j&3] = pheader->gl_texturenum[i][j - k]; 
		}
	}

	return (void *)pskintype;
}

//=========================================================================

///// glQrack / Reckless mod

typedef struct
{
	char	*name;
	//int	len;
	int	hints;

} mhints_t;

mhints_t modelhints[] =
{
	{ "progs/player.mdl"	, MOD_PLAYER },
	{ "progs/flame.mdl"	, MOD_FLAME },
	{ "progs/flame2.mdl"	, MOD_FLAME },
	{ "progs/bolt.mdl"	, MOD_THUNDERBOLT },
	{ "progs/bolt2.mdl"	, MOD_THUNDERBOLT },
	{ "progs/eyes.mdl"	, MOD_EYES },
	{ "progs/w_spike.mdl"	, MOD_SPIKE },
	{ "progs/s_light.mdl"	, MOD_LIGHT },
	{ "progs/quaddama.mdl"	, MOD_QUADDAMA },
	{ "progs/invulner.mdl"	, MOD_INVULNER },
	{ NULL, MOD_NORMAL }
};

int numhints = sizeof(modelhints) / sizeof(modelhints[0]) - 1;

int Mod_FindModelHints(char *name)
{
	int i;

	for(i=0; i < numhints; i++)
	{
		/*
		if(modelhints[i].len > 0)
		{
			if(!strncmp(name, modelhints[i].name, modelhints[i].len))
			{
				return modelhints[i].hints;
			}
		}

		else
		*/

		{
			if(!strcmp(name, modelhints[i].name))
				return modelhints[i].hints;
		}
	}
	
	return MOD_NORMAL;

}
/////

#define MOD_NOSHADOW	256
#define MOD_GLOWS	512
#define MOD_FULLBRIGHT	1024

cvar_t r_fullbright_list = {"r_fullbright_list","progs/flame.mdl,progs/flame2.mdl"};
cvar_t r_glows_list = {"r_glows_list","progs/flame.mdl,progs/flame2.mdl,progs/bolt.mdl,progs/bolt2.mdl,progs/eyes.mdl,progs/w_spike.mdl,progs/s_light.mdl,progs/quaddama.mdl,progs/invulner.mdl"};
cvar_t r_noshadow_list = {"r_noshadow_list","progs/flame.mdl,progs/flame2.mdl,progs/bolt.mdl,progs/bolt2.mdl,progs/laser.mdl,progs/spike.mdl,progs/s_spike.mdl,progs/k_spike.mdl,progs/lavaball.mdl,progs/missile.mdl"};

static qboolean nameInList(const char *list, const char *name) // from quakespasm
{
	const char *s;
	char	tmp[MAX_QPATH];
	int	i;

	s = list;

	while(*s)
	{
		i = 0;

		while(*s && *s != ',')
		{
			if (i < MAX_QPATH - 1)
				tmp[i++] = *s;

			s++;
		}

		tmp[i] = '\0';

		if(!strcmp(name, tmp))
			return true;

		while(*s && *s == ',')
			s++;
	}

	return false;
}
		
void Mod_SetExtraFlags (model_t *mod)
{
	//cvar_t r_glow_list;
	//cvar_t r_noshadow_list;

	//char	*s;
	//int	i;

	if( !mod || mod->type != mod_alias ) // bogus mod->name - Cowcat 2019
		return;

	mod->flags &= 0xFF;
	
	#if 0

	for (s = r_glows_list.string; *s ; s += i + 1, i = 0)
	{
		for(i=0; s[i] != ',' && s[i] != 0; i++) ;
			if(!strncmp(mod->name, s, i))
			{
				mod->flags |= MOD_GLOWS;
				break;
			}
	}


	for (s = r_noshadow_list.string; *s ; s += i + 1, i = 0)
	{
		for(i=0; s[i] != ',' && s[i] != 0; i++) ;
			if(!strncmp(mod->name, s, i))
			{
				mod->flags |= MOD_NOSHADOW;
				break;
			}
	}

	#endif

	if(nameInList(r_glows_list.string, mod->name))
		mod->flags |= MOD_GLOWS;

	if(nameInList(r_noshadow_list.string, mod->name))
		mod->flags |= MOD_NOSHADOW;

	if(nameInList(r_fullbright_list.string, mod->name))
		mod->flags |= MOD_FULLBRIGHT;
}
	

/*
=================
Mod_LoadAliasModel
=================
*/

void Mod_LoadAliasModel (model_t *mod, void *buffer)
{
	int			i, j, k;
	mdl_t			*pinmodel;
	stvert_t		*pinstverts;
	dtriangle_t		*pintriangles;
	int			version, numframes;
	int			size;
	daliasframetype_t	*pframetype;
	daliasskintype_t	*pskintype;
	int			start, end, total;

	start = Hunk_LowMark ();

	pinmodel = (mdl_t *)buffer;

	version = LittleLong (pinmodel->version);

	if (version != ALIAS_VERSION)
		Sys_Error ("%s has wrong version number (%i should be %i)", mod->name, version, ALIAS_VERSION);

	mod->modhint |= Mod_FindModelHints(mod->name); // glQrack - Reckless

	//
	// allocate space for a working header, plus all the data except the frames,
	// skin and group info
	//

	size =	sizeof (aliashdr_t) + (LittleLong (pinmodel->numframes) - 1) * sizeof (pheader->frames[0]);
	pheader = Hunk_AllocName (size, loadname);
  
	mod->flags = LittleLong (pinmodel->flags);

	//
	// endian-adjust and copy the data, starting with the alias model header
	//

	pheader->boundingradius = LittleFloat (pinmodel->boundingradius);
	pheader->numskins = LittleLong (pinmodel->numskins);
	pheader->skinwidth = LittleLong (pinmodel->skinwidth);
	pheader->skinheight = LittleLong (pinmodel->skinheight);

	if (pheader->skinheight > MAX_LBM_HEIGHT)
		Sys_Error ("model %s has a skin taller than %d", mod->name, MAX_LBM_HEIGHT);

	pheader->numverts = LittleLong (pinmodel->numverts);

	if (pheader->numverts <= 0)
		Sys_Error ("model %s has no vertices", mod->name);

	if (pheader->numverts > MAXALIASVERTS)
		Sys_Error ("model %s has too many vertices", mod->name);

	pheader->numtris = LittleLong (pinmodel->numtris);

	if (pheader->numtris <= 0)
		Sys_Error ("model %s has no triangles", mod->name);

	pheader->numframes = LittleLong (pinmodel->numframes);
	numframes = pheader->numframes;

	if (numframes < 1)
		Sys_Error ("Mod_LoadAliasModel: Invalid # of frames: %d\n", numframes);

	pheader->size = LittleFloat (pinmodel->size) * ALIAS_BASE_SIZE_RATIO;
	mod->synctype = LittleLong (pinmodel->synctype);
	mod->numframes = pheader->numframes;

	for (i=0 ; i<3 ; i++)
	{
		pheader->scale[i] = LittleFloat (pinmodel->scale[i]);
		pheader->scale_origin[i] = LittleFloat (pinmodel->scale_origin[i]);
		pheader->eyeposition[i] = LittleFloat (pinmodel->eyeposition[i]);
	}

	//
	// load the skins
	//

	pskintype = (daliasskintype_t *)&pinmodel[1];
	pskintype = Mod_LoadAllSkins (pheader->numskins, pskintype);

	//
	// load base s and t vertices
	//

	pinstverts = (stvert_t *)pskintype;

	for (i=0 ; i<pheader->numverts ; i++)
	{
		stverts[i].onseam = LittleLong (pinstverts[i].onseam);
		stverts[i].s = LittleLong (pinstverts[i].s);
		stverts[i].t = LittleLong (pinstverts[i].t);
	}

	//
	// load triangle lists
	//

	pintriangles = (dtriangle_t *)&pinstverts[pheader->numverts];

	for (i=0 ; i<pheader->numtris ; i++)
	{
		triangles[i].facesfront = LittleLong (pintriangles[i].facesfront);

		for (j=0 ; j<3 ; j++)
		{
			triangles[i].vertindex[j] = LittleLong (pintriangles[i].vertindex[j]);
		}
	}

	//
	// load the frames
	//

	posenum = 0;
	pframetype = (daliasframetype_t *)&pintriangles[pheader->numtris];

	for (i=0 ; i<numframes ; i++)
	{
		aliasframetype_t  frametype;

		frametype = LittleLong (pframetype->type);

		if (frametype == ALIAS_SINGLE)
		{
			pframetype = (daliasframetype_t *)Mod_LoadAliasFrame (pframetype + 1, &pheader->frames[i]);
		}

		else
		{
			pframetype = (daliasframetype_t *)Mod_LoadAliasGroup (pframetype + 1, &pheader->frames[i]);
		}
	}

	pheader->numposes = posenum;

	mod->type = mod_alias;

	// FIXME: do this right - (Done! - Cowcat)
	mod->mins[0] = mod->mins[1] = mod->mins[2] = -16;
	mod->maxs[0] = mod->maxs[1] = mod->maxs[2] = 16;
	
	// proquake - finally fix bounding boxes !!

	if (fixboundbox)
	{
		aliasbboxmins[0] = aliasbboxmins[1] = aliasbboxmins[2] = 255;	// boundfixes
		aliasbboxmaxs[0] = aliasbboxmaxs[1] = aliasbboxmaxs[2] = 0;	//

		for(i = 0; i < pheader->numposes; i++)
		{
			int j;

			for(j = 0; j < pheader->numverts; j++)
			{
				byte	*vert = poseverts[i][j].v;
				float	v;

				for(k = 0; k < 3; k++)
				{
					v = (float)vert[k];
					v *= pheader->scale[k];
					v += pheader->scale_origin[k];

					if(v < mod->mins[k])
						mod->mins[k] = v;

					if(v > mod->maxs[k])
						mod->maxs[k] = v;
				}
			}
		}

		for(k = 0; k < 3; k++)
		{
			mod->mins[k] -= 32.0f;
			mod->maxs[k] += 32.0f;
		}

		if(mod->maxs[1] > mod->maxs[0])
			mod->maxs[0] = mod->maxs[1];
	
		else
			mod->maxs[0] = mod->maxs[0];

		if(mod->maxs[2] > mod->maxs[0])
		{
			mod->maxs[0] = mod->maxs[2];
			mod->maxs[1] = mod->maxs[2];
		}
	
		else
			mod->maxs[2] = mod->maxs[0];

		if(mod->mins[1] < mod->mins[0])
			mod->mins[0] = mod->mins[1];
	
		else
			mod->mins[1] = mod->mins[0];

		if(mod->mins[2] < mod->mins[0])
		{
			mod->mins[0] = mod->mins[2];
			mod->mins[1] = mod->mins[2];
		}
	
		else
			mod->mins[2] = mod->mins[0];
		
		mod->radius = RadiusFromBounds (mod->mins, mod->maxs);
	} 

	Mod_SetExtraFlags (mod); // johnfitz - Cowcat

	//
	// build the draw lists
	//

	GL_MakeAliasModelDisplayLists (mod, pheader);

	//
	// move the complete, relocatable alias model to the cache
	// 

	end = Hunk_LowMark ();
	total = end - start;
  
	Cache_Alloc (&mod->cache, total, loadname);

	if (!mod->cache.data)
		return;

	memcpy (mod->cache.data, pheader, total);

	Hunk_FreeToLowMark (start);
}

//=============================================================================

/*
=================
Mod_LoadSpriteFrame
=================
*/

void *Mod_LoadSpriteFrame (void * pin, mspriteframe_t **ppframe, int framenum)
{
	dspriteframe_t	*pinframe;
	mspriteframe_t	*pspriteframe;
	int		width, height, size, origin[2];
	char		name[64];

	pinframe = (dspriteframe_t *)pin;

	width = LittleLong (pinframe->width);
	height = LittleLong (pinframe->height);
	size = width * height;

	pspriteframe = Hunk_AllocName (sizeof (mspriteframe_t),loadname);

	Q_memset (pspriteframe, 0, sizeof (mspriteframe_t));

	*ppframe = pspriteframe;

	pspriteframe->width = width;
	pspriteframe->height = height;
	origin[0] = LittleLong (pinframe->origin[0]);
	origin[1] = LittleLong (pinframe->origin[1]);

	pspriteframe->up = origin[1];
	pspriteframe->down = origin[1] - height;
	pspriteframe->left = origin[0];
	pspriteframe->right = width + origin[0];

	sprintf (name, "%s_%i", loadmodel->name, framenum);
	pspriteframe->gl_texturenum = GL_LoadTexture (name, width, height, (byte *)(pinframe + 1), true, true);

	return (void *)((byte *)pinframe + sizeof (dspriteframe_t) + size);
}


/*
=================
Mod_LoadSpriteGroup
=================
*/

void *Mod_LoadSpriteGroup (void * pin, mspriteframe_t **ppframe, int framenum)
{
	dspritegroup_t	*pingroup;
	mspritegroup_t	*pspritegroup;
	int		i, numframes;
	dspriteinterval_t *pin_intervals;
	float		*poutintervals;
	void		*ptemp;

	pingroup = (dspritegroup_t *)pin;

	numframes = LittleLong (pingroup->numframes);

	pspritegroup = Hunk_AllocName (sizeof (mspritegroup_t) + (numframes - 1) * sizeof (pspritegroup->frames[0]), loadname);

	pspritegroup->numframes = numframes;

	*ppframe = (mspriteframe_t *)pspritegroup;

	pin_intervals = (dspriteinterval_t *)(pingroup + 1);

	poutintervals = Hunk_AllocName (numframes * sizeof (float), loadname);

	pspritegroup->intervals = poutintervals;

	for (i=0 ; i<numframes ; i++)
	{
		*poutintervals = LittleFloat (pin_intervals->interval);

		if (*poutintervals <= 0.0)
			Sys_Error ("Mod_LoadSpriteGroup: interval<=0");

		poutintervals++;
		pin_intervals++;
	}

	ptemp = (void *)pin_intervals;

	for (i=0 ; i<numframes ; i++)
	{
		ptemp = Mod_LoadSpriteFrame (ptemp, &pspritegroup->frames[i], framenum * 100 + i);
	}

	return ptemp;
}


/*
=================
Mod_LoadSpriteModel
=================
*/

void Mod_LoadSpriteModel (model_t *mod, void *buffer)
{
	int		i;
	int		version;
	dsprite_t	*pin;
	msprite_t	*psprite;
	int		numframes;
	int		size;
	dspriteframetype_t  *pframetype;
  
	pin = (dsprite_t *)buffer;

	version = LittleLong (pin->version);

	if (version != SPRITE_VERSION)
		Sys_Error ("%s has wrong version number "
			"(%i should be %i)", mod->name, version, SPRITE_VERSION);

	numframes = LittleLong (pin->numframes);

	size = sizeof (msprite_t) + (numframes - 1) * sizeof (psprite->frames);

	psprite = Hunk_AllocName (size, loadname);

	mod->cache.data = psprite;

	psprite->type = LittleLong (pin->type);
	psprite->maxwidth = LittleLong (pin->width);
	psprite->maxheight = LittleLong (pin->height);
	psprite->beamlength = LittleFloat (pin->beamlength);
	mod->synctype = LittleLong (pin->synctype);
	psprite->numframes = numframes;

	mod->mins[0] = mod->mins[1] = -psprite->maxwidth/2;
	mod->maxs[0] = mod->maxs[1] = psprite->maxwidth/2;
	mod->mins[2] = -psprite->maxheight/2;
	mod->maxs[2] = psprite->maxheight/2;
  
	//
	// load the frames
	//

	if (numframes < 1)
		Sys_Error ("Mod_LoadSpriteModel: Invalid # of frames: %d\n", numframes);

	mod->numframes = numframes;

	pframetype = (dspriteframetype_t *)(pin + 1);

	for (i=0 ; i<numframes ; i++)
	{
		spriteframetype_t frametype;

		frametype = LittleLong (pframetype->type);
		psprite->frames[i].type = frametype;

		if (frametype == SPR_SINGLE)
		{
			pframetype = (dspriteframetype_t *)
			Mod_LoadSpriteFrame (pframetype + 1, &psprite->frames[i].frameptr, i);
		}

		else
		{
			pframetype = (dspriteframetype_t *)
			Mod_LoadSpriteGroup (pframetype + 1, &psprite->frames[i].frameptr, i);
		}
	}

	mod->type = mod_sprite;
}

//=============================================================================

/*
================
Mod_Print
================
*/
void Mod_Print (void)
{
	int   i;
	model_t *mod;

	Con_Printf ("Cached models:\n");

	for (i=0, mod=mod_known ; i < mod_numknown ; i++, mod++)
	{
		Con_Printf ("%8p : %s\n",mod->cache.data, mod->name);
	}
}


