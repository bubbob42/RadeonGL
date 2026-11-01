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

#include "quakedef.h"
#include "r_local.h"

#ifdef GLQUAKE
#include <mgl/mglmacros.h>
#endif

#define MAX_PARTICLES 2048  	// default max # of particles at one
                    		//  time

//#define ABSOLUTE_MIN_PARTICLES  512   // no fewer than this no matter what's
                    			// on the command line

//Surgeon: a performance killer in gl - can be disabled now
#define ABSOLUTE_MIN_PARTICLES  1
 
int   ramp1[8] = {0x6f, 0x6d, 0x6b, 0x69, 0x67, 0x65, 0x63, 0x61};
int   ramp2[8] = {0x6f, 0x6e, 0x6d, 0x6c, 0x6b, 0x6a, 0x68, 0x66};
int   ramp3[8] = {0x6d, 0x6b, 6, 5, 4, 3};

particle_t  *active_particles = NULL;
particle_t  *free_particles   = NULL;
particle_t  *particles        = NULL;

int     r_numparticles;
vec3_t	r_pright, r_pup, r_ppn;

/*
===============
R_InitParticles
===============
*/

void R_InitParticles (void)
{
  	int   i;

  	i = COM_CheckParm ("-particles");

  	if (i)
  	{
    		r_numparticles = (int)(Q_atoi(com_argv[i+1]));

    		if (r_numparticles < ABSOLUTE_MIN_PARTICLES)
      			r_numparticles = ABSOLUTE_MIN_PARTICLES;
  	}

  	else
  	{
    		r_numparticles = MAX_PARTICLES;
  	}

	#ifdef GLQUAKE
  	particles = (particle_t *) malloc(r_numparticles * sizeof(particle_t));
	#else
  	particles = (particle_t *) Hunk_AllocName (r_numparticles * sizeof(particle_t), "particles");
	#endif
}

#ifdef GLQUAKE
void R_FreeParticles(void)
{
  	if(particles)
		free(particles);
}
#endif


#ifdef QUAKE2
void R_DarkFieldParticles (entity_t *ent)
{
  	int     i, j, k;
  	particle_t  *p;
  	float   vel;
  	vec3_t    dir;
  	vec3_t    org;

  	org[0] = ent->origin[0];
  	org[1] = ent->origin[1];
  	org[2] = ent->origin[2];

  	for (i=-16 ; i<16 ; i+=8)
    		for (j=-16 ; j<16 ; j+=8)
      			for (k=0 ; k<32 ; k+=8)
      			{
        			if (!free_particles)
          				return;

        			p = free_particles;
        			free_particles = p->next;
        			p->next = active_particles;
        			active_particles = p;
    
        			p->die = cl.time + 0.2 + (rand()&7) * 0.02;
        			p->color = 150 + rand()%6;
        			p->type = pt_slowgrav;
        
        			dir[0] = j*8;
        			dir[1] = i*8;
        			dir[2] = k*8;
  
        			p->org[0] = org[0] + i + (rand()&3);
        			p->org[1] = org[1] + j + (rand()&3);
        			p->org[2] = org[2] + k + (rand()&3);
  
        			VectorNormalize (dir);            
        			vel = 50 + (rand()&63);
        			VectorScale (dir, vel, p->vel);
      			}
}
#endif


/*
===============
R_EntityParticles
===============
*/

#define NUMVERTEXNORMALS 162

#ifndef GLQUAKE
	
extern float r_avertexnormals[NUMVERTEXNORMALS][3];

#else

static float r_avertexnormals[NUMVERTEXNORMALS][3] = {
#include "anorms.h"
};

#endif

vec3_t	avelocities[NUMVERTEXNORMALS];
float 	beamlength = 16;

#if 0

static float FOPI = 1.27323954473516;

static float PIO4F = 0.7853981633974483096;

/* These are for a 24-bit significand: */
static float DP1 = 0.78515625;
static float DP2 = 2.4187564849853515625e-4;
static float DP3 = 3.77489497744594108e-8;
static float lossth = 8192.;
static float T24M1 = 16777215.;

static float sincof[] = {
	-1.9515295891E-4,
 	8.3321608736E-3,
	-1.6666654611E-1
};

static float coscof[] = {
 	2.443315711809948E-005,
	-1.388731625493765E-003,
 	4.166664568298827E-002
};

#ifdef ANSIC
float sinf( float xx )
#else
float sinf(xx)
double xx;
#endif
{
	float *p;
	float x, y, z;
	unsigned long j;
	int sign;

	sign = 1;
	x = xx;

	if( xx < 0 )
	{
		sign = -1;
		x = -xx;
	}

	if( x > T24M1 )
	{
		//mtherr( "sinf", TLOSS );
		return(0.0);
	}

	j = FOPI * x; /* integer part of x/(PI/4) */
	y = j;

/* map zeros to origin */
	if( j & 1 )
	{
		j += 1;
		y += 1.0;
	}

	j &= 7; /* octant modulo 360 degrees */

/* reflect in x axis */
	if( j > 3)
	{
		sign = -sign;
		j -= 4;
	}

	if( x > lossth )
	{
		//mtherr( "sinf", PLOSS );
		x = x - y * PIO4F;
	}

	else
	{
		/* Extended precision modular arithmetic */
		x = ((x - y * DP1) - y * DP2) - y * DP3;
	}

	/*einits();*/

	z = x * x;

	if( (j==1) || (j==2) )
	{
		/* measured relative error in +/- pi/4 is 7.8e-8 */
		/*
			y = ((  2.443315711809948E-005 * z
	  		- 1.388731625493765E-003) * z
	  		+ 4.166664568298827E-002) * z * z;
		*/

		p = coscof;
		y = *p++;
		y = y * z + *p++;
		y = y * z + *p++;
		y *= z * z;
		y -= 0.5 * z;
		y += 1.0;
	}

	else
	{
		/* Theoretical relative error = 3.8e-9 in [-pi/4, +pi/4] */
		/*
			y = ((-1.9515295891E-4 * z
	     		+ 8.3321608736E-3) * z
	     		- 1.6666654611E-1) * z * x;
			y += x;
		*/

		p = sincof;
		y = *p++;
		y = y * z + *p++;
		y = y * z + *p++;
		y *= z * x;
		y += x;
	}

	/*einitd();*/

	if(sign < 0)
		y = -y;

	return( y);
}


/* Single precision circular cosine
 * test interval: [-pi/4, +pi/4]
 * trials: 10000
 * peak relative error: 8.3e-8
 * rms relative error: 2.2e-8
 */

#ifdef ANSIC
float cosf( float xx )
#else
float cosf(xx)
double xx;
#endif
{
	float x, y, z;
	int j, sign;

	/* make argument positive */
	sign = 1;
	x = xx;

	if( x < 0 )
		x = -x;

	if( x > T24M1 )
	{
		//mtherr( "cosf", TLOSS );
		return(0.0);
	}

	j = FOPI * x; /* integer part of x/PIO4 */
	y = j;

/* integer and fractional part modulo one octant */

	if( j & 1 )	/* map zeros to origin */
	{
		j += 1;
		y += 1.0;
	}

	j &= 7;

	if( j > 3)
	{
		j -=4;
		sign = -sign;
	}

	if( j > 1 )
		sign = -sign;

	if( x > lossth )
	{
		//mtherr( "cosf", PLOSS );
		x = x - y * PIO4F;
	}

	else
		/* Extended precision modular arithmetic */
		x = ((x - y * DP1) - y * DP2) - y * DP3;

	z = x * x;

	if( (j==1) || (j==2) )
	{
		y = (((-1.9515295891E-4 * z
	     		+ 8.3321608736E-3) * z
	     		- 1.6666654611E-1) * z * x)
	     		+ x;
	}

	else
	{
		y = ((  2.443315711809948E-005 * z
	  		- 1.388731625493765E-003) * z
	  		+ 4.166664568298827E-002) * z * z;
		y -= 0.5 * z;
		y += 1.0;
	}

	if(sign < 0)
		y = -y;

	return( y );
}



#endif

void R_EntityParticles (entity_t *ent)
{
  	//int     	count;
  	int     	i;
  	particle_t  	*p;
  	float   	angle;
  	float   	sp, sy, cp, cy; // sr,cr
  	vec3_t    	forward;
  	float   	dist;
  
  	dist = 64;
  	//count = 50;

	if (!avelocities[0][0])
	{
		//for (i=0 ; i<NUMVERTEXNORMALS*3 ; i++)
			//avelocities[0][i] = (rand()&255) * 0.01;

		for (i=0 ; i<NUMVERTEXNORMALS ; i++) // new Cowcat
		{
			avelocities[i][0] = (rand()&255) * 0.01;
			avelocities[i][1] = (rand()&255) * 0.01;
			avelocities[i][2] = (rand()&255) * 0.01;
		}
	}

  	for (i=0 ; i<NUMVERTEXNORMALS ; i++)
  	{
		if (!free_particles)
      			return;		// Cowcat

    		angle = cl.time * avelocities[i][0];
    		sy = sin(angle);
    		cy = cos(angle);
    		angle = cl.time * avelocities[i][1];
    		sp = sin(angle);
    		cp = cos(angle);
    		//angle = cl.time * avelocities[i][2]; // Cowcat
    		//sr = sin(angle);
    		//cr = cos(angle);
  
    		forward[0] = cp*cy;
    		forward[1] = cp*sy;
    		forward[2] = -sp;

    		//if (!free_particles)
      			//return;

    		p = free_particles;
    		free_particles = p->next;
    		p->next = active_particles;
    		active_particles = p;

    		p->die = cl.time + 0.01;
    		p->color = 0x6f;
    		p->type = pt_explode;
    
    		p->org[0] = ent->origin[0] + r_avertexnormals[i][0]*dist + forward[0]*beamlength;     
    		p->org[1] = ent->origin[1] + r_avertexnormals[i][1]*dist + forward[1]*beamlength;     
    		p->org[2] = ent->origin[2] + r_avertexnormals[i][2]*dist + forward[2]*beamlength;     
  	}
}


/*
===============
R_ClearParticles
===============
*/

void R_ClearParticles (void)
{
  	int   i;
  
  	free_particles = &particles[0];
  	active_particles = NULL;

  	for (i=0 ;i<r_numparticles-1 ; i++)
  	{
    		particles[i].next = &particles[i+1];
  	}

  	particles[r_numparticles-1].next = NULL;
}


void R_ReadPointFile_f (void)
{
  	FILE  		*f;
  	vec3_t  	org;
  	int   		r;
  	int   		c;
  	particle_t  	*p;
  	char  		name[MAX_OSPATH];
  
  	sprintf (name,"maps/%s.pts", sv.name);

  	COM_FOpenFile (name, &f);

  	if (!f)
  	{
    		Con_Printf ("couldn't open %s\n", name);
    		return;
  	}
  
  	Con_Printf ("Reading %s...\n", name);
  	c = 0;

  	for ( ;; )
  	{
    		r = fscanf (f,"%f %f %f\n", &org[0], &org[1], &org[2]);

    		if (r != 3)
      			break;

    		c++;
    
    		if (!free_particles)
    		{
      			Con_Printf ("Not enough free particles\n");
      			break;
    		}

    		p = free_particles;
    		free_particles = p->next;
    		p->next = active_particles;
    		active_particles = p;
    
    		p->die = 99999;
    		p->color = (-c)&15;
    		p->type = pt_static;
    		VectorCopy (vec3_origin, p->vel);
    		VectorCopy (org, p->org);
  	}

  	fclose (f);
  	Con_Printf ("%i points read\n", c);
}


/*
===============
R_ParseParticleEffect

Parse an effect out of the server message
===============
*/

//#define bound(a, b, c) ((a) >= (c) ? (a) : (b) < (a) ? (a) : (b) > (c) ? (c) : (b))

void R_ParseParticleEffect (void)
{
  	vec3_t	org, dir;
  	int	i, count, color; 
  
  	for (i=0 ; i<3 ; i++)
    		org[i] = MSG_ReadCoord ();

  	for (i=0 ; i<3 ; i++)
    		dir[i] = MSG_ReadChar () * (1.0/16);

  	count = MSG_ReadByte (); // was msgcount
  	color = MSG_ReadByte ();

	#if 0

	if (msgcount == 255)
  		count = 1024;

	else
		count = msgcount;
		
  	R_RunParticleEffect (org, dir, color, count);

	#else

	if (count == 255) //  light for exploding barrels - joequake / glQrack source
	{
		dlight_t *dl;

		R_ParticleExplosion(org);
		
		dl = CL_AllocDlight (0);
		VectorCopy (org, dl->origin);
		dl->radius = 350; // * bound (0, r_explosionlight.value, 1); 
		dl->die = cl.time + 0.5;
		dl->decay = 300;

		//dl->type = SetDlightColor(r_explosionlightcolour.value, lt_explosion, true); // to be done
	}

	else	
	{
		R_RunParticleEffect (org, dir, color, count);
	}

	#endif
}			
  
/*
===============
R_ParticleExplosion

===============
*/

void R_ParticleExplosion (vec3_t org)
{
  	int         i, j;
  	particle_t  *p;

  	for (i=0 ; i<1024 ; i++)
  	{
    		if (!free_particles)
      			return;

    		p = free_particles;
    		free_particles = p->next;
    		p->next = active_particles;
    		active_particles = p;

    		p->die = cl.time + 5;
    		p->color = ramp1[0];
    		p->ramp = rand()&3;

		p->type = (i & 1) ? pt_explode : pt_explode2;

		for (j=0 ; j<3 ; j++)
      		{
        		p->org[j] = org[j] + ((rand()%32)-16);
        		p->vel[j] = (rand()%512)-256;
      		}
  	}
}

/*
===============
R_ParticleExplosion2

===============
*/

void R_ParticleExplosion2 (vec3_t org, int colorStart, int colorLength)
{
  	int     	i, j;
  	particle_t  	*p;
  	int     	colorMod = 0;

  	for (i=0; i<512; i++)
  	{
    		if (!free_particles)
      			return;

    		p = free_particles;
    		free_particles = p->next;
    		p->next = active_particles;
    		active_particles = p;

    		p->die = cl.time + 0.3;
    		p->color = colorStart + (colorMod % colorLength);
    		colorMod++;

    		p->type = pt_blob;

    		for (j=0 ; j<3 ; j++)
    		{
      			p->org[j] = org[j] + ((rand()%32)-16);
      			p->vel[j] = (rand()%512)-256;
    		}
  	}
}

/*
===============
R_BlobExplosion

===============
*/

void R_BlobExplosion (vec3_t org)
{
  	int     	i, j;
  	particle_t  	*p;

  	for (i=0 ; i<1024 ; i++)
  	{
    		if (!free_particles)
      			return;

    		p = free_particles;
    		free_particles = p->next;
    		p->next = active_particles;
    		active_particles = p;

    		p->die = cl.time + 1 + (rand()&8)*0.05;

		if (i & 1)
    		{
      			p->type = pt_blob;
      			p->color = 66 + rand()%6;
		}

		else
    		{
      			p->type = pt_blob2;
      			p->color = 150 + rand()%6;
		}

		for (j=0 ; j<3 ; j++)
      		{
        		p->org[j] = org[j] + ((rand()%32)-16);
        		p->vel[j] = (rand()%512)-256;
      		}
  	}
}

/*
===============
R_RunParticleEffect

===============
*/

void R_RunParticleEffect (vec3_t org, vec3_t dir, int color, int count)
{
  	int     	i, j;
  	particle_t  	*p;

  	for (i=0 ; i<count ; i++)
  	{
    		if (!free_particles)
      			return;

    		p = free_particles;
    		free_particles = p->next;
    		p->next = active_particles;
    		active_particles = p;

    		if (count == 1024)
    		{
			// rocket explosion
      			p->die = cl.time + 5;
      			p->color = ramp1[0];
      			p->ramp = rand()&3;

			if(i & 1)
				p->type = pt_explode;

			else
				p->type = pt_explode2;

			for (j=0 ; j<3 ; j++)
        		{
          			p->org[j] = org[j] + ((rand()%32)-16);
          			p->vel[j] = (rand()%512)-256;
        		}
			
    		}

    		else
    		{
      			p->die = cl.time + 0.1*(rand()%5);
      			p->color = (color&~7) + (rand()&7);
      			p->type = pt_slowgrav;

      			for (j=0 ; j<3 ; j++)
      			{
        			p->org[j] = org[j] + ((rand()&15)-8);
        			p->vel[j] = dir[j]*15; // + (rand()%300)-150;
      			}
    		}

  	}
}


/*
===============
R_LavaSplash

===============
*/

void R_LavaSplash (vec3_t org)
{
  	int     	i, j, k;
  	particle_t  	*p;
  	float   	vel;
  	vec3_t    	dir;

  	for (i=-16 ; i<16 ; i++)
    		for (j=-16 ; j<16 ; j++)
      			for (k=0 ; k<1 ; k++)
      			{
        			if (!free_particles)
          				return;

        			p = free_particles;
        			free_particles = p->next;
        			p->next = active_particles;
        			active_particles = p;
    
        			p->die = cl.time + 2 + (rand()&31) * 0.02;
        			p->color = 224 + (rand()&7);
        			p->type = pt_slowgrav;
        
        			dir[0] = j*8 + (rand()&7);
        			dir[1] = i*8 + (rand()&7);
        			dir[2] = 256;
  
        			p->org[0] = org[0] + dir[0];
        			p->org[1] = org[1] + dir[1];
        			p->org[2] = org[2] + (rand()&63);
  
        			VectorNormalize (dir);            
        			vel = 50 + (rand()&63);
        			VectorScale (dir, vel, p->vel);
      			}
}

/*
===============
R_TeleportSplash

===============
*/

void R_TeleportSplash (vec3_t org)
{
  	int     	i, j, k;
  	particle_t  	*p;
  	float   	vel;
  	vec3_t    	dir;

  	for (i=-16 ; i<16 ; i+=4)
    		for (j=-16 ; j<16 ; j+=4)
      			for (k=-24 ; k<32 ; k+=4)
      			{
        			if (!free_particles)
          				return;

        			p = free_particles;
        			free_particles = p->next;
        			p->next = active_particles;
        			active_particles = p;
    
        			p->die = cl.time + 0.2 + (rand()&7) * 0.02;
        			p->color = 7 + (rand()&7);
        			p->type = pt_slowgrav;
        
        			dir[0] = j*8;
        			dir[1] = i*8;
        			dir[2] = k*8;
  
        			p->org[0] = org[0] + i + (rand()&3);
        			p->org[1] = org[1] + j + (rand()&3);
        			p->org[2] = org[2] + k + (rand()&3);
  
        			VectorNormalize (dir);            
        			vel = 50 + (rand()&63);
        			VectorScale (dir, vel, p->vel);
      			}
}

void R_RocketTrail (vec3_t start, vec3_t end, int type)
{
  	vec3_t		vec;
  	float   	len;
  	int     	j;
  	particle_t  	*p;
  	int     	dec;
  	static int  	tracercount;

  	VectorSubtract (end, start, vec);
  	len = VectorNormalize (vec);

  	if (type < 128)
    		dec = 3;

  	else
  	{
    		dec = 1;
    		type -= 128;
  	}

  	while (len > 0)
  	{
    		len -= dec;

    		if (!free_particles)
      			return;

    		p = free_particles;
    		free_particles = p->next;
    		p->next = active_particles;
    		active_particles = p;
    
    		VectorCopy (vec3_origin, p->vel);
    		p->die = cl.time + 2;

    		switch (type)
    		{
      			case 0: // rocket trail
        			p->ramp = (rand()&3);
        			p->color = ramp3[(int)p->ramp];
        			p->type = pt_fire;
		
        			for (j=0 ; j<3 ; j++)
          				p->org[j] = start[j] + ((rand()%6)-3);

        			break;

      			case 1: // smoke smoke
        			p->ramp = (rand()&3) + 2;
        			p->color = ramp3[(int)p->ramp];
        			p->type = pt_fire;

        			for (j=0 ; j<3 ; j++)
          				p->org[j] = start[j] + ((rand()%6)-3);

        			break;

      			case 2: // blood
        			p->type = pt_grav;
        			p->color = 67 + (rand()&3);

        			for (j=0 ; j<3 ; j++)
          				p->org[j] = start[j] + ((rand()%6)-3);

        			break;

      			case 3:
      			case 5: // tracer
        			p->die = cl.time + 0.5;
        			p->type = pt_static;

        			if (type == 3)
          				p->color = 52 + ((tracercount&4)<<1);

        			else
          				p->color = 230 + ((tracercount&4)<<1);
      
        			tracercount++;

        			VectorCopy (start, p->org);

        			if (tracercount & 1)
        			{
          				p->vel[0] = 30*vec[1];
          				p->vel[1] = 30*-vec[0];
        			}

        			else
        			{
          				p->vel[0] = 30*-vec[1];
          				p->vel[1] = 30*vec[0];
        			}

        			break;

      			case 4: // slight blood
        			p->type = pt_grav;
        			p->color = 67 + (rand()&3);

        			for (j=0 ; j<3 ; j++)
          				p->org[j] = start[j] + ((rand()%6)-3);

        			len -= 3;

        			break;

      			case 6: // voor trail
        			p->color = 9*16 + 8 + (rand()&3);
        			p->type = pt_static;
        			p->die = cl.time + 0.3;

        			for (j=0 ; j<3 ; j++)
          				p->org[j] = start[j] + ((rand()&15)-8);

        			break;
    		}

    		VectorAdd (start, vec, start);
  	}
}

extern  cvar_t  sv_gravity;

//cvar_t  r_particle_hack = {"r_particle_hack","0"};//surgeon

/*
===============
R_DrawParticles
===============
*/

#if 0
void R_DrawParticles (void)
{
  	particle_t    	*p, *kill;
  	float     	grav;
  	int       	i;
  	float     	time2, time3;
  	float     	time1;
  	float     	dvel;
  	float     	frametime;
  	float 		minparticledist;

  
	#ifdef GLQUAKE

  	int 		pcount;
  	vec3_t		up, right;
  	float  		scale;
  	float 		ntscale;
  	unsigned char 	theAlpha; //surgeon
  	unsigned char 	*at; 	//surgeon

	//early out condition - Surgeon
	if(r_numparticles < 2)
		return;

	glDepthMask (GL_FALSE); //Surgeon
	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);


  	pcount = 0;

  	glPointSize (0.5f + (float)vid.width/320.f);

  	glBegin (GL_POINTS);

  	VectorScale (vup, 1.5, up);
  	VectorScale (vright, 1.5, right);

	#else 	//soft renderer
		//early out condition - Surgeon

	if(r_numparticles < 2)
		return;

  	D_StartParticles ();

  	VectorScale (vright, xscaleshrink, r_pright);
  	VectorScale (vup, yscaleshrink, r_pup);
  	VectorCopy (vpn, r_ppn);

	#endif


  	frametime = cl.time - cl.oldtime;
  	time3 = frametime * 15;
  	time2 = frametime * 10; // 15;
  	time1 = frametime * 5;
  	grav = frametime * sv_gravity.value * 0.05;
  	dvel = 4*frametime;
  
  	for ( ;; ) 
  	{
    		kill = active_particles;

    		if (kill && kill->die < cl.time)
    		{
      			active_particles = kill->next;
      			kill->next = free_particles;
      			free_particles = kill;
      			continue;
    		}

    		break;
  	}

	//LordHavoc:
  	minparticledist = DotProduct(r_origin, vpn) + 16.0f; 

  	for (p=active_particles ; p ; p=p->next)
  	{
    		for ( ;; )
    		{
      			kill = p->next;

      			if (kill && kill->die < cl.time)
      			{
        			p->next = kill->next;
        			kill->next = free_particles;
        			free_particles = kill;
        			continue;
      			}

      			break;
    		}

		#ifdef GLQUAKE

    		// LordHavoc: only render if not too close 
    		if (DotProduct(p->org, vpn) < minparticledist)
			continue; 

    		at = (byte *)&d_8to24table[(int)p->color];

		#if 1
    		if (p->type==pt_fire)
    		{
      			theAlpha = 255*(6-p->ramp)/6;
      			glColor4ub (at[0], at[1], at[2], theAlpha);
    		}

    		else
    		{
      			glColor3ubv (at);
    		}

		#else

      		theAlpha = (p->type == pt_fire) ? 255*(6 - p->ramp)/6 : 255;

		glColor4ub (at[0], at[1], at[2], theAlpha);

		#endif

    		glVertex3f (p->org[0], p->org[1], p->org[2]);
    		pcount++;

		if(pcount == 256) //warp
		{
			glEnd();
			pcount = 0;
			glBegin (GL_POINTS);
		}

		#else

    		D_DrawParticle (p);

		#endif // GLQUAKE

    		p->org[0] += p->vel[0]*frametime;
    		p->org[1] += p->vel[1]*frametime;
    		p->org[2] += p->vel[2]*frametime;
    
    		switch (p->type)
    		{

    			case pt_static:
      				break;

    			case pt_fire:
      				p->ramp += time1;

      				if (p->ramp >= 6)
        				p->die = -1;

      				else
        				p->color = ramp3[(int)p->ramp];

      				p->vel[2] += grav;

      				break;

    			case pt_explode:
      				p->ramp += time2;

      				if (p->ramp >=8)
        				p->die = -1;

      				else
        				p->color = ramp1[(int)p->ramp];

      				for (i=0 ; i<3 ; i++)
        				p->vel[i] += p->vel[i]*dvel;

      				p->vel[2] -= grav;

      				break;

    			case pt_explode2:
      				p->ramp += time3;

      				if (p->ramp >=8)
        				p->die = -1;

      				else
        				p->color = ramp2[(int)p->ramp];

      				for (i=0 ; i<3 ; i++)
        				p->vel[i] -= p->vel[i]*frametime;

      				p->vel[2] -= grav;

      				break;

    			case pt_blob:
      				for (i=0 ; i<3 ; i++)
        				p->vel[i] += p->vel[i]*dvel;

      				p->vel[2] -= grav;

      				break;

    			case pt_blob2:
      				for (i=0 ; i<2 ; i++)
        				p->vel[i] -= p->vel[i]*dvel;

      				p->vel[2] -= grav;

      				break;

    			case pt_grav:
			#ifdef QUAKE2
      				p->vel[2] -= grav * 20;

      				break;
			#endif
    			case pt_slowgrav:
      				p->vel[2] -= grav;

      				break;
    		}

  	}

#ifdef GLQUAKE
	if(pcount > 0)
  		glEnd ();
#endif


#ifdef GLQUAKE
  	glDisable (GL_BLEND);
  	glEnable(GL_TEXTURE_2D);
  	glDepthMask (GL_TRUE); //Surgeon
  	glColor4f(1,1,1,1);
#else
  	D_EndParticles ();
#endif
}

#endif


#if 1 // classic particles - Cowcat

void CL_RunParticles(void) // called from host.c
{
	particle_t    	*p, *kill;
  	float     	grav;
  	int       	i;
  	float     	time2, time3;
  	float     	time1;
  	float     	dvel;
  	float     	frametime;

	if(!active_particles)
		return; 	// joeQuake 

	frametime = cl.time - cl.oldtime;
  	time3 = frametime * 15;
  	time2 = frametime * 10; // 15;
  	time1 = frametime * 5;
  	grav = frametime * sv_gravity.value * 0.05;
  	dvel = 4*frametime;
  
  	for ( ;; ) 
  	{
    		kill = active_particles;

    		if (kill && kill->die < cl.time)
    		{
      			active_particles = kill->next;
      			kill->next = free_particles;
      			free_particles = kill;
      			continue;
    		}

    		break;
  	}

  	for (p=active_particles ; p ; p=p->next)
  	{
    		for ( ;; )
    		{
      			kill = p->next;

      			if (kill && kill->die < cl.time)
      			{
        			p->next = kill->next;
        			kill->next = free_particles;
        			free_particles = kill;
        			continue;
      			}

      			break;
    		}

		p->org[0] += p->vel[0]*frametime;
    		p->org[1] += p->vel[1]*frametime;
    		p->org[2] += p->vel[2]*frametime;
    
    		switch (p->type)
    		{
    			case pt_static:
      				break;

    			case pt_fire:
      				p->ramp += time1;

      				if (p->ramp >= 6)
        				p->die = -1;

      				else
        				p->color = ramp3[(int)p->ramp];

      				p->vel[2] += grav;

      				break;

    			case pt_explode:
      				p->ramp += time2;

      				if (p->ramp >=8)
        				p->die = -1;

      				else
        				p->color = ramp1[(int)p->ramp];

      				for (i=0 ; i<3 ; i++)
        				p->vel[i] += p->vel[i]*dvel;

      				p->vel[2] -= grav;

      				break;

    			case pt_explode2:
      				p->ramp += time3;

      				if (p->ramp >=8)
        				p->die = -1;

      				else
        				p->color = ramp2[(int)p->ramp];

      				for (i=0 ; i<3 ; i++)
        				p->vel[i] -= p->vel[i]*frametime;

      				p->vel[2] -= grav;

      				break;

    			case pt_blob:
      				for (i=0 ; i<3 ; i++)
        				p->vel[i] += p->vel[i]*dvel;

      				p->vel[2] -= grav;

      				break;

    			case pt_blob2:
      				for (i=0 ; i<2 ; i++)
        				p->vel[i] -= p->vel[i]*dvel;

      				p->vel[2] -= grav;

      				break;

    			case pt_grav:
			#ifdef QUAKE2
      				p->vel[2] -= grav * 20;

      				break;
			#endif
    			case pt_slowgrav:
      				p->vel[2] -= grav;

      				break;
    		}

  	}
}

#if 1

void R_DrawParticles (void)
{
	particle_t	*p;
  	int		pcount;
  	vec3_t		up, right;
  	unsigned char 	*at, theAlpha;
	float		minparticledist;

	if(r_numparticles < 2)
		return;
	
	VectorScale (vup, 1.5, up);
  	VectorScale (vright, 1.5, right);

	glDepthMask (GL_FALSE); //Surgeon
	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);

  	pcount = 0;

  	glPointSize (0.5f + (float)vid.width/320.f);

	/* glBegin only once there is a point to draw (2026-09-13). This used to
	 * open GL_POINTS every frame and close it only if a point was drawn, so a
	 * frame with no particle in front of the view -- the menu over a demo,
	 * the console, a quiet area -- left the driver inside glBegin. Old
	 * MiniGLs never checked; MiniGLV3D now refuses matrix calls there, as GL
	 * does, which dropped GL_Set2D's glOrtho and drew the console, menu and
	 * HUD through the 3D view. Every glBegin below now has its glEnd. */

	minparticledist = DotProduct(r_origin, vpn) + 16.0f; 

	for (p=active_particles ; p ; p=p->next)
  	{		
		// LordHavoc: only render if not too close 
    		if (DotProduct(p->org, vpn) < minparticledist)
			continue; 

		if (pcount == 0)
			glBegin (GL_POINTS);

    		at = (byte *)&d_8to24table[(int)p->color];

		#if 0

    		if (p->type==pt_fire)
    		{
      			theAlpha = 255*(6-p->ramp)/6;
      			//glColor4ub (at[0], at[1], at[2], theAlpha);
			glColor4ub (*at, *(at+1), *(at+2), theAlpha);
    		}

    		else
    		{
      			glColor3ubv (at);
    		}

		#else

      		theAlpha = (p->type == pt_fire) ? 255*(6 - p->ramp)/6 : 255;

		glColor4ub (at[0], at[1], at[2], theAlpha);

		#endif

    		glVertex3f (p->org[0], p->org[1], p->org[2]);

    		pcount++;

		if(pcount == 256) //warp
		{
			glEnd();
			pcount = 0;
		}
		
	}

	if(pcount > 0)
		glEnd();

	glDisable(GL_BLEND);
	glEnable(GL_TEXTURE_2D);
	glDepthMask (GL_TRUE); //Surgeon
	glColor4f(1,1,1,1);
}
#endif

#if 0

#define glTexVert3f(s,t,x,y,z) {\
if(CC->ShadeModel == GL_SMOOTH)\
{\
mglvert->v.color.r = mini_CurrentContext->CurrentColor.r;\
mglvert->v.color.g = mini_CurrentContext->CurrentColor.g;\
mglvert->v.color.b = mini_CurrentContext->CurrentColor.b;\
mglvert->v.color.a = mini_CurrentContext->CurrentColor.a;\
}\
mglvert->bx=(GLfloat)(x);\
mglvert->by=(GLfloat)(y);\
mglvert->bz=(GLfloat)(z);\
mglvert->v.u=(W3D_Float)(s);\
mglvert->v.v=(W3D_Float)(t);\
mini_CurrentContext->VertexBufferPointer++;\
mglvert++;\
}\

#endif

void R_DrawParticles_Classic (void)
{	
	particle_t	*p;
  	vec3_t		up, right;
  	float  		scale, dist, r_partscale;
  	unsigned char 	*at, theAlpha;

	
	if(!active_particles) 
		return;

	r_partscale = 0.004 * tan(r_refdef.fov_x * (M_PI / 180) * 0.5f);

	VectorScale (vup, 1.5, up);
  	VectorScale (vright, 1.5, right);

	GL_Bind(particletexture);

	glEnable(GL_BLEND);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glDepthMask(GL_FALSE);

	glBegin(GL_TRIANGLES);

	for (p=active_particles ; p ; p=p->next)
  	{

		#if 1

		dist = (p->org[0] - r_origin[0]) * vpn[0] + (p->org[1] - r_origin[1]) * vpn[1] 
			+ (p->org[2] - r_origin[2]) * vpn[2];

		scale = 1 + dist * r_partscale;		
		
		#else

		scale = (p->org[0] - r_origin[0]) * vpn[0]
			+ (p->org[1] - r_origin[1]) * vpn[1]
			+ (p->org[2] - r_origin[2]) * vpn[2];

		if (scale < 20)
			scale = 1;

		else
			scale = 1 + scale * 0.004;

		#endif

    		at = (byte *)&d_8to24table[(int)p->color];

		#if 0

    		if (p->type==pt_fire)
    		{
      			theAlpha = 255*(6-p->ramp)/6;
      			glColor4ub (at[0], at[1], at[2], theAlpha);
    		}

    		else
    		{
      			glColor3ubv (at);
    		}

		#else

      		theAlpha = (p->type == pt_fire) ? 255*(6 - p->ramp)/6 : 255;

		glColor4ub (at[0], at[1], at[2], theAlpha);

		#endif

		glTexCoord2f (0,0);
		glVertex3fv (p->org);

		glTexCoord2f (1,0);
		glVertex3f (p->org[0] + up[0]*scale, p->org[1] + up[1]*scale, p->org[2] + up[2]*scale);

		glTexCoord2f (0,1);
		glVertex3f (p->org[0] + right[0]*scale, p->org[1] + right[1]*scale, p->org[2] + right[2]*scale);

	}

	glEnd();

	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glColor4f(1,1,1,1); // add
}

#endif

#if 0 // test

void R_DrawParticles_Classic (void)
{
  	particle_t    	*p, *kill;
  	int       	i;
  	float     	grav, time1, time2, time3, dvel, frametime;
  	 
  	vec3_t		up, right;
  	float  		scale, dist, r_partscale;
  	unsigned char 	*at, theAlpha;

	
	if(!active_particles) 
		return;

	//int num_triangles = 0;

	//for(p=active_particles; p ; p=p->next);
		//num_triangles += 1;

	//r_partscale = 0.004 * tan(r_refdef.fov_x *(M_PI / 180) * 0.5f);

	glDepthMask (GL_FALSE);
	glEnable(GL_BLEND);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);


	GL_Bind(particletexture);

	glBegin(GL_TRIANGLES);

	//glEnableClientState(GL_COLOR_ARRAY);
	//glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	//glEnableClientState(GL_VERTEX_ARRAY);

  	VectorScale (vup, 1.5, up);
  	VectorScale (vright, 1.5, right);


  	frametime = cl.time - cl.oldtime;
  	time3 = frametime * 15;
  	time2 = frametime * 10; // 15;
  	time1 = frametime * 5;
  	grav = frametime * sv_gravity.value * 0.05;
  	dvel = 4*frametime;
  
	#if 0

	//GLfloat texCoords[2*3];
	//GLfloat verts[3*3];

	//GLubyte colors[4*3];
	//glColorPointer(4, GL_UNSIGNED_BYTE, 0, colors);
	//GLubyte *pcolors = colors;

	GLfloat texCoords[2*3] =
	{
		0, 0, 1, 0, 0, 1
	};

	glTexCoordPointer(2, GL_FLOAT, sizeof(float)*2, texCoords);
	//glVertexPointer(3, GL_FLOAT, sizeof(float)*3, verts);

	#endif

  	for ( ;; ) 
  	{
    		kill = active_particles;

    		if (kill && kill->die < cl.time)
    		{
      			active_particles = kill->next;
      			kill->next = free_particles;
      			free_particles = kill;
      			continue;
    		}

    		break;
  	}

	//int u = 0;

  	for (p=active_particles ; p ; p=p->next)
  	{
    		for ( ;; )
    		{
      			kill = p->next;

      			if (kill && kill->die < cl.time)
      			{
        			p->next = kill->next;
        			kill->next = free_particles;
        			free_particles = kill;
        			continue;
      			}

      			break;
    		}

		
		#if 0
		dist = (p->org[0] - r_origin[0]) * vpn[0] + (p->org[1] - r_origin[1]) * vpn[1] 
			+ (p->org[2] - r_origin[2]) * vpn[2];
		
		scale = 1 + dist * r_partscale;

		#else

		scale = (p->org[0] - r_origin[0]) * vpn[0]
			+ (p->org[1] - r_origin[1]) * vpn[1]
			+ (p->org[2] - r_origin[2]) * vpn[2];
		
		

		if (scale < 20)
			scale = 1;

		else
			scale = 1 + scale * 0.004;

		#endif

    		at = (byte *)&d_8to24table[(int)p->color];
      		theAlpha = (p->type == pt_fire) ? 255*(6 - p->ramp)/6 : 255;
    		
		#if 1

		glColor4ub (at[0], at[1], at[2], theAlpha);		
    		
		glTexCoord2f (0,0);
		glVertex3fv (p->org);

		glTexCoord2f (1,0);
		glVertex3f (p->org[0] + up[0]*scale, p->org[1] + up[1]*scale, p->org[2] + up[2]*scale);

		glTexCoord2f (0,1);
		glVertex3f (p->org[0] + right[0]*scale, p->org[1] + right[1]*scale, p->org[2] + right[2]*scale);
		
		#else

		//u = (num_triangles - num_triangles) + u;
		
		/*
		colors[u] = at[0]; colors[u+1] = at[1]; colors[u+2] = at[2];  colors[u+3]  = theAlpha;
		colors[u+4] = at[0]; colors[u+5] = at[1]; colors[u+6] = at[2];  colors[u+7]  = theAlpha;
		colors[u+8] = at[0]; colors[u+9] = at[1]; colors[u+10] =at[2];  colors[u+11] = theAlpha;
		// *pcolors++ = at[0]; *pcolors++ = at[1];

		//texCoords[u] = 0;
		//texCoords[u+1] = 0;
		//texCoords[u+2] = 1;
		//texCoords[u+3] = 0;
		//texCoords[u+4] = 0;
		//texCoords[u+5] = 1; 

		verts[u] = p->org[0]; verts[u+1] = p->org[1]; verts[u+2] = p->org[2];
		verts[u+3] = p->org[0] + up[0]*scale; verts[u+4] = p->org[1] + up[1]*scale; verts[u+5] = p->org[2] + up[2]*scale;
		verts[u+6] = p->org[0] + right[0]*scale; verts[u+7] = p->org[1] + right[1]*scale; verts[u+8] = p->org[2] + right[2]*scale;
		*/

		GLubyte colors[4*3] =
		{
			at[0], at[1], at[2], theAlpha,
			at[0], at[1], at[2], theAlpha,
			at[0], at[1], at[2], theAlpha
		};

		GLfloat verts[3*3] =
		{
			p->org[0], p->org[1], p->org[2],
			p->org[0] + up[0]*scale,  p->org[1] + up[1] * scale, p->org[2] + up[2]*scale,
			p->org[0] + right[0]*scale, p->org[1] + right[1] * scale, p->org[2] + right[2]*scale
		};


		glColorPointer(4, GL_UNSIGNED_BYTE, 0, colors);
		//glTexCoordPointer(2, GL_FLOAT, sizeof(float)*2, texCoords);
		glVertexPointer(3, GL_FLOAT, sizeof(float)*3, verts);

		glDrawArrays(GL_TRIANGLES, 0, 3);

		
		#endif

    		p->org[0] += p->vel[0]*frametime;
    		p->org[1] += p->vel[1]*frametime;
    		p->org[2] += p->vel[2]*frametime;
    
    		switch (p->type)
    		{

    			case pt_static:
      				break;

    			case pt_fire:
      				p->ramp += time1;

      				if (p->ramp >= 6)
        				p->die = -1;

      				else
        				p->color = ramp3[(int)p->ramp];

      				p->vel[2] += grav;

      				break;

    			case pt_explode:
      				p->ramp += time2;

      				if (p->ramp >=8)
        				p->die = -1;

      				else
        				p->color = ramp1[(int)p->ramp];

      				for (i=0 ; i<3 ; i++)
        				p->vel[i] += p->vel[i]*dvel;

      				p->vel[2] -= grav;

      				break;

    			case pt_explode2:
      				p->ramp += time3;

      				if (p->ramp >=8)
        				p->die = -1;

      				else
        				p->color = ramp2[(int)p->ramp];

      				for (i=0 ; i<3 ; i++)
        				p->vel[i] -= p->vel[i]*frametime;

      				p->vel[2] -= grav;

      				break;

    			case pt_blob:
      				for (i=0 ; i<3 ; i++)
        				p->vel[i] += p->vel[i]*dvel;

      				p->vel[2] -= grav;

      				break;

    			case pt_blob2:
      				for (i=0 ; i<2 ; i++)
        				p->vel[i] -= p->vel[i]*dvel;

      				p->vel[2] -= grav;

      				break;

    			case pt_grav:
			#ifdef QUAKE2
      				p->vel[2] -= grav * 20;

      				break;
			#endif
    			case pt_slowgrav:
      				p->vel[2] -= grav;

      				break;
    		}

  	}

	//glColorPointer(4, GL_UNSIGNED_BYTE, 0, colors);
		//glTexCoordPointer(2, GL_FLOAT, sizeof(float)*2, texCoords);
		//glVertexPointer(3, GL_FLOAT, sizeof(float)*3, verts);

		//glDrawArrays(GL_TRIANGLES, 0, 3 * num_triangles);

	//glDisableClientState(GL_COLOR_ARRAY);
	//glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	//glDisableClientState(GL_VERTEX_ARRAY);

  	glEnd ();

  	glDisable (GL_BLEND);
  	glDepthMask (GL_TRUE);
  	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE); // texenvf
	glColor4f(1,1,1,1);

}
#endif













