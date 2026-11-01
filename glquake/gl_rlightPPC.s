##
## Quake for AMIGA
##
## gl_rlightPPC.s
##
## Define WOS for PowerOpen ABI, otherwise SVR4-ABI is used.
##

.include        "macrosPPC.i"
.include	"glquakedefPPC.i"

#
# external references
#

	xrefa	cl
	xrefa	d_lightstylevalue
	xrefa	lightspot
	xrefv	lightplane
	xrefv	c0


#
# defines
#

.set	SURF_DRAWTILED      ,32
.set	MAXLIGHTMAPS        ,4




###########################################################################
#
#       int RecursiveLightPoint (mnode_t *node, vec3_t start, vec3_t end)
#
###########################################################################

	funcdef	RecursiveLightPoint

	init	0,24,7,3
	stmw	r25,gb(r1)
	stfd	f14,fb+0*8(r1)
	stfd	f15,fb+1*8(r1)
	stfd	f16,fb+2*8(r1)

	ls      f13,c0
	mr      r31,r3
	mr      r30,r4
	mr      r29,r5
	la	r28,local+8(r1)

#        if (node->contents < 0)
#                return -1;              // didn't hit anything

	li      r3,-1
	lwz     r0,NODE_CONTENTS(r31)
	mr.     r0,r0
	blt     .end

#        plane = node->plane;
#        front = DotProduct (start, plane->normal) - plane->dist;
#        back = DotProduct (end, plane->normal) - plane->dist;
#        side = front < 0;
#
#        if ( (back < 0) == side)
#                return RecursiveLightPoint (node->children[side], start, end);

	lwz     r25,NODE_PLANE(r31)
	lfs     f7,MPLANE_DIST(r25)
	lfs     f1,0(r25)
	lfs     f4,0(r30)               #f4 = start[0]
	fmsubs  f8,f1,f4,f7
	lfs     f2,4(r25)
	lfs     f5,4(r30)               #f5 = start[1]
	fmadds  f8,f2,f5,f8
	lfs     f3,8(r25)
	lfs     f6,8(r30)               #f6 = start[2]
	fmadds  f8,f3,f6,f8             #f8 = front
	lfs     f9,0(r29)               #f9 = end[0]
	fmsubs  f12,f1,f9,f7
	lfs     f10,4(r29)              #f10 = end[1]
	fmadds  f12,f2,f10,f12
	lfs     f11,8(r29)              #f11 = end[2]
	fmadds  f12,f3,f11,f12          #f12 = back
	li      r27,1                   #side = front < 0
	fcmpo   cr0,f8,f13
	blt     .cont
	li      r27,0
.cont:
	li      r26,1                   #f9 = back < 0
	fcmpo   cr0,f12,f13
	blt     .cont2
	li      r26,0
.cont2:
	cmpw    r26,r27
	bne     .cont3
	slwi    r8,r27,2
	addi    r8,r8,NODE_CHILDREN
	lwzx    r3,r31,r8
	mr      r4,r30
	mr      r5,r29
	call	RecursiveLightPoint
	b       .end

#        frac = front / (front-back);
#        mid[0] = start[0] + (end[0] - start[0])*frac;
#        mid[1] = start[1] + (end[1] - start[1])*frac;
#        mid[2] = start[2] + (end[2] - start[2])*frac;

.cont3:
	fsubs   f1,f8,f12
	fdivs   f1,f8,f1                #f1 = frac
	fsubs   f9,f9,f4
	fsubs   f10,f10,f5
	fsubs   f11,f11,f6
	fmadds  f14,f9,f1,f4
	stfs    f14,0(r28)              #f14 = mid[0]
	fmadds  f15,f10,f1,f5
	stfs    f15,4(r28)              #f15 = mid[1]
	fmadds  f16,f11,f1,f6
	stfs    f16,8(r28)              #f16 = mid[2]

#        r = RecursiveLightPoint (node->children[side], start, mid);
#        if (r >= 0)
#                return r;               // hit something
#
#        if ( (back < 0) == side )
#                return -1;              // didn't hit anuthing

	slwi    r8,r27,2
	addi    r8,r8,NODE_CHILDREN
	lwzx    r3,r31,r8
	mr      r4,r30
	mr      r5,r28
	call	RecursiveLightPoint
	mr.     r3,r3
	bge     .end
	li      r3,-1
	cmpw    r26,r27
	beq     .end

#        VectorCopy (mid, lightspot);
#        lightplane = plane;

	lxa	r11,lightspot
	sw	r25,lightplane
	stfs	f14,0(r11)
	stfs	f15,4(r11)
	stfs	f16,8(r11)

#        surf = cl.worldmodel->surfaces + node->firstsurface;
#        for (i=0 ; i<node->numsurfaces ; i++, surf++)

	lxa     r6,cl
	lwz     r12,CL_WORLDMODEL(r6)
	lwz     r12,MODEL_SURFACES(r12)
	lhz     r4,NODE_FIRSTSURFACE(r31)
	mulli   r4,r4,MSURFACE_SIZEOF
	add     r12,r12,r4

	lhz     r5,NODE_NUMSURFACES(r31)
	mr.     r5,r5
	beq     .skip
	lxa     r6,d_lightstylevalue

#                if (surf->flags & SURF_DRAWTILED)
#                        continue;       // no lightmaps
#
#                tex = surf->texinfo;

.loop:
	lwz     r0,MSURFACE_FLAGS(r12)
	andi.   r0,r0,SURF_DRAWTILED
	bne     .loopend
	lwz     r7,MSURFACE_TEXINFO(r12)

#                s = DotProduct (mid, tex->vecs[0]) + tex->vecs[0][3];
#                t = DotProduct (mid, tex->vecs[1]) + tex->vecs[1][3];;

	lfs     f4,12(r7)
	lfs     f1,0(r7)
	fmadds  f1,f1,f14,f4
	lfs     f2,4(r7)
	fmadds  f1,f2,f15,f1
	lfs     f3,8(r7)
	fmadds  f1,f3,f16,f1
	fctiwz  f0,f1
	stfd    f0,local(r1)
	lwz     r8,local+4(r1)          #r8 = s

	lfs     f8,12+16(r7)
	lfs     f5,0+16(r7)
	fmadds  f5,f5,f14,f8
	lfs     f6,4+16(r7)
	fmadds  f5,f6,f15,f5
	lfs     f7,8+16(r7)
	fmadds  f5,f7,f16,f5
	fctiwz  f0,f5
	stfd    f0,local(r1)
	lwz     r9,local+4(r1)          #r9 = t

#                if (s < surf->texturemins[0] ||
#                t < surf->texturemins[1])
#                        continue;
#
#                ds = s - surf->texturemins[0];
#                dt = t - surf->texturemins[1];

	lha     r10,MSURFACE_TEXTUREMINS(r12)
	subf.   r8,r10,r8
	lha     r11,MSURFACE_TEXTUREMINS+2(r12)
	blt     .loopend
	subf.   r9,r11,r9
	blt     .loopend

#                if ( ds > surf->extents[0] || dt > surf->extents[1] )
#                        continue;
#
#                if (!surf->samples)
#                        return 0;
#
#                ds >>= 4;
#                dt >>= 4;

	lha     r10,MSURFACE_EXTENTS(r12)
	cmpw    r8,r10
	bgt     .loopend
	lha     r11,MSURFACE_EXTENTS+2(r12)
	cmpw    r9,r11
	bgt     .loopend
	lwz     r3,MSURFACE_SAMPLES(r12) #lightmap = surf->samples
	mr.     r3,r3
	beq     .end
	srawi   r8,r8,4                 #ds >>= 4
	srawi   r9,r9,4                 #dt >>= 4

#                lightmap = surf->samples;
#                r = 0;
#                if (lightmap)
#                {
#
#                        lightmap += dt * ((surf->extents[0]>>4)+1) + ds;

	li      r4,0                    #r4 = r = 0
	srawi   r10,r10,4
	srawi   r11,r11,4
	addi    r10,r10,1
	addi    r11,r11,1
	mullw   r9,r9,r10
	add     r9,r9,r8
	add     r3,r3,r9                #lightmap +=

#                        for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
#                                        maps++)
#                        {
#                                scale = d_lightstylevalue[surf->styles[maps]];
#                                r += *lightmap * scale;
#                                lightmap += ((surf->extents[0]>>4)+1) *
#                                                ((surf->extents[1]>>4)+1);
#                        }
#
#                        r >>= 8;
#                }
#                return r;

	li      r0,MAXLIGHTMAPS
	la      r7,MSURFACE_STYLES(r12)
	mullw   r10,r10,r11
	subi    r7,r7,1
	mtctr   r0
.loop2:
	lbzu    r0,1(r7)
	cmpwi   r0,0xff
	beq     .leave
	slwi    r0,r0,2
	lwzx    r11,r6,r0
	lbz     r0,0(r3)
	mullw   r11,r11,r0
	add     r4,r4,r11
	add     r3,r3,r10
	bdnz    .loop2
.leave:
	srawi   r4,r4,8
	mr      r3,r4
	b       .end
.loopend:
	subic.  r5,r5,1
	la      r12,MSURFACE_SIZEOF(r12)
	bne     .loop

#        return RecursiveLightPoint (node->children[!side], mid, end);

.skip:
	xori    r27,r27,1
	slwi    r8,r27,2
	addi    r8,r8,NODE_CHILDREN
	lwzx    r3,r31,r8
	mr      r4,r28
	mr      r5,r29
	call	RecursiveLightPoint
.end:
	lfd	f14,fb+0*8(r1)
	lfd	f15,fb+1*8(r1)
	lfd	f16,fb+2*8(r1)
	lmw	r26,gb(r1)
	exit

	funcend	RecursiveLightPoint
