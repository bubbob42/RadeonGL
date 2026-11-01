/*
** sys_amiga.c
**
** Quake for Amiga M68k and Amiga PowerPC
**
** Written by Frank Wille <frank@phoenix.owl.de>
**
*/

#pragma pack(push,2)

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <utility/tagitem.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <proto/keymap.h> // new 2019 - Cowcat

#if defined(__PPC__)
#if defined(__GNUC__)
#include <powerpc/powerpc.h>
#include <powerpc/powerpc_protos.h>
#else
#include <powerpc/powerpc.h>
#include <proto/powerpc.h>
#endif
#endif

#pragma pack(pop)

#include "quakedef.h"
#define MAX_HANDLES	10

//#define CLOCK_PAL	  3546895  /* at snd_paula.c */
//#define CLOCK_NTSC	  3579545

#ifdef __VBCC__
#define CreateExtIO(p,s) CreateIORequest(p,s)
#define DeletePort(p) DeleteMsgPort(p)
#define DeleteExtIO(p) DeleteIORequest(p)
#endif

//struct Library *UtilityBase = NULL; // in mgl - needed ? Cowcat 2022
#ifdef __GNUC__
struct Device *TimerBase = NULL;
#else //VBCC
struct Library *TimerBase = NULL;
#endif

struct Library *SocketBase; // moved because of GCC problem
struct Library *LowLevelBase = NULL;
struct Library *KeymapBase = 0; // new 2019 - Cowcat

//struct Library *MathIeeeDoubBasBase;	/* added now also for m68k (temporary vbcc fpu bug).Cowcat */

#if defined(__PPC__)
struct Library *MathIeeeDoubBasBase;
qboolean no68kFPU = false;		/* for LC040/LC060 systems */
#endif


#if !defined (__GNUC__)

#ifndef GLQUAKE

unsigned long __stack = 0x80000;  /* program requires 512k stack */

#else

#if defined(__PPC__)
unsigned long __stack = 0x100000 ;  // now 1024 for WOS + Sonnet 
#else
unsigned long __stack = 0xC0000 ;  /* program requires 768k stack */
#endif

#endif

#endif // no __GNUC__

/* clib2 reads __stack_size (its own dos.h); __stack above is libnix's name and
 * clib2 references it nowhere, so under -mcrt=clib2 that request is dead -- and
 * here it is not even compiled, sitting inside `#if !defined (__GNUC__)`.
 * Verified with m68k-amigaos-nm: clib2/lib/libc.a defines and reads
 * ___stack_size, libnix*.a reads ___stack, and every GCC-built glquake binary
 * carried ___stack_size as BSS 0 (clib2's "no stack test at all" default).
 * 768K matches what the libnix declaration above always asked for. */
#ifdef __CLIB2__
unsigned int __stack_size = 0xC0000;
#endif


/* these are for snd_amiga.c */
qboolean isDedicated = false;
//long sysclock = CLOCK_PAL;
int FirstTime = 0;
int FirstTime2 = 0;

static BPTR amiga_stdin, amiga_stdout;
static BPTR sys_handles[MAX_HANDLES];
static struct timerequest *timerio;
static int membase_offs = 32;
static int nostdout=0, coninput=0;

#ifndef __PPC__
extern void MMUHackOn(void);
extern void MMUHackOff(void);
#endif

static int taskp; /* added task priority Cowcat */


/*
===============================================================================

FILE IO

===============================================================================
*/

static int findhandle (void)
{
	int i;
  
	for (i=1 ; i<MAX_HANDLES ; i++)
		if (!sys_handles[i])

	return i;

	Sys_Error ("out of handles");
	return -1;
}

int Sys_FileOpenRead (char *path, int *hndl)
{
	BPTR			fh;
	struct FileInfoBlock	*fib;
	int			i = -1;
	int			flen = -1;
  
	*hndl = -1;

	if ( (fib = AllocDosObjectTags(DOS_FIB,TAG_DONE)) )
	{
		if ( (fh = Open(path,MODE_OLDFILE)) )
		{
			if (ExamineFH(fh,fib))
			{
				if ((i = findhandle()) > 0)
				{
					sys_handles[i] = fh;
					*hndl = i;
					flen = (int)fib->fib_Size;
				}

				else
					Close(fh);
			}

			else
				Close(fh);
		}

		FreeDosObject(DOS_FIB,fib);
	}

	return flen;
}

int Sys_FileOpenWrite (char *path)
{
	BPTR fh;
	int  i;
  
	if ((i = findhandle ()) > 0)
	{
		if ( (fh = Open(path,MODE_NEWFILE)) )
		{
			sys_handles[i] = fh;
		}

		else
		{
			char ebuf[80];
			Fault(IoErr(),"",ebuf,80);
			Sys_Error ("Error opening %s: %s", path, ebuf);
			i = -1;
		}
	}

	return i;
}

void Sys_FileClose (int handle)
{
	if (sys_handles[handle])
	{
		Close(sys_handles[handle]);
		sys_handles[handle] = 0;
	}
}

void Sys_FileSeek (int handle, int position)
{
	Seek(sys_handles[handle],position,OFFSET_BEGINNING);
}

int Sys_FileRead (int handle, void *dest, int count)
{
	return (int)Read(sys_handles[handle],dest,(LONG)count);
	//return (int)fread(dest, 1, count, sys_handles[handle]); // bad test - Cowcat
}

int Sys_FileWrite (int handle, void *data, int count)
{
	return (int)Write(sys_handles[handle],data,(LONG)count);
}

int Sys_FileTime (char *path)
{
	BPTR lck;
	int  t = -1;

	if ( (lck = Lock(path,ACCESS_READ)) )
	{
		t = 1;
		UnLock(lck);
	}

	return t;
}

void Sys_mkdir (char *path)
{
	BPTR lck;

	if ( (lck = CreateDir(path)) )
		UnLock(lck);
}


/*
===============================================================================

SYSTEM Functions

===============================================================================
*/

/*
================
usleep
================
*/
void usleep(unsigned long timeout)
{
	timerio->tr_node.io_Command = TR_ADDREQUEST;
	timerio->tr_time.tv_secs = timeout / 1000000;
	timerio->tr_time.tv_micro = timeout % 1000000;
	SendIO(&timerio->tr_node);
	WaitIO(&timerio->tr_node);
}


static qboolean prevent_crash = true; //surgeon

static void cleanup(int rc)
{
	int i;

	if(prevent_crash)
		Host_Shutdown();

	if (coninput)
		SetMode(amiga_stdin, 0);	/* put console back into normal CON mode */

	if (host_parms.membase)
		FreeMem((byte *)host_parms.membase-membase_offs, host_parms.memsize+3*32);

#ifndef __PPC__
	if (COM_CheckParm("-hack"))
		MMUHackOff();
#endif

	if (TimerBase)
	{
		if (!CheckIO((struct IORequest *)timerio))
		{
			AbortIO((struct IORequest *)timerio);
			WaitIO((struct IORequest *)timerio);
		}

		CloseDevice((struct IORequest *)timerio);
		//DeletePort(timerio->tr_node.io_Message.mn_ReplyPort);
		//DeleteExtIO((struct IORequest *)timerio);
		DeleteMsgPort(timerio->tr_node.io_Message.mn_ReplyPort);
		DeleteIORequest((struct IORequest *)timerio);
	}

#ifdef __PPC__
	if (no68kFPU)
	{
		if (MathIeeeDoubBasBase)
			CloseLibrary(MathIeeeDoubBasBase);
	}
#endif

	if(SocketBase)
	{
		CloseLibrary(SocketBase);
		SocketBase = NULL;
	}

	if (KeymapBase)
	{
		CloseLibrary(KeymapBase);
		KeymapBase = NULL;
	}

	for (i=1; i<MAX_HANDLES; i++)
	{
		if (sys_handles[i])
			Close(sys_handles[i]);
	}

	if (LowLevelBase)
		CloseLibrary(LowLevelBase);

	//if (UtilityBase)
		//CloseLibrary(UtilityBase); //closed at mgl.lib - Cowcat 2022

	exit(rc);
}


void Sys_MakeCodeWriteable (unsigned long startaddr, unsigned long length)
{
}


void Sys_DebugLog(char *file, char *fmt, ...)
{
	va_list argptr; 
	static char data[1024];
	BPTR fh;

	va_start(argptr, fmt);
	vsprintf(data, fmt, argptr);
	va_end(argptr);

	if ( (fh = Open(file,MODE_READWRITE)) )
	{
		Seek(fh,0,OFFSET_END);
		Write(fh,data,(LONG)strlen(data));
		Close(fh);
	}
}

#if defined(__GNUC__)
__attribute__((noinline))
#endif
void Sys_Error (char *error, ...)
{
	/* noinline (GCC-only): see common.c's va() for the root-cause trace. */
	va_list argptr;

	// Fix udp_init validation network hang - Hedeon
/*
#ifdef GLQUAKE //surgeon
	if (cls.state == ca_connected)
		CL_Disconnect ();

	Host_Shutdown();	//avoid crashing
	prevent_crash = false;
#endif
*/
	printf ("I_Error: ");	
	va_start (argptr,error);
	vprintf (error,argptr);
	va_end (argptr);
	printf ("\n");

	if (cls.state == ca_connected)
	{
		printf("disconnecting from server...\n");
		CL_Disconnect ();	/* leave server gracefully (phx) */
	}

	prevent_crash = true; // Hedeon

	cleanup(1);
}

#if defined(__GNUC__)
__attribute__((noinline))
#endif
void Sys_Printf (char *fmt, ...)
{
	/* noinline (GCC-only): see common.c's va() for the root-cause trace. */
	if (!nostdout)
	{
		va_list argptr;

		va_start (argptr,fmt);
		vprintf (fmt,argptr);
		va_end (argptr);
	}
}

void Sys_Quit (void)
{
	//prevent_crash = true; // Cowcat
	cleanup(0);
}

double Sys_FloatTime (void)
{
	struct timeval tv;

	#ifdef __PPC__
	GetSysTimePPC(&tv);
	#else
	GetSysTime(&tv);
	#endif

	return ((double)(tv.tv_secs-FirstTime) + (((double)tv.tv_micro) / 1000000.0));
}

long Sys_Milliseconds(void)
{
	struct timeval tv;

	#ifdef __PPC__ 
	GetSysTimePPC(&tv);
	#else
	GetSysTime(&tv);
	#endif

	return (tv.tv_secs-FirstTime)*1000 + tv.tv_micro/1000;
}

char *Sys_ConsoleInput (void)
{
	if (coninput)
	{
		static const char *backspace = "\b \b";
		static char text[256];
		static int len = 0;
		char ch;

		while (WaitForChar(amiga_stdin,10) == DOSTRUE)
		{
			Read(amiga_stdin,&ch,1);	/* note: console is in RAW mode! */

			if (ch == '\r')
			{
				ch = '\n';
				Write(amiga_stdout,&ch,1);
				text[len] = 0;
				len = 0;
				return text;
			}

			else if (ch == '\b')
			{
				if (len > 0)
				{
					len--;
					Write(amiga_stdout,(char *)backspace,3);
				}
			}

			else
			{
				if (len < sizeof(text)-1)
				{
					Write(amiga_stdout,&ch,1);
					text[len++] = ch;
				}
			}
		}
	}

	return NULL;
}

void Sys_Sleep (void)
{
}

void Sys_HighFPPrecision (void)
{
}

void Sys_LowFPPrecision (void)
{
}

int Sys_MapRawKey(int rawkey) // new 2019 - Cowcat
{
	struct InputEvent ie;
	UBYTE buf[4];
	WORD result;

	UWORD Code = rawkey & 0xFF;
	UWORD Qualifier = (rawkey >> 8) % 0xFFFF;

	switch(Code)
	{
		case 65:
			return -1;

		default:
			ie.ie_Class = IECLASS_RAWKEY;
			ie.ie_SubClass = 0;
			ie.ie_Code = Code;
			ie.ie_Qualifier = Qualifier;
			ie.ie_EventAddress = NULL;

			result = MapRawKey(&ie, buf, 4, 0);
				
			if (result != 1 )
				return -1;

			else
				return buf[0];
	}

	return -1;
}
	

//=============================================================================

int main (int argc, char *argv[])
{
	extern		int vcrFile;
	extern		int recording;
	struct timeval	tv;
	quakeparms_t	parms;
	double		time, oldtime, newtime;
	struct MsgPort	*timerport;
	char		cwd[128];
	int i;

	memset(&parms,0,sizeof(parms));
	parms.memsize = 16*1024*1024;	/* 16MB is default */

	/* parse command string */
	COM_InitArgv (argc, argv);
	parms.argc = com_argc;
	parms.argv = com_argv;
	host_parms.membase = parms.cachedir = NULL;

#ifndef __PPC__
	if (COM_CheckParm("-hack"))
		MMUHackOn();
#endif

	/* Amiga Init */
	//if((UtilityBase = OpenLibrary("utility.library",36)) == NULL || (LowLevelBase = OpenLibrary("lowlevel.library",36)) == NULL )
	if ( (LowLevelBase = OpenLibrary("lowlevel.library",36)) == NULL ) // joy support and more ? Cowcat 2024 test
		Sys_Error("OS2.0 required!"); // needed ? Cowcat 2022

#ifdef __PPC__
	no68kFPU = COM_CheckParm("-no68kfpu") != 0;

	if (no68kFPU)
	{
		if (!(MathIeeeDoubBasBase = OpenLibrary("mathieeedoubbas.library",36)))
			Sys_Error("mathieeedoubbas.library is missing");
	}
#endif
	
	if(SocketBase == NULL)
		SocketBase = OpenLibrary("bsdsocket.library", 0L);

	if (!KeymapBase)
		KeymapBase = OpenLibrary("keymap.library", 0); // new 2019 - Cowcat

	amiga_stdin = Input();
	amiga_stdout = Output();

	isDedicated = COM_CheckParm("-dedicated") != 0;
	nostdout = !(COM_CheckParm("-stdout"));
	coninput = COM_CheckParm("-console");

	if (coninput)
		SetMode(amiga_stdin, 1);	 /* put console into RAW mode */

	/* open timer.device */
	if ( (timerport = CreateMsgPort()) )
	{
		if ( (timerio = (struct timerequest *) CreateIORequest(timerport,sizeof(struct timerequest))) )
		{
			if (OpenDevice(TIMERNAME,UNIT_MICROHZ, (struct IORequest *)timerio,0) == 0)
			{
#ifdef __GNUC__
				TimerBase = (struct Device *)timerio->tr_node.io_Device;
#else //VBCC
				TimerBase = (struct Library *)timerio->tr_node.io_Device;
#endif
			}

			else
			{
				DeleteIORequest((struct IORequest *)timerio);
				DeleteMsgPort(timerport);
			}
		}

		else
			DeleteMsgPort(timerport);
	}

	if (!TimerBase)
		Sys_Error("Can't open timer.device");

	usleep(1);  /* don't delete, otherwise we can't do timer.device cleanup */

	#ifdef __PPC__

	GetSysTimePPC(&tv);
	FirstTime = tv.tv_secs;
	GetSysTime(&tv);	
	FirstTime2 = tv.tv_secs;

	#else

	GetSysTime(&tv);
	FirstTime2 = FirstTime = tv.tv_secs;

	#endif

	if ( (i = COM_CheckParm("-mem")) )
		parms.memsize = (int)(Q_atof(com_argv[i+1]) * 1024 * 1024);

	/* alloc 16-byte aligned quake memory */
	if ( (i =! COM_CheckParm("-malloc_unaligned")) )	//Surgeon
		parms.memsize = (parms.memsize+15)&~15;

	if ( (parms.membase = AllocMem((ULONG)parms.memsize, MEMF_FAST)) == NULL )
		Sys_Error("Can't allocate %ld bytes\n", parms.memsize);

	if ( (ULONG)parms.membase & 8 )
		membase_offs = 40;	/* guarantee 16-byte aligment */

	else
		membase_offs = 32;


	parms.membase = (char *)parms.membase + membase_offs;
	parms.memsize -= 3*32;

	/* get name of current directory */
	if ( (i = COM_CheckParm("-dir")) )
		strcpy(cwd,com_argv[i+1]);

	else
		GetCurrentDirName(cwd,128);

	parms.basedir = cwd;

	/* task priority Cowcat */

	if ( (i = COM_CheckParm("-startpri")) )
	{
		taskp = Q_atoi(com_argv[i+1]);

		if (!taskp) taskp = -3;
				SetTaskPri( FindTask (NULL), taskp);
	}

#if 0
	/* set the clock constant */  at snd_paula.c 

	if (GfxBase = (struct GfxBase *)OpenLibrary("graphics.library",36))
	{
		if (GfxBase->DisplayFlags & REALLY_PAL)	 //changed PAL
			sysclock = CLOCK_PAL;

		else
			sysclock = CLOCK_NTSC;

		CloseLibrary((struct Library *)GfxBase);
	}
#endif

#if defined(MGLV3D_CBUF_MINITEST)
	/* Isolated repro: skip Host_Init (VID/WAD/V3D) entirely, just exercise
	 * Memory_Init/Cbuf_Init/Cbuf_InsertText directly -- tests whether the
	 * zone allocator (Z_Malloc/Z_Free) or Cbuf_InsertText itself is the
	 * exit11 hang, decoupled from any display/V3D state at all. */
	host_parms = parms;
	Memory_Init(parms.membase, parms.memsize);
	Cbuf_Init();
	printf("MGLV3D_CBUF_MINITEST: Memory_Init+Cbuf_Init done\n");
	Cbuf_InsertText("exec quake.rc\n");
	printf("MGLV3D_CBUF_MINITEST: first Cbuf_InsertText done, cursize now nonzero\n");
	Cbuf_InsertText("exec quake2.rc\n");
	printf("MGLV3D_CBUF_MINITEST: second Cbuf_InsertText done (exercised the templen>0 copy-off path)\n");
	printf("MGLV3D_CBUF_MINITEST: clean exit, no crash\n");
	exit(0);
#endif

	Host_Init (&parms);
	oldtime = Sys_FloatTime () - 0.1;

	while (1)
	{
		newtime = Sys_FloatTime ();
		time = newtime - oldtime;

		if (cls.state == ca_dedicated)
		{
			if (time < sys_ticrate.value && (vcrFile == -1 || recording))
			{
				usleep(1);
				continue;
			}

			time = sys_ticrate.value;
		}

		if (time > sys_ticrate.value*2)
			oldtime = newtime;

		else
			oldtime += time;

		Host_Frame (time);
	}

	return 0;
}
