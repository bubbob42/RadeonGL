; M68k support routines for determining PPC bus clock.
; Reserves/frees CIA timer for PPC.
; Based on powerpc.library source by Sam Jordan. Thanks Sam!
; ** This code is used for the PowerUp port only **


	include	"exec/interrupts.i"
	include	"devices/timer.i"
	include	"hardware/cia.i"


; exec, timer and cia LVOs (PowerUp amiga.lib doesn't include those?)
_LVODisable		=	-120
_LVOEnable		=	-126
_LVOOpenResource	=	-498
_LVOCacheClearU		=	-636
_LVOReadEClock		=	-60
_LVOAddICRVector	=	-6
_LVORemICRVector	=	-12
_LVOAbleICR		=	-18

; The CIAs
CIA_A			=	$bfe001
CIA_B			=	$bfd000

; CIATIMER structure
CIATIMER_RESOURCE	=	0
CIATIMER_INTERRUPT	=	4
CIATIMER_CONTROL	=	8
CIATIMER_LOW		=	12
CIATIMER_HIGH		=	16
CIATIMER_STOPMASK	=	20
CIATIMER_STARTMASK	=	21
CIATIMER_ICRBIT		=	22
CIATIMER_ECLOCK		=	24
CIATIMER_SIZE		=	28

; macros
	macro	CALLSYS
	move.l	SysBase,a6
	jsr	_LVO\1(a6)
	endm


; external symbols (from PPC)

	xref	SysBase
	xref	TimerBase



	code



	cnop	0,4
	xdef	ReserveCIA
ReserveCIA:
; Reserves a CIA timer and initializes ciatimer structure.
; a0 = ciatimer structure pointer
; -> d0 = success (0=error, -1=success)

	movem.l	d1-d7/a0-a6,-(sp)
	move.l	a0,d2
	lea	DummyInt,a3
	lea	DummyIntName(pc),a0
	move.l	a0,LN_NAME(a3)
	clr.l	IS_DATA(a3)
	lea	DummyInterrupt(pc),a0
	move.l	a0,IS_CODE(a3)
	lea	CIA_A,a5
	lea	CIAA_name(pc),a1
	CALLSYS	OpenResource
	tst.l	d0
	beq	.error
.loop:
	move.l	d0,a2
	CALLSYS	Disable
	move.l	#ciacra,d7
	move.l	#ciatalo,d6
	move.l	#ciatahi,d5
	move.b	#CIACRAF_TODIN|CIACRAF_PBON|CIACRAF_OUTMODE|CIACRAF_SPMODE,d4
	move.b	#CIACRAF_START,d3
	move.l	a2,a6
	move.w	#CIAICRB_TA,d0
	move.l	d0,-(sp)
	move.l	a3,a1
	jsr	_LVOAddICRVector(a6)
	move.l	(sp)+,d1
	tst.l	d0
	beq	.timerfound
	move.b	#CIACRBF_ALARM|CIACRBF_PBON|CIACRBF_OUTMODE,d4
	move.b	#CIACRBF_START,d3
	move.l	a2,a6
	move.w	#CIAICRB_TB,d0
	move.l	d0,-(sp)
	move.l	a3,a1
	jsr	_LVOAddICRVector(a6)
	move.l	(sp)+,d1
	tst.l	d0
	beq	.timerfound
	CALLSYS	Enable
	lea	CIA_B,a5
	lea	CIAB_name(pc),a1
	CALLSYS	OpenResource
	tst.l	d0
	beq	.error
	bra	.loop

.timerfound:
	move.l	a2,a6
	move.w	d1,d0
	move.l	d1,-(sp)
	CALLSYS	AbleICR
	CALLSYS	Enable
	move.l	(sp)+,d1
	move.l	d2,a0
	add.l	a5,d7
	add.l	a5,d6
	add.l	a5,d5
	move.l	a2,CIATIMER_RESOURCE(a0)
	move.l	a3,CIATIMER_INTERRUPT(a0)
	move.l	d7,CIATIMER_CONTROL(a0)
	move.l	d6,CIATIMER_LOW(a0)
	move.l	d5,CIATIMER_HIGH(a0)
	move.b	d4,CIATIMER_STOPMASK(a0)
	move.b	d3,CIATIMER_STARTMASK(a0)
	move.w	d1,CIATIMER_ICRBIT(a0)
	bsr	GetEClock
	move.l	d0,CIATIMER_ECLOCK(a0)
	CALLSYS	CacheClearU
	moveq	#-1,d0
	bra.b	.end

.error:
	moveq	#0,d0
.end:
	movem.l (sp)+,d1-d7/a0-a6
	rts

CIAA_name:
	dc.b	"ciaa.resource",0
CIAB_name:
	dc.b	"ciab.resource",0
DummyIntName:
	dc.b	"Quake Bus Clock Measuring",0


	cnop	0,4
	xdef	FreeCIA
FreeCIA:
; Release CIA timer.
; a0 = ciatimer structure pointer

	move.l	a6,-(sp)
	move.l	CIATIMER_RESOURCE(a0),a6
	move.w	CIATIMER_ICRBIT(a0),d0
	move.l	CIATIMER_INTERRUPT(a0),a1
	jsr	_LVORemICRVector(a6)
	move.l	(sp)+,a6
	rts


	cnop	0,4
GetEClock:
; Evaluate E clock (CIA timer clock)
; -> d0 = EClock

	movem.l	d1-d7/a0-a6,-(sp)
	subq.l	#EV_SIZE,sp
	move.l	TimerBase,a6
	move.l	sp,a0
	jsr	_LVOReadEClock(a6)
	addq.l	#EV_SIZE,sp
	movem.l	(sp)+,d1-d7/a0-a6
	rts


	cnop	0,4
DummyInterrupt:
	moveq	#0,d0
	rts



	section	DummyInt,bss

DummyInt:
	ds.b	IS_SIZE
