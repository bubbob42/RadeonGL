/*
** AmigaOS Clipboard Support Functions
** Based on example source from the Amiga Developer CD
*/

#define CBIO
#include "sys.h"
#include "sys_amigaclip.h"

#ifdef __VBCC__
#pragma amiga-align
#elif defined(WARPUP)
#pragma pack(push,2)
#endif

#include <proto/exec.h>
#include <clib/alib_protos.h>

#ifdef __VBCC__
#pragma default-align
#elif defined(WARPUP)
#pragma pack(pop)
#endif

static int ReadLong(struct IOClipReq *ior,ULONG *ldata)
{
  ior->io_Command = CMD_READ;
  ior->io_Data    = (STRPTR)ldata;
  ior->io_Length  = 4L;
  DoIO( (struct IORequest *) ior);
  if (ior->io_Actual == 4)
    return( ior->io_Error ? FALSE : TRUE);
  return(FALSE);
}


static struct cbbuf *FillCBData(struct IOClipReq *ior,ULONG size)
{
  UBYTE *to,*from;
  ULONG x,count;
  ULONG length;
  struct cbbuf *buf,*success=NULL;

  if (buf = Sys_Alloc(sizeof(struct cbbuf),MEMF_PUBLIC)) {
    length = size;
    if (size & 1)
      length++;            /* if odd size, read 1 more */

    if (buf->mem = Sys_Alloc(length+1L,MEMF_PUBLIC)) {
      buf->size = length+1;
      ior->io_Command = CMD_READ;
      ior->io_Data    = (STRPTR)buf->mem;
      ior->io_Length  = length;
      to = buf->mem;
      count = 0;

      if (!(DoIO((struct IORequest *)ior))) {
        if (ior->io_Actual == length) {
          success = buf;      /* everything succeeded */
          /* strip NULL bytes */
          for (x=0,from=buf->mem; x<size; x++) {
            if (*from) {
              *to = *from;
              to++;
              count++;
            }
            from++;
          }
          *to = '\0';         /* Null terminate buffer */
          buf->count = count; /* cache count of chars in buf */
        }
      }
      if (!(success))
        Sys_Free(buf->mem);
    }
    if (!(success))
      Sys_Free(buf);
  }

  return(success);
}


struct IOClipReq *CBOpen(ULONG unit)
{
  	struct MsgPort *mp;
  	struct IOStdReq *ior;

  	//if (mp = CreatePort(0,0))
	if (mp = CreateMsgPort())
	{
    		//if (ior=(struct IOStdReq *)CreateExtIO(mp,sizeof(struct IOClipReq)))
		if (ior=(struct IOStdReq *)CreateIORequest(mp,sizeof(struct IOClipReq)))
		{
      			if (!(OpenDevice("clipboard.device",unit,(struct IORequest *)ior,0)))
        			return((struct IOClipReq *)ior);

      			//DeleteExtIO((struct IORequest *)ior);
			DeleteIORequest((struct IORequest *)ior);
    		}

    		//DeletePort(mp);
		DeleteMsgPort(mp);
  	}

  	return(NULL);
}


void CBClose(struct IOClipReq *ior)
{
  	struct MsgPort *mp = ior->io_Message.mn_ReplyPort;

  	CloseDevice((struct IORequest *)ior);
  	//DeleteExtIO((struct IORequest *)ior);
  	//DeletePort(mp);
	DeleteIORequest((struct IORequest *)ior);
	DeleteMsgPort(mp);
}


void CBReadDone(struct IOClipReq *ior)
{
  char buffer[256];

  ior->io_Command = CMD_READ;
  ior->io_Data    = (STRPTR)buffer;
  ior->io_Length  = 254;
  while (ior->io_Actual) {
    if (DoIO((struct IORequest *)ior))
      break;
  }
}


int CBQueryFTXT(struct IOClipReq *ior)
{
  ULONG cbuff[4];

  ior->io_Offset = 0;
  ior->io_Error  = 0;
  ior->io_ClipID = 0;

  /* Look for "FORM[size]FTXT" */
  ior->io_Command = CMD_READ;
  ior->io_Data    = (STRPTR)cbuff;
  ior->io_Length  = 12;
  DoIO((struct IORequest *)ior);

  if (ior->io_Actual == 12) {
    /* Check to see if it starts with "FORM[size]FTXT" */
    if (cbuff[0]==ID_FORM && cbuff[2]==ID_FTXT)
        return(TRUE);
  }

  /* It's not "FORM[size]FTXT", so tell clipboard we are done */
  CBReadDone(ior);
  return(FALSE);
}


struct cbbuf *CBReadCHRS(struct IOClipReq *ior)
{
  ULONG chunk,size;
  struct cbbuf *buf = NULL;
  int looking = TRUE;

  /* Find next CHRS chunk */
  while (looking) {
    looking = FALSE;
    if (ReadLong(ior,&chunk)) {
      if (chunk == ID_CHRS) {
        /* Get size of chunk, and copy data */
        if (ReadLong(ior,&size)) {
          if (size)
            buf = FillCBData(ior,size);
        }
      }
      else {
        if (ReadLong(ior,&size)) {
          looking = TRUE;
          if (size & 1)
            size++;    /* if odd size, add pad byte */
          ior->io_Offset += size;
        }
      }
    }
  }
  if (buf == NULL)
    CBReadDone(ior);        /* tell clipboard we are done */

  return(buf);
}


void CBFreeBuf(struct cbbuf *buf)
{
  Sys_Free(buf->mem);
  Sys_Free(buf);
}
