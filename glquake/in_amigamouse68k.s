** surgeon: mousebuttons are handled in c

**
** Input interrupt routine for mouse control
**
** used for M68k as well as for PowerPC
**
** modified by Matt Hey <matthey7@gmail.com>

	include "exec/types.i"
	include "devices/inputevent.i"


	STRUCTURE       InputIntDat,0
; must be the same as struct InputIntDat in in_amigamouse.c
	WORD            iid_MouseX
	WORD            iid_MouseY
	UBYTE           iid_LeftButtonDown
	UBYTE           iid_LeftButtonUp
	UBYTE           iid_RightButtonDown
	UBYTE           iid_RightButtonUp
	UBYTE           iid_MidButtonDown
	UBYTE           iid_MidButtonUp


	code

	xdef    _InputIntCode
	xdef    InputIntCode
_InputIntCode:
InputIntCode:
	move.l  a0,-(sp)
.loop:
	cmp.b   #IECLASS_RAWMOUSE,(ie_Class,a0)
	bne.b   .next
	move.b  (ie_Code+1,a0),d0
	smi     d1			;if IECODE_UP_PREFIX
	and.w   #$7f,d0			;clear IECODE_UP_PREFIX
	cmp.w   #IECODE_LBUTTON,d0
	beq.b   .lmb
	cmp.w   #IECODE_RBUTTON,d0
	beq.b   .rmb
	cmp.w   #IECODE_MBUTTON,d0
	beq.b   .mmb
.move:
	move.l	(ie_X,a0),(iid_MouseX,a1)	;ie_X,ie_Y -> iid_MouseX,iid_MouseY
.next:
	move.l  (ie_NextEvent,a0),a0
	tst.l   a0
	bne.b   .loop
	move.l  (sp)+,d0
	rts

.lmb:
	move.b  d1,(iid_LeftButtonUp,a1)
	not.l   d1
	move.b  d1,(iid_LeftButtonDown,a1)
	bra.b   .move
.rmb:
	move.b  d1,(iid_RightButtonUp,a1)
	not.l   d1
	move.b  d1,(iid_RightButtonDown,a1)
	bra.b   .move
.mmb:
	move.b  d1,(iid_MidButtonUp,a1)
	not.l   d1
	move.b  d1,(iid_MidButtonDown,a1)
	bra.b   .move
