/*
** sys_amiga.c
**
** Quake for Amiga M68k and Amiga PowerPC
**
** Written by Frank Wille <frank@phoenix.owl.de>
**
*/

#pragma amiga-align
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <utility/tagitem.h>
#include <graphics/gfxbase.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <clib/alib_protos.h>
#if defined(__PPC__) && !defined(MORPHOS)
#ifdef WOS
#ifndef __GNUC__
#include <clib/powerpc_protos.h>
#else
#include <powerpc/powerpc_protos.h>
#endif
#else
#include <powerup/ppclib/time.h>
#include <powerup/gcclib/powerup_protos.h>
#endif
#endif
#pragma default-align

#include "quakedef.h"
#if defined(__PPC__) && !defined(WOS)
#include "sys_timer.h"  /* GetSysTimePPC() emulation for PowerUp */
#endif

#define MAX_HANDLES     10
#define CLOCK_PAL       3546895
#define CLOCK_NTSC      3579545


#ifdef __STORM__
#define CreatePort(x,y) CreateMsgPort()
#define CreateExtIO(p,s) CreateIORequest(p,s)
#define DeletePort(p) DeleteMsgPort(p)
#define DeleteExtIO(p) DeleteIORequest(p)
extern struct Library *TimerBase;
extern struct Library *UtilityBase;
#else
	#ifdef __GNUC__
	struct Library *UtilityBase = NULL;
	struct Device *TimerBase = NULL;
	#else //VBCC
	struct Library *TimerBase = NULL;
	struct Library *UtilityBase = NULL;
	#endif
#endif
struct Library *LowLevelBase = NULL;
#ifdef __PPC__
#ifdef __STORM__
extern struct Library *MathIeeeDoubBasBase = NULL;
#else
struct Library *MathIeeeDoubBasBase;
#endif
qboolean no68kFPU = false; /* for LC040/LC060 systems */
#endif

#ifndef GLQUAKE

static const char *_ver = "$VER: QuakeAmiga " AMIGA_VERSTRING "\r\n";
unsigned long __stack = 0x40000;  /* program requires 256k stack */

#else

unsigned long __stack = 0x100000;  /* program requires 1024k stack */

#endif

/* these are for snd_amiga.c */
qboolean isDedicated = false;
long sysclock = CLOCK_PAL;
int FirstTime = 0;
int FirstTime2 = 0;

static BPTR amiga_stdin,amiga_stdout;
static BPTR sys_handles[MAX_HANDLES];
static struct timerequest *timerio;
static int membase_offs = 32;
static int nostdout=0, coninput=0;

#ifndef __PPC__
extern void MMUHackOn(void);
extern void MMUHackOff(void);
#endif


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
  BPTR fh;
  struct FileInfoBlock *fib;
  int     i = -1;
  int flen = -1;
  
  *hndl = -1;
  if (fib = AllocDosObjectTags(DOS_FIB,TAG_DONE)) {
    if (fh = Open(path,MODE_OLDFILE)) {
      if (ExamineFH(fh,fib)) {
        if ((i = findhandle()) > 0) {
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
  
  if ((i = findhandle ()) > 0) {
    if (fh = Open(path,MODE_NEWFILE)) {
      sys_handles[i] = fh;
    }
    else {
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
  if (sys_handles[handle]) {
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
}

int Sys_FileWrite (int handle, void *data, int count)
{
  return (int)Write(sys_handles[handle],data,(LONG)count);
}

int Sys_FileTime (char *path)
{
  BPTR lck;
  int  t = -1;

  if (lck = Lock(path,ACCESS_READ)) {
    t = 1;
    UnLock(lck);
  }

  return t;
}

void Sys_mkdir (char *path)
{
  BPTR lck;

  if (lck = CreateDir(path))
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

/*
================
strcasecmp
================
*/
int strcasecmp(const char *str1, const char *str2)
{
  while(tolower((unsigned char)*str1) == tolower((unsigned char)*str2)) {
    if(!*str1) return(0);
    str1++;str2++;
  }
  return(tolower(*(unsigned char *)str1)-tolower(*(unsigned char *)str2));
}

/*
================
strncasecmp
================
*/
int strncasecmp(const char *str1, const char *str2, size_t n)
{
  if (n==0) return 0;
  while (--n && tolower((unsigned char)*str1)==tolower((unsigned char)*str2)){
    if (!*str1) return(0);
    str1++; str2++;
  }
  return(tolower(*(unsigned char *)str1)-tolower(*(unsigned char *)str2));
}

static qboolean prevent_crash = true; //surgeon

static void cleanup(int rc)
{
  int i;

  if(prevent_crash)
  Host_Shutdown();

  if (coninput)
    SetMode(amiga_stdin,0);  /* put console back into normal CON mode */

  if (host_parms.membase)
    FreeMem((byte *)host_parms.membase-membase_offs,host_parms.memsize+3*32);

#ifndef __PPC__
  if (COM_CheckParm("-hack"))
  MMUHackOff();
#endif

  if (TimerBase) {
    if (!CheckIO((struct IORequest *)timerio)) {
      AbortIO((struct IORequest *)timerio);
      WaitIO((struct IORequest *)timerio);
    }
    CloseDevice((struct IORequest *)timerio);
    DeletePort(timerio->tr_node.io_Message.mn_ReplyPort);
    DeleteExtIO((struct IORequest *)timerio);
  }

#ifdef __PPC__
  if (no68kFPU) {
    if (MathIeeeDoubBasBase)
      CloseLibrary(MathIeeeDoubBasBase);
  }
#endif

  if (LowLevelBase)
    CloseLibrary(LowLevelBase);
  if (UtilityBase)
    CloseLibrary(UtilityBase);

  for (i=1; i<MAX_HANDLES; i++) {
    if (sys_handles[i])
      Close(sys_handles[i]);
  }
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
  if (fh = Open(file,MODE_READWRITE)) {
    Seek(fh,0,OFFSET_END);
    Write(fh,data,(LONG)strlen(data));
    Close(fh);
  }
}

void Sys_Error (char *error, ...)
{
  va_list         argptr;

#ifdef GLQUAKE //surgeon
  if (cls.state == ca_connected)
    CL_Disconnect ();

  Host_Shutdown();
  prevent_crash = false;
#endif

  printf ("I_Error: ");   
  va_start (argptr,error);
  vprintf (error,argptr);
  va_end (argptr);
  printf ("\n");

  if (cls.state == ca_connected) {
    printf("disconnecting from server...\n");
    CL_Disconnect ();   /* leave server gracefully (phx) */
  }
  cleanup(1);
}

void Sys_Printf (char *fmt, ...)
{
  if (!nostdout) {
    va_list         argptr;

    va_start (argptr,fmt);
    vprintf (fmt,argptr);
    va_end (argptr);
  }
}

void Sys_Quit (void)
{
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

char *Sys_ConsoleInput (void)
{
  if (coninput) {
    static const char *backspace = "\b \b";
    static char text[256];
    static int len = 0;
    char ch;

    while (WaitForChar(amiga_stdin,10) == DOSTRUE) {
      Read(amiga_stdin,&ch,1);  /* note: console is in RAW mode! */
      if (ch == '\r') {
        ch = '\n';
        Write(amiga_stdout,&ch,1);
        text[len] = 0;
        len = 0;
        return text;
      }
      else if (ch == '\b') {
        if (len > 0) {
          len--;
          Write(amiga_stdout,(char *)backspace,3);
        }
      }
      else {
        if (len < sizeof(text)-1) {
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


//=============================================================================

main (int argc, char *argv[])
{
  extern int vcrFile;
  extern int recording;
  struct timeval tv;
  quakeparms_t parms;
  double time, oldtime, newtime;
  struct MsgPort *timerport;
  char cwd[128];
  struct GfxBase *GfxBase;
  int i;

  memset(&parms,0,sizeof(parms));
  parms.memsize = 16*1024*1024;  /* 16MB is default */

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
  if ((UtilityBase = OpenLibrary("utility.library",36)) == NULL ||
      (LowLevelBase = OpenLibrary("lowlevel.library",36)) == NULL)
    Sys_Error("OS2.0 required!");
#ifdef __PPC__
  no68kFPU = COM_CheckParm("-no68kfpu") != 0;
  if (no68kFPU) {
     if (!(MathIeeeDoubBasBase = OpenLibrary("mathieeedoubbas.library",36)))
       Sys_Error("mathieeedoubbas.library is missing");
  }
#endif
  amiga_stdin = Input();
  amiga_stdout = Output();

  isDedicated = COM_CheckParm("-dedicated") != 0;
  nostdout = !COM_CheckParm("-stdout");
  coninput = COM_CheckParm("-console");
  if (coninput)
    SetMode(amiga_stdin,1);  /* put console into RAW mode */

  /* open timer.device */
  if (timerport = CreatePort(NULL,0)) {
    if (timerio = (struct timerequest *)
                   CreateExtIO(timerport,sizeof(struct timerequest))) {
      if (OpenDevice(TIMERNAME,UNIT_MICROHZ,
                     (struct IORequest *)timerio,0) == 0) {
        TimerBase = (struct Device *)timerio->tr_node.io_Device;
      }
      else {
        DeleteExtIO((struct IORequest *)timerio);
        DeletePort(timerport);
      }
    }
    else
      DeletePort(timerport);
  }

  if (!TimerBase)
    Sys_Error("Can't open timer.device");
  usleep(1);  /* don't delete, otherwise we can't do timer.device cleanup */

#if defined(__PPC__) && !defined(WOS)
  /* init GetSysTimePPC() emulation for PowerUp */
  InitSysTimePPC();
#endif

#ifdef __PPC__
  GetSysTimePPC(&tv);
  FirstTime = tv.tv_secs;
  GetSysTime(&tv);
  FirstTime2 = tv.tv_secs;
#else
  GetSysTime(&tv);
  FirstTime2 = FirstTime = tv.tv_secs;
#endif

  if (i = COM_CheckParm("-mem"))
    parms.memsize = (int)(Q_atof(com_argv[i+1]) * 1024 * 1024);

  /* alloc 16-byte aligned quake memory */
  parms.memsize = (parms.memsize+15)&~15;
  if ((parms.membase = AllocMem((ULONG)parms.memsize,MEMF_FAST)) == NULL)
    Sys_Error("Can't allocate %ld bytes\n", parms.memsize);
  if ((ULONG)parms.membase & 8)
    membase_offs = 40;  /* guarantee 16-byte aligment */
  else
    membase_offs = 32;
  parms.membase = (char *)parms.membase + membase_offs;
  parms.memsize -= 3*32;

  /* get name of current directory */
  if (i = COM_CheckParm("-dir"))
    strcpy(cwd,com_argv[i+1]);
  else
    GetCurrentDirName(cwd,128);
  parms.basedir = cwd;

  /* set the clock constant */
  if (GfxBase = (struct GfxBase *)OpenLibrary("graphics.library",36)) {
    if (GfxBase->DisplayFlags & PAL)
      sysclock = CLOCK_PAL;
    else
      sysclock = CLOCK_NTSC;
    CloseLibrary((struct Library *)GfxBase);
  }

  Host_Init (&parms);
  oldtime = Sys_FloatTime () - 0.1;

  while (1) {
    newtime = Sys_FloatTime ();
    time = newtime - oldtime;

    if (cls.state == ca_dedicated) {
      if (time < sys_ticrate.value && (vcrFile == -1 || recording)) {
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
}
