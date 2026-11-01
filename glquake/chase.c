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
// chase.c -- chase camera code

//modified as glQrack/Mark V - Cowcat

#include "quakedef.h"

cvar_t	chase_back = {"chase_back","100", true};
cvar_t	chase_up = {"chase_up","16", true};
cvar_t	chase_right = {"chase_right","0", true};
cvar_t	chase_active = {"chase_active","0", true};
cvar_t	chase_transparent = {"chase_transparent","0", true};

float chase_alpha;

extern qboolean SV_RecursiveHullCheck (hull_t *, int, float, float, vec3_t, vec3_t, trace_t *);

void Chase_Init (void)
{
	Cvar_RegisterVariable (&chase_back);
	Cvar_RegisterVariable (&chase_up);
	Cvar_RegisterVariable (&chase_right);
	Cvar_RegisterVariable (&chase_active);
	Cvar_RegisterVariable (&chase_transparent);
}

#if 0 // old

qboolean TraceLine (vec3_t start, vec3_t end, vec3_t impact)
{
	trace_t trace; 

	memset (&trace, 0, sizeof(trace));
	
	SV_RecursiveHullCheck (cl.worldmodel->hulls, 0, 0, 1, start, end, &trace);

	VectorCopy (trace.endpos, impact);

	if(impact[0] || impact[1] || impact[2])
		return false;
	
	return true;
	
}

void LerpVector (const vec3_t from, const vec3_t to, float frac, vec3_t out)
{
	out[0] = from[0] + frac * (to[0] - from[0]);
	out[1] = from[1] + frac * (to[1] - from[1]);
	out[2] = from[2] + frac * (to[2] - from[2]);
}

void Chase_Update (void)
{
	int	i;
	float	dist;
	vec3_t	forward, up, right, dest, stop;

	float	alpha, alphadist;
	//extern cvar_t chase_transparent;

	// if can't see player, reset
	AngleVectors (cl.viewangles, forward, right, up);

	// calc exact destination
	for (i=0 ; i<3 ; i++)
		chase_dest[i] = r_refdef.vieworg[i] - forward[i]*chase_back.value - right[i]*chase_right.value;

	chase_dest[2] = r_refdef.vieworg[2] + chase_up.value;

	// find the spot the player is looking at
	VectorMA (r_refdef.vieworg, 4096, forward, dest);
	TraceLine (r_refdef.vieworg, dest, stop);

	// calculate pitch to look at the same spot from camera
	VectorSubtract (stop, r_refdef.vieworg, stop);

	dist = DotProduct (stop, forward);

	if (dist < 1)
		dist = 1;

	r_refdef.viewangles[PITCH] = - 180 / M_PI * atan2(stop[2], dist);
	r_refdef.viewangles[YAW] = cl.viewangles[YAW];

	TraceLine (r_refdef.vieworg, chase_dest, stop); // fix 

	//if (Length(stop) != 0)
	if (stop[0] != 0 || stop[1] != 0 || stop[2] != 0 )
	{
		VectorCopy (stop, chase_dest); // fix 

		#if 0
		alphadist = VecLength2(r_refdef.vieworg, chase_dest);

		//if(chase_transparent.value)
		{
			alpha = bound(0.1,(alphadist/chase_back.value),1);

			if(alpha < 0.25)
				alpha = 0;

			cl_entities[cl.viewentity].alpha = min(alpha, 0.3 /*chase_transparent.value*/ );
		}

		else
			cl_entities[cl.viewentity].alpha = 0.5 /*1.0*/ ;
		#endif

		//pr_alpha_supported = true; // hack !

		cl_entities[cl.viewentity].alpha = 0.5;

		// set MygluPerspective znear to 1
		LerpVector (r_refdef.vieworg, chase_dest, 0.9f, chase_dest);
	}
	

	// move towards destination
	VectorCopy (chase_dest, r_refdef.vieworg);
}

#else

void LerpVector (const vec3_t from, const vec3_t to, float frac, vec3_t out)
{
	out[0] = from[0] + frac * (to[0] - from[0]);
	out[1] = from[1] + frac * (to[1] - from[1]);
	out[2] = from[2] + frac * (to[2] - from[2]);
}

void TraceLine (vec3_t start, vec3_t end, vec3_t impact)
{
	trace_t trace; 
	qboolean result;

	memset (&trace, 0, sizeof(trace));
	
	result = SV_RecursiveHullCheck (cl.worldmodel->hulls, 0, 0, 1, start, end, &trace);

	if(!result)
	{
		VectorCopy (trace.endpos, impact);

	}

	else
		VectorCopy (end, impact);
}

/*
void VectorAngles (const vec3_t forward, vec3_t angles)
{
	vec3_t temp;

	temp[0] = forward[0];
	temp[1] = forward[1];
	temp[2] = 0;

	angles[PITCH] = -atan2(forward[2], Length(temp)) / (M_PI / 180.0);
	angles[YAW] = atan2(forward[1], forward[0]) / (M_PI / 180.0);
	angles[ROLL] = 0;
} 
*/

float VecLength2(vec3_t v1, vec3_t v2)
{
	vec3_t k;
	VectorSubtract(v1, v2, k);
	return sqrt(k[0]*k[0] + k[1]*k[1] + k[2]*k[2]);
}

#define bound(min, num, max) (num >= min ? (num < max ? num : max) : min)
#define min(a,b) (a < b ? a : b)

void Chase_Update (void) // QuakeSpasm modded - Cowcat 2019
{
	int	i;
	vec3_t	forward, up, right;
	vec3_t	ideal, crosshair, temp;
	float	alpha = 1, alphadist = 1;
	
	//AngleVectors (cl.viewangles, forward, right, up);
	AngleVectors (cl.lerpangles, forward, right, up); // this fixes playdemo with chase - Cowcat

	// calc ideal camera location before checking for walls
	for (i=0 ; i<3 ; i++)
		ideal[i] = cl.viewent.origin[i] - forward[i]*chase_back.value + right[i]*chase_right.value;

	ideal[2] = cl.viewent.origin[2] + chase_up.value;

	// make sure camera is not in or behind a wall
	TraceLine (r_refdef.vieworg, ideal, temp);

	//if(Length(temp) != 0)
	if ( temp[0] != 0 || temp[1] != 0 || temp[2] != 0 )
	{
		VectorCopy(temp, ideal);

		#if 1
		
		if(chase_transparent.value)
		{
			alphadist = VecLength2(r_refdef.vieworg, ideal);
			alpha = bound(0.1, ( alphadist / chase_back.value ), 1);

			if(alpha < 0.09)
				alpha = 0;

			//chase_alpha = min(alpha, chase_transparent.value);
			chase_alpha = alpha;

			//cl_entities[cl.viewentity].alpha = alpha;
		}

		else
			chase_alpha = 1.0;
			//cl_entities[cl.viewentity].alpha = 1.0;
		#endif

		// set MygluPerspective znear to 1 - R00k
		LerpVector (r_refdef.vieworg, ideal, 0.9f, ideal); // - Cowcat 2019
	}

	//chase_alpha = 0.5f;

	// place camera
	VectorCopy (ideal, r_refdef.vieworg);

	// find the spot the player is looking at
	VectorMA (cl.viewent.origin, 4096, forward, temp);
	TraceLine (cl.viewent.origin, temp, crosshair); 

	// calculate camera angles to look at the same spot
	VectorSubtract (crosshair, r_refdef.vieworg, temp);

	/* VectorAngles(temp, r_refdef.viewangles); */
	
	// much better - Cowcat 2019
	float dist = DotProduct (temp, forward);

	if (dist < 1)
		dist = 1;

	r_refdef.viewangles[PITCH] = -atan(temp[2] / dist) / M_PI * 180.0;
	//r_refdef.viewangles[PITCH] = -180 / M_PI * atan2(temp[2], dist);
	//

	if( r_refdef.viewangles[PITCH] == 90 || r_refdef.viewangles[PITCH] == -90)
		r_refdef.viewangles[YAW] = cl.viewangles[YAW];
}

#endif
