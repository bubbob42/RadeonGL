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
// cl_parse.c  -- parse a message received from the server

#include "quakedef.h"

char *svc_strings[] =
{
	"svc_bad",
	"svc_nop",
	"svc_disconnect",
	"svc_updatestat",
	"svc_version",		// [long] server version
	"svc_setview",		// [short] entity number
	"svc_sound",		// <see code>
	"svc_time",		// [float] server time
	"svc_print",		// [string] null terminated string
	"svc_stufftext",	// [string] stuffed into client's console buffer
				// the string should be \n terminated
	"svc_setangle",		// [vec3] set the view angle to this absolute value
  
	"svc_serverinfo",	// [long] version
				// [string] signon string
				// [string]..[0]model cache [string]...[0]sounds cache
				// [string]..[0]item cache
	"svc_lightstyle",	// [byte] [string]
	"svc_updatename",	// [byte] [string]
	"svc_updatefrags",	// [byte] [short]
	"svc_clientdata",	// <shortbits + data>
	"svc_stopsound",	// <see code>
	"svc_updatecolors",	// [byte] [byte]
	"svc_particle",		// [vec3] <variable>
	"svc_damage",		// [byte] impact [byte] blood [vec3] from
  
	"svc_spawnstatic",
	"OBSOLETE svc_spawnbinary",
	"svc_spawnbaseline",
  
	"svc_temp_entity",	// <variable>
	"svc_setpause",
	"svc_signonnum",
	"svc_centerprint",
	"svc_killedmonster",
	"svc_foundsecret",
	"svc_spawnstaticsound",
	"svc_intermission",
	"svc_finale",		// [string] music [string] text
	"svc_cdtrack",		// [byte] track [byte] looptrack
	"svc_sellscreen",
	"svc_cutscene",

	//johnfitz -- new server messages
	"",			// 35
	"",			// 36
	"svc_skybox",		// 37	// [string] skyname
	"",			// 38
	"",			// 39
	"svc_bf",		// 40	// no data
	"svc_fog",		// 41	// [byte] density [byte] red [byte] green [byte] blue [float] time
	"svc_spawnbaseline2",	// 42	// support for large modelindex, large framenum, alpha, using flags
	"svc_spawnstatic2",	// 43	// support for large modelindex, large framenum, alpha, using flags
	"svc_spawnstaticsound2",// 44	// [coord3] [short] samp [byte] vol [byte] aten
	"",			// 44
	"",			// 45
	"",			// 46
	"",			// 47
	"",			// 48
	"",			// 49
	//johnfitz
};

//=============================================================================

/*
===============
CL_EntityNum

This error checks and tracks the total number of entities
===============
*/
entity_t *CL_EntityNum (int num)
{
	//johnfitz -- check minimum number too
	if (num < 0)
		Host_Error ("CL_EntityNum: %i is an invalid number",num);
	//john

	if (num >= cl.num_entities)
	{
		#if 0

		if (num >= MAX_EDICTS)
			Host_Error ("CL_EntityNum: %i is an invalid number", num);
		#else

		if (num >= cl_max_edicts) //johnfitz -- no more MAX_EDICTS
			Host_Error ("CL_EntityNum: %i is an invalid number", num);

		#endif

		while (cl.num_entities <= num)
		{
			cl_entities[cl.num_entities].colormap = vid.colormap;
			//cl_entities[cl.num_entities].lerpflags |= LERP_RESETMOVE|LERP_RESETANIM; //johnfitz
			cl.num_entities++;
		}
	}
    
	return &cl_entities[num];
}


/*
==================
CL_ParseStartSoundPacket
==================
*/
void CL_ParseStartSoundPacket(void)
{
	vec3_t	pos;
	int	channel, ent;
	int	sound_num;
	int	volume;
	int	field_mask;
	float	attenuation;  
	int	i;
	     
	field_mask = MSG_ReadByte(); 

	if (field_mask & SND_VOLUME)
		volume = MSG_ReadByte ();

	else
		volume = DEFAULT_SOUND_PACKET_VOLUME;
  
	if (field_mask & SND_ATTENUATION)
		attenuation = MSG_ReadByte () / 64.0;

	else
		attenuation = DEFAULT_SOUND_PACKET_ATTENUATION;

	//johnfitz -- PROTOCOL_FITZQUAKE
	if (field_mask & SND_LARGEENTITY)
	{
		ent = (unsigned short) MSG_ReadShort ();
		channel = MSG_ReadByte ();
	}

	else
	{
		channel = (unsigned short) MSG_ReadShort ();
		ent = channel >> 3;
		channel &= 7;
	}

	if (field_mask & SND_LARGESOUND)
		sound_num = (unsigned short) MSG_ReadShort ();

	else
		sound_num = MSG_ReadByte ();

	if (sound_num >= MAX_SOUNDS)	// MAX_FITZQUAKE_SOUNDS
		Host_Error ("CL_ParseStartSoundPacket: %i > MAX_SOUNDS", sound_num);

	//johnfitz

	if (ent > cl_max_edicts) // no more MAX_EDICTS
		Host_Error ("CL_ParseStartSoundPacket: ent = %i", ent);
  
	for (i=0 ; i<3 ; i++)
		pos[i] = MSG_ReadCoord ();
 
	S_StartSound (ent, channel, cl.sound_precache[sound_num], pos, volume/255.0, attenuation);
}	

/*
==================
CL_KeepaliveMessage

When the client is taking a long time to load stuff, send keepalive messages
so the server doesn't disconnect.
==================
*/
void CL_KeepaliveMessage (void)
{
	float		time;
	static float	lastmsg;
	int		ret;
	sizebuf_t	old;
	byte		olddata[8192];
  
	if (sv.active)
		return;	  // no need if server is local

	if (cls.demoplayback)
		return;

	// read messages from server, should just be nops
	old = net_message;
	memcpy (olddata, net_message.data, net_message.cursize);
  
	do
	{
		ret = CL_GetMessage ();

		switch (ret)
		{
			default:
				Host_Error ("CL_KeepaliveMessage: CL_GetMessage failed");
   
			case 0:
				break;	// nothing waiting

			case 1:
				Host_Error ("CL_KeepaliveMessage: received a message");
				break;

			case 2:
				if (MSG_ReadByte() != svc_nop)
					Host_Error ("CL_KeepaliveMessage: datagram wasn't a nop");
				break;
		}

	} while (ret);

	net_message = old;
	memcpy (net_message.data, olddata, net_message.cursize);

	// check time
	time = Sys_FloatTime ();

	if (time - lastmsg < 5)
		return;

	lastmsg = time;

	// write out a nop
	Con_Printf ("--> client to server keepalive\n");

	MSG_WriteByte (&cls.message, clc_nop);
	NET_SendMessage (cls.netcon, &cls.message);
	SZ_Clear (&cls.message);
}

/*
==================
CL_ParseServerInfo
==================
*/
//void CL_ClearState(unsigned int protocol_num); // Baker

void CL_ParseServerInfo (void)
{
	char  *str;
	int   i;
	int   nummodels, numsounds;
	char  model_precache[MAX_MODELS][MAX_QPATH];
	char  sound_precache[MAX_SOUNDS][MAX_QPATH];
  
	Con_DPrintf ("\nServerinfo packet received.\n");

	//
	// wipe the client_state_t struct
	//

	CL_ClearState (); // down ? Baker

	// parse protocol version number
	i = MSG_ReadLong ();

	//johnfitz
	if ( i != PROTOCOL_NETQUAKE && i != PROTOCOL_FITZQUAKE )
	{
		Con_Printf ("\n"); //because there's no newline after serverinfo print
		Con_Printf ("Server returned version %i, not %i or %i", i, PROTOCOL_NETQUAKE, PROTOCOL_FITZQUAKE);
		return;
	}

	cl.protocol = i;

	//CL_ClearState (i); // Baker sets protocol here
	//

	// parse maxclients
	cl.maxclients = MSG_ReadByte ();

	if (cl.maxclients < 1 || cl.maxclients > MAX_SCOREBOARD)
	{
		Con_Printf("Bad maxclients (%u) from server\n", cl.maxclients);
		return;
	}

	cl.scores = Hunk_AllocName (cl.maxclients*sizeof(*cl.scores), "scores");
	cl.teamscores = Hunk_AllocName (14 * sizeof(*cl.teamscores), "teamscor"); // JPG - for teamscore status bar

	// parse gametype
	cl.gametype = MSG_ReadByte ();

	// parse signon message
	str = MSG_ReadString ();
	strncpy (cl.levelname, str, sizeof(cl.levelname)-1);


	// seperate the printfs so the server message can have a color
	Con_Printf("\n\n\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\37\n\n");
	Con_Printf ("%c%s\n", 2, str);

	//johnfitz -- tell user which protocol this is
	Con_Printf ("Using protocol %i\n", i);

	//
	// first we go through and touch all of the precache data that still
	// happens to be in the cache, so precaching something else doesn't
	// needlessly purge it
	//

	// precache models
	memset (cl.model_precache, 0, sizeof(cl.model_precache));

	for (nummodels=1 ; ; nummodels++)
	{
		str = MSG_ReadString ();

		if (!str[0])
			break;

		if (nummodels == MAX_MODELS) // MAX_FITZQUAKE_MODELS
		{
			Con_Printf ("Server sent too many model precaches\n");
			return;
		}

		strcpy (model_precache[nummodels], str);
		Mod_TouchModel (str);
	}

	//johnfitz -- check for excessive models
	if (nummodels >= 256)		// MAX_WINQUAKE_MODELS
		Con_DPrintf ("%i models exceeds standard limit of 256.\n", nummodels);
	//johnfitz

	// precache sounds
	memset (cl.sound_precache, 0, sizeof(cl.sound_precache));

	for (numsounds=1 ; ; numsounds++)
	{
		str = MSG_ReadString ();

		if (!str[0])
			break;

		if (numsounds == MAX_SOUNDS) // MAX_FITZQUAKE_SOUNDS correct ??
		{
			Con_Printf ("Server sent too many sound precaches\n");
			return;
		}

		strcpy (sound_precache[numsounds], str);
		S_TouchSound (str);
	}

	//johnfitz -- check for excessive sounds
	if (numsounds >= 256)		// MAX_WINQUAKE_SOUNDS
		Con_DPrintf ("%i sounds exceeds standard limit of 256.\n", numsounds);
	//johnfitz

	//
	// now we try to load everything else until a cache allocation fails
	//

	for (i=1 ; i<nummodels ; i++)
	{
		cl.model_precache[i] = Mod_ForName (model_precache[i], false);

		if (cl.model_precache[i] == NULL)
		{
			Con_Printf("Model %s not found\n", model_precache[i]);
			return;
		}

		CL_KeepaliveMessage ();
	}

	S_BeginPrecaching ();

	for (i=1 ; i<numsounds ; i++)
	{
		cl.sound_precache[i] = S_PrecacheSound (sound_precache[i]);
		CL_KeepaliveMessage ();
	}

	S_EndPrecaching ();

	// local state
	cl_entities[0].model = cl.worldmodel = cl.model_precache[1];

	LOC_LoadLocations();  // JPG - read in the location data for the new map
  
	R_NewMap ();

	//con_lastcenterstring[0] = 0; // johnfitz

	Hunk_Check ();			// make sure nothing is hurt
  
	noclip_anglehack = false;	// noclip is turned off at start  
}


/*
==================
CL_ParseUpdate

Parse an entity update message from the server
If an entities model or origin changes from frame to frame, it must be
relinked.  Other attributes can change without relinking.
==================
*/

void CL_ParseUpdate (int bits)
{
	int		i;
	model_t		*model;
	int		modnum;
	qboolean	forcelink;
	entity_t	*ent;
	int		num;
	int		skin;

	if (cls.signon == SIGNONS - 1)
	{
		// first update is the final signon stage
		cls.signon = SIGNONS;
		CL_SignonReply ();
	}

	if (bits & U_MOREBITS)
	{
		i = MSG_ReadByte ();
		bits |= (i<<8);
	}

	//johnfitz -- PROTOCOL_FITZQUAKE
	if (cl.protocol == PROTOCOL_FITZQUAKE)
	{
		if (bits & U_EXTEND1)
			bits |= MSG_ReadByte() << 16;

		if (bits & U_EXTEND2)
			bits |= MSG_ReadByte() << 24;
	}
	//johnfitz

	if (bits & U_LONGENTITY)  
		num = MSG_ReadShort ();

	else
		num = MSG_ReadByte ();

	ent = CL_EntityNum (num);

	if (ent->msgtime != cl.mtime[1])
		forcelink = true; // no previous frame to lerp from

	else
		forcelink = false;

	#if 0
	//johnfitz -- lerping
	if (ent->msgtime + 0.2 < cl.mtime[0]) //more than 0.2 seconds since the last message (most entities think every 0.1 sec)
		ent->lerpflags |= LERP_RESETANIM; //if we missed a think, we'd be lerping from the wrong frame
	//johnfitz
	#endif

	ent->msgtime = cl.mtime[0];
  
	if (bits & U_MODEL)
	{
		modnum = MSG_ReadByte ();

		if (modnum >= MAX_MODELS) // MAX_FITZQUAKE_MODELS
			Host_Error ("CL_ParseModel: bad modnum");
	}

	else
		modnum = ent->baseline.modelindex;

	if (bits & U_FRAME)
		ent->frame = MSG_ReadByte ();

	else
		ent->frame = ent->baseline.frame;

	if (bits & U_COLORMAP)
		i = MSG_ReadByte();

	else
		i = ent->baseline.colormap;

	if (!i)
		ent->colormap = vid.colormap;

	else
	{
		if (i > cl.maxclients)
			Sys_Error ("i >= cl.maxclients");

		ent->colormap = cl.scores[i-1].translations;
	}

	#ifdef GLQUAKE

	if (bits & U_SKIN)
		skin = MSG_ReadByte();

	else
		skin = ent->baseline.skin;

	if (skin != ent->skinnum)
	{
		ent->skinnum = skin;

		if (num > 0 && num <= cl.maxclients)
			R_TranslatePlayerSkin (num - 1);
	}

	#else

	if (bits & U_SKIN)
		ent->skinnum = MSG_ReadByte();

	else
		ent->skinnum = ent->baseline.skin;

	#endif

	if (bits & U_EFFECTS)
		ent->effects = MSG_ReadByte();

	else
		ent->effects = ent->baseline.effects;

	// shift the known values for interpolation
	VectorCopy (ent->msg_origins[0], ent->msg_origins[1]);
	VectorCopy (ent->msg_angles[0], ent->msg_angles[1]);

	if (bits & U_ORIGIN1)
		ent->msg_origins[0][0] = MSG_ReadCoord ();

	else
		ent->msg_origins[0][0] = ent->baseline.origin[0];

	if (bits & U_ANGLE1)
		ent->msg_angles[0][0] = MSG_ReadAngle();

	else
		ent->msg_angles[0][0] = ent->baseline.angles[0];

	if (bits & U_ORIGIN2)
		ent->msg_origins[0][1] = MSG_ReadCoord ();

	else
		ent->msg_origins[0][1] = ent->baseline.origin[1];

	if (bits & U_ANGLE2)
		ent->msg_angles[0][1] = MSG_ReadAngle();

	else
		ent->msg_angles[0][1] = ent->baseline.angles[1];

	if (bits & U_ORIGIN3)
		ent->msg_origins[0][2] = MSG_ReadCoord ();

	else
		ent->msg_origins[0][2] = ent->baseline.origin[2];

	if (bits & U_ANGLE3)
		ent->msg_angles[0][2] = MSG_ReadAngle();

	else
		ent->msg_angles[0][2] = ent->baseline.angles[2];

	#if 1

	if ( bits & U_STEP )
		ent->forcelink = true;

	#else

	//johnfitz -- lerping for movetype_step entities
	if ( bits & U_STEP )
	{
		ent->lerpflags |= LERP_MOVESTEP;
		ent->forcelink = true;
	}
	else
		ent->lerpflags &= ~LERP_MOVESTEP;
	//johnfitz

	#endif

	//johnfitz -- PROTOCOL_FITZQUAKE
	if (cl.protocol == PROTOCOL_FITZQUAKE)
	{
		if (bits & U_ALPHA)
			ent->alpha = MSG_ReadByte();

		else
			ent->alpha = ent->baseline.alpha;

		if (bits & U_FRAME2)
			ent->frame = (ent->frame & 0x00FF) | (MSG_ReadByte() << 8);

		if (bits & U_MODEL2)
			modnum = (modnum & 0x00FF) | (MSG_ReadByte() << 8);
	
		#if 0
		if (bits & U_LERPFINISH)
		{
			ent->lerpfinish = ent->msgtime + ((float)(MSG_ReadByte()) / 255);
			ent->lerpflags |= LERP_FINISH;
		}

		else
			ent->lerpflags &= ~LERP_FINISH;
		#endif
		
	}

	else if (cl.protocol == PROTOCOL_NETQUAKE)
	{
		#if 0
		//HACK: if this bit is set, assume this is PROTOCOL_NEHAHRA
		if (bits & U_TRANS)
		{
			float a,b;

			if (warn_about_nehahra_protocol)
			{
				Con_Printf ("nonstandard update bit, assuming Nehahra protocol\n");
				warn_about_nehahra_protocol = false;
			}

			a = MSG_ReadFloat();
			b = MSG_ReadFloat(); //alpha

			if (a == 2)
				MSG_ReadFloat(); //fullbright (not using this yet)

			ent->alpha = ENTALPHA_ENCODE(b);
		}

		else
		#endif
			ent->alpha = ent->baseline.alpha;
	}
	//johnfitz

	//johnfitz -- moved here from above
	model = cl.model_precache[modnum];

	if (model != ent->model)
	{
		ent->model = model;

		// automatic animation (torches, etc) can be either all together
		// or randomized

		if (model)
		{
			if (model->synctype == ST_RAND)
				ent->syncbase = (float)(rand()&0x7fff) / 0x7fff;

			else
				ent->syncbase = 0.0;
		}

		else
			forcelink = true;	// hack to make null model players work

		if (num > 0 && num <= cl.maxclients)
			R_TranslatePlayerSkin (num - 1); //johnfitz -- R_TranslateNewPlayerSkin ?

		//ent->lerpflags |= LERP_RESETANIM;	//johnfitz -- don't lerp animation across model changes
	}
	//johnfitz

	if ( forcelink )
	{
		// didn't have an update last message
		VectorCopy (ent->msg_origins[0], ent->msg_origins[1]);
		VectorCopy (ent->msg_origins[0], ent->origin);
		VectorCopy (ent->msg_angles[0], ent->msg_angles[1]);
		VectorCopy (ent->msg_angles[0], ent->angles);
		ent->forcelink = true;

		// new 2019 - Cowcat
		ent->frame_start_time = 0;
		ent->frame_interval = 0.1; // default
		ent->pose1 = ent->pose2 = 0;
		ent->translate_start_time = 0;
	}
}

/*
==================
CL_ParseBaseline
==================
*/
void CL_ParseBaseline (entity_t *ent, int version) // jf
{
	int	i;
	int	bits; //johnfitz

	#if 1
	//johnfitz -- PROTOCOL_FITZQUAKE

	bits = (version == 2) ? MSG_ReadByte() : 0;
	ent->baseline.modelindex = (bits & B_LARGEMODEL) ? MSG_ReadShort() : MSG_ReadByte();
	ent->baseline.frame = (bits & B_LARGEFRAME) ? MSG_ReadShort() : MSG_ReadByte();

	//johnfitz

	#else

	ent->baseline.modelindex = MSG_ReadByte ();
	ent->baseline.frame = MSG_ReadByte ();

	#endif

	ent->baseline.colormap = MSG_ReadByte();
	ent->baseline.skin = MSG_ReadByte();

	for (i=0 ; i<3 ; i++)
	{
		ent->baseline.origin[i] = MSG_ReadCoord ();
		ent->baseline.angles[i] = MSG_ReadAngle ();
	}

	ent->baseline.alpha = (bits & B_ALPHA) ? MSG_ReadByte() : ENTALPHA_DEFAULT; //johnfitz -- PROTOCOL_FITZQUAKE
}


/*
==================
CL_ParseClientdata

Server information pertaining to this client only
==================
*/
void CL_ParseClientdata (void) // was int bits -johnfitz
{
	int	i, j;
	int	bits;

	bits = (unsigned short)MSG_ReadShort (); //johnfitz -- read bits here isntead of in CL_ParseServerMessage()

	//johnfitz -- PROTOCOL_FITZQUAKE
	if (bits & SU_EXTEND1)
		bits |= (MSG_ReadByte() << 16);

	if (bits & SU_EXTEND2)
		bits |= (MSG_ReadByte() << 24);
	//johnfitz
  
	if (bits & SU_VIEWHEIGHT)
		cl.viewheight = MSG_ReadChar ();

	else
		cl.viewheight = DEFAULT_VIEWHEIGHT;

	if (bits & SU_IDEALPITCH)
		cl.idealpitch = MSG_ReadChar ();

	else
		cl.idealpitch = 0;
  
	VectorCopy (cl.mvelocity[0], cl.mvelocity[1]);

	for (i=0 ; i<3 ; i++)
	{
		if (bits & (SU_PUNCH1<<i) )
			cl.punchangle[i] = MSG_ReadChar();

		else
			cl.punchangle[i] = 0;

		if (bits & (SU_VELOCITY1<<i) )
			cl.mvelocity[0][i] = MSG_ReadChar()*16;

		else
			cl.mvelocity[0][i] = 0;
	}

	// [always sent]  if (bits & SU_ITEMS)
	i = MSG_ReadLong ();

	if (cl.items != i)
	{
		// set flash times
		Sbar_Changed ();

		for (j=0 ; j<32 ; j++)
			if ( (i & (1<<j)) && !(cl.items & (1<<j)))
				cl.item_gettime[j] = cl.time;

		cl.items = i;
	}	
    
	cl.onground = (bits & SU_ONGROUND) != 0;
	cl.inwater = (bits & SU_INWATER) != 0;

	if (bits & SU_WEAPONFRAME)
		cl.stats[STAT_WEAPONFRAME] = MSG_ReadByte ();

	else
		cl.stats[STAT_WEAPONFRAME] = 0;

	if (bits & SU_ARMOR)
		i = MSG_ReadByte ();

	else
		i = 0;

	if (cl.stats[STAT_ARMOR] != i)
	{
		cl.stats[STAT_ARMOR] = i;
		Sbar_Changed ();
	}

	if (bits & SU_WEAPON)
		i = MSG_ReadByte ();

	else
		i = 0;

	if (cl.stats[STAT_WEAPON] != i)
	{
		cl.stats[STAT_WEAPON] = i;
		Sbar_Changed ();
	}
  
	i = MSG_ReadShort ();

	if (cl.stats[STAT_HEALTH] != i)
	{
		cl.stats[STAT_HEALTH] = i;
		Sbar_Changed ();
	}

	i = MSG_ReadByte ();

	if (cl.stats[STAT_AMMO] != i)
	{
		cl.stats[STAT_AMMO] = i;
		Sbar_Changed ();
	}

	for (i=0 ; i<4 ; i++)
	{
		j = MSG_ReadByte ();

		if (cl.stats[STAT_SHELLS+i] != j)
		{
			cl.stats[STAT_SHELLS+i] = j;
			Sbar_Changed ();
		}
	}

	i = MSG_ReadByte ();

	if (standard_quake)
	{
		if (cl.stats[STAT_ACTIVEWEAPON] != i)
		{
			cl.stats[STAT_ACTIVEWEAPON] = i;
			Sbar_Changed ();
		}
	}

	else
	{
		if (cl.stats[STAT_ACTIVEWEAPON] != (1<<i))
		{
			cl.stats[STAT_ACTIVEWEAPON] = (1<<i);
			Sbar_Changed ();
		}
	}

	//johnfitz -- PROTOCOL_FITZQUAKE
	if (bits & SU_WEAPON2)
		cl.stats[STAT_WEAPON] |= (MSG_ReadByte() << 8);

	if (bits & SU_ARMOR2)
		cl.stats[STAT_ARMOR] |= (MSG_ReadByte() << 8);

	if (bits & SU_AMMO2)
		cl.stats[STAT_AMMO] |= (MSG_ReadByte() << 8);

	if (bits & SU_SHELLS2)
		cl.stats[STAT_SHELLS] |= (MSG_ReadByte() << 8);

	if (bits & SU_NAILS2)
		cl.stats[STAT_NAILS] |= (MSG_ReadByte() << 8);

	if (bits & SU_ROCKETS2)
		cl.stats[STAT_ROCKETS] |= (MSG_ReadByte() << 8);

	if (bits & SU_CELLS2)
		cl.stats[STAT_CELLS] |= (MSG_ReadByte() << 8);

	if (bits & SU_WEAPONFRAME2)
		cl.stats[STAT_WEAPONFRAME] |= (MSG_ReadByte() << 8);

	if (bits & SU_WEAPONALPHA)
		cl.viewent.alpha = MSG_ReadByte();

	else
		cl.viewent.alpha = ENTALPHA_DEFAULT;
	//johnfitz

	#if 0
	//johnfitz -- lerping
	if (cl.viewent.model != cl.model_precache[cl.stats[STAT_WEAPON]])
		cl.viewent.lerpflags |= LERP_RESETANIM; //don't lerp animation across model changes
	//johnfitz
	#endif
}

/*
=====================
CL_NewTranslation
=====================
*/
void CL_NewTranslation (int slot)
{
	int   i, j;
	int   top, bottom;
	byte  *dest, *source;
  
	if (slot > cl.maxclients)
		Sys_Error ("CL_NewTranslation: slot > cl.maxclients");

	dest = cl.scores[slot].translations;
	source = vid.colormap;

	memcpy (dest, vid.colormap, sizeof(cl.scores[slot].translations));

	top = cl.scores[slot].colors & 0xf0;
	bottom = (cl.scores[slot].colors &15)<<4;

	#ifdef GLQUAKE
	R_TranslatePlayerSkin (slot);
	#endif

	for (i=0 ; i<VID_GRADES ; i++, dest += 256, source+=256)
	{
		if (top < 128)	// the artists made some backwards ranges.  sigh.
			memcpy (dest + TOP_RANGE, source + top, 16);

		else
			for (j=0 ; j<16 ; j++)
				dest[TOP_RANGE+j] = source[top+15-j];
	
		if (bottom < 128)
			memcpy (dest + BOTTOM_RANGE, source + bottom, 16);

		else
			for (j=0 ; j<16 ; j++)
				dest[BOTTOM_RANGE+j] = source[bottom+15-j];   
	}
}

/*
=====================
CL_ParseStatic
=====================
*/

extern cvar_t r_flames; //surgeon

void CL_ParseStatic (int version) // was (void) jf
{
	entity_t *ent;
	int	 i;
    
	i = cl.num_statics;

	#if 1

	if (i >= MAX_STATIC_ENTITIES)
		Host_Error ("Too many static entities");

	ent = &cl_static_entities[i];

	#else // new Feb 4 2016 - now on hunk - Mark V - to test Cowcat // mh tutorial

	ent = (entity_t *)Hunk_Alloc(sizeof(entity_t));

	#endif
	
	cl.num_statics++;
	CL_ParseBaseline (ent, version); // jf

	if (!r_flames.value)
	{
		//if ( (!strcmp(cl.model_precache[ent->baseline.modelindex]->name, "progs/flame.mdl")) || (!strcmp(cl.model_precache[ent->baseline.modelindex]->name, "progs/flame2.mdl")) )
		if( cl.model_precache[ent->baseline.modelindex]->modhint == MOD_FLAME )
			return;
	}

	// copy it to the current state

	ent->model = cl.model_precache[ent->baseline.modelindex];
	//ent->lerpflags |= LERP_RESETANIM;	//johnfitz -- lerping
	ent->frame = ent->baseline.frame;

	ent->colormap = vid.colormap;
	ent->skinnum = ent->baseline.skin;
	ent->effects = ent->baseline.effects;
	ent->alpha = ent->baseline.alpha;	//johnfitz -- alpha

	VectorCopy (ent->baseline.origin, ent->origin);
	VectorCopy (ent->baseline.angles, ent->angles);

	R_AddEfrags (ent);
}

/*
===================
CL_ParseStaticSound
===================
*/
void CL_ParseStaticSound (int version) // was (void) jf
{
	vec3_t	org;
	int	sound_num, vol, atten;
	int	i;
  
	for (i=0 ; i<3 ; i++)
		org[i] = MSG_ReadCoord ();
	
	#if 1
	//johnfitz -- PROTOCOL_FITZQUAKE
	if (version == 2)
		sound_num = MSG_ReadShort (); // unsigned s ???

	else
		sound_num = MSG_ReadByte ();
	//johnfitz

	#else

	sound_num = MSG_ReadByte ();

	#endif

	vol = MSG_ReadByte ();
	atten = MSG_ReadByte ();
  
	S_StaticSound (cl.sound_precache[sound_num], org, vol, atten);
}

// JPG - added this
int MSG_ReadBytePQ (void)
{
	return MSG_ReadByte() * 16 + MSG_ReadByte() - 272;
}

// JPG - added this
int MSG_ReadShortPQ (void)
{
	return MSG_ReadBytePQ() * 256 + MSG_ReadBytePQ();
}

/* JPG - added this function for ProQuake messages
=======================
CL_ParseProQuakeMessage
=======================
*/
void CL_ParseProQuakeMessage (void)
{
	int cmd, i;
	int team, frags, shirt, ping;

	MSG_ReadByte();
	cmd = MSG_ReadByte();
  
	switch (cmd)
	{
		case pqc_new_team:
			Sbar_Changed ();
			team = MSG_ReadByte() - 16;

			if (team < 0 || team > 13)
				Host_Error ("CL_ParseProQuakeMessage: pqc_new_team invalid team");

			shirt = MSG_ReadByte() - 16;
			cl.teamgame = true;
			cl.teamscores[team].colors = 16 * shirt + team;
			// cl.teamscores[team].frags = 0;	// JPG 3.20 - removed this
			// Con_Printf("pqc_new_team %d %d\n", team, shirt);
			break;

		case pqc_erase_team:
			Sbar_Changed ();
			team = MSG_ReadByte() - 16;

			if (team < 0 || team > 13)
				Host_Error ("CL_ParseProQuakeMessage: pqc_erase_team invalid team");

			cl.teamscores[team].colors = 0;
			cl.teamscores[team].frags = 0;		// JPG 3.20 - added this
			//Con_Printf("pqc_erase_team %d\n", team);
			break;

		case pqc_team_frags:
			Sbar_Changed ();
			team = MSG_ReadByte() - 16;

			if (team < 0 || team > 13)
				Host_Error ("CL_ParseProQuakeMessage: pqc_team_frags invalid team");

			frags = MSG_ReadShortPQ(); // fix Cowcat

			if (frags & 32768)
				frags = frags - 65536;

			cl.teamscores[team].frags = frags;
			//Con_Printf("pqc_team_frags %d %d\n", team, frags);
			break;	    

		case pqc_match_time:
			Sbar_Changed ();
			cl.minutes = MSG_ReadBytePQ();
			cl.seconds = MSG_ReadBytePQ();
			cl.last_match_time = cl.time;
			//Con_Printf("pqc_match_time %d %d\n", cl.minutes, cl.seconds);
			break;

		case pqc_match_reset:
			Sbar_Changed ();

			for (i = 0 ; i < 14 ; i++)
			{
				cl.teamscores[i].colors = 0;
				cl.teamscores[i].frags = 0;	// JPG 3.20 - added this
			}

			//Con_Printf("pqc_match_reset\n");
			break;

		case pqc_ping_times:

			while (ping = MSG_ReadShortPQ())
			{
				if ((ping / 4096) >= cl.maxclients)
					Host_Error ("CL_ParseProQuakeMessage: pqc_ping_times > MAX_SCOREBOARD");

				cl.scores[ping / 4096].ping = ping & 4095;
			}

			cl.last_ping_time = cl.time;

			/*
			Con_Printf("pqc_ping_times ");

			for (i = 0 ; i < cl.maxclients ; i++)
				Con_Printf("%4d ", cl.scores[i].ping);

			Con_Printf("\n");
			*/
			break;
	}
}

extern cvar_t pq_scoreboard_pings; // JPG - need this for CL_ParseProQuakeString

/* JPG - on a svc_print, check to see if the string contains useful information
======================
CL_ParseProQuakeString
======================
*/
void CL_ParseProQuakeString (char *string)
{
	static int checkping = -1;
	int	ping, i;
	char	*s, *s2;;

	// check for a ping time
	s = string;

	if (checkping >= 0)
	{
		while (*s == ' ')
			s++;

		ping = 0;

		while (*s >= '0' && *s <= '9')
			ping = 10 * ping + *s++ - '0';

		if ((*s++ == ' ') && *s && (s2 = strchr(s, '\n')))
		{
			*s2 = 0;

			if (strcmp(s, "unconnected"))
			{
				for ( ; checkping < cl.maxclients ; checkping++)
				{
					if (!strcmp(cl.scores[checkping].name, s))
					{
						cl.scores[checkping].ping = ping > 9999 ? 9999 : ping;

						if (cl.sbar_ping)
							*string = 0;
						break;
					}
				}
			}

			*s2 = '\n';

			if (checkping == cl.maxclients)
				checkping = -1;

		}

		else
			checkping = -1;

		cl.sbar_ping = cl.sbar_ping && (checkping >= 0);
	}

	// check for match time
	if (!strncmp(string, "Match ends in ", 14))
	{
		s = string + 14;

		if ((*s != 'T') && strchr(s, 'm'))
		{
			sscanf(s, "%d", &cl.minutes);
			cl.seconds = 0;
			cl.last_match_time = cl.time;
		}
	}

	else if (!strcmp(string, "Match paused\n"))
		cl.match_pause_time = cl.time;

	else if (!strcmp(string, "Match unpaused\n"))
	{
		cl.last_match_time += (cl.time - cl.match_pause_time);
		cl.match_pause_time = 0;
	}

	else if (!strcmp(string, "The match is over\n") || !strncmp(string, "Match begins in", 15))
		cl.minutes = 255;

	else if (checkping < 0)
	{
		s = string;
		i = 0;

		while (*s >= '0' && *s <= '9')
			i = 10 * i + *s++ - '0';

		if (!strcmp(s, " minutes remaining\n"))
		{
			cl.minutes = i;
			cl.seconds = 0;
			cl.last_match_time = cl.time;
		}
	}

	if (!strcmp(string, "Client ping times:\n") && pq_scoreboard_pings.value)
	{
		cl.last_ping_time = cl.time;
		checkping = 0;

		if (cl.sbar_ping)
			*string = 0;
	}
}

#define SHOWNET(x) if(cl_shownet.value==2)Con_Printf ("%3i:%s\n", msg_readcount-1, x);
#define q_max(a, b) (((a) > (b)) ? (a) : (b))

/*
=====================
CL_ParseServerMessage
=====================
*/

//extern qboolean fog;

void CL_ParseServerMessage (void)
{
	int	cmd;
	int	i;
	//char	  *s;

	char	*str;			//johnfitz
	int	total, j, lastcmd = 0;	//johnfitz
  
	//
	// if recording demos, copy the message out
	//

	if (cl_shownet.value == 1)
		Con_Printf ("%i ",net_message.cursize);

	else if (cl_shownet.value == 2)
		Con_Printf ("------------------\n");
  
	cl.onground = false;  // unless the server says otherwise

	//
	// parse the message
	//

	MSG_BeginReading ();
  
	while (1)
	{
		if (msg_badread)
			Host_Error ("CL_ParseServerMessage: Bad server message");

		cmd = MSG_ReadByte ();

		if (cmd == -1)
		{
			SHOWNET("END OF MESSAGE");
			return;	  // end of message
		}

		// if the high bit of the command byte is set, it is a fast update
		if (cmd & /*128*/ U_SIGNAL)	// johnfitz
		{
			SHOWNET("fast update");
			CL_ParseUpdate (cmd&127);
			continue;
		}

		SHOWNET(svc_strings[cmd]);
  
		// other commands

		switch (cmd)
		{

			default:
				Host_Error ("CL_ParseServerMessage: Illegible server message, previous was %s\n", svc_strings[lastcmd]); // jf
				break;
      
			case svc_nop:
				// Con_Printf ("svc_nop\n");
				break;
      
			case svc_time:
				cl.mtime[1] = cl.mtime[0];
				cl.mtime[0] = MSG_ReadFloat ();	    
				break;
      
			case svc_clientdata:
				//i = MSG_ReadShort ();
				CL_ParseClientdata (); // johnfitz 
				break;
    
			case svc_version:
				i = MSG_ReadLong ();

				if (i != PROTOCOL_NETQUAKE && i != PROTOCOL_FITZQUAKE)
					//Host_Error ("CL_ParseServerMessage: Server is protocol %i instead of %i\n", i, PROTOCOL_NETQUAKE);
					Host_Error ("CL_PArseServerMessage: Server returned version %i, not %i or %i\n", i, PROTOCOL_NETQUAKE, PROTOCOL_FITZQUAKE);

				cl.protocol = i;
				break;
      
			case svc_disconnect:
				Host_EndGame ("Server disconnected\n");

			case svc_print:

				#if 1

				// JPG - check to see if the message contains useful information
				str = MSG_ReadString();
				CL_ParseProQuakeString(str);
				Con_Printf ("%s", str);

				#else

				Con_Printf ("%s", MSG_ReadString ());

				#endif
	
				break;
      
			case svc_centerprint:

				//SCR_CenterPrint (MSG_ReadString ());
				//johnfitz -- log centerprints to console
				str = MSG_ReadString ();
				SCR_CenterPrint (str);
				//Con_LogCenterPrint (str);
				//johnfitz
				break;
      
			case svc_stufftext:

				#if 1

				// JPG - check for ProQuake message
				if (MSG_PeekByte() == MOD_PROQUAKE)
					CL_ParseProQuakeMessage();

				// Still want to add text, even on ProQuake messages.  This guarantees compatibility;
				// unrecognized messages will essentially be ignored but there will be no parse errors

				#endif

				Cbuf_AddText (MSG_ReadString ());
				break;
      
			case svc_damage:
				V_ParseDamage ();
				break;
      
			case svc_serverinfo:
				CL_ParseServerInfo ();
				vid.recalc_refdef = true; // leave intermission full screen
				break;
      
			case svc_setangle: // JPG - added mviewangles for smooth chasecam, set last_angle_time

				for (i=0 ; i<3 ; i++)
					cl.viewangles[i] = MSG_ReadAngle ();
				
				#if 0	// test Cowcat
				if (cls.demoplayback)
				{
					VectorCopy(cl.viewangles, cl.mviewangles[0]);
					VectorCopy(cl.mviewangles[0], cl.mviewangles[1]);
					VectorCopy(cl.mviewangles[0], cl.lerpangles);
					cl.mviewangles[0][0] = cl.mviewangles[1][0] = cl.lerpangles[0] = -cl.lerpangles[0];
				}
				#endif

				if (!cls.demoplayback)
				{
					VectorCopy (cl.mviewangles[0], cl.mviewangles[1]);

					// JPG - hack with last_angle_time to autodetect continuous svc_setangles
					if (last_angle_time > host_time - 0.3)
						last_angle_time = host_time + 0.3;

					else if (last_angle_time > host_time - 0.6)
						last_angle_time = host_time;

					else
						last_angle_time = host_time - 0.3;

					for (i=0 ; i<3 ; i++)
						cl.mviewangles[0][i] = cl.viewangles[i];
				}
				

				break;
      
			case svc_setview:
				cl.viewentity = MSG_ReadShort ();
				break;
	  
			case svc_lightstyle:
				i = MSG_ReadByte ();

				if (i >= MAX_LIGHTSTYLES)
					Sys_Error ("svc_lightstyle > MAX_LIGHTSTYLES");

				Q_strcpy (cl_lightstyle[i].map,	 MSG_ReadString());
				cl_lightstyle[i].length = Q_strlen(cl_lightstyle[i].map);
				

				#if 0 // not now....

				//johnfitz -- save extra info
				if (cl_lightstyle[i].length)
				{
					total = 0;
					cl_lightstyle[i].peak = 'a';

					for (j=0; j<cl_lightstyle[i].length; j++)
					{
						total += cl_lightstyle[i].map[j] - 'a';
						cl_lightstyle[i].peak = q_max(cl_lightstyle[i].peak, cl_lightstyle[i].map[j]);
					}

					cl_lightstyle[i].average = total / cl_lightstyle[i].length + 'a';
				}

				else
					cl_lightstyle[i].average = cl_lightstyle[i].peak = 'm';
				//johnfitz

				#endif


				break;
      
			case svc_sound:
				CL_ParseStartSoundPacket();
				break;
      
			case svc_stopsound:
				i = MSG_ReadShort();
				S_StopSound(i>>3, i&7);
				break;
    
			case svc_updatename:
				Sbar_Changed ();
				i = MSG_ReadByte ();

				if (i >= cl.maxclients)
					Host_Error ("CL_ParseServerMessage: svc_updatename > MAX_SCOREBOARD");

				strcpy (cl.scores[i].name, MSG_ReadString ());
				break;
      
			case svc_updatefrags:
				Sbar_Changed ();
				i = MSG_ReadByte ();

				if (i >= cl.maxclients)
					Host_Error ("CL_ParseServerMessage: svc_updatefrags > MAX_SCOREBOARD");

				cl.scores[i].frags = MSG_ReadShort ();
				break;	    

			case svc_updatecolors:
				Sbar_Changed ();
				i = MSG_ReadByte ();

				if (i >= cl.maxclients)
					Host_Error ("CL_ParseServerMessage: svc_updatecolors > MAX_SCOREBOARD");

				cl.scores[i].colors = MSG_ReadByte ();
				CL_NewTranslation (i);
				break;
      
			case svc_particle:
				R_ParseParticleEffect ();
				break;

			case svc_spawnbaseline:
				i = MSG_ReadShort ();
				// must use CL_EntityNum() to force cl.num_entities up
				CL_ParseBaseline (CL_EntityNum(i), 1); // johnfitz
				break;

			case svc_spawnstatic:
				CL_ParseStatic (1); // johnfitz
				break;
    
			case svc_temp_entity:
				CL_ParseTEnt ();
				break;

			case svc_setpause:
				cl.paused = MSG_ReadByte ();

				if (cl.paused)
				{
					CDAudio_Pause ();

					#ifdef _WIN32
					VID_HandlePause (true);
					#endif
				}

				else
				{
					CDAudio_Resume ();

					#ifdef _WIN32
					VID_HandlePause (false);
					#endif
				}

				break;
      
			case svc_signonnum:
				i = MSG_ReadByte ();

				if (i <= cls.signon)
					Host_Error ("Received signon %i when at %i", i, cls.signon);

				cls.signon = i;

				//johnfitz -- if signonnum==2, signon packet has been fully parsed, so check for excessive static ents and efrags
				if (i == 2)
				{
					if (cl.num_statics > 128)
						Con_Printf ("%i static entities exceeds standard limit of 128.\n", cl.num_statics);

					//R_CheckEfrags (); // devstats johnfitz - Cowcat 2019
				}
				//johnfitz

				CL_SignonReply ();
				break;

			case svc_killedmonster:
				cl.stats[STAT_MONSTERS]++;
				break;

			case svc_foundsecret:
				cl.stats[STAT_SECRETS]++;
				break;

			case svc_updatestat:
				i = MSG_ReadByte ();

				if (i < 0 || i >= MAX_CL_STATS)
					//Sys_Error ("svc_updatestat: %i is invalid", i);
					Host_Error ("svc_updatestat: %i is invalid", i);

				cl.stats[i] = MSG_ReadLong ();;
				break;
      
			case svc_spawnstaticsound:
				CL_ParseStaticSound (1); // johnfitz
				break;

			case svc_cdtrack:
				cl.cdtrack = MSG_ReadByte ();
				cl.looptrack = MSG_ReadByte ();

				if ( (cls.demoplayback || cls.demorecording) && (cls.forcetrack != -1) )
					CDAudio_Play ((byte)cls.forcetrack, true);

				else
					CDAudio_Play ((byte)cl.cdtrack, true);

				break;

			case svc_intermission:
				cl.intermission = 1;
				cl.completed_time = cl.time;
				vid.recalc_refdef = true; // go to full screen
				break;

			case svc_finale:
				cl.intermission = 2;
				cl.completed_time = cl.time;
				vid.recalc_refdef = true; // go to full screen
				SCR_CenterPrint (MSG_ReadString ());	  
				break;

			case svc_cutscene:
				cl.intermission = 3;
				cl.completed_time = cl.time;
				vid.recalc_refdef = true; // go to full screen
				SCR_CenterPrint (MSG_ReadString ());	  
				break;

			case svc_sellscreen:
				Cmd_ExecuteString ("help", src_command);
				break;

			//johnfitz -- new svc types
			case svc_skybox:
				//Sky_LoadSkyBox (MSG_ReadString());
				R_LoadSkys();
				break;

			case svc_bf:
				Cmd_ExecuteString ("bf", src_command);
				break;

			case svc_fog:
				if(MSG_ReadByte())
				{
					Cvar_SetValue ("gl_fogdensity", MSG_ReadFloat());
					Cvar_SetValue ("gl_fogred", MSG_ReadByte() / 255.0);
					Cvar_SetValue ("gl_foggreen", MSG_ReadByte() / 255.0);
					Cvar_SetValue ("gl_fogdensity", MSG_ReadByte() / 255.0);
					Cvar_SetValue ("gl_fog", 1);
				}

				else
				{
					Cvar_SetValue ("gl_fog", 0);
					Cvar_SetValue ("gl_fogdensity", 0);
				}
									
				break;

			case svc_spawnbaseline2: //PROTOCOL_FITZQUAKE
				i = MSG_ReadShort ();
				// must use CL_EntityNum() to force cl.num_entities up
				CL_ParseBaseline (CL_EntityNum(i), 2);
				break;

			case svc_spawnstatic2: //PROTOCOL_FITZQUAKE
				CL_ParseStatic (2);
				break;

			case svc_spawnstaticsound2: //PROTOCOL_FITZQUAKE
				CL_ParseStaticSound (2);
				break;
			//johnfitz
			
		}

		lastcmd = cmd; //johnfitz
	}
}

