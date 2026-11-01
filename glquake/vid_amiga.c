/*
** vid_amiga.c
**
** Amiga Video Drivers
**
** AGA, CGX and ChunkyPPC
**
** Written by Frank Wille <frank@phoenix.owl.de>
**     and Steffen Häuser <magicsn@birdland.es.bawue.de>
**
** FIXME: Support for DirectRect is missing
**
*/

#define  CALC_SURFCACHE

#pragma amiga-align
#include <exec/libraries.h>
#include <exec/memory.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#pragma default-align

#include "quakedef.h"
#include "d_local.h"
#include "vid_amiga.h"


extern byte *r_warpbuffer;

#define CHUNKYPPC 0
#define CYBERGFX 1
#define AGAC2P 2

static int gfxmode = CHUNKYPPC;  /* one of the constants above */

/* Library bases used by the video drivers */
struct Library *GfxBase=NULL;
struct Library *CyberGfxBase=NULL;
struct Library *P96Base=NULL;
struct Library *RtgBase=NULL;
struct Library *ChunkyPPCBase=NULL;
struct Library *IntuitionBase=NULL;
struct Library *AslBase=NULL;

viddef_t vid;                    /* global video state */
byte *vid_buffer;
static byte *vid_buf_base = NULL;
short *zbuffer = NULL;
int config_notify=0;
int shutdown_keyboard=0;
struct Screen *QuakeScreen = NULL;  /* will be assigned by the video driver */

#ifndef CALC_SURFCACHE
#define SURFCACHESIZE_SMALL     256*1024
#define SURFCACHESIZE_MEDIUM    512*1024
#define SURFCACHESIZE_LARGE     1024*1024
static byte surfcache[SURFCACHESIZE_LARGE];
#else
static byte* surfcache;
static int surfcachesize;
#endif

unsigned short d_8to16table[256];
unsigned d_8to24table[256];

unsigned char rawkeyconv[] = {
  '`','1','2','3','4','5','6','7','8','9','0','-','=','\\',0,'0',
  'q','w','e','r','t','y','u','i','o','p','[',']',0,K_END,K_DOWNARROW,K_PGDN,
  'a','s','d','f','g','h','j','k','l',';','\'',0,0,K_LEFTARROW,0,K_RIGHTARROW,
  0,'z','x','c','v','b','n','m',',','.','/',0,0,K_HOME,K_UPARROW,K_PGUP,
  K_SPACE,K_BACKSPACE,K_TAB,K_ENTER,K_ENTER,K_ESCAPE,K_F11,0,
  0,0,'-',0,K_UPARROW,K_DOWNARROW,K_RIGHTARROW,K_LEFTARROW,
  K_F1,K_F2,K_F3,K_F4,K_F5,K_F6,K_F7,K_F8,K_F9,K_F10,'(',')','/',K_PAUSE,'+',K_F12,
  K_SHIFT,K_SHIFT,0,K_CTRL,K_ALT,K_ALT,K_AMIGA,K_AMIGA,
  K_MOUSE1,K_MOUSE2,K_MOUSE3
};


void VID_MenuDraw (void);
void VID_MenuKey (int);
void M_Menu_Options_f(void);

byte vid_current_palette[768];



void VID_SetPalette (unsigned char *palette)
{
  if (palette!=vid_current_palette) Q_memcpy(vid_current_palette,palette,768);

  switch (gfxmode) {
    case CHUNKYPPC:
      VID_SetPalette_ChunkyPPC(palette);
      break;
    case CYBERGFX:
      VID_SetPalette_CGFX(palette);
      break;
    case AGAC2P:
      VID_SetPalette_AGA(palette);
      break;
  }
}


void VID_ShiftPalette (unsigned char *palette)
{
  VID_SetPalette(palette);
}

int vid_modenum;
int modearray_x[11]={320,320,320,400,480,512,640,640,800,1024,1280};
int modearray_y[11]={200,240,400,300,384,384,400,480,600,768,1024};

void VID_MenuDraw(void);
void VID_MenuKey(int key);

cvar_t vid_mode={"vid_mode","1",false};
cvar_t _vid_default_mode={"_vid_default_mode","1",true};

int firstopen=1;

void VID_Init (unsigned char *palette)
{
  char *module = "VID_Init: ";
  int i;

	if (!(IntuitionBase = OpenLibrary("intuition.library",36)))
		Sys_Error("%sCan't open intuition.library V36",module);

  if (firstopen)
  {
   vid_modenum=_vid_default_mode.value;
   Cvar_SetValue("vid_mode",vid_modenum);
   firstopen=0;
   vid_mode.value=_vid_default_mode.value;
   S_Init();
  }

  gfxmode=-1;

  if (i = COM_CheckParm("-cppc"))
    gfxmode = CHUNKYPPC;
  if (i = COM_CheckParm("-cgfx"))
    gfxmode = CYBERGFX;
  if (i = COM_CheckParm("-aga"))
    gfxmode = AGAC2P;

  if (gfxmode==-1)
  {
    char strMode[255];
    if (0<GetVar("env:Quake1/gfxmode",strMode,255,0))
    {
      if (!stricmp(strMode,"chunkyppc")) gfxmode=CHUNKYPPC;
      else if (!stricmp(strMode,"cybergfx")) gfxmode=CYBERGFX;
      else if (!stricmp(strMode,"agac2p")) gfxmode=AGAC2P;
      else gfxmode=CHUNKYPPC;
    }
    else 
    {
#ifdef WOS
      struct Library *LibBase=OpenLibrary("chunkyppc.library",13);
      if (LibBase)
      {
        gfxmode=CHUNKYPPC;
        CloseLibrary(LibBase);
        LibBase=0;
      }
      else gfxmode=CYBERGFX;  
#else
      gfxmode=CHUNKYPPC;
#endif
    }
  }

  switch (gfxmode) {
    case AGAC2P:
      VID_Init_AGA(palette);
      break;
    case CYBERGFX:
      VID_Init_CGFX(palette);
      break;
    case CHUNKYPPC:
      VID_Init_ChunkyPPC(palette);
      break;
  }

  vid.colormap = host_colormap;
  vid.fullbright = 256 - LittleLong (*((int *)vid.colormap + 2048));

  if (!(vid_buf_base = (byte *)Sys_Alloc(vid.width * vid.height + 16,
                                         MEMF_FAST|MEMF_PUBLIC)))
    Sys_Error("%sNot enough memory for video buffer",module);
  /* Make sure vid_buffer is 16-bytes aligned */
  vid_buffer = (byte *)((ULONG)(vid_buf_base + 15) & ~15);

  if (!(zbuffer = (short *)Sys_Alloc(vid.width * vid.height * 2,
                                     MEMF_FAST|MEMF_PUBLIC)))
    Sys_Error("%sNot enough memory for z-buffer",module);
  if (!(r_warpbuffer = (byte *)Sys_Alloc(vid.width * vid.height,
                                         MEMF_FAST|MEMF_PUBLIC)))
    Sys_Error("%sNot enough memory for warp buffer",module);

  vid.conbuffer = vid.buffer = vid_buffer;
  d_pzbuffer = zbuffer;
  vid.maxwarpwidth = vid.rowbytes;
  vid.maxwarpheight = vid.height;

#ifndef CALC_SURFCACHE
  if ((vid.width*vid.height) < 65000)
    D_InitCaches (surfcache, SURFCACHESIZE_SMALL);
  else if ((vid.width*vid.height) < 200000)
    D_InitCaches (surfcache, SURFCACHESIZE_MEDIUM);
  else
    D_InitCaches (surfcache, SURFCACHESIZE_LARGE);

#else
  surfcachesize = D_SurfaceCacheForRes(vid.width,vid.height);

  if (!(surfcache = (byte *)Sys_Alloc(surfcachesize,MEMF_FAST)))
    Sys_Error("%sNot enough memory for surface cache",module);
  D_InitCaches (surfcache, surfcachesize);
#endif
  if (gfxmode!=AGAC2P)
  {
    vid_menudrawfn = VID_MenuDraw;
    vid_menukeyfn = VID_MenuKey;
  }
  else
  {
    vid_menudrawfn = NULL;
    vid_menukeyfn = NULL;
  }
}


void VID_Shutdown (void)
{
  D_FlushCaches();
#ifdef CALC_SURFCACHE
  if (surfcache)
    Sys_Free(surfcache);
#endif
  if (r_warpbuffer)
    Sys_Free(r_warpbuffer);
  if (zbuffer)
    Sys_Free(zbuffer);
  if (vid_buf_base)
    Sys_Free(vid_buf_base);

  switch (gfxmode) {
    case AGAC2P:
      VID_Shutdown_AGA();
      break;
    case CYBERGFX:
      VID_Shutdown_CGFX();
      break;
    case CHUNKYPPC:
      VID_Shutdown_ChunkyPPC();
      break;
  }

  if (IntuitionBase)
    CloseLibrary(IntuitionBase);
}

void VID_Update (vrect_t *rects)
{
  if (config_notify)
  {
    D_FlushCaches();
    vid.recalc_refdef = 1;
    if (vid_buf_base) Sys_Free(vid_buf_base);
    vid_buf_base = NULL;
    if (zbuffer) Sys_Free(zbuffer);
    zbuffer = NULL;
    if (r_warpbuffer) Sys_Free(r_warpbuffer);
    d_viewbuffer = r_warpbuffer;
    r_warpbuffer = NULL;
#ifdef CALC_SURFCACHE
    if (surfcache) Sys_Free(surfcache);
    surfcache = NULL;
#endif
    if (gfxmode==CYBERGFX) VID_Shutdown_CGFX();
    VID_Init(vid_current_palette);
    shutdown_keyboard = 1;
    config_notify = 0;
    Con_CheckResize();
    Con_Clear_f();
    return;
  }

  switch (gfxmode) {
    case CHUNKYPPC:
      VID_Update_ChunkyPPC(rects);
      break;
    case CYBERGFX:
      VID_Update_CGFX(rects);
      break;
    case AGAC2P:
      VID_Update_AGA(rects);
      break;
  }
}


void Sys_SendKeyEvents (void)
{
  switch (gfxmode) {
    case CHUNKYPPC:
      Sys_SendKeyEvents_ChunkyPPC();
      break;
    case CYBERGFX:
      Sys_SendKeyEvents_CGFX();
      break;
    case AGAC2P:
      Sys_SendKeyEvents_AGA();
      break;
  }
}


/*
================
D_BeginDirectRect
================
*/
void D_BeginDirectRect (int x, int y, byte *pbitmap, int width, int height)
{
}


/*
================
D_EndDirectRect
================
*/
void D_EndDirectRect (int x, int y, int width, int height)
{
}


int numvidmodes=11;
static int vid_wmodes,vid_line;
int vid_column_size;

typedef struct
{
  int     modenum;
  char    *desc;
  int     iscur;
} modedesc_t;

#define MAX_COLUMN_SIZE 11
#define MAX_MODEDESCS   33
static modedesc_t   modedescs[33];

typedef struct vmode_s {
  struct vmode_s *pnext;
  char           *name;
  char           *header;
  unsigned       width;
  unsigned       height;
} vmode_t;


vmode_t vmode10={0,"1280*1024","HiRes V",1280,1024};
vmode_t vmode9={&vmode10,"1024*768","HiRes IV",1024,768};
vmode_t vmode8={&vmode9,"800*600","HiRes III",800,600};
vmode_t vmode7={&vmode8,"640*480","HiRes II",640,480};
vmode_t vmode6={&vmode7,"640*400","HiRes I",640,400};
vmode_t vmode5={&vmode6,"512*384","MedRes III",512,384};
vmode_t vmode4={&vmode5,"480*384","MedRes II",480,384};
vmode_t vmode3={&vmode4,"400*300","MedRes I",400,300};
vmode_t vmode2={&vmode3,"320*400","LowRes III",320,400};
vmode_t vmode1={&vmode2,"320*240","LowRes II",320,240};
vmode_t vmode0={&vmode1,"320*200","LowRes I",320,200};
vmode_t *pvidmodes=&vmode0;

int VID_NumModes ()
{
  return (numvidmodes);
}

vmode_t *VID_GetModePtr (int modenum)
{
  vmode_t *pv;
  pv = pvidmodes;
  if (!pv) Sys_Error ("VID_GetModePtr: empty vid mode list");
  while (modenum--)
  {
    pv = pv->pnext;     
    if (!pv) Sys_Error ("VID_GetModePtr: corrupt vid mode list");
  }
  return pv;
}

char *VID_ModeInfo (int modenum, char **ppheader)
{
  static char *badmodestr = "Bad mode number";
  vmode_t     *pv;
  pv = VID_GetModePtr (modenum);
  if (!pv)
  {
     if (ppheader) *ppheader = NULL;
     return badmodestr;
  }
  else
  {
      if (ppheader) *ppheader = pv->header;
      return pv->name;
  }
}

char *VID_GetModeDescription (int mode)
{
  char        *pinfo, *pheader;
  vmode_t     *pv;
  pv = VID_GetModePtr (mode);
  pinfo = VID_ModeInfo (mode, &pheader);
  return pinfo;
}

/*
================
VID_MenuDraw
================
*/

void VID_MenuDraw (void)
{
  qpic_t      *p;
  char        *ptr;
  int         nummodes, i, j, column, row, dup;
  char        temp[100];

  vid_wmodes = 0;
  nummodes = VID_NumModes ();
  
  p = Draw_CachePic ("gfx/vidmodes.lmp");
  M_DrawPic ( (320-p->width)/2, 4, p);

  for (i=0 ; i<nummodes ; i++)
  {
    if (vid_wmodes < MAX_MODEDESCS)
    {
      ptr = VID_GetModeDescription (i);
      if (ptr)
      {
        dup = 0;

        for (j=0 ; j<vid_wmodes ; j++)
        {
          if (!strcmp (modedescs[j].desc, ptr))
          {
            if (modedescs[j].modenum != 0)
            {
              modedescs[j].modenum = i;
              dup = 1;

              if (i == vid_modenum) modedescs[j].iscur = 1;
            }
            else
            {
              dup = 1;
            }
            break;
          }
        }

        if (!dup)
        {
          modedescs[vid_wmodes].modenum = i;
          modedescs[vid_wmodes].desc = ptr;
          modedescs[vid_wmodes].iscur = 0;

          if (i == vid_modenum) modedescs[vid_wmodes].iscur = 1;

          vid_wmodes++;
        }
      }
    }
  }

  vid_column_size = (vid_wmodes + 2) / 3;

  column = 16;
  row = 36;

  for (i=0 ; i<vid_wmodes ; i++)
  {
    if (modedescs[i].iscur) M_PrintWhite (column, row, modedescs[i].desc);
    else M_Print (column, row, modedescs[i].desc);

    row += 8;

    if ((i % vid_column_size) == (vid_column_size - 1))
    {
      column += 13*8;
      row = 36;
    }
  }

  M_Print (9*8, 36 + MAX_COLUMN_SIZE * 8 + 8*3, "Press Enter to set mode");
  ptr = VID_GetModeDescription (vid_modenum);
  sprintf (temp, "D to make %s the default", ptr);
  M_Print (6*8, 36 + MAX_COLUMN_SIZE * 8 + 8*5, temp);
  ptr = VID_GetModeDescription ((int)_vid_default_mode.value);

  if (ptr)
  {
    sprintf (temp, "Current default is %s", ptr);
    M_Print (7*8, 36 + MAX_COLUMN_SIZE * 8 + 8*6, temp);
  }

  M_Print (15*8, 36 + MAX_COLUMN_SIZE * 8 + 8*8, "Esc to exit");

  row = 36 + (vid_line % vid_column_size) * 8;
  column = 8 + (vid_line / vid_column_size) * 13*8;

  M_DrawCharacter (column, row, 12+((int)(realtime*4)&1));
}

/*
================
VID_MenuKey
================
*/
void VID_MenuKey (int key)
{
  switch (key)
  {
    case K_ESCAPE:
      S_LocalSound ("misc/menu1.wav");
      M_Menu_Options_f ();
      break;
  
    case K_UPARROW:
      S_LocalSound ("misc/menu1.wav");
      vid_line--;
  
      if (vid_line < 0) vid_line = vid_wmodes - 1;
      break;
  
    case K_DOWNARROW:
      S_LocalSound ("misc/menu1.wav");
      vid_line++;

      if (vid_line >= vid_wmodes)
      vid_line = 0;
      break;
  
    case K_LEFTARROW:
      S_LocalSound ("misc/menu1.wav");
      vid_line -= vid_column_size;
  
      if (vid_line < 0)
      {
        vid_line += ((vid_wmodes + (vid_column_size - 1)) /
                     vid_column_size) * vid_column_size;
  
        while (vid_line >= vid_wmodes)
               vid_line -= vid_column_size;
      }
      break;
  
    case K_RIGHTARROW:
      S_LocalSound ("misc/menu1.wav");
      vid_line += vid_column_size;
  
      if (vid_line >= vid_wmodes)
      {
        vid_line -= ((vid_wmodes + (vid_column_size - 1)) /
                     vid_column_size) * vid_column_size;
  
        while (vid_line < 0)
          vid_line += vid_column_size;
      }
      break;
  
    case K_ENTER:
      S_LocalSound ("misc/menu1.wav");
      Cvar_SetValue("vid_mode",modedescs[vid_line].modenum);
      vid_modenum=modedescs[vid_line].modenum;
      config_notify=1;
      break;

    case 'D':
    case 'd':
      S_LocalSound ("misc/menu1.wav");
      Cvar_SetValue ("_vid_default_mode", modedescs[vid_line].modenum);
      vid_modenum=modedescs[vid_line].modenum;
      break;

    default:
      break;
  }
}
