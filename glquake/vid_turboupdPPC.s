##
## vid_turboupdPPC.s
##
## turbo video buffer update for Quake-WarpOS
##
## Define WOS for PowerOpen ABI, otherwise SVR4-ABI is used.
##

	.text

.ifdef	WOS
	.globl	_ChunkyCopyPPC
	.align	4
_ChunkyCopyPPC:
.else
	.globl	ChunkyCopyPPC
	.align	4
ChunkyCopyPPC:
.endif
# r3 = Mode_Screen
# r4 = gfxaddr
# r5 = vid_buffer
	mr	r0,r5
	lwz	r5,8(r3)	# ms.bpr
	li	r6,0	
	li	r7,0
	lwz	r8,544(r3)	# ms.SCREENWIDTH
	lwz	r9,548(r3)	# ms.SCREENHEIGHT
	mr	r3,r0
	nop			# 16 byte alignment for TurboUpdatePPC

.ifdef	WOS
	.globl	_TurboUpdatePPC
_TurboUpdatePPC:
.else
	.globl	TurboUpdatePPC
TurboUpdatePPC:
.endif
# r3 = vid_buffer
# r4 = gfxaddr
# r5 = bytesperrow
# r6 = x
# r7 = y
# r8 = width
# r9 = height

	mullw	r0,r7,r5
	add     r4,r4,r0
	add     r4,r4,r6
	subf    r5,r8,r5
	srawi   r8,r8,5
	mr      r10,r8
	subi    r3,r3,8
	subi    r4,r4,8
.loop:
	mr      r8,r10
	mtctr   r8
.loop2:
	lfdu    f0,8(r3)
	lfdu    f1,8(r3)
	lfdu    f2,8(r3)
	lfdu    f3,8(r3)
	stfdu   f0,8(r4)
	stfdu   f1,8(r4)
	stfdu   f2,8(r4)
	stfdu   f3,8(r4)
	bdnz    .loop2
	add     r4,r4,r5
	subic.  r9,r9,1
	bne     .loop
	blr

.ifdef	WOS
	.type	_TurboUpdatePPC,@function
	.size	_TurboUpdatePPC,$-_TurboUpdatePPC
.else
	.type	TurboUpdatePPC,@function
	.size	TurboUpdatePPC,$-TurboUpdatePPC
.endif

.ifdef	WOS
	.type	_ChunkyCopyPPC,@function
	.size	_ChunkyCopyPPC,$-_ChunkyCopyPPC
.else
	.type	ChunkyCopyPPC,@function
	.size	ChunkyCopyPPC,$-ChunkyCopyPPC
.endif
