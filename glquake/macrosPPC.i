##
## Quake for AMIGA
##
## macrosPPC.i
##
## Macros used in all PPC assembler sources of Quake.
## Define WOS for WarpOS/PowerOpen ABI, otherwise code for
## PowerUp/SVR4 will be generated.
##
## Define NOLR, if the init/exit macros must not save/restore the LR.
##

# ABI-specific macros

.ifdef	WOS
# WarpOS/PowerOpen

.macro	xrefv	data# cross reference to an elementary data type
	.extern	_\data
.endm

.macro	xrefa	object# cross reference to an object's address
	.extern	@__\object
.endm

.macro	funcdef	globalfunc# global function definition
	.text
	.globl	_\globalfunc
	#.globl	_\globalfunc__r
_\globalfunc:
#_\globalfunc__r:
.endm
 
.macro	lab	label# label definition
_\label:
.endm

.macro	init	nParams, localSize, nGPR, nFPR# init stack frame: nParams,localSize,nGPR,nFPR
.set	param,	24
.set	local,	24+(\nParams*4)
.set	gb,	(local+\localSize+3)&~3
.set	fb,	(gb+\nGPR*4+7)&~7
.set	sfsize,	(fb+\nFPR*8+15)&~15
.ifndef	NOLR
	mflr	r0
	stw	r0,8(r1)
.endif
	stwu	r1,-sfsize(r1)
.endm

.macro	exit	# free stack frame and return
	addi	r1,r1,sfsize
.ifndef	NOLR
	lwz	r0,8(r1)
	mtlr	r0
.endif
	blr
.endm

.macro	funcend	fend# set function type and size
        .type   _\fend,@function
        .size   _\fend,$-_\fend
.endm

.ifndef	NOLR
.macro	call	calsr# call sub routine
	bl	_\calsr
.endm

.macro	mcall	calms# call math subroutine
.ifdef	__STORM__
	bl	_\calms__r
.else
	bl	_\calms
.endif
.endm
.endif

.macro	lxa	register, value# load external object's address
	lwz	\register,@__\value(r2)
.endm

.macro	lla	register, value# load local object's address
	la	\register,_\value(r2)
.endm

.macro	lw	register, value# load word
	lwz	\register,_\value(r2)
.endm

.macro	sw	register, value# store word
	stw	\register,_\value(r2)
.endm

.macro	lf	register, value# load double float
	lfd	\register,_\value(r2)
.endm

.macro	ls	register, value# load single float
	lfs	\register,_\value(r2)
.endm

.macro	ss	register, value# store single float
	stfs	\register,_\vaue(r2)
.endm


.else
# SVR4/ELF (PowerUp)

.macro	xrefv	# cross reference to an elementary data type
	.extern	\1
.endm

.macro	xrefa	# cross reference to an object's address
	.extern	\1
.endm

.macro	funcdef	# global function definition
	.text
	.globl	\1
\1:
.set	rtmp,	12	# r12 is default reg. for loading/storing data
.endm

.macro	lab	# label definition
\1:
.endm

.macro	init	# init stack frame: nParams,localSize,nGPR,nFPR
.set	param,	8
.set	local,	8+(\1*4)
.set	save14,	(local+\2+3)&~3
.set	gb,	save14+4
.set	fb,	(gb+\3*4+7)&~7
.set	sfsize,	(fb+\4*8+15)&~15
.ifndef	NOLR
	mflr	r0
	stw	r0,4(r1)
.endif
	stwu	r1,-sfsize(r1)
	stw	r14,save14(r1)
.set	rtmp,	14	# now we may use r14 for loading/storing data
.endm

.macro	exit	# free stack frame and return
	lwz	r14,save14(r1)
	addi	r1,r1,sfsize
.ifndef	NOLR
	lwz	r0,4(r1)
	mtlr	r0
.endif
	blr
.set	rtmp,	12
.endm

.macro	funcend	# set function type and size
        .type   \1,@function
        .size   \1,$-\1
.endm

.ifndef	NOLR
.macro	call	# call sub routine
	bl	\1
.endm

.macro	mcall	# call math subroutine
	bl	\1
.endm
.endif

.macro	lxa	# load external object's address
	lis	\1,\2@ha
	addi	\1,\1,\2@l
.endm

.macro	lla	# load local object's address
	lis	\1,\2@ha
	addi	\1,\1,\2@l
.endm

.macro	lw	# load word
	lis	rtmp,\2@ha
	lwz	\1,\2@l(rtmp)
.endm

.macro	sw	# store word
	lis	rtmp,\2@ha
	stw	\1,\2@l(rtmp)
.endm

.macro	lf	# load double float
	lis	rtmp,\2@ha
	lfd	\1,\2@l(rtmp)
.endm

.macro	ls	# load single float
	lis	rtmp,\2@ha
	lfs	\1,\2@l(rtmp)
.endm

.macro	ss	# store single float
	lis	rtmp,\2@ha
	stfs	\1,\2@l(rtmp)
.endm

.endif


#
# ABI-independant macros
#

# int2dbl: Convert Signed Integer to Double Precision Floating Point
.macro	int2dbl	destFPR, srcGPR, trash, StkFrameOffs=0x4330000000000000, FPR=0x4330000080000000
	xoris	\trash,\srcGPR,0x8000
	stw	\trash,\StkFrameOffs+4(r1)
	lfd	\destFPR,\StkFrameOffs(r1)
	fsub	\destFPR,\destFPR,\FPR
.endm

.macro	save	register, offset# firstGPR, frameOffset
.set	__reg,\register
.set	__off,\offset
	.rept	32-\register
	stw	__reg,__off(r1)
.set	__reg,__reg+1
.set	__off,__off+4
	.endr
.endm

.macro	restore	register, offset# firstGPR, frameOffset
.set	__reg,\register
.set	__off,\offset
	.rept	32-\register
	lwz	__reg,__off(r1)
.set	__reg,__reg+1
.set	__off,__off+4
	.endr
.endm
