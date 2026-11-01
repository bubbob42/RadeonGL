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

#include <libraries/lowlevel.h> 
#include <proto/exec.h> 
#include <proto/lowlevel.h> 
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h> 

#ifdef __VBCC__
#pragma default-align
#elif defined(WARPUP)
#pragma pack(pop)
#endif
 
cvar_t  joy_advanced={"joy_advanced","0",true};
cvar_t  joy_forwardthreshold={"joy_forwardthreshold","0.15",true};
cvar_t  joy_sidethreshold={"joy_sidethreshold","0.15",true};
cvar_t  joy_pitchthreshold={"joy_pitchthreshold","0.15",true};
cvar_t  joy_pitchsensitivity={"joy_pitchsensitivity","1",true};
cvar_t  joy_yawsensitivity={"joy_yawsensitivity","-1",true};
 
cvar_t  joy_side_reverse={"joy_side_reverse","0",true};
cvar_t  joy_up_reverse={"joy_up_reverse","0",true};
cvar_t  joy_forward_reverse={"joy_forward_reverse","0",true};

cvar_t joy_forwardsensitivity={"joy_forwardsensitivity","-1",true};
cvar_t joy_sidesensitivity={"joy_sidesensitivity","-1",true};

cvar_t joy_force0={"joy_force0","AUTOSENSE",true};
cvar_t joy_force1={"joy_force1","AUTOSENSE",true};
cvar_t joy_force2={"joy_force2","AUTOSENSE",true};
cvar_t joy_force3={"joy_force3","AUTOSENSE",true};
cvar_t joy_onlypsx={"joy_onlypsx","0",true};

extern int psxused;

struct TheJoy
{
	int mv_a;
	int mv_b;
	int axis;
	int value;
	int nvalue;
};
 
struct JoyType
{ 
	char *Name;
	ULONG Type;
}; 
 

static struct TheJoy tj;
static int ack0 = -1, ack1 = -1;
static int Port;
static int JoyState[4];
static int stick=1;

#ifndef GLQUAKE
extern struct Screen *QuakeScreen;
#endif

extern struct IntuitionBase *IntuitionBase;

static struct JoyType jt[] =
{ 
	{"GAMECTRL", SJA_TYPE_GAMECTLR},
	{"MOUSE",    SJA_TYPE_MOUSE},
	{"JOYSTICK", SJA_TYPE_JOYSTK},
	{"AUTOSENSE",SJA_TYPE_AUTOSENSE},
	{NULL,       0},
}; 
 

 
int StringToType(char *type)
{ 
	int i = 0;

	while (i<4)
	{
		if (0 == Q_strcasecmp(type, jt[i].Name))
			return jt[i].Type;

		i++;
	}

	return SJA_TYPE_AUTOSENSE;
} 

void ResetJoystick(void)
{ 
	int Port;

	for (Port = 0; Port < 4; ++Port)
	{
		JoyState[Port] = (ULONG)JP_TYPE_NOTAVAIL;
		SetJoyPortAttrs(Port, SJA_Type, SJA_TYPE_AUTOSENSE, TAG_DONE);
	}
} 


void IN_InitJoy()
{
  int numdevs;
  cvar_t *cv;
  char strValue[256];
  float f;

  f = Cvar_VariableValue("in_initjoy");
  if (!Cvar_FindVar("in_initjoy")) f=1.0;
  if ( !f) return;
  
  if (joy_onlypsx.value) return;
  
  stick=1;

  numdevs=-1;
  if (LowLevelBase)
  {
    int MyPort=0;
    ResetJoystick();
    for (MyPort=0;MyPort<4;MyPort++)
    {
      JoyState[MyPort] = (int)(ReadJoyPort(MyPort));
      if ((JoyState[MyPort]!=JP_TYPE_NOTAVAIL)&&(!(JoyState[MyPort]&JP_TYPE_MOUSE)))
      {
        numdevs=MyPort;
        break;
      }
    }
  }
  tj.mv_a=32768;
  tj.mv_b=32768;

  if (numdevs == -1)
    return;
  else Port=numdevs;

  Con_Printf ("\nAmiga joystick available\n");
  strcpy(strValue,Cvar_VariableString("joy_force0"));
  if (strlen(strValue)==0) strcpy(strValue,"AUTOSENSE");
  SetJoyPortAttrs(0, SJA_Type,   StringToType(strValue), TAG_DONE);
  Con_Printf("Joystick Port 0 is %s\n", strValue);

  strcpy(strValue,Cvar_VariableString("joy_force1"));
  if (strlen(strValue)==0) strcpy(strValue,"AUTOSENSE");
  SetJoyPortAttrs(1, SJA_Type,   StringToType(strValue), TAG_DONE);
  Con_Printf("Joystick Port 1 is %s\n", strValue);

  strcpy(strValue,Cvar_VariableString("joy_force2"));
  if (strlen(strValue)==0) strcpy(strValue,"AUTOSENSE");
  SetJoyPortAttrs(2, SJA_Type,   StringToType(strValue), TAG_DONE);
  Con_Printf("Joystick Port 2 is %s\n", strValue);

  strcpy(strValue,Cvar_VariableString("joy_force3"));
  if (strlen(strValue)==0) strcpy(strValue,"AUTOSENSE");
  SetJoyPortAttrs(3, SJA_Type,   StringToType(strValue), TAG_DONE);
  Con_Printf("Joystick Port 3 is %s\n", strValue);
}
 
void IN_ShutdownJoystick(void) 
{ 
  if (LowLevelBase)
  {
    if (!ack1) SystemControl(SCON_RemCreateKeys,1,TAG_END);
    if (!ack0) SystemControl(SCON_RemCreateKeys,0,TAG_END);
    ResetJoystick();
  }
} 

void RawValuePointer()
{ 
  int button=0;
  int dir=0;
  int joytype;

#ifndef GLQUAKE
  if (IntuitionBase->FirstScreen != QuakeScreen) return;
#endif
 
  if ( ( JoyState[ Port ] = ReadJoyPort(Port)) != JP_TYPE_NOTAVAIL )
  {
    button=(JoyState[ Port ]) & (JP_BUTTON_MASK);
    dir = (JoyState[ Port ]) & (JP_DIRECTION_MASK);
    tj.nvalue=0;
    joytype=JoyState[Port]&JP_TYPE_MASK;
    if ((joytype&JP_TYPE_JOYSTK)&&(joytype&JP_TYPE_GAMECTLR)) joytype=JP_TYPE_GAMECTLR;
    if (((joytype&JP_TYPE_JOYSTK)||(joytype&JP_TYPE_GAMECTLR))==0) joytype=JP_TYPE_JOYSTK;
    switch(joytype)
    {
      case JP_TYPE_GAMECTLR:
      {
        if (stick) stick=0;
        if (button&JPF_BUTTON_RED) tj.value=tj.value|1;
        else if (tj.value&1)
        {
          tj.value=tj.value&~1;
          tj.nvalue|=1;
        }
        if (button&JPF_BUTTON_YELLOW) tj.value=tj.value|4;
        else if (tj.value&4)
        {
          tj.value=tj.value&~4;
          tj.nvalue|=4;
        }
        if (button&JPF_BUTTON_GREEN) tj.value=tj.value|8;
        else if (tj.value&8)
        {
          tj.value=tj.value&~8;
          tj.nvalue|=8;
        }
        if (button&JPF_BUTTON_BLUE) tj.value=tj.value|2;
        else if (tj.value&2)
        {
          tj.value=tj.value&~2;
          tj.nvalue|=2;
        }
        if (button&JPF_BUTTON_FORWARD) tj.value=tj.value|16;
        else if (tj.value&16)
        {
          tj.value=tj.value&~16;
          tj.nvalue|=16;
        }
        if (button&JPF_BUTTON_REVERSE) tj.value=tj.value|32;
        else if (tj.value&32)
        {
          tj.value=tj.value&~32;
          tj.nvalue|=32;
        }
        if (button&JPF_BUTTON_PLAY) tj.value=tj.value|64;
        else if (tj.value&64)
        {
          tj.value=tj.value&~64;
          tj.nvalue|=64;
        }
        if (dir&JPF_JOY_UP) tj.mv_a=0;
        else if (dir&JPF_JOY_DOWN) tj.mv_a=65536;
        else tj.mv_a=32768;
        if (dir&JPF_JOY_LEFT) tj.mv_b=0;
        else if (dir&JPF_JOY_RIGHT) tj.mv_b=65536;
        else tj.mv_b=32768;
      }
      break;
      case JP_TYPE_JOYSTK :
      {
        if (button&JPF_BUTTON_RED) tj.value=tj.value|1;
        else if (tj.value&1)
        {
          tj.value=tj.value&~1;
          tj.nvalue|=1;
        }
        if (dir&JPF_JOY_UP) tj.mv_a=0;
        else if (dir&JPF_JOY_DOWN) tj.mv_a=65536;
        else tj.mv_a=32768;
        if (dir&JPF_JOY_LEFT) tj.mv_b=0;
        else if (dir&JPF_JOY_RIGHT) tj.mv_b=65536;
        else tj.mv_b=32768;
      }
      break;
    }
  }
}
 
void IN_JoyMove (usercmd_t *cmd) 
{ 
  float   speed, aspeed;
  int   fAxisValue;
  int key_index;
 
  if (!LowLevelBase) return;
  if (joy_onlypsx.value) return;
  
  if (in_speed.state & 1) speed = cl_movespeedkey.value;
  else speed = 1;
  aspeed = speed * host_frametime;
 
  RawValuePointer();
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
  if (stick) return;
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
} 
