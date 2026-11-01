/*
**  keys_amiga68k.c
**
**  68K function for reading keyboard and mouse events
**
**  Written by Jarmo Laakkonen <jami.laakkonen@kolumbus.fi>
**
*/

#include <exec/exec.h>
#include <exec/types.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include "SDI_compiler.h"
#include "keys_amiga.h"


int ASM GetMessages68k(REG(a1, struct MsgPort *port), REG(a0, struct MsgStruct *msg),REG(d0,int maxmsg))
{
	int i = 0;
	struct IntuiMessage *imsg;

	while ((imsg = (struct IntuiMessage *)GetMsg(port)))
	{
		if (i < maxmsg)
		{
			msg[i].Code = imsg->Code;
			msg[i].Class = imsg->Class;
			msg[i].rawkey = imsg->Qualifier; // new Cowcat 2019
			i++;
		}

		ReplyMsg((struct Message *)imsg);
	}

	return i;
}
