**
** Audio Interrupt routine
**
** used for PPC systems with a LC040 or LC060 CPU
** written by Frank Wille <frank@phoenix.owl.de>
**

	include	"devices/timer.i"
	include "hardware/custom.i"
	include	"hardware/intbits.i"

CUSTOM		equ	$dff000
GetSysTime	equ	-66
IEEEDPFlt	equ	-36
IEEEDPAdd	equ	-66
IEEEDPDiv	equ	-84


; a1 = IntData
; Offset 0:	*TimerBase
; Offset 4:	*FirstTime2
; Offset 8:	*aud_start_time
; Offset 12:    *MathIeeeDoubBasBase


	code

	xdef	_AudioIntCodeNoFPU
	xdef	AudioIntCodeNoFPU
_AudioIntCodeNoFPU:
AudioIntCodeNoFPU:
	movem.l	d2-d5/a4-a6,-(sp)
	move.w	CUSTOM+intreqr,d0
	and.w	#INTF_AUD0|INTF_AUD1|INTF_AUD2|INTF_AUD3,d0
	move.w	d0,CUSTOM+intreq
	move.l	a1,a5
	move.l	(a5)+,a6		; TimerBase
	lea	TVstruct(pc),a0
	move.l	a0,a4
	jsr	GetSysTime(a6)
	move.l	(a5)+,a1
	move.l	TV_SECS(a4),d0
	sub.l	(a1),d0			; - FirstTime2
	move.l	(a5)+,a0
	move.l	(a5),a6			; MathIeeeDoubBasBase
	move.l	a0,a5			; *aud_start_time
	jsr	IEEEDPFlt(a6)
	move.l	d0,d4
	move.l	d1,d5
	move.l	TV_MICRO(a4),d0
	jsr	IEEEDPFlt(a6)
	movem.l	double1M(pc),d2-d3
	jsr	IEEEDPDiv(a6)		; TV_MICRO / 1000000.0
	move.l	d4,d2
	move.l	d5,d3
	jsr	IEEEDPAdd(a6)		; + TV_SECS
	movem.l	d0-d1,(a5)		; -> *aud_start_time
	movem.l	(sp)+,d2-d5/a4-a6
	moveq	#0,d0
	rts


	cnop	0,8
double1M:
	dc.l	$412e8480,$00000000	; 1000000.0
TVstruct:
	dcb.b	TV_SIZE,0
