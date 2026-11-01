##
## Quake for AMIGA
##
## fconstPPC.s
##
## Includes some frequently used floating point constants for the
## r_#?PPC.s and d_#?PPC.s PowerPC assembler modules.
##

.ifdef	WOS
.macro  lab function
	.globl	_\function
_\function:
.endm
	.tocd
.else
.macro  lab function
	.globl	\function
\function:
.endm
	.data
.endif


lab c0
#	.float	0.0
	.long	0x00000000
lab c0_5
#	.float	0.5
	.long	0x3f000000
lab c2
#	.float	2.0
	.long	0x40000000
lab c65536
#	.float	65536.0
	.long	0x47800000
lab INT2DBL_0
	.long	0x43300000,0x80000000


.ifdef	WOS
	.globl	@___ReciprocTable
@___ReciprocTable:
	.long	__ReciprocTable
	.data
.endif

lab _ReciprocTable
	.long	0x00000000,0x00000000,0x00000000,0x55555555
	.long	0x00000000,0x33333333,0x2aaaaaab,0x24924925

	.long   0x20000000,0x1c71c71c,0x19999999,0x1745d174
	.long	0x15555555,0x13b13b14,0x12492492,0x11111111

	.long	0x10000000,0x0f0f0f0f,0x0e38e38e,0x0d79435e 

	.long	0x0ccccccd,0x0c30c30c,0x0ba2e8ba,0x0b21642d

	.long	0x0aaaaaab,0x0a3d70a4,0x09d89d8a,0x097b425f

	.long	0x09249249,0x08d3dcb1,0x08888889,0x08421084
