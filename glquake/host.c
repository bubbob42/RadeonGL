/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// host.c -- coordinates spawning and killing of local servers

#include "quakedef.h"
#include "r_local.h"
#include <stdlib.h>

/*

A server can allways be started, even if the system started out as a client
to a remote system.

A client can NOT be started if the system started as a dedicated server.

Memory is cleared / released when a server or client begins, not when they end.

*/

int		fps_count;	//surgeon: for scr_drawfps 
quakeparms_t	host_parms;

qboolean  host_initialized;	// true if into command execution

double	host_frametime;
double	host_time;
double	realtime;		// without any filtering or bounding
double	oldrealtime;		// last frame run
double	last_angle_time;	// JPG - for smooth chasecam
int	host_framecount;

int	host_hunklevel;

int	minimum_memory;

client_t *host_client;		// current client

jmp_buf host_abortserver;

byte	*host_basepal;
byte	*host_colormap;

cvar_t	host_framerate = {"host_framerate","0"};// set for slow motion
cvar_t	host_speeds = {"host_speeds","0"};	// set for running times
cvar_t	max_edicts = {"max_edicts", "8192"}; // johnfitz - no more CVAR_ARCHIVE - Cowcat 2019

cvar_t	sys_ticrate = {"sys_ticrate","0.05"};
cvar_t	serverprofile = {"serverprofile","0"};

cvar_t	fraglimit = {"fraglimit","0",false,true};
cvar_t	timelimit = {"timelimit","0",false,true};
cvar_t	teamplay = {"teamplay","0",false,true};

cvar_t	samelevel = {"samelevel","0"};
cvar_t	noexit = {"noexit","0",false,true};

#ifdef QUAKE2
cvar_t	developer = {"developer","1"};		// should be 0 for release!
#else
cvar_t	developer = {"developer","0", true}; // now true - Cowcat 2019
#endif

cvar_t	skill = {"skill","1"};			// 0 - 3
cvar_t	deathmatch = {"deathmatch","0"};	// 0, 1, or 2
cvar_t	coop = {"coop","0"};			// 0 or 1
cvar_t	pausable = {"pausable","1"};
cvar_t	temp1 = {"temp1","0"};
cvar_t	proquake = {"proquake", "L33T"};	// JPG - added this

cvar_t	r_bobitems = {"r_bobitems", "0", true}; // Cowcat

#ifndef GLQUAKE
extern cvar_t vid_mode;
extern cvar_t _vid_default_mode;
#endif

extern cvar_t joy_advanced;
extern cvar_t joy_forwardthreshold;
extern cvar_t joy_sidethreshold;
extern cvar_t joy_pitchthreshold;
extern cvar_t joy_pitchsensitivity;
extern cvar_t joy_yawsensitivity;
 
extern cvar_t joy_side_reverse;
extern cvar_t joy_up_reverse;
extern cvar_t joy_forward_reverse;

extern cvar_t joy_forwardsensitivity;
extern cvar_t joy_sidesensitivity;

extern cvar_t joy_force0;
extern cvar_t joy_force1;
extern cvar_t joy_force2;
extern cvar_t joy_force3;
extern cvar_t joy_onlypsx;
extern cvar_t cl_name;
extern cvar_t cl_color;
extern cvar_t cl_upspeed;
extern cvar_t cl_forwardspeed;
extern cvar_t cl_sidespeed;
extern cvar_t cl_backspeed;
extern cvar_t cl_movespeedkey;
extern cvar_t cl_yawspeed;
extern cvar_t cl_pitchspeed;
extern cvar_t cl_anglespeedkey;
extern cvar_t lookstrafe;
extern cvar_t lookspring;
extern cvar_t sensitivity;
extern cvar_t m_pitch;
extern cvar_t m_forward;
extern cvar_t m_yaw;
extern cvar_t m_side;
extern cvar_t _snd_mixahead;
extern cvar_t bgmvolume;
extern cvar_t volume;
extern cvar_t scr_viewsize;
extern cvar_t m_filter;
extern cvar_t m_speed;

//2016 - Cowcat
extern cvar_t gl_fog;
extern cvar_t gl_fogstart;
extern cvar_t gl_fogend;
extern cvar_t gl_fogred;
extern cvar_t gl_foggreen;
extern cvar_t gl_fogblue;
extern cvar_t gl_fogdensity;
extern cvar_t gl_classic_particles;
extern cvar_t cl_gunfov;
extern cvar_t scr_fov;
extern cvar_t shadowalpha;
//2017
extern cvar_t r_wateralpha;
extern cvar_t r_waterwarp;
extern cvar_t gl_conalpha;
extern cvar_t gl_conanim;
extern cvar_t r_noshadow_list;
extern cvar_t r_fullbright_list;

//cvar_t r_lerpmodels = {"r_lerpmodels","1"};	// johnfitz - not used - Cowcat
//cvar_t r_lerpmove   = {"r_lerpmove","1"};	//

//2019
extern cvar_t r_interpolations;
extern cvar_t drawfps;
extern cvar_t gl_farclip;
extern cvar_t gl_gamma;

#if 1
//JohnFitz
void Max_Edicts_f(void)
{
	//static float oldval = 1024;

	//if(max_edicts.value == oldval)
		//return;

	if(cls.state == ca_connected || sv.active)
		Con_Printf("Changes will not take effect until the next level load\n");

	//oldval = max_edicts.value;
}
#endif

/*
================
Host_EndGame
================
*/
#if defined(__GNUC__)
__attribute__((noinline))
#endif
void Host_EndGame (char *message, ...)
{
	/* noinline (GCC-only): see common.c's va() for the root-cause trace. */
	va_list argptr;
	char	string[1024];
  
	va_start (argptr,message);
	vsprintf (string,message,argptr);
	va_end (argptr);

	Con_DPrintf ("Host_EndGame: %s\n",string);
  
	if (sv.active)
		Host_ShutdownServer (false);

	if (cls.state == ca_dedicated)
		Sys_Error ("Host_EndGame: %s\n",string);  // dedicated servers exit
  
	if (cls.demonum != -1)
		CL_NextDemo ();

	else
		CL_Disconnect ();

	longjmp (host_abortserver, 1);
}

/*
================
Host_Error

This shuts down both the client and server
================
*/
#if defined(__GNUC__)
__attribute__((noinline))
#endif
void Host_Error (char *error, ...)
{
	/* noinline (GCC-only): see common.c's va() for the root-cause trace. */
	va_list argptr;
	char	string[1024];
	static	qboolean inerror = false;
  
	if (inerror)
		Sys_Error ("Host_Error: recursively entered");

	inerror = true;
  
	SCR_EndLoadingPlaque ();    // reenable screen updates

	va_start (argptr,error);
	vsprintf (string,error,argptr);
	va_end (argptr);

	Con_Printf ("Host_Error: %s\n",string);
  
	if (sv.active)
		Host_ShutdownServer (false);

	if (cls.state == ca_dedicated)
		Sys_Error ("Host_Error: %s\n",string);	// dedicated servers exit

	CL_Disconnect ();
	cls.demonum = -1;

	inerror = false;

	longjmp (host_abortserver, 1);
}

/*
================
Host_FindMaxClients
================
*/
void Host_FindMaxClients (void)
{
	int   i;

	svs.maxclients = 1;
    
	i = COM_CheckParm ("-dedicated");

	if (i)
	{
		cls.state = ca_dedicated;

		if (i != (com_argc - 1))
		{
			svs.maxclients = Q_atoi (com_argv[i+1]);
		}

		else
			svs.maxclients = 8;
	}

	else
		cls.state = ca_disconnected;

	i = COM_CheckParm ("-listen");

	if (i)
	{
		if (cls.state == ca_dedicated)
			Sys_Error ("Only one of -dedicated or -listen can be specified");

		if (i != (com_argc - 1))
			svs.maxclients = Q_atoi (com_argv[i+1]);

		else
			svs.maxclients = 8;
	}

	if (svs.maxclients < 1)
		svs.maxclients = 8;

	else if (svs.maxclients > MAX_SCOREBOARD)
		svs.maxclients = MAX_SCOREBOARD;

	svs.maxclientslimit = svs.maxclients;

	if (svs.maxclientslimit < 4)
		svs.maxclientslimit = 4;

	svs.clients = Hunk_AllocName (svs.maxclientslimit*sizeof(client_t), "clients");

	if (svs.maxclients > 1)
		Cvar_SetValue ("deathmatch", 1.0);

	else
		Cvar_SetValue ("deathmatch", 0.0);
}


/*
=======================
Host_InitLocal
======================
*/
void Host_InitLocal (void)
{
	Host_InitCommands ();
  
	Cvar_RegisterVariable (&host_framerate);
	Cvar_RegisterVariable (&host_speeds);
	Cvar_RegisterVariable (&sys_ticrate);
	Cvar_RegisterVariable (&serverprofile);
	Cvar_RegisterVariable (&fraglimit);
	Cvar_RegisterVariable (&timelimit);
	Cvar_RegisterVariable (&teamplay);
	Cvar_RegisterVariable (&samelevel);
	Cvar_RegisterVariable (&noexit);
	Cvar_RegisterVariable (&skill);
	Cvar_RegisterVariable (&developer);
	Cvar_RegisterVariable (&deathmatch);
	Cvar_RegisterVariable (&coop);
	Cvar_RegisterVariable (&pausable);
	Cvar_RegisterVariable (&temp1);
	Cvar_RegisterVariable (&proquake); // JPG - added this so QuakeC can find it
	
	Cvar_RegisterVariable (&max_edicts); // johnfitz
	//Cvar_SetCallback(&max_edicts, Max_Edicts_f);

	Host_FindMaxClients ();
  
	host_time = 1.0;	// so a think at time 0 won't get called
	last_angle_time = 0.0;	// JPG - smooth chasecam
}


/*
===============
Host_WriteConfiguration

Writes key bindings and archived cvars to config.cfg
===============
*/
void Host_WriteConfiguration (void)
{
	FILE  *f;

	// dedicated servers initialize the host but don't parse and set the
	// config.cfg cvars

	if (host_initialized && !isDedicated) // added +& - Cowcat 2019
	{
		f = fopen (va("%s/config.cfg",com_gamedir), "w");

		if (!f)
		{
			Con_Printf ("Couldn't write config.cfg.\n");
			return;
		}
    
		Key_WriteBindings (f);
		Cvar_WriteVariables (f);

		fclose (f);
	}
}


/*
=================
SV_ClientPrintf

Sends text across to be displayed 
FIXME: make this just a stuffed echo?
=================
*/
#if defined(__GNUC__)
__attribute__((noinline))
#endif
void SV_ClientPrintf (char *fmt, ...)
{
	/* noinline (GCC-only): see common.c's va() for the root-cause trace. */
	va_list argptr;
	char	string[1024];

	va_start (argptr,fmt);
	vsprintf (string, fmt,argptr);
	va_end (argptr);

	MSG_WriteByte (&host_client->message, svc_print);
	MSG_WriteString (&host_client->message, string);
}

/*
=================
SV_BroadcastPrintf

Sends text to all active clients
=================
*/
#if defined(__GNUC__)
__attribute__((noinline))
#endif
void SV_BroadcastPrintf (char *fmt, ...)
{
	/* noinline (GCC-only): see common.c's va() for the root-cause trace. */
	va_list argptr;
	char	string[1024];
	int	i;
  
	va_start (argptr,fmt);
	vsprintf (string, fmt,argptr);
	va_end (argptr);
  
	for (i=0 ; i<svs.maxclients ; i++)
		if (svs.clients[i].active && svs.clients[i].spawned)
		{
			MSG_WriteByte (&svs.clients[i].message, svc_print);
			MSG_WriteString (&svs.clients[i].message, string);
		}
}

/*
=================
Host_ClientCommands

Send text over to the client to be executed
=================
*/
#if defined(__GNUC__)
__attribute__((noinline))
#endif
void Host_ClientCommands (char *fmt, ...)
{
	/* noinline (GCC-only): see common.c's va() for the root-cause trace. */
	va_list argptr;
	char	string[1024];

	va_start (argptr,fmt);
	vsprintf (string, fmt,argptr);
	va_end (argptr);
  
	MSG_WriteByte (&host_client->message, svc_stufftext);
	MSG_WriteString (&host_client->message, string);
}

/*
=====================
SV_DropClient

Called when the player is getting totally kicked off the host
if (crash = true), don't bother sending signofs
=====================
*/
void SV_DropClient (qboolean crash)
{
	int	 saveSelf;
	int	 i;
	client_t *client;

	if (!crash)
	{
		// send any final messages (don't check for errors)
		if (NET_CanSendMessage (host_client->netconnection))
		{
			MSG_WriteByte (&host_client->message, svc_disconnect);
			NET_SendMessage (host_client->netconnection, &host_client->message);
		}
  
		if (host_client->edict && host_client->spawned)
		{
			// call the prog function for removing a client
			// this will set the body to a dead frame, among other things

			saveSelf = pr_global_struct->self;
			pr_global_struct->self = EDICT_TO_PROG(host_client->edict);
			PR_ExecuteProgram (pr_global_struct->ClientDisconnect);
			pr_global_struct->self = saveSelf;
		}

		Sys_Printf ("Client %s removed\n", host_client->name);
	}

	// break the net connection
	NET_Close (host_client->netconnection);
	host_client->netconnection = NULL;

	// free the client (the body stays around)
	host_client->active = false;
	host_client->name[0] = 0;
	host_client->old_frags = -999999;
	net_activeconnections--;

	// send notification to all clients
	for (i=0, client = svs.clients ; i<svs.maxclients ; i++, client++)
	{
		if (!client->active)
			continue;

		MSG_WriteByte (&client->message, svc_updatename);
		MSG_WriteByte (&client->message, host_client - svs.clients);
		MSG_WriteString (&client->message, "");
		MSG_WriteByte (&client->message, svc_updatefrags);
		MSG_WriteByte (&client->message, host_client - svs.clients);
		MSG_WriteShort (&client->message, 0);
		MSG_WriteByte (&client->message, svc_updatecolors);
		MSG_WriteByte (&client->message, host_client - svs.clients);
		MSG_WriteByte (&client->message, 0);
	}
}

/*
==================
Host_ShutdownServer

This only happens at the end of a game, not between levels
==================
*/
void Host_ShutdownServer(qboolean crash)
{
	int		i;
	int		count;
	sizebuf_t	buf;
	byte		message[4]; // was char - Cowcat 2019
	double		start;

	if (!sv.active)
		return;

	sv.active = false;

	// stop all client sounds immediately
	if (cls.state == ca_connected)
		CL_Disconnect ();

	// flush any pending messages - like the score!!!
	start = Sys_FloatTime();

	do
	{
		count = 0;

		for (i=0, host_client = svs.clients ; i<svs.maxclients ; i++, host_client++)
		{
			if (host_client->active && host_client->message.cursize)
			{
				if (NET_CanSendMessage (host_client->netconnection))
				{
					NET_SendMessage(host_client->netconnection, &host_client->message);
					SZ_Clear (&host_client->message);
				}

				else
				{
					NET_GetMessage(host_client->netconnection);
					count++;
				}
			}
		}

		if ((Sys_FloatTime() - start) > 3.0)
			break;

	} while (count);

	// make sure all the clients know we're disconnecting
	buf.data = message;
	buf.maxsize = 4;
	buf.cursize = 0;
	MSG_WriteByte(&buf, svc_disconnect);
	count = NET_SendToAll(&buf, 5);

	if (count)
		Con_Printf("Host_ShutdownServer: NET_SendToAll failed for %u clients\n", count);

	for (i=0, host_client = svs.clients ; i<svs.maxclients ; i++, host_client++)
		if (host_client->active)
			SV_DropClient(crash);

	//
	// clear structures
	//

	//memset (&sv, 0, sizeof(sv)); // ServerSpawn already do this by Host_ClearMemory
	memset (svs.clients, 0, svs.maxclientslimit*sizeof(client_t));
}


/*
================
Host_ClearMemory

This clears all the memory used by both the client and server, but does
not reinitialize anything.
================
*/
void Host_ClearMemory (void)
{
	Con_DPrintf ("Clearing memory\n");
	D_FlushCaches ();
	Mod_ClearAll ();

	//if (host_hunklevel) // disabled - cowcat 2019
	Hunk_FreeToLowMark (host_hunklevel);

	cls.signon = 0;

	//ericw malloc 
	//

	//if(sv.edicts)
	free(sv.edicts); // only free it - Cowcat

	memset (&sv, 0, sizeof(sv));
	memset (&cl, 0, sizeof(cl));
}


//============================================================================


/*
===================
Host_FilterTime

Returns false if the time is too short to run a frame
===================
*/
qboolean Host_FilterTime (float time)
{
	realtime += time;

	if (!cls.timedemo && realtime - oldrealtime < 1.0/72.0)
		return false;	// framerate is too high

	host_frametime = realtime - oldrealtime;
	oldrealtime = realtime;

	if (host_framerate.value > 0)
		host_frametime = host_framerate.value;

	else
	{	// don't allow really long or short frames
		if (host_frametime > 0.1)
			host_frametime = 0.1;

		if (host_frametime < 0.001)
			host_frametime = 0.001;
	}
  
	return true;
}


/*
===================
Host_GetConsoleCommands

Add them exactly as if they had been typed at the console
===================
*/
void Host_GetConsoleCommands (void)
{
	char  *cmd;

	while (1)
	{
		cmd = Sys_ConsoleInput ();

		if (!cmd)
			break;

		Cbuf_AddText (cmd);
	}
}


/*
==================
Host_ServerFrame

==================
*/
#ifdef FPS_20

void _Host_ServerFrame (void)
{
	// run the world state	
	pr_global_struct->frametime = host_frametime;

	// read client messages
	SV_RunClients ();
  
	// move things around and think
	// always pause in single player if in console or menus
	if (!sv.paused && (svs.maxclients > 1 || key_dest == key_game) )
		SV_Physics ();
}

void Host_ServerFrame (void)
{
	float save_host_frametime;
	float temp_host_frametime;
	
	// run the world state	
	pr_global_struct->frametime = host_frametime;

	// set the time and clear the general datagram
	SV_ClearDatagram ();
  
	// check for new clients
	SV_CheckForNewClients ();

	temp_host_frametime = save_host_frametime = host_frametime;

	while(temp_host_frametime > (1.0/72.0))
	{
		if (temp_host_frametime > 0.05)
			host_frametime = 0.05;

		else
			host_frametime = temp_host_frametime;

		temp_host_frametime -= host_frametime;
		_Host_ServerFrame ();
	}

	host_frametime = save_host_frametime;

	// send all messages to the clients
	SV_SendClientMessages ();
}

#else

void Host_ServerFrame (void)
{
	// run the world state	
	pr_global_struct->frametime = host_frametime;

	// set the time and clear the general datagram
	SV_ClearDatagram ();
  
	// check for new clients
	SV_CheckForNewClients ();

	// read client messages
	SV_RunClients ();
  
	// move things around and think
	// always pause in single player if in console or menus
	if (!sv.paused && (svs.maxclients > 1 || key_dest == key_game) )
		SV_Physics ();

	// send all messages to the clients
	SV_SendClientMessages ();
}

#endif


/*
==================
Host_Frame

Runs all active servers
==================
*/

extern void CL_RunParticles(void);

void _Host_Frame (float time)
{
	static double	time1 = 0;
	static double	time2 = 0;
	static double	time3 = 0;
	int		pass1, pass2, pass3;

	if (setjmp (host_abortserver) )
		return;	    // something bad happened, or the server disconnected

	// keep the random time dependent
	rand ();
  
	// decide the simulation time
	if (!Host_FilterTime (time))
	{
		#if 0 // disabled - Cowcat 2019
		// JPG - if we're not doing a frame, still check for lagged moves to send
		if (!sv.active && (cl.movemessages > 2))
			CL_SendLagMove();
		#endif

		return;	    // don't run too fast, or packets will flood out
	}
    
	// get new key events
	Sys_SendKeyEvents ();

	// allow mice or other external controllers to add commands
	IN_Commands ();

	// process console commands
	Cbuf_Execute ();

	NET_Poll();

	// if running the server locally, make intentions now
	if (sv.active)
		CL_SendCmd ();
  
	//-------------------
	//
	// server operations
	//
	//-------------------

	// check for commands typed to the host
	Host_GetConsoleCommands ();
  
	if (sv.active)
		Host_ServerFrame ();

	//-------------------
	//
	// client operations
	//
	//-------------------

	// if running the server remotely, send intentions now after
	// the incoming messages have been read
	if (!sv.active)
		CL_SendCmd ();

	host_time += host_frametime;

	// fetch results from server
	if (cls.state == ca_connected)
	{
		CL_ReadFromServer ();
	}

	// update video
	if (host_speeds.value)
		time1 = Sys_FloatTime ();

#ifndef MGLV3D_FRAME_EXITCOUNT
#define MGLV3D_FRAME_EXITCOUNT 20
#endif
	{
		static int mglv3d_framecounter = 0;
		mglv3d_framecounter++;

#if defined(MGLV3D_EXITPOINT) && MGLV3D_EXITPOINT == 30
		if (mglv3d_framecounter >= MGLV3D_FRAME_EXITCOUNT)
		{
			Host_Shutdown(); exit(0);
		}
#endif

		SCR_UpdateScreen ();

#if defined(MGLV3D_EXITPOINT) && MGLV3D_EXITPOINT == 31
		if (mglv3d_framecounter >= MGLV3D_FRAME_EXITCOUNT)
		{
			Host_Shutdown(); exit(0);
		}
#endif

		//if(gl_classic_particles.value) // Cowcat
		CL_RunParticles();	// r_part.c (depending on version - Johnfitz)

		if (host_speeds.value)
			time2 = Sys_FloatTime ();

		// update audio
		if (cls.signon == SIGNONS)
		{
			S_Update (r_origin, vpn, vright, vup);
			CL_DecayLights ();
		}

		else
			S_Update (vec3_origin, vec3_origin, vec3_origin, vec3_origin);

		CDAudio_Update();

#if defined(MGLV3D_EXITPOINT) && MGLV3D_EXITPOINT == 32
		if (mglv3d_framecounter >= MGLV3D_FRAME_EXITCOUNT)
		{
			Host_Shutdown(); exit(0);
		}
#endif
	}

	if (host_speeds.value)
	{
		pass1 = (time1 - time3)*1000;
		time3 = Sys_FloatTime ();
		pass2 = (time2 - time1)*1000;
		pass3 = (time3 - time2)*1000;

		Con_Printf ("%3i tot %3i server %3i gfx %3i snd\n", pass1+pass2+pass3, pass1, pass2, pass3);
	}
  
	host_framecount++;
	fps_count++;
}

void Host_Frame (float time)
{
	double	time1, time2;
	static	double timetotal;
	static	int    timecount;
	int	i, c, m;

	if (!serverprofile.value)
	{
		_Host_Frame (time);
		return;
	}
  
	time1 = Sys_FloatTime ();
	_Host_Frame (time);
	time2 = Sys_FloatTime (); 
  
	timetotal += time2 - time1;
	timecount++;
	
	if (timecount < 1000)
		return;

	m = timetotal*1000/timecount;
	timecount = 0;
	timetotal = 0;
	c = 0;

	for (i=0 ; i<svs.maxclients ; i++)
	{
		if (svs.clients[i].active)
			c++;
	}

	Con_Printf ("serverprofile: %2i clients %2i msec\n",  c,  m);
}

//============================================================================


extern int vcrFile;
#define VCR_SIGNATURE 0x56435231
// "VCR1"

void Host_InitVCR (quakeparms_t *parms)
{
	int   i, len, n;
	char  *p;
  
	if (COM_CheckParm("-playback"))
	{
		if (com_argc != 2)
			Sys_Error("No other parameters allowed with -playback\n");

		Sys_FileOpenRead("quake.vcr", &vcrFile);

		if (vcrFile == -1)
			Sys_Error("playback file not found\n");

		Sys_FileRead (vcrFile, &i, sizeof(int));

		if (i != VCR_SIGNATURE)
			Sys_Error("Invalid signature in vcr file\n");

		Sys_FileRead (vcrFile, &com_argc, sizeof(int));
		com_argv = malloc(com_argc * sizeof(char *));
		com_argv[0] = parms->argv[0];

		for (i = 0; i < com_argc; i++)
		{
			Sys_FileRead (vcrFile, &len, sizeof(int));
			p = malloc(len);
			Sys_FileRead (vcrFile, p, len);
			com_argv[i+1] = p;
		}

		com_argc++; /* add one for arg[0] */
		parms->argc = com_argc;
		parms->argv = com_argv;
	}

	if ( (n = COM_CheckParm("-record")) != 0)
	{
		vcrFile = Sys_FileOpenWrite("quake.vcr");

		i = VCR_SIGNATURE;
		Sys_FileWrite(vcrFile, &i, sizeof(int));
		i = com_argc - 1;
		Sys_FileWrite(vcrFile, &i, sizeof(int));

		for (i = 1; i < com_argc; i++)
		{
			if (i == n)
			{
				len = 10;
				Sys_FileWrite(vcrFile, &len, sizeof(int));
				Sys_FileWrite(vcrFile, "-playback", len);
				continue;
			}

			len = Q_strlen(com_argv[i]) + 1;
			Sys_FileWrite(vcrFile, &len, sizeof(int));
			Sys_FileWrite(vcrFile, com_argv[i], len);
		}
	}
  
}

/*
====================
Host_Init
====================
*/
void Host_Init (quakeparms_t *parms)
{
	if (standard_quake)
		minimum_memory = MINIMUM_MEMORY;

	else
		minimum_memory = MINIMUM_MEMORY_LEVELPAK;

	if (COM_CheckParm ("-minmemory"))
		parms->memsize = minimum_memory;

	host_parms = *parms;

	if (parms->memsize < minimum_memory)
		Sys_Error ("Only %4.1f megs of memory available, can't execute game", parms->memsize / (float)0x100000);

	com_argc = parms->argc;
	com_argv = parms->argv;

	Memory_Init (parms->membase, parms->memsize);
	Cbuf_Init ();
#if defined(MGLV3D_EXITPOINT) && MGLV3D_EXITPOINT == 22
	{
		void *zmtest = Z_Malloc(64);
		if (zmtest) Z_Free(zmtest);
	}
	exit(0);
#endif
	Cmd_Init ();
	V_Init ();
	Chase_Init ();
	Host_InitVCR (parms);
	COM_Init (parms->basedir);
	Host_InitLocal ();
	W_LoadWadFile ("gfx.wad");
#if defined(MGLV3D_EXITPOINT) && MGLV3D_EXITPOINT == 23
	{
		void *zmtest = Z_Malloc(64);
		if (zmtest) Z_Free(zmtest);
	}
	exit(0);
#endif
	Key_Init ();
	Con_Init ();
	M_Init ();
	PR_Init ();
	Mod_Init ();
	NET_Init ();
	SV_Init ();
#if defined(MGLV3D_EXITPOINT) && MGLV3D_EXITPOINT == 24
	{
		void *zmtest = Z_Malloc(64);
		if (zmtest) Z_Free(zmtest);
	}
	exit(0);
#endif

#ifdef AMIGAOS
	#ifndef GLQUAKE
	Con_Printf ("Quake%s V%4.2f\n",
	#else
	Con_Printf ("GLQuake%s 2019 V%4.2f\n",
	#endif
#ifdef __PPC__
#ifdef WOS
	      "WOS",
#else
	      "PPC",
#endif
#else
	      "68k",
#endif
	      AMIGA_VERSION);

	#ifndef GLQUAKE
	Con_Printf ("Amiga port 2000 by Frank Wille and Steffen Haeuser\n");
	#else
	Con_Printf ("Amiga port 2001 by Christian Michael\n");
	#endif
	#endif

	Con_Printf ("Exe: "__TIME__" "__DATE__"\n");
	Con_Printf ("%4.1f megabyte heap\n", parms->memsize / (1024*1024.0));
  
	R_InitTextures ();    // needed even for dedicated servers
 
	if (cls.state != ca_dedicated)
	{
		host_basepal = (byte *)COM_LoadHunkFile ("gfx/palette.lmp");

		if (!host_basepal)
			Sys_Error ("Couldn't load gfx/palette.lmp");

		host_colormap = (byte *)COM_LoadHunkFile ("gfx/colormap.lmp");

		if (!host_colormap)
			Sys_Error ("Couldn't load gfx/colormap.lmp");

		#ifdef AMIGAOS

		#ifndef GLQUAKE
		Cvar_RegisterVariable(&vid_mode);
		Cvar_RegisterVariable(&_vid_default_mode);
		#endif

		Cvar_RegisterVariable (&joy_advanced);
		Cvar_RegisterVariable (&joy_forwardthreshold);
		Cvar_RegisterVariable (&joy_sidethreshold);
		Cvar_RegisterVariable (&joy_pitchthreshold);
		Cvar_RegisterVariable (&joy_pitchsensitivity);
		Cvar_RegisterVariable (&joy_yawsensitivity);
		Cvar_RegisterVariable (&joy_forward_reverse);
		Cvar_RegisterVariable (&joy_side_reverse);
		Cvar_RegisterVariable (&joy_up_reverse);
		Cvar_RegisterVariable (&joy_sidesensitivity);
		Cvar_RegisterVariable (&joy_forwardsensitivity);
		Cvar_RegisterVariable (&joy_force0);
		Cvar_RegisterVariable (&joy_force1);
		Cvar_RegisterVariable (&joy_force2);
		Cvar_RegisterVariable (&joy_force3);
		Cvar_RegisterVariable (&joy_onlypsx);
		Cvar_RegisterVariable (&cl_name);
		Cvar_RegisterVariable (&cl_color);
		Cvar_RegisterVariable (&cl_upspeed);
		Cvar_RegisterVariable (&cl_forwardspeed);
		Cvar_RegisterVariable (&cl_backspeed);
		Cvar_RegisterVariable (&cl_sidespeed);
		Cvar_RegisterVariable (&cl_movespeedkey);
		Cvar_RegisterVariable (&cl_yawspeed);
		Cvar_RegisterVariable (&cl_pitchspeed);
		Cvar_RegisterVariable (&cl_anglespeedkey);
		Cvar_RegisterVariable (&lookspring);
		Cvar_RegisterVariable (&lookstrafe);
		Cvar_RegisterVariable (&sensitivity);
		Cvar_RegisterVariable (&m_pitch);
		Cvar_RegisterVariable (&m_yaw);
		Cvar_RegisterVariable (&m_forward);
		Cvar_RegisterVariable (&m_side);
		Cvar_RegisterVariable (&_snd_mixahead);
		Cvar_RegisterVariable (&bgmvolume);
		Cvar_RegisterVariable (&volume);
		Cvar_RegisterVariable (&scr_viewsize);	
		Cvar_RegisterVariable (&m_filter);
		Cvar_RegisterVariable (&m_speed);

		//added vars /*cowcat*/ 
		Cvar_RegisterVariable (&r_vertexarrays);
		Cvar_RegisterVariable (&r_cull_aggressive);
		Cvar_RegisterVariable (&r_litfiles);
		Cvar_RegisterVariable (&r_skybox_format);
		Cvar_RegisterVariable (&r_flames);
		Cvar_RegisterVariable (&r_elim_areasize);
		Cvar_RegisterVariable (&gl_fake_multitexture);
		Cvar_RegisterVariable (&gl_triplebuffer);
		Cvar_RegisterVariable (&gl_glows);

		// new 2016
		Cvar_RegisterVariable (&r_shadows);
		Cvar_RegisterVariable (&gl_fog);
		Cvar_RegisterVariable (&gl_fogstart);
		Cvar_RegisterVariable (&gl_fogend);
		Cvar_RegisterVariable (&gl_fogred);
		Cvar_RegisterVariable (&gl_foggreen);
		Cvar_RegisterVariable (&gl_fogblue);
		Cvar_RegisterVariable (&gl_fogdensity);
		Cvar_RegisterVariable (&gl_classic_particles);
		Cvar_RegisterVariable (&cl_gunfov);
		Cvar_RegisterVariable (&r_bobitems);
		Cvar_RegisterVariable (&scr_fov);
		Cvar_RegisterVariable (&shadowalpha);

		// 2017
		Cvar_RegisterVariable (&gl_conalpha);
		Cvar_RegisterVariable (&gl_conanim);
		Cvar_RegisterVariable (&r_wateralpha);
		Cvar_RegisterVariable (&r_waterwarp);

		// 2019
		Cvar_RegisterVariable (&r_interpolations);
		Cvar_RegisterVariable (&drawfps);
		Cvar_RegisterVariable (&gl_farclip);
		/* Before the exec config.cfg below, so a saved Brightness is loaded
		 * before VID_Init runs Check_Gamma. */
		Cvar_RegisterVariable (&gl_gamma);

		//Cvar_RegisterVariable (&r_lerpmove);	// johnfitz - not used
		//Cvar_RegisterVariable (&r_lerpmodels);//

		Cvar_RegisterVariable (&r_noshadow_list); // Cowcat (mod also in autoexec.cfg)
		Cvar_RegisterVariable (&r_fullbright_list); // Cowcat (mod also in autoexec.cfg)
		//

		host_initialized=true;
		Cbuf_InsertText("exec config.cfg\n");
		Cbuf_Execute();
		host_initialized=false;

		#endif

		#ifdef AMIGAOS

		VID_Init(host_basepal);
#if defined(MGLV3D_EXITPOINT) && MGLV3D_EXITPOINT == 25
		{
			void *zmtest = Z_Malloc(64);
			if (zmtest) Z_Free(zmtest);
		}
		exit(0);
#endif
		IN_Init();

		#else

		#ifndef _WIN32 // on non win32, mouse comes before video for security reasons
		IN_Init ();
		#endif

		VID_Init (host_basepal);
		#endif

#if defined(MGLV3D_EXITPOINT) && MGLV3D_EXITPOINT == 18
		/* Real Z_Malloc test, right before Draw_Init -- isolates whether
		 * the zone is already broken by VID_Init/IN_Init/WAD-loading/
		 * COM_Init, before any of Draw_Init/SCR_Init/R_Init/Sbar_Init run. */
		{
			void *zmtest = Z_Malloc(64);
			if (zmtest) Z_Free(zmtest);
		}
		exit(0);
#endif

		Draw_Init ();
#if defined(MGLV3D_EXITPOINT) && MGLV3D_EXITPOINT == 4
		Host_Shutdown(); exit(0);
#endif
#if defined(MGLV3D_EXITPOINT) && MGLV3D_EXITPOINT == 19
		{
			void *zmtest = Z_Malloc(64);
			if (zmtest) Z_Free(zmtest);
		}
		exit(0);
#endif

		SCR_Init ();

		R_Init ();
#if defined(MGLV3D_EXITPOINT) && MGLV3D_EXITPOINT == 5
		Host_Shutdown(); exit(0);
#endif
#if defined(MGLV3D_EXITPOINT) && MGLV3D_EXITPOINT == 20
		{
			void *zmtest = Z_Malloc(64);
			if (zmtest) Z_Free(zmtest);
		}
		exit(0);
#endif

		#ifndef _WIN32

		// on Win32, sound initialization has to come before video initialization, so we
		// can put up a popup if the sound hardware is in use

		#if !defined(AMIGAOS) && !defined(GLQUAKE)
		S_Init ();
		#endif

		#else

		#if defined (GLQUAKE) && !defined(AMIGAOS)
		// FIXME: doesn't use the new one-window approach yet
		S_Init ();
		#endif

		#endif	// _WIN32

		#ifndef GLQUAKE
		CDAudio_Init (); //Surgeon: moved to gl_vidamiga.c
		#endif

		Sbar_Init ();
#if defined(MGLV3D_EXITPOINT) && MGLV3D_EXITPOINT == 8
		Host_Shutdown(); exit(0);
#endif
#if defined(MGLV3D_EXITPOINT) && MGLV3D_EXITPOINT == 21
		{
			void *zmtest = Z_Malloc(64);
			if (zmtest) Z_Free(zmtest);
		}
		exit(0);
#endif

		CL_Init ();
#if defined(MGLV3D_EXITPOINT) && MGLV3D_EXITPOINT == 6
		Host_Shutdown(); exit(0);
#endif
#if defined(MGLV3D_EXITPOINT) && MGLV3D_EXITPOINT == 17
		/* Explicitly exercise Z_Malloc/Z_Free right here, matching exit16's
		 * bare exit(0) (no Host_Shutdown) for a clean comparison -- tests
		 * whether the zone is already corrupted before Cbuf_InsertText
		 * ever runs. */
		{
			void *zmtest = Z_Malloc(64);
			if (zmtest) Z_Free(zmtest);
		}
		exit(0);
#endif

		#ifdef _WIN32 // on non win32, mouse comes before video for security reasons
		IN_Init ();
		#endif
	}

	Cbuf_InsertText("exec quake.rc\n");
#if defined(MGLV3D_EXITPOINT) && MGLV3D_EXITPOINT == 11
	Host_Shutdown(); exit(0);
#endif
#if defined(MGLV3D_EXITPOINT) && MGLV3D_EXITPOINT == 16
	/* Bare exit, no Host_Shutdown() at all -- isolates whether the crash
	 * is inside Cbuf_InsertText's real execution (this heap context) or
	 * only appears once Host_Shutdown() subsequently touches something. */
	exit(0);
#endif

	Hunk_AllocName (0, "-HOST_HUNKLEVEL-");
	host_hunklevel = Hunk_LowMark ();
#if defined(MGLV3D_EXITPOINT) && MGLV3D_EXITPOINT == 12
	Host_Shutdown(); exit(0);
#endif

	host_initialized = true;
#if defined(MGLV3D_EXITPOINT) && MGLV3D_EXITPOINT == 13
	Host_Shutdown(); exit(0);
#endif

	Con_Printf("\n========Quake Initialized=========\n"); // was Sys_Printf - jf
#if defined(MGLV3D_EXITPOINT) && MGLV3D_EXITPOINT == 7
	Host_Shutdown(); exit(0);
#endif
}

#ifdef GLQUAKE
extern void R_FreeParticles (void);
extern void qgl_FreeArrays (void);
#endif


/*
===============
Host_Shutdown

FIXME: this is a callback from Sys_Quit and Sys_Error.	It would be better
to run quit through here before the final handoff to the sys code.
===============
*/

void Host_Shutdown(void)
{
	static qboolean isdown = false;
  
	if (isdown)
	{
		printf ("recursive shutdown\n");
		return;
	}

	isdown = true;

	// keep Con_Printf from trying to update the screen
	scr_disabled_for_loading = true;

	Host_WriteConfiguration (); 

#ifdef GLQUAKE
	R_FreeParticles();
	qgl_FreeArrays();
#endif

	CDAudio_Shutdown ();
	NET_Shutdown ();
	S_Shutdown();
	IN_Shutdown ();

	if (cls.state != ca_dedicated)
	{
		VID_Shutdown();
	}
}

