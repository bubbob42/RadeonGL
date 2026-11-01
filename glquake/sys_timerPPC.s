# Determine PPC bus clock by using CIA timers.
# Based on powerpc.library source by Sam Jordan. Thanks Sam!
# ** This code is used for the PowerUp port only **


# CIATIMER structure
.set	CIATIMER_RESOURCE,	0
.set	CIATIMER_INTERRUPT,	4
.set	CIATIMER_CONTROL,	8
.set	CIATIMER_LOW,		12
.set	CIATIMER_HIGH,		16
.set	CIATIMER_STOPMASK,	20
.set	CIATIMER_STARTMASK,	21
.set	CIATIMER_ICRBIT,	22
.set	CIATIMER_ECLOCK,	24
.set	CIATIMER_SIZE,		28


	.text


	.align	3
	.globl	MeasureBusClock
MeasureBusClock:
# r3 = ciatimer structure pointer
# -> r3 = bus clock in microseconds

	mflr	r0
	stw	r0,4(r1)
	stwu	r1,-16(r1)
	li	r10,-1
	lwz	r4,CIATIMER_CONTROL(r3)
	lwz	r5,CIATIMER_LOW(r3)
	lwz	r6,CIATIMER_HIGH(r3)
	lbz	r7,CIATIMER_STOPMASK(r3)
	lbz	r8,CIATIMER_STARTMASK(r3)
	lbz	r0,0(r4)
	and	r0,r0,r7
	stb	r0,0(r4)		# stop CIA timer
	stb	r10,0(r5)		# set to 0xffff
	stb	r10,0(r6)
	lbz	r0,0(r4)
	or	r0,r0,r8
	mftbl	r9			# PPC start time
	stb	r0,0(r4)		# start CIA timer
	sync
check:
	lbz	r0,0(r6)
	mr.	r0,r0
	bne	check
check2:
	lbz	r0,0(r6)
	mr.	r0,r0
	beq	check2
check3:
	lbz	r0,0(r6)
	mr.	r0,r0
	bne	check3
	mftbl	r8			# PPC stop time
	lbz	r6,0(r5)
	lbz	r0,0(r4)
	and	r0,r0,r7
	stb	r0,0(r4)		# stop CIA timer
	subf	r8,r9,r8		# PPC timer difference
	lis	r7,0x1ffff@h
	ori	r7,r7,0x1ffff@l
	subf	r7,r6,r7
	lwz	r5,CIATIMER_ECLOCK(r3)
	lis	r9,4000000000@h
	ori	r9,r9,4000000000@l
	mullw	r4,r7,r9
	mulhwu	r3,r7,r9
	bl	Div64_32
	mr	r5,r3
	mullw	r4,r8,r9
	mulhwu	r3,r8,r9
	bl	Div64_32
	slwi	r3,r3,2			# PPC timer is busclock/4
	addi	r1,r1,16
	lwz	r0,4(r1)
	mtlr	r0
	blr

	.type	MeasureBusClock,@function
	.size	MeasureBusClock,$-MeasureBusClock


	.align	3
	.globl	CorrectBusClock
CorrectBusClock:
# r3 = bus clock measured
# -> r3 = bus clock corrected

	mr	r5,r3
	mulli	r3,r3,3
	lis	r4,5000000@h
	ori	r4,r4,5000000@l
	add	r3,r3,r4
	lis	r4,10000000@h
	ori	r4,r4,10000000@l
	divwu	r3,r3,r4
	mullw	r3,r3,r4
	li	r4,3
	divwu	r3,r3,r4
	blr

	.type	CorrectBusClock,@function
	.size	CorrectBusClock,$-CorrectBusClock


	.align	3
Div64_32:
	mfctr	r0
	stwu	r1,-32(r1)
	stw	r0,8(r1)
	stw	r4,12(r1)
	stw	r5,16(r1)
	stw	r6,20(r1)
	li	r0,32
	mtctr	r0
	li	r6,0
loop:
	mr.	r3,r3
	bge	cont
	addc	r4,r4,r4
	adde	r3,r3,r3
	add	r6,r6,r6
	b	cont1
cont:
	addc	r4,r4,r4
	adde	r3,r3,r3
	add	r6,r6,r6
	cmplw	r5,r3
	bgt	cont2
cont1:
	subf.	r3,r5,r3
	addi	r6,r6,1
cont2:
	bdnz	loop
	lwz	r0,8(r1)
	mr	r3,r6
	mtctr	r0
	lwz	r4,12(r1)
	lwz	r5,16(r1)
	lwz	r6,20(r1)
	addi	r1,r1,32
	blr

	.type	Div64_32,@function
	.size	Div64_32,$-Div64_32
