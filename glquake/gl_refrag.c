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
// r_efrag.c

#include "quakedef.h"

mnode_t *r_pefragtopnode;


//===========================================================================

/*
===============================================================================

	  ENTITY FRAGMENT FUNCTIONS

===============================================================================
*/

//efrag_t **lastlink;
vec3_t	r_emins, r_emaxs;
entity_t *r_addent;

// mh extended efrags

#if 1

//ericw based on RMQEngine

#define	EXTRA_EFRAGS 128 // 32

static efrag_t *R_GetEfrag(void)
{
	if(cl.free_efrags)
	{
		efrag_t *ef = cl.free_efrags;
		cl.free_efrags = ef->leafnext;
		ef->leafnext = NULL;

		cl.num_efrags++;
	
		return ef;
	}

	else
	{
		int i;
	
		cl.free_efrags = (efrag_t *) Hunk_AllocName (EXTRA_EFRAGS * sizeof(efrag_t), "efrags");

		for (i=0; i < EXTRA_EFRAGS - 1 ; i++)
			cl.free_efrags[i].leafnext = &cl.free_efrags[i + 1];

		cl.free_efrags[i].leafnext = NULL;

		//call recursively
		return R_GetEfrag();
	}
}

#else

//#define	EXTRA_EFRAGS 128 // was 32
#define	EXTRA_EFRAGS 32 // 2019 - Cowcat
	
void R_GetMoreEfrags(void)
{
	int i;
	
	cl.free_efrags = (efrag_t *) Hunk_Alloc (EXTRA_EFRAGS * sizeof(efrag_t));
	//cl.free_efrags = (efrag_t *) Hunk_AllocName (EXTRA_EFRAGS * sizeof(efrag_t), "efrags");

	for (i=0; i < EXTRA_EFRAGS - 1 ; i++)
		cl.free_efrags[i].entnext = &cl.free_efrags[i + 1];

	cl.free_efrags[i].entnext = NULL;
}
//

#endif

/*
================
R_RemoveEfrags

Call when removing an object from the world or moving it to another position
================
*/

#if 0  // not used now - Cowcat
void R_RemoveEfrags (entity_t *ent)
{
	efrag_t	  *ef, *old, *walk, **prev;
  
	ef = ent->efrag;
  
	while (ef)
	{
		prev = &ef->leaf->efrags;

		while (1)
		{
			walk = *prev;

			if (!walk)
				break;

			if (walk == ef)
			{
				// remove this fragment
				*prev = ef->leafnext;
				break;
			}

			else
				prev = &walk->leafnext;
		}
	
		old = ef;
		ef = ef->entnext;
    
		// put it on the free list
		old->entnext = cl.free_efrags;
		cl.free_efrags = old;
	}
  
	ent->efrag = NULL; 
}
#endif


/*
===================
R_SplitEntityOnNode
===================
*/

#if 0

void R_SplitEntityOnNode (mnode_t *node)
{
	efrag_t		*ef;
	mplane_t	*splitplane;
	mleaf_t		*leaf;
	int		sides;
  
loc0:
	if (node->contents == CONTENTS_SOLID)
		return;
  
	// add an efrag if the node is a leaf

	if ( node->contents < 0)
	{
		if (!r_pefragtopnode)
			r_pefragtopnode = node;

		leaf = (mleaf_t *)node;

		// grab an efrag off the free list
		ef = cl.free_efrags;

		if (!ef)
		{
			#if 0

			Con_Printf ("Too many efrags!\n");
			return;	  // no free fragments...

			#else // mh

			Con_Printf ("Get more efrags!\n");
			R_GetEfrags();
			ef = cl.free_efrags;

			#endif
		}

		cl.free_efrags = cl.free_efrags->entnext;

		ef->entity = r_addent;
    
		// add the entity link	
		*lastlink = ef;
		lastlink = &ef->entnext;
		ef->entnext = NULL;
    
		// set the leaf links
		ef->leaf = leaf;
		ef->leafnext = leaf->efrags;
		leaf->efrags = ef;
      
		return;
	}
  
	// NODE_MIXED

	splitplane = node->plane;

#ifdef FASTBOPS
	sides = BOX_ON_PLANE_SIDE(r_emins, r_emaxs, splitplane);
#else
	sides = BoxOnPlaneSide(r_emins, r_emaxs, splitplane);
#endif
  
	if (sides == 3)
	{
		// split on this plane
		// if this is the first splitter of this bmodel, remember it
		if (!r_pefragtopnode)
			r_pefragtopnode = node;
	}
  
	// recurse down the contacted sides
	/*
	if (sides & 1)
		R_SplitEntityOnNode (node->children[0]);
    
	if (sides & 2)
		R_SplitEntityOnNode (node->children[1]);
	*/

	//Surgeon: optimized Recursion a'la LordHavoc
	switch (sides)
	{
		case 1:
			node = node->children[0];
			goto loc0;

		case 2:
			node = node->children[1];
			goto loc0;

		default: // 3
			if (node->children[0]->contents != CONTENTS_SOLID)
				R_SplitEntityOnNode (node->children[0]);

			node = node->children[1];
			goto loc0;
	}

}

#else

void R_SplitEntityOnNode (mnode_t *node)
{
	efrag_t		*ef;
	mplane_t	*splitplane;
	mleaf_t		*leaf;
	int		sides;
  
loc0:
	if (node->contents == CONTENTS_SOLID)
		return;
  
	// add an efrag if the node is a leaf

	if ( node->contents < 0)
	{
		if (!r_pefragtopnode)
			r_pefragtopnode = node;

		leaf = (mleaf_t *)node;

		// grab an efrag off the free list
		ef = R_GetEfrag();
		ef->entity = r_addent;

		// set the leaf links
		ef->leafnext = leaf->efrags;
		leaf->efrags = ef;
      
		return;
	}

	// NODE_MIXED

	splitplane = node->plane;

#ifdef FASTBOPS
	sides = BOX_ON_PLANE_SIDE(r_emins, r_emaxs, splitplane);
#else
	sides = BoxOnPlaneSide(r_emins, r_emaxs, splitplane);
#endif
  
	if (sides == 3)
	{
		// split on this plane
		// if this is the first splitter of this bmodel, remember it
		if (!r_pefragtopnode)
			r_pefragtopnode = node;
	}
  
	// recurse down the contacted sides
	/*
	if (sides & 1)
		R_SplitEntityOnNode (node->children[0]);
    
	if (sides & 2)
		R_SplitEntityOnNode (node->children[1]);
	*/

	//Surgeon: optimized Recursion a'la LordHavoc
	switch (sides)
	{
		case 1:
			node = node->children[0];
			goto loc0;

		case 2:
			node = node->children[1];
			goto loc0;

		default: // 3
			if (node->children[0]->contents != CONTENTS_SOLID)
				R_SplitEntityOnNode (node->children[0]);

			node = node->children[1];
			goto loc0;
	}
}

#endif

/*
===========
R_AddEfrags
===========
*/

void R_AddEfrags (entity_t *ent)
{
	model_t *entmodel;
	int	i;
    
	if (!ent->model)
		return;

	r_addent = ent;
      
	//lastlink = &ent->efrag; // 
	r_pefragtopnode = NULL;
  
	entmodel = ent->model;

	for (i=0 ; i<3 ; i++)
	{
		r_emins[i] = ent->origin[i] + entmodel->mins[i];
		r_emaxs[i] = ent->origin[i] + entmodel->maxs[i];
	}

	R_SplitEntityOnNode (cl.worldmodel->nodes);

	ent->topnode = r_pefragtopnode;
}


/*
================
R_StoreEfrags

// FIXME: a lot of this goes away with edge-based
================
*/

void R_StoreEfrags (efrag_t **ppefrag) // pointless switch statement removed
{
	entity_t  *pent;
	efrag_t	  *pefrag;

	#if 0

	model_t	  *clmodel;

	while ((pefrag = *ppefrag) != NULL)
	{
		pent = pefrag->entity;
		clmodel = pent->model;

		switch (clmodel->type)
		{
			case mod_alias:
			case mod_brush:
			case mod_sprite:
				pent = pefrag->entity;

			if ((pent->visframe != r_framecount) && (cl_numvisedicts < MAX_VISEDICTS))
			{
				cl_visedicts[cl_numvisedicts++] = pent;

			// mark that we've recorded this entity for this frame
				pent->visframe = r_framecount;
			}

			ppefrag = &pefrag->leafnext;
		break;

		default:  
			Sys_Error ("R_StoreEfrags: Bad entity type %d\n", clmodel->type);
		}
	}

	#else

	while ((pefrag = *ppefrag) != NULL)
	{
		pent = pefrag->entity;

		if ((pent->visframe != r_framecount) && (cl_numvisedicts < MAX_VISEDICTS))
		{
			cl_visedicts[cl_numvisedicts++] = pent;

			// mark that we've recorded this entity for this frame
			pent->visframe = r_framecount;
		}

		ppefrag = &pefrag->leafnext;
	}

	#endif
	
}	


