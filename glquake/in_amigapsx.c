// Input-Sub-Module for Joystick

/*
            Portability
            ===========

            This file should compile under all Amiga-based Kernels,
            including WarpUP, 68k and PowerUP. There might be some
            changes in the OS-Includes included needed (for example
            adding inline-includes for EGCS or something like that).

            Steffen Haeuser (MagicSN@Birdland.es.bawue.de)
*/

#include "quakedef.h" 
 
#ifdef __VBCC__
#pragma amiga-align
#elif defined(WARPUP)
#pragma pack(push,2)
#endif

#include <proto/lowlevel.h> 
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h> 
#include <devices/gameport.h> 
#include <libraries/lowlevel.h>
#include <proto/exec.h>
#include <proto/lowlevel.h>
#include "psxport.h" 

#ifdef __VBCC__
#pragma default-align
#elif defined(WARPUP)
#pragma pack(pop)
#endif

extern cvar_t  joy_advanced;
extern cvar_t  joy_forwardthreshold;
extern cvar_t  joy_sidethreshold;
extern cvar_t  joy_pitchthreshold;
extern cvar_t  joy_pitchsensitivity;
extern cvar_t  joy_sensitivity;
 
extern cvar_t  joy_side_reverse;
extern cvar_t  joy_up_reverse;
extern cvar_t  joy_forward_reverse;

extern cvar_t joy_forwardsensitivity;
extern cvar_t joy_sidesensitivity;
cvar_t joy_psxdeadzone={"joy_psxdeadzone","48",true};

int analog_centered=FALSE;
int analog_clx, analog_cly;
int analog_lx, analog_ly;
int analog_crx, analog_cry;
int analog_rx, analog_ry;

struct TheJoy
{
	int mv_a;
	int mv_b;
	int axis;
	int value;
	int nvalue;
};

static struct TheJoy tj;

#ifndef GLQUAKE
extern struct Screen *QuakeScreen;
#else
extern struct Window *QuakeWindow;
#endif

extern struct IntuitionBase *IntuitionBase;

struct GamePortTrigger gameport_gpt =
{
	GPTF_UPKEYS | GPTF_DOWNKEYS,	/* gpt_Keys */
	0,				/* gpt_Timeout */
	1,				/* gpt_XDelta */
 	1				/* gpt_YDelta */
};

static struct InputEvent gameport_ie;
 
int psxport=0; 

static struct MsgPort *gameport_mp = NULL;
static struct IOStdReq *gameport_io = NULL;
static BYTE gameport_ct;
static int gameport_is_open = FALSE;
static int gameport_io_in_progress = FALSE;
static int gameport_curr,gameport_prev;

void ResetPsx(void)
{
	int ix;

//#ifdef __STORM__
	if ((gameport_mp = CreateMsgPort ()) == NULL ||
		(gameport_io = (struct IOStdReq *)CreateIORequest (gameport_mp, sizeof(struct IOStdReq))) == NULL)
/*
#else
	if ((gameport_mp = CreatePort(NULL,0)) == NULL || (gameport_io = CreateStdIO(gameport_mp)) == NULL)
#endif
*/
    	Sys_Error ("Can't create MsgPort or IoRequest");

  	for (ix=0; ix<4; ix++) 
  	{
    		if (OpenDevice ("psxport.device", ix, (struct IORequest *)gameport_io, 0) == 0) 
    		{
      			Con_Printf ("\npsxport.device unit %d opened.\n",ix);
      			gameport_io->io_Command = GPD_ASKCTYPE;
      			gameport_io->io_Length = 1;
      			gameport_io->io_Data = &gameport_ct;
      			DoIO ((struct IORequest *)gameport_io);

      			if (gameport_ct == GPCT_NOCONTROLLER)
			{
				gameport_is_open = TRUE;
				ix=4;
			}

			else
			{
				CloseDevice ((struct IORequest *)gameport_io);
			}
		}
	}

	if (!gameport_is_open) 
	{
		Con_Printf ("No psxport, or no free psx controllers!  Joystick disabled.\n");
	}

	else
	{

		gameport_ct = GPCT_ALLOCATED;
		gameport_io->io_Command = GPD_SETCTYPE;
		gameport_io->io_Length = 1;
		gameport_io->io_Data = &gameport_ct;
		DoIO ((struct IORequest *)gameport_io);

		gameport_io->io_Command = GPD_SETTRIGGER;
		gameport_io->io_Length = sizeof(struct GamePortTrigger);
		gameport_io->io_Data = &gameport_gpt;
		DoIO ((struct IORequest *)gameport_io);

		gameport_io->io_Command = GPD_READEVENT;
		gameport_io->io_Length = sizeof (struct InputEvent);
		gameport_io->io_Data = &gameport_ie;
		SendIO ((struct IORequest *)gameport_io);
		gameport_io_in_progress = TRUE;
	}
}


void IN_InitPsx()
{
	//int numdevs;
	//cvar_t *cv;
	//char strValue[256];
	float f;

	joy_advanced.value=Cvar_VariableValue("joy_advanced");

	if (!Cvar_FindVar("joy_advanced")) joy_advanced.value=0;
	joy_forwardthreshold.value=Cvar_VariableValue("joy_forwardthreshold");
	if (!Cvar_FindVar("joy_forwardthreshold")) joy_forwardthreshold.value=0.15;
	joy_sidethreshold.value=Cvar_VariableValue("joy_sidethreshold");
	if (!Cvar_FindVar("joy_sidethreshold")) joy_sidethreshold.value=0.15;
	joy_pitchthreshold.value=Cvar_VariableValue("joy_pitchthreshold");
	if (!Cvar_FindVar("joy_pitchthreshold")) joy_pitchthreshold.value=0.15;
	joy_pitchsensitivity.value=Cvar_VariableValue("joy_pitchsensitivity");
	if (!Cvar_FindVar("joy_pitchsensitivity")) joy_pitchsensitivity.value=1;
	joy_forward_reverse.value= Cvar_VariableValue("joy_forward_reverse");
	if (!Cvar_FindVar("joy_forward_reverse")) joy_forward_reverse.value=0;
	joy_side_reverse.value = Cvar_VariableValue("joy_side_reverse");
	if (!Cvar_FindVar("joy_side_reverse")) joy_side_reverse.value=0;
	joy_forwardsensitivity.value=Cvar_VariableValue("joy_forwardsensitivity");
	if (!Cvar_FindVar("joy_forwardsensitivity")) joy_forwardsensitivity.value=-1;
	joy_sidesensitivity.value=Cvar_VariableValue("joy_sidesensitivity");
	if (!Cvar_FindVar("joy_sidesensitivity")) joy_sidesensitivity.value=-1;
	joy_psxdeadzone.value=Cvar_VariableValue("joy_psxdeadzone");
	if (!Cvar_FindVar("joy_psxdeadzone")) joy_psxdeadzone.value=48;

	f = Cvar_VariableValue("in_initjoy");

	if (!Cvar_FindVar("in_initjoy")) f=1.0;
	if ( !f) return;
  
	ResetPsx();
	tj.mv_a=32768;
	tj.mv_b=32768;
}
 
void IN_ShutdownPsx(void) 
{
	if (gameport_io_in_progress) 
	{
		AbortIO ((struct IORequest *)gameport_io);
		WaitIO ((struct IORequest *)gameport_io);
		gameport_io_in_progress = FALSE;
		gameport_ct = GPCT_NOCONTROLLER;
		gameport_io->io_Command = GPD_SETCTYPE;
		gameport_io->io_Length = 1;
		gameport_io->io_Data = &gameport_ct;
		DoIO ((struct IORequest *)gameport_io);
	}

	if (gameport_is_open)
	{
		CloseDevice ((struct IORequest *)gameport_io);
		gameport_is_open = FALSE;
	}

	if (gameport_io != NULL)
	{
		#ifdef __STORM__
		DeleteIORequest ((struct IORequest *)gameport_io);
		#else
		//DeleteStdIO(gameport_io);
		DeleteIORequest ((struct IORequest *)gameport_io);
		#endif

		gameport_io = NULL;
	}

	if (gameport_mp != NULL) 
	{
		#ifdef __STORM__
		DeleteMsgPort (gameport_mp);
		#else
		//DeletePort(gameport_mp);
		DeleteMsgPort (gameport_mp);
		#endif

		gameport_mp = NULL;
	}
} 

void RawValuePointerPsx(usercmd_t *cmd)
{
	int x,y;

	#ifndef GLQUAKE
	if (IntuitionBase->FirstScreen != QuakeScreen) return;
	#endif

	if (GetMsg (gameport_mp) != NULL) 
	{
		if ((gameport_ie.ie_Class == PSX_CLASS_JOYPAD) || (gameport_ie.ie_Class == PSX_CLASS_WHEEL)) 
      		analog_centered = false;

		if (PSX_CLASS(gameport_ie) != PSX_CLASS_MOUSE)
		{
			gameport_curr = ~PSX_BUTTONS(gameport_ie);

			if (gameport_curr !=gameport_prev)
			{

				if (gameport_curr & PSX_TRIANGLE)
					tj.value = tj.value|1;

				else if (tj.value&1)
				{
					tj.value=tj.value&~1;
					tj.nvalue|=1;
				}

        if (gameport_curr & PSX_SQUARE) tj.value=tj.value|4;
        else if (tj.value&4)
        {
          tj.value=tj.value&~4;
          tj.nvalue|=4;
        }

        if (gameport_curr & PSX_CIRCLE) tj.value=tj.value|8;
        else if (tj.value&8)
        {
          tj.value=tj.value&~8;
          tj.nvalue|=8;
        }

        if (gameport_curr & PSX_CROSS) tj.value=tj.value|2;
        else if (tj.value&2)
        {
          tj.value=tj.value&~2;
          tj.nvalue|=2;
        }
  
        if (gameport_curr & PSX_LEFT) tj.mv_b=0;
        else if (gameport_curr & PSX_RIGHT) tj.mv_b=65536;
        else tj.mv_b=32768;
       
        if (gameport_curr & PSX_UP) tj.mv_a=0;
        else if (gameport_curr & PSX_DOWN) tj.mv_a=65536;
        else tj.mv_a=32768;

        if (gameport_curr & PSX_START) tj.value=tj.value|64;
        else if (tj.value&64)
        {
          tj.value=tj.value&~64;
          tj.nvalue|=64;
        }

        if (gameport_curr & PSX_SELECT) tj.value=tj.value|128;
        else if (tj.value&128)
        {
          tj.value=tj.value&~128;
          tj.nvalue|=128;
        }

        if (gameport_curr & PSX_R1) tj.value=tj.value|16;
        else if (tj.value&16)
        {
          tj.value=tj.value&~16;
          tj.nvalue|=16;
        }

        if (gameport_curr & PSX_L1) tj.value=tj.value|32;
        else if (tj.value&32)
        {
          tj.value=tj.value&~32;
          tj.nvalue|=32;
        }

        if (gameport_curr & PSX_R2) tj.value=tj.value|256;
        else if (tj.value&256)
        {
          tj.value=tj.value&~256;
          tj.nvalue|=256;
        }

        if (gameport_curr & PSX_L2) tj.value=tj.value|512;
        else if (tj.value&512)
        {
          tj.value=tj.value&~512;
          tj.nvalue|=512;
        }

        gameport_prev = gameport_curr;

      }

    }

    if ((PSX_CLASS(gameport_ie) == PSX_CLASS_ANALOG) 
     || (PSX_CLASS(gameport_ie) == PSX_CLASS_ANALOG2) 
     || (PSX_CLASS(gameport_ie) == PSX_CLASS_ANALOG_MODE2)) 
    {
      analog_lx = PSX_LEFTX(gameport_ie);
      analog_ly = PSX_LEFTY(gameport_ie);
      analog_rx = PSX_RIGHTX(gameport_ie);
      analog_ry = PSX_RIGHTY(gameport_ie);

      if (!analog_centered) 
      {
        analog_clx = analog_lx;
        analog_cly = analog_ly;
        analog_crx = analog_rx;
        analog_cry = analog_ry;
        analog_centered = true;
      }          

      x=analog_lx-analog_clx;
      y=analog_cly-analog_ly;
      if ((abs(x)>joy_psxdeadzone.value) || (abs(y)>joy_psxdeadzone.value))
      {
        x<<=1;
        y<<=1;
        x=x*sensitivity.value;
        y=y*sensitivity.value;
  	if ( (in_strafe.state & 1) || (lookstrafe.value && (in_mlook.state & 1) ))
        cmd->sidemove += m_side.value * x;
	else cl.viewangles[YAW] -= m_yaw.value * x;
	
        if ((in_strafe.state & 1) && noclip_anglehack) cmd->upmove += m_forward.value * y;
        else cmd->forwardmove += m_forward.value * y;
      }
      
      x=analog_rx-analog_crx;
      y=analog_cry-analog_ry;
      if ((abs(x)>joy_psxdeadzone.value) || (abs(y)>joy_psxdeadzone.value))
      {
        x<<=1;
        y<<=1;
        x=x*sensitivity.value;
        y=y*sensitivity.value;
        if ( (in_strafe.state & 1) || (lookstrafe.value && (in_mlook.state & 1) ))
        cmd->sidemove += m_side.value * x;
	else cl.viewangles[YAW] -= m_yaw.value * x;
	
	V_StopPitchDrift ();
		
	cl.viewangles[PITCH] += m_pitch.value * y;
        if (cl.viewangles[PITCH] > 80) cl.viewangles[PITCH] = 80;
        if (cl.viewangles[PITCH] < -70) cl.viewangles[PITCH] = -70;
      }

      if (gameport_curr & PSX_R3) tj.value=tj.value|1024;
      else if (tj.value&1024)
      {
        tj.value=tj.value&~1024;
        tj.nvalue|=1024;
      }

      if (gameport_curr & PSX_L3) tj.value=tj.value|2048;
      else if (tj.value&2048)
      {
        tj.value=tj.value&~2048;
        tj.nvalue|=2048;
      }
    }

    if (gameport_ie.ie_Class == PSX_CLASS_MOUSE) 
    {
      gameport_curr = ~PSX_BUTTONS(gameport_ie);

      if (gameport_curr & PSX_LMB) tj.value = tj.value|1;
      else if (tj.value&1)
      {
        tj.value=tj.value&~1;
        tj.nvalue|=1;
      }

      if (gameport_curr & PSX_RMB) tj.value=tj.value|4;
      else if (tj.value&4)
      {
        tj.value=tj.value&~4;
        tj.nvalue|=4;
      }


      x=PSX_MOUSEDX(gameport_ie);
      y=-PSX_MOUSEDY(gameport_ie);
      x<<=2;
      y<<=2;
      x=x*sensitivity.value;
      y=y*sensitivity.value;
      if ( (in_strafe.state & 1) || (lookstrafe.value && (in_mlook.state & 1) ))
      cmd->sidemove += m_side.value * x;
      else cl.viewangles[YAW] -= m_yaw.value * x;
	
      if (in_mlook.state & 1) V_StopPitchDrift ();
	 
      if ( (in_mlook.state & 1) && !(in_strafe.state & 1)) 
      {
        cl.viewangles[PITCH] += m_pitch.value * y;
        if (cl.viewangles[PITCH] > 80) cl.viewangles[PITCH] = 80;
        if (cl.viewangles[PITCH] < -70) cl.viewangles[PITCH] = -70;
      }
      else 
      {
        if ((in_strafe.state & 1) && noclip_anglehack) cmd->upmove -= m_forward.value * y;
        else cmd->forwardmove -= m_forward.value * y;
      }

    }

    gameport_io->io_Command = GPD_READEVENT;
    gameport_io->io_Length = sizeof (struct InputEvent);
    gameport_io->io_Data = &gameport_ie;
    SendIO ((struct IORequest *)gameport_io);
  }
}
 
void IN_PsxMove (usercmd_t *cmd) 
{ 
  float   speed, aspeed;
  int   fAxisValue;
  int key_index;
  
  if (!gameport_is_open) return;

  if (in_speed.state & 1) speed = cl_movespeedkey.value;
  else speed = 1;
  aspeed = speed * host_frametime;
 
  RawValuePointerPsx(cmd);
  fAxisValue= tj.mv_a;
  fAxisValue -= 32768;
  fAxisValue /= 32768;
 
  cmd->forwardmove += (fAxisValue * joy_forwardsensitivity.value) * speed * cl_forwardspeed.value;

#if 0
  if ((joy_advanced.value == 0.0) && (in_mlook.state & 1))
  {
    if (fabs(fAxisValue) > joy_pitchthreshold.value)
    {
      if (m_pitch.value < 0.0) cl.viewangles[PITCH] -= (fAxisValue * joy_pitchsensitivity.value) * aspeed * cl_pitchspeed.value;
      else cl.viewangles[PITCH] += (fAxisValue * joy_pitchsensitivity.value) * aspeed * cl_pitchspeed.value;
    }
  }
  else cmd->forwardmove += (fAxisValue * joy_forwardsensitivity.value) * speed * cl_forwardspeed.value;
#endif
 
  fAxisValue = tj.mv_b;
  fAxisValue -= 32768;
  fAxisValue /= 32768;
  cl.viewangles[YAW] -= (fAxisValue * 0.5 * joy_pitchsensitivity.value) * aspeed * cl_pitchspeed.value;

  if (tj.value&1)
  {
    key_index = K_JOY1;
    Key_Event(key_index+3,true, -1);
  }
  else
  {
    if (tj.nvalue&1)
    {
      key_index = K_JOY1;
      Key_Event(key_index+3,false, -1);
    }
  }
  if (tj.value&2)
  {
    key_index = K_JOY1;
    Key_Event(key_index,true, -1);
  }
  else
  {
    if (tj.nvalue&2)
    {
      key_index = K_JOY1;
      Key_Event(key_index,false, -1);
    }
  }
  if (tj.value&4)
  {
    key_index = K_JOY1;
    Key_Event(key_index+2,true, -1);
  }
  else
  {
    if (tj.nvalue&4)
    {
      key_index = K_JOY1;
      Key_Event(key_index+2,false, -1);
    }
  }
  if (tj.value&8)
  {
    key_index = K_JOY1;
    Key_Event(key_index+1,true, -1);
  }
  else
  {
    if (tj.nvalue&8)
    {
      key_index = K_JOY1;
      Key_Event(key_index+1,false, -1);
    }
  }
  if (tj.value&16) cmd->sidemove -= (joy_sidesensitivity.value) * speed * cl_sidespeed.value;
  if (tj.value&32) cmd->sidemove += (joy_sidesensitivity.value) * speed * cl_sidespeed.value;

  if (tj.value&64)
  {
    key_index = K_JOY1;
    Key_Event(key_index+4,true, -1);
  }
  else
  {
    if (tj.nvalue&64)
    {
      key_index=K_JOY1;
      Key_Event(key_index+4,false, -1);
    }
  }
  if (tj.value&128)
  {
    key_index = K_JOY1;
    Key_Event(key_index+5,true, -1);
  }
  else
  {
    if (tj.nvalue&128)
    {
      key_index=K_JOY1;
      Key_Event(key_index+5,false, -1);
    }
  }
  if (tj.value&256)
  {
    key_index = K_JOY1;
    Key_Event(key_index+6,true, -1);
  }
  else
  {
    if (tj.nvalue&256)
    {
      key_index=K_JOY1;
      Key_Event(key_index+6,false, -1);
    }
  }
  if (tj.value&512)
  {
    key_index = K_JOY1;
    Key_Event(key_index+7,true, -1);
  }
  else
  {
    if (tj.nvalue&512)
    {
      key_index=K_JOY1;
      Key_Event(key_index+7,false, -1);
    }
  }
  if (tj.value&1024)
  {
    key_index = K_JOY1;
    Key_Event(key_index+8,true, -1);
  }
  else
  {
    if (tj.nvalue&1024)
    {
      key_index=K_JOY1;
      Key_Event(key_index+8,false, -1);
    }
  }
  if (tj.value&2048)
  {
    key_index = K_JOY1;
    Key_Event(key_index+9,true, -1);
  }
  else
  {
    if (tj.nvalue&2048)
    {
      key_index=K_JOY1;
      Key_Event(key_index+9,false, -1);
    }
  }
} 
