##
##  Sound mixing routines for Amiga PPC
##  Written by Frank Wille <frank@phoenix.owl.de>
##
## This implementation of S_TransferPaintBuffer() handles the following
## two formats:
##
## 1. 8-bits STEREO for Amiga native Paula sound-chip.
##    DMA buffer layout (8 bits signed samples):
##    <-- shm.samples bytes left ch. --><-- shm.samples bytes right ch. -->
##
## 2. 16-bits STEREO for AHI.
##    DMA buffer layout (16 bits signed big-endian samples):
##    <-- shm.samples * (<16bits left ch.>|<16 bits right ch.>) -->
##

.include        "macrosPPC.i"
.include	"glquakedefPPC.i"



	.text


# external references
	xrefv	shm
        xrefv	paintedtime
        xrefa	paintbuffer
        xrefa	volume


	funcdef	S_TransferPaintBuffer
# r3 = enttim

	lw	r4,shm			# r4 shm (struct dma_t)
	lxa	r5,paintbuffer		# r5 paintbuffer (int left,int right)
	lxa	r6,volume
	lfs	f1,16(r6)		# volume.value * 256
	ls	f2,c256
	fmuls	f1,f1,f2
	lw	r7,paintedtime
	subf.	r0,r7,r3		# count
	beq	exit
	mtctr	r0
	lwz	r8,dma_buffer(r4)	# r8 dma buffer start address
	fctiwz	f1,f1
	stfd	f1,-8(r1)
	lwz	r10,-4(r1)		# r10 = snd_vol
	lwz	r11,dma_samples(r4)	# r11 = num samples in buffer
	lwz	r3,dma_samplebits(r4)
	cmpwi	r3,8
	beq	paula8bit

	# 16 bit AHI transfer
	add	r3,r11,r11		# * 2
	add	r9,r8,r3		# r9 = dma buffer end address
	srwi	r11,r11,1
	subi	r11,r11,1
	and	r7,r7,r11
	slwi	r0,r7,2
	add	r3,r8,r0		# r3 = out
	subi	r5,r5,4
	li	r4,0x7fff		# r4 = max val
	not	r6,r4			# r6 = min val
	sub	r8,r9,r8		# r8 = buffer size
loop16:
	lwzu	r11,4(r5)
	mullw	r11,r11,r10
	lwzu	r12,4(r5)
	srawi	r11,r11,8
	mullw	r12,r12,r10
	cmpw	r11,r4
	ble+	.1
	mr	r11,r4
	b	.2
.1:	cmpw	r11,r6
	bge+	.2
	mr	r11,r6
.2:	srawi	r12,r12,8
	slwi	r11,r11,16
	cmpw	r12,r4
	ble+	.3
	mr	r12,r4
	b	.4
.3:	cmpw	r12,r6
	bge+	.4
	mr	r12,r6
.4:	insrwi	r11,r12,16,16
	stw	r11,0(r3)
	addi	r3,r3,4
	cmpw	r3,r9
	blt+	.5
	sub	r3,r3,r8
.5:	bdnz	loop16
exit:
	blr

paula8bit:
	# 8 bit PAULA transfer
	stwu	r1,-32(r1)
	stw	r30,24(r1)
	stw	r31,28(r1)
	srawi	r11,r11,1
	add	r9,r8,r11		# r9 = dma buffer end address
	subi	r0,r11,1
	and	r7,r7,r0
	add	r3,r8,r7		# r3 = out
	subi	r5,r5,4
	li	r4,0x7f			# r4 = max val
	not	r6,r4			# r6 = min val
	sub	r8,r9,r8		# r8 = buffer size
	mr	r7,r11			# r7 = stereo offset
	mr	r0,r10			# r0 = volume
	mfctr	r10			# r10 = count
loop8:
	andi.	r31,r3,3
	beq	loop8_aligned
	# unaligned, bytewise transfer
loop8_unaligned:
	lwzu	r11,4(r5)
	mullw	r11,r11,r0
	lwzu	r12,4(r5)
	srawi	r11,r11,16
	mullw	r12,r12,r0
	cmpw	r11,r4
	ble+	.l8u1
	mr	r11,r4
	b	.l8u2
.l8u1:	cmpw	r11,r6
	bge+	.l8u2
	mr	r11,r6
.l8u2:	srawi	r12,r12,16
	stb	r11,0(r3)		# left channel
	cmpw	r12,r4
	ble+	.l8u3
	mr	r12,r4
	b	.l8u4
.l8u3:	cmpw	r12,r6
	bge+	.l8u4
	mr	r12,r6
.l8u4:	stbx	r12,r3,r7		# right channel
	addi	r3,r3,1
	cmpw	r3,r9
	blt+	.l8u5
	sub	r3,r3,r8
.l8u5:	subic.	r10,r10,1
	bne	loop8
	b	loop8_exit

loop8_aligned:				# output buffer is 32-bit aligned
	cmpwi	r10,4
	blt-	loop8_unaligned

	lwzu	r11,4(r5)
	mullw	r11,r11,r0
	lwzu	r12,4(r5)
	srawi	r11,r11,16
	mullw	r12,r12,r0
	cmpw	r11,r4
	ble+	.l8a1
	mr	r11,r4
	b	.l8a2
.l8a1:	cmpw	r11,r6
	bge+	.l8a2
	mr	r11,r6
.l8a2:	rlwimi	r30,r11,24,0,7
	srawi	r12,r12,16
	cmpw	r12,r4
	ble+	.l8a3
	mr	r12,r4
	b	.l8a4
.l8a3:	cmpw	r12,r6
	bge+	.l8a4
	mr	r12,r6
.l8a4:	rlwimi	r31,r12,24,0,7

	lwzu	r11,4(r5)
	mullw	r11,r11,r0
	lwzu	r12,4(r5)
	srawi	r11,r11,16
	mullw	r12,r12,r0
	cmpw	r11,r4
	ble+	.l8a5
	mr	r11,r4
	b	.l8a6
.l8a5:	cmpw	r11,r6
	bge+	.l8a6
	mr	r11,r6
.l8a6:	rlwimi	r30,r11,16,8,15
	srawi	r12,r12,16
	cmpw	r12,r4
	ble+	.l8a7
	mr	r12,r4
	b	.l8a8
.l8a7:	cmpw	r12,r6
	bge+	.l8a8
	mr	r12,r6
.l8a8:	rlwimi	r31,r12,16,8,15

	lwzu	r11,4(r5)
	mullw	r11,r11,r0
	lwzu	r12,4(r5)
	srawi	r11,r11,16
	mullw	r12,r12,r0
	cmpw	r11,r4
	ble+	.l8a9
	mr	r11,r4
	b	.l8aa
.l8a9:	cmpw	r11,r6
	bge+	.l8aa
	mr	r11,r6
.l8aa:	rlwimi	r30,r11,8,16,23
	srawi	r12,r12,16
	cmpw	r12,r4
	ble+	.l8ab
	mr	r12,r4
	b	.l8ac
.l8ab:	cmpw	r12,r6
	bge+	.l8ac
	mr	r12,r6
.l8ac:	rlwimi	r31,r12,8,16,23

	lwzu	r11,4(r5)
	mullw	r11,r11,r0
	lwzu	r12,4(r5)
	srawi	r11,r11,16
	mullw	r12,r12,r0
	cmpw	r11,r4
	ble+	.l8ad
	mr	r11,r4
	b	.l8ae
.l8ad:	cmpw	r11,r6
	bge+	.l8ae
	mr	r11,r6
.l8ae:	rlwimi	r30,r11,0,24,31
	stw	r30,0(r3)
	srawi	r12,r12,16
	cmpw	r12,r4
	ble+	.l8af
	mr	r12,r4
	b	.l8ag
.l8af:	cmpw	r12,r6
	bge+	.l8ag
	mr	r12,r6
.l8ag:	rlwimi	r31,r12,0,24,31
	stwx	r31,r3,r7

	addi	r3,r3,4
	cmpw	r3,r9
	blt+	.l8ah
	sub	r3,r3,r8
.l8ah:	subic.	r10,r10,4
	bne	loop8_aligned

loop8_exit:
	lwz	r30,24(r1)
	lwz	r31,28(r1)
	addi	r1,r1,32
	blr

	funcend	S_TransferPaintBuffer



.ifdef	WOS
	.tocd
.else
        .data
.endif
	.align	2
lab c256
#	.float	256.0
	.long	0x43800000
