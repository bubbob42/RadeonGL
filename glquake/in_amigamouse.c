/* in_amigamouse.c -- AMIGA mouse driver */
/* Written by Frank Wille <frank@phoenix.owl.de> */
/* Modded by Cowcat for windowplay */
/* MattHey assembler mod */

#pragma pack(push,2)

#include <exec/memory.h>
#include <exec/interrupts.h>
#include <devices/input.h>
#include <intuition/intuitionbase.h>
#include <clib/alib_protos.h>
#include <proto/exec.h>

#pragma pack(pop)

#include "quakedef.h"

extern struct IntuitionBase *IntuitionBase;

#ifndef GLQUAKE
extern struct Screen *QuakeScreen;
#else
extern struct Window *QuakeWindow;
#endif

cvar_t  m_filter = {"m_filter","1"};
cvar_t  m_speed = {"m_speed","16"};

qboolean mouse_avail = false;

#ifndef GLQUAKE
float mouse_x, mouse_y;
#else
extern float mouse_x, mouse_y;
#endif

float old_mouse_x, old_mouse_y;

extern int psxused;

/* AMIGA specific */
static struct MsgPort *inputport=NULL;
static struct IOStdReq *input=NULL;
static byte input_dev = -1;
static struct Interrupt InputHandler;

struct InputIntDat
{
	short MouseX;  // must be 2 bytes
	short MouseY;  // must be 2 bytes
	unsigned char LeftButtonDown;
	unsigned char LeftButtonUp;
	unsigned char RightButtonDown;
	unsigned char RightButtonUp;
	unsigned char MidButtonDown;
	unsigned char MidButtonUp;
};

static struct InputIntDat *InputIntData;

/* this is a 68k routine! */

extern void InputIntCode(void);
extern void InputIntCodeWin(void);

extern BOOL windowplay;

qboolean MouseHandler; // new 2019 - Cowcat

/*
===========
IN_InitMouse
===========
*/

void IN_InitMouse (void)
{
  	if ( COM_CheckParm ("-nomouse") )
    		return;

  	/* open input.device */

  	//if (inputport = CreatePort(NULL,0))
	if ( (inputport = CreateMsgPort()) )
	{    
    		//if (input = CreateStdIO(inputport))
		if ( (input = (struct IOStdReq *) CreateIORequest(inputport, sizeof(struct IOStdReq))) )
		{
      			if ( (InputIntData = Sys_Alloc(sizeof(struct InputIntDat), MEMF_CHIP|MEMF_PUBLIC|MEMF_CLEAR)) )
			{
        			if ( !(input_dev = OpenDevice("input.device",0, (struct IORequest *)input,0)) )
				{
          				InputHandler.is_Node.ln_Type = NT_INTERRUPT;
          				InputHandler.is_Node.ln_Pri = 90; // was 100 - Cowcat 2019
          				InputHandler.is_Data = InputIntData;

         				if (!windowplay) 
						InputHandler.is_Code = (void(*)())InputIntCode;

         				else 		
						InputHandler.is_Code = (void(*)())InputIntCodeWin;

          				input->io_Data = (void *)&InputHandler;
          				input->io_Command = IND_ADDHANDLER;
          				DoIO((struct IORequest *)input);
          				mouse_avail = true;
       				}
      			}
    		}
  	}

  	if (!mouse_avail)
	{
    		Con_Printf("Couldn't open input.device\n");
		MouseHandler = false; // new 2019 - Cowcat
    		return;
  	}

	MouseHandler = true; // new 2019 - Cowcat

  	Con_Printf("Amiga mouse available\n");
}

/*
===========
IN_ShutdownMouse
===========
*/

void IN_ShutdownMouse (void)
{
	if (mouse_avail)
	{
    		input->io_Data=(void *)&InputHandler;
    		input->io_Command=IND_REMHANDLER;
    		DoIO((struct IORequest *)input);
  	}

  	if (!input_dev)
		CloseDevice((struct IORequest *)input);

  	Sys_Free(InputIntData);

  	if (input)
		//DeleteExtIO((struct IORequest *)input);
		DeleteIORequest((struct IORequest *)input);

  	if (inputport)
		//DeletePort(inputport);
		DeleteMsgPort(inputport);

  	mouse_avail = false;
  	input_dev = -1;

	MouseHandler = false; // new 2019 - Cowcat
}


/*
===========
IN_MouseCommands
===========
*/
void IN_MouseCommands (void)
{
	if (mouse_avail && psxused)
	{
		if (InputIntData->LeftButtonDown)
			Key_Event (K_MOUSE1, TRUE, -1);

		if (InputIntData->LeftButtonUp)
			Key_Event (K_MOUSE1, FALSE, -1);

		if (InputIntData->MidButtonDown)
			Key_Event (K_MOUSE2, TRUE, -1);

		if (InputIntData->MidButtonUp)
			Key_Event (K_MOUSE2, FALSE, -1);

		if (InputIntData->RightButtonDown)
			Key_Event (K_MOUSE3, TRUE, -1);

		if (InputIntData->RightButtonUp)
	    		Key_Event (K_MOUSE3, FALSE, -1);
	}
}


/*
===========
IN_MouseMove
===========
*/
void IN_MouseMove (usercmd_t *cmd)
{
	float mx, my;	//surgeon: was int

	if (!mouse_avail)
		return;

#ifndef GLQUAKE
        if (IntuitionBase->FirstScreen != QuakeScreen)
          return;
#else
        if (IntuitionBase->ActiveWindow != QuakeWindow)
          return;
#endif

#ifdef NOMOUSEASM
	mx = (float)mousemove[0] * m_speed.value;
	my = (float)mousemove[1] * m_speed.value;

	mousemove[0] = 0;
	mousemove[1] = 0;
#else
	mx = (float)InputIntData->MouseX * m_speed.value; // + new mouse Cowcat
	my = (float)InputIntData->MouseY * m_speed.value; // + 

	InputIntData->MouseX = 0;
	InputIntData->MouseY = 0;
#endif

	if (m_filter.value)
	{
		mouse_x = (mx + old_mouse_x) * 0.5 * sensitivity.value;
		mouse_y = (my + old_mouse_y) * 0.5 * sensitivity.value;
	}

	else
	{
		mouse_x = mx * sensitivity.value;
		mouse_y = my * sensitivity.value;
	}

	old_mouse_x = mx;
	old_mouse_y = my;


  	/* add mouse X/Y movement to cmd */

	if ( (in_strafe.state & 1) || (lookstrafe.value && (in_mlook.state & 1) ))
	{
		cmd->sidemove += m_side.value * mouse_x;
	}

	else
	{
		cl.viewangles[YAW] -= m_yaw.value * mouse_x;
	}

	if (in_mlook.state & 1)
		V_StopPitchDrift ();
		
	if ( (in_mlook.state & 1) && !(in_strafe.state & 1))
	{
		cl.viewangles[PITCH] += m_pitch.value * mouse_y;

		if (cl.viewangles[PITCH] > 80)
			cl.viewangles[PITCH] = 80;

		if (cl.viewangles[PITCH] < -70)
			cl.viewangles[PITCH] = -70;
	}

	else
	{
		if ((in_strafe.state & 1) && noclip_anglehack)
			cmd->upmove -= m_forward.value * mouse_y;

		else
			cmd->forwardmove -= m_forward.value * mouse_y;
	}
}


