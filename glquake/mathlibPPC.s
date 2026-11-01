#
# Quake for AMIGA
#
# mathlibPPC.s
#
# Define WOS for PowerOpen ABI, otherwise SVR4-ABI is used.
#

.include        "macrosPPC.i"
.include	"quakedefPPC.i"

#
# external references
#

	xrefv	c0
	xrefv	c0_5
	xrefv	INT2DBL_0
	
.ifdef	__STORM__
	xrefv	sin__r
	xrefv	cos__r
.else
	xrefv	sin
	xrefv	cos
.endif

#
# defines
#

.set PITCH,0
.set YAW,1
.set ROLL,2





###########################################################################
#
#       float anglemod(float a)
#
###########################################################################

	funcdef	anglemod

# a = (360.0/65536) * ((int)(a*(65536/360.0)) & 65535);

	stwu	r1,-32(r1)
	lf	f2,c64kDIV360	# must be a double precision constant!
	fmul	f1,f1,f2
	fctiwz	f0,f1
	stfd	f0,24(r1)
	lhz	r3,30(r1)
	lis	r11,0x43300000@h
	stw	r11,24(r1)
	xoris	r11,r3,0x80000000@h
	stw	r11,28(r1)
	lfd	f1,24(r1)
	lf	f2,INT2DBL_0
	fsub	f1,f1,f2
	ls	f2,c360DIV64k
	fmul	f1,f1,f2
	addi	r1,r1,32
	blr

	funcend	anglemod

##############################################

# Not used - Cowcat

###########################################################################
#
#       int BoxOnPlaneSide (vec3_t emins, vec3_t emaxs, mplane_t *p)
#
###########################################################################
#
#	funcdef	BoxOnPlaneSide
#
#	lbz     r0,MPLANE_SIGNBITS(r5)
#	lfs     f1,0(r5)                #f1 = p->normal[0]
#	lfs     f2,4(r5)                #f2 = p->normal[1]
#	cmplwi  r0,3
#	lfs     f3,8(r5)                #f3 = p->normal[2]
#	lfs     f4,0(r3)                #f4 = emins[0]
#	cmplwi  cr1,r0,5
#	lfs     f5,0(r4)                #f5 = emaxs[0]
#	lfs     f6,4(r3)                #f6 = emins[1]
#	cmplwi  cr2,r0,7
#	lfs     f7,4(r4)                #f7 = emaxs[1]
#	lfs     f8,8(r3)                #f8 = emins[2]
#	cmplwi  cr3,r0,1
#	lfs     f9,8(r4)                #f9 = emaxs[2]
#	ble     .lower
#.upper:
#	blt     cr1,.c4
#	beq     cr1,.c5
#	blt     cr2,.c6
#	beq     cr2,.c7
#	trap
#.lower:
#	beq     .c3
#	bgt     cr3,.c2
#	beq     cr3,.c1
#.c0:
#	fmuls   f10,f5,f1               #f10 = p->normal[0]*emaxs[0]
#	fmuls   f11,f4,f1               #f11 = p->normal[0]*emins[0]
#	fmadd   f10,f2,f7,f10           #f10+= p->normal[1]*emaxs[1]
#	fmadd   f11,f2,f6,f11           #f11+= p->normal[1]*emins[1]
#	fmadd   f10,f3,f9,f10           #f10+= p->normal[2]*emaxs[2]
#	fmadd   f11,f3,f8,f11           #f11+= p->normal[2]*emins[2]
#	b       .cont
#.c1:
#	fmuls   f10,f4,f1               #f10 = p->normal[0]*emins[0]
#	fmuls   f11,f5,f1               #f11 = p->normal[0]*emaxs[0]
#	fmadd   f10,f2,f7,f10           #f10+= p->normal[1]*emaxs[1]
#	fmadd   f11,f2,f6,f11           #f11+= p->normal[1]*emins[1]
#	fmadd   f10,f3,f9,f10           #f10+= p->normal[2]*emaxs[2]
#	fmadd   f11,f3,f8,f11           #f11+= p->normal[2]*emins[2]
#	b       .cont
#.c2:
#	fmuls   f10,f5,f1               #f10 = p->normal[0]*emaxs[0]
#	fmuls   f11,f4,f1               #f11 = p->normal[0]*emins[0]
#	fmadd   f10,f2,f6,f10           #f10+= p->normal[1]*emins[1]
#	fmadd   f11,f2,f7,f11           #f11+= p->normal[1]*emaxs[1]
#	fmadd   f10,f3,f9,f10           #f10+= p->normal[2]*emaxs[2]
#	fmadd   f11,f3,f8,f11           #f11+= p->normal[2]*emins[2]
#	b       .cont
#.c3:
#	fmuls   f10,f4,f1               #f10 = p->normal[0]*emins[0]
#	fmuls   f11,f5,f1               #f11 = p->normal[0]*emaxs[0]
#	fmadd   f10,f2,f6,f10           #f10+= p->normal[1]*emins[1]
#	fmadd   f11,f2,f7,f11           #f11+= p->normal[1]*emaxs[1]
#	fmadd   f10,f3,f9,f10           #f10+= p->normal[2]*emaxs[2]
#	fmadd   f11,f3,f8,f11           #f11+= p->normal[2]*emins[2]
#	b       .cont
#.c4:
#	fmuls   f10,f5,f1               #f10 = p->normal[0]*emaxs[0]
#	fmuls   f11,f4,f1               #f11 = p->normal[0]*emins[0]
#	fmadd   f10,f2,f7,f10           #f10+= p->normal[1]*emaxs[1]
#	fmadd   f11,f2,f6,f11           #f11+= p->normal[1]*emins[1]
#	fmadd   f10,f3,f8,f10           #f10+= p->normal[2]*emins[2]
#	fmadd   f11,f3,f9,f11           #f11+= p->normal[2]*emaxs[2]
#	b       .cont
#.c5:
#	fmuls   f10,f4,f1               #f10 = p->normal[0]*emins[0]
#	fmuls   f11,f5,f1               #f11 = p->normal[0]*emaxs[0]
#	fmadd   f10,f2,f7,f10           #f10+= p->normal[1]*emaxs[1]
#	fmadd   f11,f2,f6,f11           #f11+= p->normal[1]*emins[1]
#	fmadd   f10,f3,f8,f10           #f10+= p->normal[2]*emins[2]
#	fmadd   f11,f3,f9,f11           #f11+= p->normal[2]*emaxs[2]
#	b       .cont
#.c6:
#	fmuls   f10,f5,f1               #f10 = p->normal[0]*emaxs[0]
#	fmuls   f11,f4,f1               #f11 = p->normal[0]*emins[0]
#	fmadd   f10,f2,f6,f10           #f10+= p->normal[1]*eminx[1]
#	fmadd   f11,f2,f7,f11           #f11+= p->normal[1]*emaxs[1]
#	fmadd   f10,f3,f8,f10           #f10+= p->normal[2]*emins[2]
#	fmadd   f11,f3,f9,f11           #f11+= p->normal[2]*emaxs[2]
#	b       .cont
#.c7:
#	fmuls   f10,f4,f1               #f10 = p->normal[0]*emins[0]
#	fmuls   f11,f5,f1               #f11 = p->normal[0]*emaxs[0]
#	fmadd   f10,f2,f6,f10           #f10+= p->normal[1]*eminx[1]
#	fmadd   f11,f2,f7,f11           #f11+= p->normal[1]*emaxs[1]
#	fmadd   f10,f3,f8,f10           #f10+= p->normal[2]*emins[2]
#	fmadd   f11,f3,f9,f11           #f11+= p->normal[2]*emaxs[2]
#.cont:
#	lfs     f0,MPLANE_DIST(r5)
#	fcmpo   cr0,f10,f0
#	blt     .cont2
#	li      r3,1
#.cont2:
#	fcmpo   cr0,f11,f0
#	bge     .cont3
#	ori     r3,r3,2
#.cont3:
#	blr
#
#	funcend	BoxOnPlaneSide
#



###########################################################################
#
#       void AngleVectors (vec3_t angles, vec3_t forward, vec3_t right,
#                           vec3_t up)
#
###########################################################################

	funcdef	AngleVectors

	init	0,128,4,6
	stmw	r28,gb(r1)
	stfd	f14,fb+0*8(r1)
	stfd	f15,fb+1*8(r1)
	stfd	f16,fb+2*8(r1)
	stfd	f17,fb+3*8(r1)
	stfd	f18,fb+4*8(r1)
	stfd	f19,fb+5*8(r1)

	mr      r31,r3
	mr      r30,r4
	mr      r29,r5
	mr      r28,r6
	ls	f19,c1DEGREE
	lfs     f14,YAW*4(r31)
	fmuls   f14,f14,f19
	fmr     f1,f14
	mcall	sin
	fmr     f15,f1                  #f15 = sy
	fmr     f1,f14
	mcall	cos
	fmr     f16,f1                  #f16 = cy
	lfs     f14,PITCH*4(r31)
	fmuls   f14,f14,f19
	fmr     f1,f14
	mcall	sin
	fmr     f17,f1                  #f17 = sp
	fmr     f1,f14
	mcall	cos
	fmr     f18,f1                  #f18 = cp
	lfs     f14,ROLL*4(r31)
	fmuls   f14,f14,f19
	fmr     f1,f14
	mcall	sin
	fmr     f19,f1                  #f19 = sr
	fmr     f1,f14
	mcall	cos
	fmr     f14,f1                  #f14 = cr
	fmuls   f0,f18,f16
	stfs    f0,0(r30)
	fmuls   f1,f18,f15
	stfs    f1,4(r30)
	fneg    f2,f17                  #f2 = -sp
	stfs    f2,8(r30)
	fmuls   f3,f19,f16              #f3 = sr * cy
	fmuls   f4,f19,f15              #f4 = sr * sy
	fmuls   f5,f14,f15              #f5 = cr * sy
	fmuls   f6,f14,f16              #f6 = cr * cy
	fmadds  f7,f3,f2,f5
	stfs    f7,0(r29)
	fmsubs  f8,f4,f2,f6
	stfs    f8,4(r29)
	fmuls   f9,f18,f19
	fneg    f9,f9
	stfs    f9,8(r29)
	fmadds  f7,f6,f17,f4
	stfs    f7,0(r28)
	fmsubs  f8,f5,f17,f3
	stfs    f8,4(r28)
	fmuls   f9,f14,f18
	stfs    f9,8(r28)

	lfd	f14,fb+0*8(r1)
	lfd	f15,fb+1*8(r1)
	lfd	f16,fb+2*8(r1)
	lfd	f17,fb+3*8(r1)
	lfd	f18,fb+4*8(r1)
	lfd	f19,fb+5*8(r1)
	lmw	r28,gb(r1)
	exit

	funcend	AngleVectors




###########################################################################
#
#       void VectorMA (vec3_t veca, float scale, vec3_t vecb, vec3_t vecc)
#
###########################################################################

	funcdef	VectorMA

	lfs     f3,0(r3)
	lfs     f2,0(r4)
	fmadds  f0,f1,f2,f3
	stfs    f0,0(r5)
	lfs     f3,4(r3)
	lfs     f2,4(r4)
	fmadds  f0,f1,f2,f3
	stfs    f0,4(r5)
	lfs     f3,8(r3)
	lfs     f2,8(r4)
	fmadds  f0,f1,f2,f3
	stfs    f0,8(r5)
	blr

	funcend	VectorMA




###########################################################################
#
#       vec_t _DotProduct (vec3_t v1, vec3_t v2)
#
###########################################################################

	funcdef	DotProduct

	lfs     f1,0(r3)
	lfs     f2,0(r4)
	fmuls   f3,f1,f2
	lfs     f4,4(r3)
	lfs     f5,4(r4)
	fmadd   f6,f4,f5,f3
	lfs     f7,8(r3)
	lfs     f8,8(r4)
	fmadd   f1,f7,f8,f6
	blr

	funcend	DotProduct




###########################################################################
#
#      void VectorSubtract (vec3_t veca, vec3_t vecb, vec3_t out)
#
###########################################################################

	funcdef	VectorSubtract

	lfs     f1,0(r3)
	lfs     f2,0(r4)
	fsubs   f3,f1,f2
	stfs    f3,0(r5)
	lfs     f4,4(r3)
	lfs     f5,4(r4)
	fsubs   f6,f4,f5
	stfs    f6,4(r5)
	lfs     f7,8(r3)
	lfs     f8,8(r4)
	fsubs   f9,f7,f8
	stfs    f9,8(r5)
	blr

	funcend	VectorSubtract




###########################################################################
#
#       void VectorAdd (vec3_t veca, vec3_t vecb, vec3_t out)
#
###########################################################################

	funcdef	VectorAdd

	lfs     f1,0(r3)
	lfs     f2,0(r4)
	fadds   f3,f1,f2
	stfs    f3,0(r5)
	lfs     f4,4(r3)
	lfs     f5,4(r4)
	fadds   f6,f4,f5
	stfs    f6,4(r5)
	lfs     f7,8(r3)
	lfs     f8,8(r4)
	fadds   f9,f7,f8
	stfs    f9,8(r5)
	blr

	funcend	VectorAdd




###########################################################################
#
#       void VectorCopy (vec3_t in, vec3_t out)
#
###########################################################################

	funcdef	VectorCopy

	lfs     f1,0(r3)
	lwz     r5,4(r3)
	lfs     f3,8(r3)
	stfs    f1,0(r4)
	stw     r5,4(r4)
	stfs    f3,8(r4)
	blr

	funcend	VectorCopy




###########################################################################
#
#       void CrossProduct (vec3_t v1, vec3_t v2, vec3_t cross)
#
###########################################################################

	funcdef	CrossProduct

	lfs     f4,8(r3)
	lfs     f5,4(r4)
	fmuls   f6,f4,f5
	lfs     f1,4(r3)
	lfs     f2,8(r4)
	fmsubs  f3,f1,f2,f6
	stfs    f3,0(r5)
	lfs     f7,0(r3)
	fmuls   f8,f7,f2
	lfs     f9,0(r4)
	fmsubs  f10,f4,f9,f8
	stfs    f10,4(r5)
	fmuls   f11,f1,f9
	fmsubs  f12,f7,f5,f11
	stfs    f12,8(r5)
	blr

	funcend	CrossProduct




###########################################################################
#
#       vec3_t Length (vec3_t v)
#
###########################################################################

	funcdef	Length

	ls	f3,c0
	lfs     f0,0(r3)
	fmuls   f0,f0,f0
	lfs     f1,4(r3)
	fmadds  f0,f1,f1,f0
	lfs     f2,8(r3)
	fmadds  f1,f2,f2,f0
	fcmpo   cr0,f1,f3
	beqlr
	frsqrte f0,f1
	fres    f1,f0
	blr

	funcend	Length




###########################################################################
#
#       float VectorNormalize (vec3_t v)
#
###########################################################################

	funcdef	VectorNormalize

	ls	f3,c0
	ls	f7,c0_5
	lfs     f0,0(r3)
	fmuls   f4,f0,f0
	lfs     f6,4(r3)
	fmadds  f5,f6,f6,f4
	lfs     f2,8(r3)
	fmadds  f1,f2,f2,f5
	fcmpo   cr0,f1,f3
	beqlr
	
	frsqrte f10,f1
	fres    f8,f10
	fdivs   f9,f1,f8
	fadd    f8,f9,f8
	fmul    f1,f8,f7
	fres    f3,f1

	
	#frsqrte f10,f1		# f10  	y = approx 1/sqrt(x)			
	#fmuls	f8,f1,f7	# f8   	x = x * 0.5												
	#fmuls	f9,f10,f10	# f9  	(y * y)
	#lfs	f11,f_1_5(r2)								
	#fnmsubs f12,f9,f8,f11	# f12   1.5 - (x * y * y)
	#fmuls	f3,f12,f10	# f10 or f3  y * (1.5 - x * y * y)

	#fmuls	f9,f10,f10	# 2nd round
	#fnmsubs 12,f9,f8,f11
	#fmuls	f3,f12,f10
			
	#fmuls	f1,f3,f1	# end


	fmuls   f0,f0,f3
	stfs    f0,0(r3)
	fmuls   f6,f6,f3
	stfs    f6,4(r3)
	fmuls   f2,f2,f3
	stfs    f2,8(r3)
	blr

	funcend	VectorNormalize




###########################################################################
#
#       void VectorInverse (vec3_t v)
#
###########################################################################

	funcdef	VectorInverse

	lfs     f1,0(r3)
	fneg    f0,f1
	stfs    f0,0(r3)
	lfs     f3,4(r3)
	fneg    f2,f3
	stfs    f2,4(r3)
	lfs     f5,8(r3)
	fneg    f4,f5
	stfs    f4,8(r3)
	blr

	funcend	VectorInverse




###########################################################################
#
#       void VectorScale (vec3_t in, vec3_t scale, vec3_t out)
#
###########################################################################

	funcdef	VectorScale

	lfs     f2,0(r3)
	fmuls   f3,f2,f1
	stfs    f3,0(r4)
	lfs     f4,4(r3)
	fmuls   f5,f4,f1
	stfs    f5,4(r4)
	lfs     f6,8(r3)
	fmuls   f7,f6,f1
	stfs    f7,8(r4)
	blr

	funcend	VectorScale




###########################################################################
#
#       void FloorDivMod (double numer, double denom, int *quotient,
#                          int *rem)
#
###########################################################################

	funcdef	FloorDivMod

	fctiwz  f0,f1
	stfd    f0,-8(r1)
	lwz     r5,-4(r1)
	mr.	r5,r5
	fctiwz  f0,f2
	stfd    f0,-8(r1)
	lwz     r6,-4(r1)
	divw    r7,r5,r6
	mullw   r8,r7,r6
	subf    r8,r8,r5
	bge     .fdm_exit
	subi    r7,r7,1
	add     r8,r8,r6
.fdm_exit:
	stw     r7,0(r3)
	stw     r8,0(r4)
	blr

	funcend	FloorDivMod




###########################################################################


.ifdef WOS
	.tocd
.else
	.data
.endif

lab c1DEGREE
#	.float  0.01745329252
	.long	0x3c8efa35

lab c64kDIV360
	.long	0x4066c16c,0x16c16c17	# (double)182.0444444...

lab c360DIV64k
#	.float	0.0054931641
	.long   0x3bb40000


f_1_5:
	.long	0x3fc00000	# 1.5
