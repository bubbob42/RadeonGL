#
# 64-bit multiplication and division functions for PowerUp and MorphOS.
#
	.file	"MulDiv64PPC.s"


	.text


PPCDivu64:
	cmpwi	r3,0
	cntlzw	r0,r3
	cntlzw	r9,r4
	bne-	.divu1
	addi	r0,r9,32
.divu1:	cmpwi	r5,0
	cntlzw	r9,r5
	cntlzw	r10,r6
	bne-	.divu2
	addi	r9,r10,32
.divu2:	cmpw	r0,r9
	subfic	r10,r0,64
	bgt-	.divu9
	addi	r9,r9,1
	subfic	r9,r9,64
	add	r0,r0,r9
	sub	r9,r10,r9
	mtctr	r9
	cmpwi	r9,32
	subi	r7,r9,32
	blt-	.divu3
	srw	r8,r3,r7
	li	r7,0
	b	.divu4
.divu3:	srw	r8,r4,r9
	subfic	r7,r9,32
	slw	r7,r3,r7
	or	r8,r8,r7
	srw	r7,r3,r9
.divu4:	cmpwi	r0,32
	subic	r9,r0,32
	blt-	.divu5
	slw	r3,r4,r9
	li	r4,0
	b	.divu6
.divu5:	slw	r3,r3,r0
	subfic	r9,r0,32
	srw	r9,r4,r9
	or	r3,r3,r9
	slw	r4,r4,r0
.divu6:	li	r10,-1
	addic	r7,r7,0
.divu7:	adde	r4,r4,r4
	adde	r3,r3,r3
	adde	r8,r8,r8
	adde	r7,r7,r7
	subc	r0,r8,r6
	subfe.	r9,r5,r7
	blt-	.divu8
	mr	r8,r0
	mr	r7,r9
	addic	r0,r10,1
.divu8:	bdnz+	.divu7
	adde	r4,r4,r4
	adde	r3,r3,r3
	mr	r6,r8
	mr	r5,r7
	blr
.divu9:	mr	r6,r4
	mr	r5,r3
	li	r4,0
	li	r3,0
	blr

	.type	PPCDivu64,@function
	.size	PPCDivu64,$-PPCDivu64


	.globl	PPCDivu64p
PPCDivu64p:
	mflr	r0
	stw	r0,4(r1)
	stwu	r1,-16(r1)
	stw	r31,12(r1)
	mr	r31,r3
	lwz	r5,0(r4)
	lwz	r6,4(r4)
	lwz	r4,4(r3)
	lwz	r3,0(r3)
	bl	PPCDivu64
	stw	r3,0(r31)
	stw	r4,4(r31)
	lwz	r31,12(r1)
	addi	r1,r1,16
	lwz	r0,4(r1)
	mtlr	r0
	blr

	.type	PPCDivu64p,@function
	.size	PPCDivu64p,$-PPCDivu64p


	.globl	PPCDivs64p
PPCDivs64p:
	mflr	r0
	stw	r0,4(r1)
	stwu	r1,-16(r1)
	stw	r30,8(r1)
	stw	r31,12(r1)
	mr	r31,r3
	lwz	r5,0(r4)
	srawi.	r30,r5,31
	lwz	r6,4(r4)
	beq-	.divs4
	subfic	r6,r6,0
	subfze	r5,r5
.divs4:	lwz	r4,4(r3)
	lwz	r3,0(r3)
	srawi	r7,r3,31
	beq-	.divs5
	subfic	r4,r4,0
	subfze	r3,r3
	xor	r30,r30,r7
.divs5:	bl	PPCDivu64
	cmpwi	r30,0
	beq-	.divs6
	subfic	r4,r4,0
	subfze	r3,r3
.divs6:	stw	r3,0(r31)
	stw	r4,4(r31)
	lwz	r30,8(r1)
	lwz	r31,12(r1)
	addi	r1,r1,16
	lwz	r0,4(r1)
	mtlr	r0
	blr

	.type	PPCDivs64p,@function
	.size	PPCDivs64p,$-PPCDivs64p


	.globl	PPCModu64p
PPCModu64p:
	mflr	r0
	stw	r0,4(r1)
	stwu	r1,-16(r1)
	stw	r31,12(r1)
	mr	r31,r3
	lwz	r5,0(r4)
	lwz	r6,4(r4)
	lwz	r4,4(r3)
	lwz	r3,0(r3)
	bl	PPCDivu64
	stw	r5,0(r31)
	stw	r6,4(r31)
	lwz	r31,12(r1)
	addi	r1,r1,16
	lwz	r0,4(r1)
	mtlr	r0
	blr

	.type	PPCModu64p,@function
	.size	PPCModu64p,$-PPCModu64p


	.globl	PPCMods64p
PPCMods64p:
	mflr	r0
	stw	r0,4(r1)
	stwu	r1,-16(r1)
	stw	r30,8(r1)
	stw	r31,12(r1)
	mr	r31,r3
	lwz	r5,0(r4)
	srawi.	r30,r5,31
	lwz	r6,4(r4)
	beq-	.modp1
	subfic	r6,r6,0
	subfze	r5,r5
.modp1:	lwz	r4,4(r3)
	lwz	r3,0(r3)
	srawi	r7,r3,31
	beq-	.modp2
	subfic	r4,r4,0
	subfze	r3,r3
	xor	r30,r30,r7
.modp2:	bl	PPCDivu64
	cmpwi	r30,0
	beq-	.modp3
	subfic	r6,r6,0
	subfze	r5,r5
.modp3:	stw	r5,0(r31)
	stw	r6,4(r31)
	lwz	r30,8(r1)
	lwz	r31,12(r1)
	addi	r1,r1,16
	lwz	r0,4(r1)
	mtlr	r0
	blr

	.type	PPCMods64p,@function
	.size	PPCMods64p,$-PPCMods64p


	.globl	PPCMulu64p
PPCMulu64p:
	lwz	r5,0(r3)
	lwz	r6,4(r3)
	lwz	r7,0(r4)
	lwz	r8,4(r4)
	mullw	r9,r5,r7
	mullw	r10,r6,r8
	mulhwu	r0,r6,r8
	add	r9,r9,r0
	mullw	r11,r6,r7
	add	r9,r9,r11
	stw	r10,4(r3)
	mullw	r0,r5,r8
	add	r11,r9,r0
	stw	r11,0(r3)
	blr

	.type	PPCMulu64p,@function
	.size	PPCMulu64p,$-PPCMulu64p



	.globl	PPCMuls64p
PPCMuls64p:
	lwz	r5,0(r3)
	lwz	r6,4(r3)
	lwz	r7,0(r4)
	lwz	r8,4(r4)
	mullw	r9,r5,r7
	mullw	r10,r6,r8
	mulhw	r0,r6,r8
	add	r9,r9,r0
	mullw	r11,r6,r7
	add	r9,r9,r11
	stw	r10,4(r3)
	mullw	r0,r5,r8
	add	r11,r9,r0
	stw	r11,0(r3)
	blr

	.type	PPCMuls64p,@function
	.size	PPCMuls64p,$-PPCMuls64p
