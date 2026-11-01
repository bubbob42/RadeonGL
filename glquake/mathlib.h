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
// mathlib.h

typedef float vec_t;
typedef vec_t vec3_t[3];
typedef vec_t vec5_t[5];

typedef int fixed4_t;
typedef int fixed8_t;
typedef int fixed16_t;

#ifndef M_PI
#define M_PI    3.14159265358979323846  // matches value in gcc v2 math.h
#endif

#define M_PI_DIV_180 (M_PI / 180.0) //johnfitz

struct mplane_s;

extern vec3_t vec3_origin;
extern int nanmask;

#define IS_NAN(x) (((*(int *)&x)&nanmask)==nanmask)

#define CLAMP(min, x, max) ((x) < (min) ? (min) : (x) > (max) ? (max) : (x)) //johnfitz

#define Q_rint(x) ((x) > 0 ? (int)((x) + 0.5) : (int)((x) - 0.5)) //johnfitz -- from joequake


#define DotProduct(x,y) (x[0]*y[0]+x[1]*y[1]+x[2]*y[2])
#define VectorSubtract(a,b,c) {c[0]=a[0]-b[0];c[1]=a[1]-b[1];c[2]=a[2]-b[2];}
#define VectorAdd(a,b,c) {c[0]=a[0]+b[0];c[1]=a[1]+b[1];c[2]=a[2]+b[2];}
#define VectorCopy(a,b) {b[0]=a[0];b[1]=a[1];b[2]=a[2];}

#define VectorScaleQuick(in, scale, out) {(out)[0] = (in)[0] * (scale);(out)[1] = (in)[1] * (scale);(out)[2] = (in)[2] * (scale);} 

#define VectorMAQuick(a, scale, b, c) {(c)[0] = (a)[0] + (scale) * (b)[0];(c)[1] = (a)[1] + (scale) * (b)[1];(c)[2] = (a)[2] + (scale) * (b)[2];} 

void VectorMA (vec3_t veca, float scale, vec3_t vecb, vec3_t vecc);

vec_t _DotProduct (vec3_t v1, vec3_t v2);
void _VectorSubtract (vec3_t veca, vec3_t vecb, vec3_t out);
void _VectorAdd (vec3_t veca, vec3_t vecb, vec3_t out);
void _VectorCopy (vec3_t in, vec3_t out);

int VectorCompare (vec3_t v1, vec3_t v2);
vec_t Length (vec3_t v);
void CrossProduct (vec3_t v1, vec3_t v2, vec3_t cross);
/*
#define CrossProduct(v1, v2,cross){cross[0] = v1[1]*v2[2] - v1[2]*v2[1];cross[1] = v1[2]*v2[0] - v1[0]*v2[2];cross[2] = v1[0]*v2[1] - v1[1]*v2[0];}
*/
float VectorNormalize (vec3_t v);   // returns vector length
void VectorInverse (vec3_t v);
void VectorScale (vec3_t in, vec_t scale, vec3_t out);
int Q_log2(int val);

void R_ConcatRotations (float in1[3][3], float in2[3][3], float out[3][3]);
void R_ConcatTransforms (float in1[3][4], float in2[3][4], float out[3][4]);

void FloorDivMod (double numer, double denom, int *quotient, int *rem);
fixed16_t Invert24To16(fixed16_t val);
int GreatestCommonDivisor (int i1, int i2);

void AngleVectors (vec3_t angles, vec3_t forward, vec3_t right, vec3_t up);

#ifndef FASTBOPS
int BoxOnPlaneSide (vec3_t emins, vec3_t emaxs, struct mplane_s *plane);
#endif

float anglemod(float a);

// LordHavoc: like AngleVectors, but taking a forward vector instead of angles, useful! 
void VectorVectors(const vec3_t forward, vec3_t right, vec3_t up); 

#ifdef FASTBOPS
void BoxOnPlaneSideClassify(struct mplane_s *p);

#define BOX_ON_PLANE_SIDE(emins, emaxs, p)	\
(((p)->type < 3)?		\
(				\
	((p)->dist <= (emins)[(p)->type])?\
		1		\
	:			\
	(			\
		((p)->dist >= (emaxs)[(p)->type])?\
			2	\
		:		\
			3	\
		)		\
	)			\
	:			\
	(p)->BoxOnPlaneSideFunc( (emins), (emaxs), (p)))


// LordHavoc: was a major time waster 
#define R_CullBox(mins,maxs) (frustum[0].BoxOnPlaneSideFunc(mins, maxs, &frustum[0]) == 2 || frustum[1].BoxOnPlaneSideFunc(mins, maxs, &frustum[1]) == 2 || frustum[2].BoxOnPlaneSideFunc(mins, maxs, &frustum[2]) == 2 || frustum[3].BoxOnPlaneSideFunc(mins, maxs, &frustum[3]) == 2) 

#else

#define BOX_ON_PLANE_SIDE(emins, emaxs, p)  \
  (((p)->type < 3)?           \
  (                   \
    ((p)->dist <= (emins)[(p)->type])?  \
      1               \
    :                 \
    (                 \
      ((p)->dist >= (emaxs)[(p)->type])?\
        2             \
      :               \
        3             \
    )                 \
  )                   \
  :                   \
    BoxOnPlaneSide( (emins), (emaxs), (p)))
#endif
