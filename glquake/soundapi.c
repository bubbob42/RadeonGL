
#include <proto/ahi.h>

#define FREQUENCY  22050
#define TYPE       AHIST_S16S
#define BUFFERSIZE 2048
struct MsgPort    *AHImp     = NULL;
struct AHIRequest *AHIio     = NULL;
struct AHIRequest *AHIio2    = NULL;
BYTE               AHIDevice = -1;

SHORT buffer1[BUFFERSIZE];
SHORT buffer2[BUFFERSIZE];

void AmiSoundcleanup(LONG rc)
{
  if(!AHIDevice)
    CloseDevice((struct IORequest *)AHIio);
  DeleteIORequest((struct IORequest *)AHIio);
  DeleteIORequest((struct IORequest *)AHIio2);
  DeleteMsgPort(AHImp);
}

void AmiSoundThread(void)
{
  SHORT *p1=buffer1,*p2=buffer2;
  SHORT *tmp;
  ULONG signals,length;
  struct AHIRequest *link = NULL,*t2;
  LONG priority = 0;
  BYTE pri;

  pri = priority;

  if(AHImp=CreateMsgPort()) {
    if(AHIio=(struct AHIRequest *)CreateIORequest(AHImp,sizeof(struct
AHIRequest))) {
      AHIio->ahir_Version = 4;
      AHIDevice=OpenDevice(AHINAME,0,(struct IORequest *)AHIio,NULL);
    }
  }

  if(AHIDevice) {
    AmiSoundcleanup(RETURN_FAIL);
    return;
  }

// Make a copy of the request (for double buffering)
  AHIio2 = (struct AHIRequest *) AllocMem(sizeof(struct AHIRequest),
MEMF_ANY);
  if(! AHIio2) {
    AmiSoundcleanup(RETURN_FAIL);
  }
  CopyMem(AHIio, AHIio2, sizeof(struct AHIRequest));

  while (threadrun==2) {

// Fill buffer
//    length = Read(Input(),p1,BUFFERSIZE);
// for(int i=0;i<BUFFERSIZE;i++)
//  p1[i]=sin(((float)i)*2*3.14/BUFFERSIZE)*20000;
 S9xMixSamplesO((uint8 *)p1,2048,0);
 length=4096;

// Play buffer
    AHIio->ahir_Std.io_Message.mn_Node.ln_Pri = pri;
    AHIio->ahir_Std.io_Command  = CMD_WRITE;
    AHIio->ahir_Std.io_Data     = p1;
    AHIio->ahir_Std.io_Length   = length;
    AHIio->ahir_Std.io_Offset   = 0;
    AHIio->ahir_Frequency       = FREQUENCY;
    AHIio->ahir_Type            = TYPE;
    AHIio->ahir_Volume          = 0x10000;          // Full volume
    AHIio->ahir_Position        = 0x8000;           // Centered
    AHIio->ahir_Link            = link;
    SendIO((struct IORequest *) AHIio);

    if(link) {

// Wait until the last buffer is finished (== the new buffer is started)
      signals=Wait(1L << AHImp->mp_SigBit);

// Remove the reply and abort on error
      if(WaitIO((struct IORequest *) link)) {
        break;
      }
    }

    link = AHIio;

// Swap buffer and request pointers, and restart
    tmp    = p1;
    p1     = p2;
    p2     = tmp;

    t2     = AHIio;
    AHIio  = AHIio2;
    AHIio2 = t2;
  }


// Abort any pending iorequests
  AbortIO((struct IORequest *) AHIio);
  WaitIO((struct IORequest *) AHIio);

  if(link) { // Only if the second request was started
    AbortIO((struct IORequest *) AHIio2);
    WaitIO((struct IORequest *) AHIio2);
  }

  AmiSoundcleanup(RETURN_OK);

  threadrun=0;

}

void *soundthread;

void CreateThreads(void)
{
    struct TagItem TaskTags[] =
    {
 TASKATTR_CODE,          (ULONG)0,
 TASKATTR_NAME,          (ULONG)0,
 TASKATTR_PRI,           (ULONG)0,
 TASKATTR_STACKSIZE,     (ULONG)100000,
 TASKATTR_INHERITR2,     (ULONG)1,
 TAG_DONE,               (ULONG)0,
    };

    // Parameter for the thread
    TaskTags[0].ti_Data = (ULONG)AmiSoundThread;
    TaskTags[1].ti_Data = (ULONG)"WarpSNES sound thread";


    // Set this so the thread will not terminate at once...
    threadrun = 2;

    // Start the thread
    soundthread = CreateTaskPPC(TaskTags);
}

bool8 S9xOpenSoundDevice (int mode, bool8 stereo, int buffer_size)
{
 so.stereo=1;
 so.sixteen_bit=1;
 so.playback_rate=22050;
 S9xSetPlaybackRate (so.playback_rate);
 so.buffer_size=2048;
 so.encoded=0;
 CreateThreads();
 return (TRUE);
}

