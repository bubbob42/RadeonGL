/*
**  snd_ahieff.c
**
**  AHI sample position routine
**
**  Written by Jarmo Laakkonen <jami.laakkonen@kolumbus.fi>
**  MorphOS/OS4 adaption by Frank Wille <frank@phoenix.owl.de>
**
*/

#include <devices/ahi.h>

#ifdef __MORPHOS__
#include <emul/emulregs.h>
#else
#include "SDI_compiler.h"
#endif


#ifdef __MORPHOS__
ULONG EffFunc(void)
{
	#if 0
  	struct Hook *hook = (struct Hook *)REG_A0;
  	struct AHIAudioCtrl *actrl = (struct AHIAudioCtrl *)REG_A2;
	#endif

  	struct AHIEffChannelInfo *info = (struct AHIEffChannelInfo *)REG_A1;

#elif defined(__amigaos4__)
ULONG EffFunc(struct Hook *hook,struct AHIAudioCtrl *actrl, struct AHIEffChannelInfo *info)
{

#else
ULONG ASM EffFunc(REG(a0, struct Hook *hook), REG(a2, struct AHIAudioCtrl *actrl), REG(a1, struct AHIEffChannelInfo *info))
{
#endif
	extern int ahi_pos;

	ahi_pos = info->ahieci_Offset[0];

	return 0;
}
