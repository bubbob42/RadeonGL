/*
**  snd_amiga.c
**
**  Written by Frank Wille <frank@phoenix.owl.de>
**  AHI support by Jarmo Laakkonen <jami.laakkonen@kolumbus.fi>
**
*/

#include "quakedef.h"

static int use_ahi;

/* from AHI */
extern void SNDDMA_ShutdownAHI(void);
extern qboolean SNDDMA_InitAHI(void);
extern int SNDDMA_GetDMAPosAHI(void);

#ifndef NO_PAULA
/* from Paula */
extern void SNDDMA_ShutdownPaula(void);
extern qboolean SNDDMA_InitPaula(void);
extern int SNDDMA_GetDMAPosPaula(void);
extern void SNDDMA_PaulaFilterOn(void);
extern void SNDDMA_PaulaFilterOff(void);
#endif


void SNDDMA_Shutdown(void)
{
	#ifndef NO_PAULA

	if (!use_ahi)
		SNDDMA_ShutdownPaula();
	else
	#endif
		SNDDMA_ShutdownAHI();
}


qboolean SNDDMA_Init(void)
{
	#ifndef NO_PAULA

	use_ahi = COM_CheckParm("-ahi");

	if (!use_ahi)
		return SNDDMA_InitPaula();

	#endif
	
	return SNDDMA_InitAHI();
}


int SNDDMA_GetDMAPos(void)
{
	#ifndef NO_PAULA

	if (!use_ahi)
		return SNDDMA_GetDMAPosPaula();

	#endif

	return SNDDMA_GetDMAPosAHI();
}


void SNDDMA_Submit(void)
{
}


void SNDDMA_EnterWater(void)
{
	#ifndef NO_PAULA
	if (!use_ahi)
		SNDDMA_PaulaFilterOn();
	#endif
}


void SNDDMA_LeaveWater(void)
{
	#ifndef NO_PAULA
	if (!use_ahi)
		SNDDMA_PaulaFilterOff();
	#endif
}
