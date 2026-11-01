/*
**  snd_paula.c
**
**  Paula sound driver
**
**  Written by Frank Wille <frank@phoenix.owl.de>
**
*/

#pragma pack(push,2)

#include <exec/memory.h>
#include <exec/tasks.h>
#include <exec/interrupts.h>
#include <exec/libraries.h>
#include <devices/audio.h>
#include <hardware/custom.h>
#include <hardware/cia.h>
#include <hardware/intbits.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <clib/alib_protos.h>
//#include <proto/graphics.h>
#include <graphics/gfxbase.h>

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

#define NSAMPLES 0x8000

#define CLOCK_PAL       3546895
#define CLOCK_NTSC      3579545

/* custom chip base address */
static volatile struct Custom *custom = (struct Custom *)0xdff000;

/* CIA A base address */
static volatile struct CIA *cia_a = (struct CIA *)0xbfe001;

static UBYTE *dmabuf = NULL;
static UWORD period;
static BYTE audio_dev = -1;
static float speed;
static struct MsgPort *audioport1=NULL,*audioport2=NULL;
static struct IOAudio *audio1=NULL,*audio2=NULL;
static struct Interrupt AudioInt;
static struct Interrupt *OldInt;
static UWORD OldINTENA = 0;
static UWORD aud_intflag;
static UBYTE saved_filter;

//#pragma pack(push,2) // test Cowcat 2022
static struct
{
	#if defined(__VBCC__) || (defined(__GNUC__) && !defined(__PPC__))
	struct Device *TimerBase;
	#else
  	struct Library *TimerBase;
	#endif

  	int *FirstTime2;
	/* volatile (2026-08-09, hardware-confirmed): the audio interrupt
	   handler (snd_int68k.s/snd_int68k_gas.s) writes *aud_start_time
	   asynchronously, invisible to the compiler's own data-flow
	   analysis. SNDDMA_InitPaula's "while (!(*IntData.aud_start_time));"
	   busy-wait has no write to this memory anywhere in its own visible
	   code, so without volatile a sufficiently aggressive optimizer is
	   free to hoist the read out of the loop entirely -- an infinite
	   loop regardless of what the interrupt ever does. Confirmed this
	   is exactly what happened under GCC: audio.device opened
	   successfully (that already completes before this loop), then
	   execution hung with zero further OS calls and a pegged CPU --
	   consistent with a hoisted read, not with the interrupt genuinely
	   never firing. vbcc's much less aggressive optimizer apparently
	   never attempts this same hoist on the identical source. Assembly
	   is untouched by this -- volatile is a C-compiler-only concept,
	   the interrupt handler just writes raw memory either way. */
  	volatile double *aud_start_time;

	#if defined(__PPC__)
  	struct Library *MathIeeeDoubBasBase; /* offset 12 */
	#endif

} IntData;
//#pragma pack(pop)

//extern long sysclock;
long sysclock = CLOCK_PAL;

extern int desired_speed;
extern int FirstTime2;

/* vbcc: redeclaration of var <TimerBase> with new type */
/* extern struct Device *TimerBase; */

#if defined(__PPC__)
extern struct Library *MathIeeeDoubBasBase;
extern qboolean no68kFPU; 		/* for PPC LC040/LC060 systems */
extern void AudioIntCodeNoFPU(void);
#endif

extern void AudioIntCode(void);

void (*audintptr)(void) = AudioIntCode;


void SNDDMA_PaulaFilterOn(void)
{
	cia_a->ciapra &= ~CIAF_LED;
}


void SNDDMA_PaulaFilterOff(void)
{
	cia_a->ciapra |= CIAF_LED;
}

void SNDDMA_ShutdownPaula(void)
{
	if (OldINTENA)
	{
    		cia_a->ciapra &= ~CIAF_LED;
    		cia_a->ciapra |= saved_filter;
    		custom->intena = aud_intflag;
    		custom->intena = INTF_SETCLR | OldINTENA;
  	}

  	if (OldInt)
    		SetIntVector(7,OldInt);

  	if (audio_dev == 0)
	{
    		if (!CheckIO(&audio1->ioa_Request))
		{
      			AbortIO(&audio1->ioa_Request);
      			WaitPort(audioport1);
      			while (GetMsg(audioport1));
    		}

    		if (!CheckIO(&audio2->ioa_Request))
		{
      			AbortIO(&audio2->ioa_Request);
      			WaitPort(audioport2);
      			while (GetMsg(audioport2));
    		}

    		CloseDevice(&audio1->ioa_Request);
  	}

  	if (audio2)
    		Sys_Free(audio2);

  	if (audio1)
    		DeleteIORequest(&audio1->ioa_Request);

  	if (audioport2)
    		DeleteMsgPort(audioport2);

  	if (audioport1)
    		DeleteMsgPort(audioport1);

  	if (shm)
    		Sys_Free((void*)shm);

#ifndef MEMDEBUG
  	if (IntData.aud_start_time) 
  		#if !defined(__PPC__)   
    		Sys_Free((void*)(IntData.aud_start_time));
		#else
		FreeMem((void*)(IntData.aud_start_time), sizeof(double));
  		#endif

  	if (dmabuf)
  		#if !defined(__PPC__)
    		Sys_Free(dmabuf);
  		#else
		FreeMem(dmabuf, NSAMPLES);
  		#endif
#endif
}


qboolean SNDDMA_InitPaula(void)
{
	struct GfxBase *GfxBase;  //already opened on mgl.lib

  	/* first try ch. 0/1, then 2/3 */
  	static const UBYTE channelalloc[2] = { 1|2, 4|8 };
  	int channelnr, swapchannels, i;

  	/* evaluate parameters */
  	if ( (i = COM_CheckParm("-audspeed")) )
	{
    		period = (UWORD)(sysclock / Q_atoi(com_argv[i+1]));
  	}

  	else
    		period = (UWORD)(sysclock / desired_speed);

  	swapchannels = COM_CheckParm("-swapchannels");

	#if defined(__PPC__)
  	if (no68kFPU)
    		audintptr = AudioIntCodeNoFPU; /* use mathieeedoubbas.library */
	#endif

  	/* allocate dma buffer and sound structure */

	#ifdef MEMDEBUG

  	memset((UBYTE *)0x180000,0xa5,0x60000);
  	dmabuf = (UBYTE *)0x1a0000;
  	IntData.aud_start_time = (double *)0x1c0000;

	#else

  	#if !defined(__PPC__)
  	if ( !(dmabuf = Sys_Alloc(NSAMPLES,MEMF_CHIP|MEMF_PUBLIC|MEMF_CLEAR)) )
  	#else
  	if ( !(dmabuf = AllocMem(NSAMPLES,MEMF_CHIP|MEMF_PUBLIC|MEMF_CLEAR)) )
  	#endif 
  	{ 
    		Con_Printf("Can't allocate Paula DMA buffer\n");
    		return (false);
  	}

  	#if !defined(__PPC__)
  	if ( !(IntData.aud_start_time = Sys_Alloc(sizeof(double),MEMF_CHIP|MEMF_CLEAR)) ) 
  	#else
  	if ( !(IntData.aud_start_time = AllocMem(sizeof(double),MEMF_CHIP|MEMF_CLEAR)) )
  	#endif
  	{
    		Con_Printf("Failed to allocate 8 bytes of CHIP-RAM buffer\n");
    		return (false);
  	}

	#endif

  	if ( !(shm = Sys_Alloc(sizeof(dma_t),MEMF_ANY|MEMF_CLEAR)) )
	{
    		Con_Printf("Failed to allocate shm\n");
    		return (false);
  	}


//from sys_amiga.c

	if ( (GfxBase = (struct GfxBase *)OpenLibrary("graphics.library",0L)) )
	//if ( GfxBase != NULL ) // 2024 test Cowcat
	{
    		if (GfxBase->DisplayFlags & REALLY_PAL)  
      			sysclock = CLOCK_PAL;

    		else
      			sysclock = CLOCK_NTSC;

    		//CloseLibrary((struct Library *)GfxBase); //closed by mgl.lib
  	}

	else
	{
		Con_Printf("Failed to open gfxbase\n"); // should not happen - Cowcat 2024
    		return (false);
	}


  	/* init shm */
  	shm->buffer = (unsigned char *)dmabuf;
  	shm->channels = 2;
  	shm->speed = sysclock/(long)period;
  	shm->samplebits = 8;
  	shm->samples = NSAMPLES;
  	shm->submission_chunk = 1;
  	speed = (float)(shm->speed * shm->channels);

  	/* open audio.device */
  	if ( (audioport1 = CreateMsgPort()) )
	{
    		if ( (audioport2 = CreateMsgPort()) )
		{
        		if ( (audio1 = (struct IOAudio *)CreateIORequest(audioport1, sizeof(struct IOAudio))) )
			{
          			if ( (audio2 = (struct IOAudio *)Sys_Alloc(sizeof(struct IOAudio), MEMF_PUBLIC)) )
				{
              				audio1->ioa_Request.io_Message.mn_Node.ln_Pri = ADALLOC_MAXPREC;
              				audio1->ioa_Request.io_Command = ADCMD_ALLOCATE;
              				audio1->ioa_Request.io_Flags = ADIOF_NOWAIT;
              				audio1->ioa_AllocKey = 0;
              				audio1->ioa_Data = (UBYTE *)channelalloc;
              				audio1->ioa_Length = sizeof(channelalloc);
              				audio_dev = OpenDevice(AUDIONAME,0, &audio1->ioa_Request,0);
          			}
        		}
    		}
  	}

  	if (audio_dev == 0)
	{
    		/* set up audio io blocks */
    		audio1->ioa_Request.io_Command = CMD_WRITE;
    		audio1->ioa_Request.io_Flags = ADIOF_PERVOL;
    		audio1->ioa_Data = swapchannels ? dmabuf : (dmabuf + (NSAMPLES >> 1));
    		audio1->ioa_Length = NSAMPLES >> 1;
    		audio1->ioa_Period = period;
    		audio1->ioa_Volume = 64;
    		audio1->ioa_Cycles = 0;  /* loop forever */
    		*audio2 = *audio1;
    		audio2->ioa_Request.io_Message.mn_ReplyPort = audioport2;
    		audio1->ioa_Request.io_Unit = (struct Unit *) ((ULONG)audio1->ioa_Request.io_Unit & 9);
    		audio2->ioa_Request.io_Unit = (struct Unit *) ((ULONG)audio2->ioa_Request.io_Unit & 6);
    		audio2->ioa_Data = swapchannels ? (dmabuf + (NSAMPLES >> 1)) : dmabuf;
  	}

  	else
	{
    		Con_Printf("Couldn't open audio.device\n");
    		return (false);
  	}

  	/* set up audio interrupt */
  	IntData.TimerBase = TimerBase;
  	IntData.FirstTime2 = &FirstTime2;

	#if defined(__PPC__)
  	IntData.MathIeeeDoubBasBase = MathIeeeDoubBasBase;
	#endif

  	AudioInt.is_Node.ln_Type = NT_INTERRUPT;
  	AudioInt.is_Node.ln_Pri = 0;
  	AudioInt.is_Data = &IntData;
  	AudioInt.is_Code = (void(*)())audintptr;

  	/* start replay of left and right channel */
  	switch ((ULONG)audio1->ioa_Request.io_Unit)
	{
    		case 1: channelnr = 0;break;
    		case 2: channelnr = 1;break;
    		case 4: channelnr = 2;break;
    		case 8: channelnr = 3;break;
  	}

	//#if defined(__VBCC__) // needs libamigawos.a gcc - Cowcat
  	BeginIO(&audio1->ioa_Request);
  	BeginIO(&audio2->ioa_Request);
	//#endif

  	OldInt = SetIntVector(channelnr+7,&AudioInt);
  	OldINTENA = custom->intenar;
  	*IntData.aud_start_time = 0;
  	aud_intflag = INTF_AUD0 << channelnr;
  	custom->intena = OldINTENA | INTF_SETCLR | INTF_INTEN | aud_intflag;

  	while ( !(*IntData.aud_start_time) );

  	saved_filter = cia_a->ciapra & CIAF_LED;
  	cia_a->ciapra |= CIAF_LED;
  	Con_Printf("Paula audio initialized\n");
  	Con_Printf("Output: 8bit stereo\n");

  	return (true);
}


int SNDDMA_GetDMAPosPaula(void)
{
	int pos;

	#ifdef __PPC__

  	static struct timeval tv;
  	static int SyncCounter=0;
  	static double timediff;
  	double time;

  	if (SyncCounter == 0)
	{
    		GetSysTime(&tv);
    		time = ((double)(tv.tv_secs-FirstTime2) + (((double)tv.tv_micro) / 1000000.0));
    		timediff = (Sys_FloatTime()) - time;
  	}

  	SyncCounter = (SyncCounter + 1) % 50;
  	pos = (int)((Sys_FloatTime()-(*IntData.aud_start_time+timediff))*speed);

	#else

  	pos = (int)((Sys_FloatTime()-*IntData.aud_start_time)*speed);

	#endif

  	if (pos >= NSAMPLES)
    		pos = 0;

  	return (shm->samplepos = pos);
}
