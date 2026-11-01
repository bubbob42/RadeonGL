#include <exec/types.h>

struct MsgStruct
{
	ULONG Class;
	UWORD Code;
	UWORD rawkey;
};

/* mouse wheel */
#ifndef NM_WHEEL_UP
#define NM_WHEEL_UP (0x7a)
#endif
#ifndef NM_WHEEL_DOWN
#define NM_WHEEL_DOWN (0x7b)
#endif

//int GetMessagesNat(struct MsgPort *,struct MsgStruct *,int);

#ifndef __PPC__
int ASM GetMessages68k(REG(a1, struct MsgPort *), REG(a0, struct MsgStruct *), REG(d0, int));
#endif

