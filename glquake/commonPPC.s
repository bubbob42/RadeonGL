##
## commonPPC.s
##
## PPC assembler implementations of several functions in common.c
##

.include	"macrosPPC.i"


# void Q_memcpy (void *dest, void *src, int count)


# short ShortSwap (short l)
#       swap word (LE->BE)

	funcdef	ShortSwap

	extrwi	r0,r3,8,16
	insrwi	r0,r3,8,16
	extsh	r3,r0
	blr
	funcend	ShortSwap


# int LongSwap (int l)
#     swap longword (LE->BE)

	funcdef	LongSwap

	extrwi	r0,r3,8,0
	rlwimi	r0,r3,24,16,23
	rlwimi	r0,r3,8,8,15
	insrwi	r0,r3,8,0
	mr	r3,r0
	blr
	funcend	LongSwap


# float FloatSwap (float f)
#       swap float (LE->BE)

	funcdef	FloatSwap

	stwu	r1,-32(r1)
	stfs	f1,24(r1)
	lwz	r3,24(r1)
	li	r0,24
	stwbrx	r3,r1,r0
	lfs	f1,24(r1)
	addi	r1,r1,32
	blr
	funcend	FloatSwap


# float FloatSwapPPC (float *f)
#       swap float (LE->BE)

	funcdef	FloatSwapPPC

	stwu	r1,-32(r1)
	lbz	r0,0(r3)
	stb	r0,27(r1)
	lbz	r0,1(r3)
	stb	r0,26(r1)
	lbz	r0,2(r3)
	stb	r0,25(r1)
	lbz	r0,3(r3)
	stb	r0,24(r1)
	lfs	f1,24(r1)
	addi	r1,r1,32
	blr
	funcend	FloatSwapPPC
