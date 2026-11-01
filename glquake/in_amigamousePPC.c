#include <exec/types.h>
#include <devices/inputevent.h>


struct InputIntDat
{
	int LeftButtonDown;
	int MidButtonDown;
	int RightButtonDown;
	int LeftButtonUp;
	int MidButtonUp;
	int RightButtonUp;
	int MouseX;
	int MouseY;
	int MouseSpeed;
};

/*
	STRUCTURE       InputIntDat,0
; must be the same as struct InputIntDat in in_amigamouse.c
	ULONG           iid_LeftButtonDown
	ULONG           iid_MidButtonDown
	ULONG           iid_RightButtonDown
	ULONG           iid_LeftButtonUp
	ULONG           iid_MidButtonUp
	ULONG           iid_RightButtonUp
	ULONG           iid_MouseX
	ULONG           iid_MouseY
	ULONG		iid_MouseSpeed
*/

static struct InputIntDat *InputIntData;

void InputIntCode(void)
{
	short d0;
	int d1;
	struct InputEvent *ie, next_ie;

	/****************************
	move.l  a0,-(sp)
	move.l	iid_MouseSpeed(a1),d1
	****************************/

	ie = &
	d1 = InputIntData.MouseSpeed;
loop:
	/**************************
	cmp.b   #IECLASS_RAWMOUSE,ie_Class(a0)
	bne.b   .next
	***************************************/

	if(ie->class != IECLASS_RAWMOUSE)
	{
		goto next;
	}


	/**************************
	move.w  ie_Code(a0),d0
	btst    #7,d0
	bne.b   .up
	**************************/

	d0 = ie->code;
	if(d0 != 7)
	{
		goto up;
	}

	/**************************
	cmp.w   #IECODE_LBUTTON,d0
	beq.b   .lmb_down
	cmp.w   #IECODE_MBUTTON,d0
	beq.b   .mmb_down
	cmp.w   #IECODE_RBUTTON,d0
	beq.b   .rmb_down
	bra.b   .move
	**************************/

	if(d0 == IECODE_LBUTTON)
	{
		goto lmb_down;
	}
	else if(d0 == IECODE_MBUTTON)
	{
		goto mmb_down;
	}
	else if(d0 == IECODE_RBUTTON)
	{
		goto rmb_down;
	}
	else
	{
		goto move;
	}

up:
	/**************************
	and.w   #$007f,d0
	cmp.w   #IECODE_LBUTTON,d0
	beq.b   .lmb_up
	cmp.w   #IECODE_MBUTTON,d0
	beq.b   .mmb_up
	cmp.w   #IECODE_RBUTTON,d0
	beq.b   .rmb_up
	bra.b   .move
	**************************/

	d0 &= 0x007F;

	if(d0 == IECODE_LBUTTON)
	{
		goto lmb_up;
	}
	else if(d0 == IECODE_MBUTTON)
	{
		goto mmb_up;
	}
	else if(d0 == IECODE_RBUTTON)
	{
		goto rmb_up;
	}
	else
	{
		goto move;
	}

lmb_down:
	/*********************************
	move.l  #-1,iid_LeftButtonDown(a1)
	clr.l   iid_LeftButtonUp(a1)
	bra.b   .move
	*********************************/

	IntupIntData.LeftButtonDown = -1;
	IntupIntData.LeftButtonUp   =  0;
	goto move;

mmb_down:
	/********************************
	move.l  #-1,iid_MidButtonDown(a1)
	clr.l   iid_MidButtonUp(a1)
	bra.b   .move
	********************************/

	IntupIntData.MidButtonDown = -1;
	IntupIntData.MidButtonUp   =  0;
	goto move;

rmb_down:
	/**********************************
	move.l  #-1,iid_RightButtonDown(a1)
	clr.l   iid_RightButtonUp(a1)
	bra.b   .move
	**********************************/

	IntupIntData.RightButtonDown = -1;
	IntupIntData.RightButtonUp   =  0;
	goto move;

lmb_up:
	/*******************************
	move.l  #-1,iid_LeftButtonUp(a1)
	clr.l   iid_LeftButtonDown(a1)
	bra.b   .move
	*******************************/

	IntupIntData.LeftButtonUp   = -1;
	IntupIntData.LeftButtonDown =  0;
	goto move;

mmb_up:
	/******************************
	move.l  #-1,iid_MidButtonUp(a1)
	clr.l   iid_MidButtonDown(a1)
	bra.b   .move
	******************************/

	IntupIntData.MidButtonUp   = -1;
	IntupIntData.MidButtonDown =  0;
	goto move;

rmb_up:
	/********************************
	move.l  #-1,iid_RightButtonUp(a1)
	clr.l   iid_RightButtonDown(a1)
	********************************/

	IntupIntData.RightButtonUp   = -1;
	IntupIntData.RightButtonDown =  0;

move:
	/**************************
	move.w  ie_X(a0),d0
	muls	d1,d0
	move.l  d0,iid_MouseX(a1)
	**************************/

	d0  = ie->X;
	d0 *= d1;
	IntupIntData.MouseX = d0;

	/**************************
	move.w  ie_Y(a0),d0
	muls	d1,d0
	move.l  d0,iid_MouseY(a1)
	**************************/

	d0  = ie->Y;
	d0 *= d1;
	IntupIntData.MouseY = d0;

next:
	/**************************
	move.l  ie_NextEvent(a0),d0
	move.l  d0,a0
	bne.b   .loop
	move.l  (sp)+,d0
	rts
	**************************/

	next_ie = ie->NextEvent;

	if(next_ie != ie)
	{
		ie = next_ie;
		goto loop;
	}

	return;
}

