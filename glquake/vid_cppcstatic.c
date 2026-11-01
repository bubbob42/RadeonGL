#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#pragma amiga-align
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <libraries/asl.h>
#include <libraries/Picasso96.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#ifdef CGFX_V4
#include <cybergraphx/cybergraphics.h>
#else
#include <cybergraphics/cybergraphics.h>
#endif
#include <clib/alib_protos.h>
#include <proto/cybergraphics.h>
#include <proto/Picasso96.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/asl.h>
#pragma default-align

#include "amigacompiler.h"

#if defined(__PPC__) && !defined(WOS)
#define ELF
#endif

/* for symbols which are shared between ELF and 68k objects */
#ifdef ELF
#define CNAME(x) _##x
#else
#define CNAME(x) x
#endif


struct Mode_Screen
{
    struct Screen *video_screen;
    struct Window *video_window;
    int bpr;
    int wb_int;
    int pip_int;
    int dbuf_int;
    int oldstyle_int;
    char pubscreenname[512];
    int mode;
    int SCREENWIDTH;
    int SCREENHEIGHT;
    int MAXWIDTH;
    int MAXHEIGHT;
    int MINDEPTH;
    int MAXDEPTH;
    int format;
    int video_depth;
    UWORD *emptypointer;
    struct BitMap *video_tmp_bm;
    int video_is_native_mode;
    int video_is_cyber_mode;
    unsigned char *screen;
    int video_oscan_height;
    int bufnum;
    struct RastPort *video_rastport;
    struct BitMap *bitmapa;
    struct BitMap *bitmapb;
    struct BitMap *bitmapc;
    struct BitMap *thebitmap;
    struct RastPort *video_temprp;
    struct ScreenModeRequester *video_smr;
    int ham_int;
    UBYTE *wbcolors;
    UBYTE *transtable;
    unsigned long *WBColorTable;
    unsigned long *ColorTable;
    int pal_changed;
    int pen_obtained;
    unsigned char *screenb;
    unsigned char *screenc;
    int numbuffers;
    int rtgm_int;
    struct ScreenBuffer *Buf1;
    struct ScreenBuffer *Buf2;
    struct ScreenBuffer *Buf3;
    void * (*algo)(struct Mode_Screen *ms,unsigned char *dest,
                   unsigned char *src, int srcformat,
                   void *(*hook68k)(unsigned char *data),unsigned char *data);
    void (*Internal1)(void);
    void (*Internal2)(void);
    int onlyptr;
    int likecgx;
    UBYTE *c2p_compare_buffer;  /* AGA c2p extension (phx) */
};

 
int CNAME(cppc_minwidth);
int CNAME(cppc_minheight);
int CNAME(cppc_maxwidth);
int CNAME(cppc_maxheight);
int CNAME(cppc_mindepth);
int CNAME(cppc_maxdepth);

static int locked=0;
static int lockingmode=1;

extern struct ExecBase *SysBase;
extern struct Library *P96Base;
extern struct Library *RtgBase;
extern struct Library *CyberGfxBase;
extern struct Library *AslBase;
extern struct GfxBase *GfxBase;
extern struct IntuitionBase *IntuitionBase;
#ifdef ELF
struct GfxBase *_GfxBase;
#endif

/* ASL filter M68k(!) function */
extern ULONG CNAME(filterfunc)();
struct Hook filterfunc_hook = {
  {NULL, NULL}, (void *)&CNAME(filterfunc), NULL, NULL
};


#ifdef __PPC__
extern void TurboUpdatePPC(unsigned char *,unsigned char *,int,
                           int,int,int,int);
extern void c2p_8_ppc(UBYTE *,PLANEPTR,UBYTE *,ULONG);
#else
extern void ASM TurboUpdate68k(REG(a0,unsigned char *),
                               REG(a1,unsigned char *),
                               REG(d4,int),
                               REG(d0,int), REG(d1,int),
                               REG(d2,int), REG(d3,int));
extern void ASM c2p_8_040(REG(a0,UBYTE *),REG(a1,PLANEPTR),REG(a2,UBYTE *),
                          REG(d1,ULONG));
#endif



static void error (char *myerror, struct Mode_Screen *ms, ...)
{
  va_list aptr;
  char text[2048];
  int i,depth;

  if (myerror!=0) {
    va_start (aptr, ms);
    vsprintf (text, myerror, aptr);
    va_end (aptr);
  }
  if (locked) UnlockPubScreen(0,ms->video_screen);

  if ((ms->dbuf_int)&&((!ms->oldstyle_int))) {
    if (ms->video_screen) {
      if (ms->Buf1) {
        (((ms->Buf1)->sb_DBufInfo)->dbi_SafeMessage).mn_ReplyPort = NULL;
        while (!ChangeScreenBuffer(ms->video_screen, ms->Buf1));
      }
      if (ms->Buf1) FreeScreenBuffer(ms->video_screen, ms->Buf1);
      if (ms->Buf2) FreeScreenBuffer(ms->video_screen, ms->Buf2);
      if (ms->Buf3) FreeScreenBuffer(ms->video_screen, ms->Buf3);
    }
  }

  if (ms->likecgx) {
    FreeScreenBuffer(ms->video_screen,ms->Buf1);
    ms->Buf1=0;
  }

  if ((ms->wb_int)&&(!(ms->pip_int))&&(ms->pal_changed)) {
    struct ColorMap *ColorMap=((ms->video_screen)->ViewPort).ColorMap;

    for (i=0;i<256;i++) ReleasePen(ColorMap,ms->transtable[i]);
    LoadRGB32(&(((ms->video_window)->WScreen)->ViewPort),ms->WBColorTable);
  }

  if (ms->video_window) {
    if (!(ms->pip_int)) CloseWindow(ms->video_window);
    else p96PIP_Close(ms->video_window);
  }
  ms->video_window=0;

  if (!ms->wb_int) {
    if (ms->video_screen) CloseScreen(ms->video_screen);
  }
  ms->video_screen=0;

  if (ms->emptypointer) FreeVec(ms->emptypointer);
  ms->emptypointer=0;

  if (ms->video_depth!=0) {
    if (ms->format==PIXFMT_LUT8) {
      for (depth = 0; depth < ms->video_depth; depth++) {
        if (ms->video_tmp_bm->Planes[depth] != NULL) {
          FreeRaster (ms->video_tmp_bm->Planes[depth], ms->SCREENWIDTH, 1);
          ms->video_tmp_bm->Planes[depth] = NULL;
        }
      }
      FreeVec(ms->video_tmp_bm);
      ms->video_tmp_bm=0;
    }
  }

  if (ms->wbcolors) FreeVec(ms->wbcolors);
  if (ms->transtable) FreeVec(ms->transtable);
  if (ms->WBColorTable) FreeVec(ms->WBColorTable);
  if (ms->ColorTable) FreeVec(ms->ColorTable);
  ms->wbcolors=0;
  ms->transtable=0;
  ms->WBColorTable=0;
  ms->ColorTable=0;
}


static void Rearrange(struct Mode_Screen *ms)
{
  error(0,ms);
}


static int acceptmode(struct Mode_Screen *ms, int mode)
{
  DisplayInfoHandle handle;
  int nbytes;
  int mydepth;
  struct DimensionInfo dimsinfo;
  int SW,SH;

  if ((handle = FindDisplayInfo (mode)) == NULL) {
    error ("Can't FindDisplayInfo() for mode %08x", ms,mode);
    return 0;
  }

  if ((nbytes = GetDisplayInfoData (handle, (UBYTE *)&dimsinfo,
                                    sizeof(dimsinfo), DTAG_DIMS,
                                    0)) < 66) {
    error ("Can't GetDisplayInfoData() for mode %08x, got %d bytes",
           ms,mode, nbytes);
    return 0;
  }
  SW = dimsinfo.Nominal.MaxX-dimsinfo.Nominal.MinX+1;
  SH = dimsinfo.Nominal.MaxY-dimsinfo.Nominal.MinY+1;

  if (ms->SCREENWIDTH!=SW) return 0;
  if (ms->SCREENHEIGHT!=SH) return 0;

  if (CyberGfxBase)
    mydepth=GetCyberIDAttr(CYBRIDATTR_DEPTH,mode);
  else mydepth=16;

  if (ms->MINDEPTH>mydepth) return 0;
  if (ms->MAXDEPTH<mydepth) return 0;

  return 1;
}


static int existsmode(char *envvar,char *base,int modeid, int num)
{
  int f;
  int id;
  char temp[1024];
  char temp2[1024];
  char tb[1024];

  strcpy(temp,base);
  strcpy(temp2,envvar);
  sprintf(temp,"%s",temp);

  if (GetVar(temp,tb,1024,0)!=-1) {
    sprintf(temp2,"%s/modeid",temp2);
    GetVar(temp2,tb,1024,0);
    id=atoi(tb);
    if (id==modeid) return 1;
  }

  for (f=1;f<=num;f++) {
    strcpy(temp,base);
    sprintf(temp,"%s%i",temp,f);
    if (GetVar(temp,tb,1024,0)!=-1) {
      strcpy(temp2,envvar);
      sprintf(temp2,"%s/modeid%i",temp2,f);
      GetVar(temp2,tb,1024,0);
      id=atoi(tb);
      if (id==modeid) return 1;
    }
  }

  return 0;
}


static APTR LockBitMapTags68k(struct Mode_Screen *ms, unsigned char **screen)
{
  APTR video_bitmap_handle;
  struct BitMap *bm;
  if (ms->bufnum==2) bm=ms->bitmapa;
  else if (ms->bufnum==0) bm=ms->bitmapb;
  else bm=ms->bitmapc;
  if (ms->oldstyle_int) bm=ms->bitmapa;
  if (ms->numbuffers==1) bm=ms->bitmapa;
  video_bitmap_handle = (APTR)LockBitMapTags (bm,
                                              LBMI_BASEADDRESS, screen,
                                              TAG_DONE);
  return video_bitmap_handle;
}


static APTR LockBitMapTags68k_b(struct Mode_Screen *ms,unsigned char **screen)
{
  APTR video_bitmap_handle = (APTR)LockBitMapTags (ms->bitmapa,
                                                   LBMI_BASEADDRESS, screen,
                                                   TAG_DONE);
  return video_bitmap_handle;
}


static void UnLockBitMap68k(APTR lock)
{
  UnLockBitMap(lock);
}


static void *c2p(struct Mode_Screen *ms,unsigned char *dest,
                 unsigned char *src, int srcformat,
                 void *(*hook68k)(unsigned char *data), unsigned char *data)
{
#ifdef __PPC__
  c2p_8_ppc(src,dest,ms->c2p_compare_buffer,ms->bpr*ms->SCREENHEIGHT);
#else
  c2p_8_040(src,dest,ms->c2p_compare_buffer,ms->bpr*ms->SCREENHEIGHT);
#endif
  return NULL;
}


static void *Chunky8(struct Mode_Screen *ms,unsigned char *dest,
                     unsigned char *src, int srcformat,
                     void *(*hook68k)(unsigned char *data),
                     unsigned char *data)
{
  unsigned char *screen;
  APTR video_bitmap_handle;

  if (lockingmode&&(!(ms->pip_int))) {
    if (ms->video_is_cyber_mode) {
      video_bitmap_handle = (APTR)LockBitMapTags68k (ms,&screen);
      if (ms->oldstyle_int&&(ms->bufnum==0))
        screen+=(ms->bpr*ms->SCREENHEIGHT);
    }
  }
  else
    screen=dest;

#ifdef __PPC__
  TurboUpdatePPC(src,screen,ms->bpr,0,0,ms->SCREENWIDTH,ms->SCREENHEIGHT);
#else
#ifdef _68881 /* 030/88x */
  /* sorry, no 030 support yet */
#else /* 040/060 */
  TurboUpdate68k(src,screen,ms->bpr,0,0,ms->SCREENWIDTH,ms->SCREENHEIGHT);
#endif
#endif

  if (lockingmode&&(ms->video_is_cyber_mode) && (!(ms->pip_int)))
    UnLockBitMap68k (video_bitmap_handle);
  /* @@@ hook function will never be called! */
  return NULL;
}


static void *WbChunky8(struct Mode_Screen *ms,unsigned char *dest,
                       unsigned char *src, int srcformat,
                       void *(*hook68k)(unsigned char *d),
                       unsigned char *data)
{
  int f;

  for (f=0;f<(ms->SCREENWIDTH*ms->SCREENHEIGHT);f++)
    src[f]=ms->transtable[src[f]];

  WritePixelArray8((ms->video_window)->RPort,(ms->video_window)->BorderLeft,
                   (ms->video_window)->BorderTop,
                   (UWORD)((ms->video_window)->BorderLeft+ms->SCREENWIDTH-1),
                   (UWORD)((ms->video_window)->BorderTop+ms->SCREENHEIGHT-1),
                   (UBYTE *)src,ms->video_temprp);
 /* @@@ hook function will never be called! */
  return NULL;
}


void CloseGraphics (struct Mode_Screen *ms,int shutdownlibs)
{
  if (ms->onlyptr==0) error(0,ms);
  if (ms->video_is_native_mode && ms->c2p_compare_buffer) /* phx */
    free(ms->c2p_compare_buffer);

  if (shutdownlibs) {
    if (P96Base) CloseLibrary(P96Base);
    P96Base = NULL;
    if (CyberGfxBase) CloseLibrary(CyberGfxBase);
    CyberGfxBase = NULL;
    if (AslBase) CloseLibrary(AslBase);
    AslBase = NULL;
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    GfxBase = NULL;
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    IntuitionBase = NULL;
  }
  memset(ms,0,sizeof(struct Mode_Screen));
}


struct Mode_Screen *OpenGraphics (char *title,struct Mode_Screen *ms,
                                  int override)
{
  static struct TagItem aslreq[]= {
    ASLSM_TitleText,        0,
    ASLSM_InitialDisplayID, 0,
    ASLSM_MinWidth,         0,
    ASLSM_MinHeight,        0,
    ASLSM_MaxWidth,         1280,
    ASLSM_MaxHeight,        1024,
    ASLSM_MinDepth,         8,
    ASLSM_MaxDepth,         16,
    ASLSM_PropertyMask,     0,
    ASLSM_PropertyFlags,    0,
    ASLSM_FilterFunc,       0,
    0,0
  };
  struct TagItem aslreqtags[]= {
    0,0
  };
  static struct BitMap video_tmp_bm = {
     0, 0, 0, 0, 0, {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL}
  };
  static struct RastPort video_rastport[3];
  static struct RastPort video_temprp;
  static void *thehook=(void *)(&filterfunc_hook);
  int i;
  int mode;
  struct ScreenReq *sreq=0;
  APTR video_bitmap_handle;
  int video_depth;
  struct BitMap *bitmap;
  ULONG propertymask, idcmp, wflags, width, pixfmt;
  struct Rectangle rect;
  char reqtitle[256];
  int  depth, nbytes;
  DisplayInfoHandle handle;
  struct DisplayInfo dispinfo;
  struct DimensionInfo dimsinfo;
  int mode_num;
  float resize_int;
  char tb[1024];
  char realid[1024];
  char title2[512];
  char wb[512];
  char pip[512];
  char dbuf[512];
  char modeid3[512];
  char oldstyle[512];
  char psn[512];
  char pubscreenname[512];
  char pipnoclear[512];
  char modeid2[512];
  char ham[512];
  char likecgx[512];
  int likecgx_int=0;
  int wb_int=0;
  int pip_int=0;
  int dbuf_int=0;
  int oldstyle_int=0;
  int pipnoclear_int=0;
  int ham_int=ms->ham_int;
  int reqwidth;
  int reqheight;
  UWORD *emptypointer;
  int SCREENWIDTH=320;
  int SCREENHEIGHT=200;
  int different;
  int bpr;
  struct ScreenModeRequester *video_smr = NULL;
  BOOL video_is_native_mode = FALSE;
  BOOL video_is_cyber_mode = FALSE;
  int mydepth=8;
  int numbuffers=1;
  unsigned char *screen=0;
  unsigned char *screenb=0;
  unsigned char *screenc=0;
  int video_oscan_height;
  int mode2_changed;
  struct Screen *video_screen=ms->video_screen;
  struct Window *video_window=ms->video_window;
  BPTR mylock;
  locked=0;

  ms->onlyptr=0;
  if (override==3)
  {
    ms->Internal1=0; /* No input handler support here! */
    ms->Internal2=0; /* Outdated Code */
    ms->onlyptr=1;
    return ms;
  }

  sprintf(title2,"%s%s","ENV:",title);
  mylock = Lock(title2,ACCESS_READ);
  if (!mylock) UnLock(CreateDir(title2));
  else UnLock(mylock);

  sprintf(title2,"%s%s","ENVARC:",title);
  mylock = Lock(title2,ACCESS_READ);
  if (!mylock) UnLock(CreateDir(title2));
  else UnLock(mylock);

  strcpy(title2,title);

  if (video_window || video_screen) Rearrange(ms);
  else {
    ms->Buf1=0;
    ms->Buf2=0;
    ms->Buf3=0;
    ms->video_smr=0;
    ms->emptypointer=0;
    ms->video_screen=0;
    ms->video_window=0;
    ms->video_depth=0;
    ms->transtable=0;
    ms->wbcolors=0;
    ms->WBColorTable=0;
    ms->ColorTable=0;
  }

  SCREENWIDTH=ms->SCREENWIDTH;
  SCREENHEIGHT=ms->SCREENHEIGHT;

  reqwidth=SCREENWIDTH;
  reqheight=SCREENHEIGHT;
  different=0;

  ms->Internal1=0; /* No input handler support here! */
  ms->Internal2=0; /* Outdated Code */

  if ((AslBase = OpenLibrary ("asl.library", 37L)) != 0) {
    video_smr = (void *)AllocAslRequest(ASL_ScreenModeRequest, aslreqtags);
    ms->video_smr=video_smr;
    if (!video_smr) {
      error ("AllocAslRequest failed!\n",ms);
      return 0;
    }
  }
  else {
    error("asl.library V37 could not be opened\n",ms);
    return(0);
  }

  if (GfxBase==0) {
    if ((GfxBase = (struct GfxBase *)
                    OpenLibrary("graphics.library",36)) == 0) {
      error("graphics.library V36 could not be opened\n",ms);
      return(0);
    }
#ifdef ELF
    _GfxBase = GfxBase;  /* to make it visible for the 68k part */
#endif
  }

  if (IntuitionBase==0) {
    if ((IntuitionBase = (struct IntuitionBase *)
                          OpenLibrary("intuition.library",36)) == 0) {
      error("intuition.library V36 could not be opened\n",ms);
      return 0;
    }
  }

  if (RtgBase=OpenLibrary("Picasso96/rtg.library",0)) {
    CloseLibrary(RtgBase);
    if (P96Base==0)
      P96Base = OpenLibrary("Picasso96API.library",0);
  }

  if (CyberGfxBase==0)
    CyberGfxBase = OpenLibrary ("cybergraphics.library", 0);

  propertymask =DIPF_IS_DUALPF | DIPF_IS_PF2PRI | DIPF_IS_HAM;

  sprintf(reqtitle,title2);
  strcpy(title2,"ENV:");
  strcat(title2,reqtitle);
  strcpy(modeid2,"ENVARC:");
  strcat(modeid2,reqtitle);
  strcpy(modeid3,title2);
  strcpy(wb,title2);
  strcpy(dbuf,title2);
  strcpy(ham,title2);
  strcpy(pip,title2);
  strcpy(oldstyle,title2);
  strcpy(psn,title2);
  strcpy(pipnoclear,title2);
  strcpy(likecgx,title2);
  strcat(likecgx,"/resize");
  strcat(modeid2,"/modeid");
  strcat(ham,"/ham");
  strcat(wb,"/wb");
  strcat(dbuf,"/dbuf");
  strcat(pip,"/pip");
  strcat(oldstyle,"/oldstyle");
  strcat(title2,"/modeid");
  strcat(psn,"/pubscreenname");
  strcat(pipnoclear,"/pipnoclear");
  if (GetVar(dbuf,tb,1024,0)!=-1) {
    dbuf_int=atoi(tb);
  }
  lockingmode=1;
  if (GetVar("env:lockingmode",tb,1024,0)!=-1)
    lockingmode=atoi(tb);

  resize_int=1.0;
  if (GetVar(likecgx,tb,1024,0)!=-1)
    resize_int=atof(tb);
  likecgx_int=1;

  if (GetVar(ham,tb,1024,0)!=-1)
    ham_int=atoi(tb);

  if (GetVar(oldstyle,tb,1024,0)!=-1) {
    oldstyle_int=atoi(tb);
    if (oldstyle_int) dbuf_int=1;
  }

  if (GetVar(wb,tb,1024,0)!=-1) {
    wb_int=atoi(tb);
    if (wb_int) dbuf_int=0;
  }

  if ((GetVar(pip,tb,1024,0)!=-1)&&(P96Base)) {
    pip_int=atoi(tb);
    if (pip_int) dbuf_int=0;
    if (pip_int) wb_int=1;
  }

  if ((GetVar(pipnoclear,tb,1024,0)!=-1)&&(P96Base)) {
    pipnoclear_int=atoi(tb);
    if (pipnoclear_int) pip_int=1;
    if (pipnoclear_int) dbuf_int=0;
    if (pip_int) wb_int=1;
  }

  strcpy(pubscreenname,"Workbench");
  if (GetVar(psn,tb,1024,0)!=-1)
    strcpy(pubscreenname,tb);

  mode2_changed=0;
  mode_num=-1;

  if ((GetVar(title2,tb,1024,0)!=-1)&&(override!=1)) {
    char modeid[512];
    int f=0;
    mode2_changed=2;
    strcpy(modeid,title2);
    mode=atoi(tb);
    if ((!acceptmode(ms,mode))&&(override==2)) {
      mode=0;
      mode2_changed=1;
      while (f<20) {
        int found;
        mode_num=f+1;
        sprintf(realid,"%s%i",modeid,f+1);
        found=GetVar(realid,tb,1024,0);
        mode=0;
        if (found==-1) break;
        mode=atoi(tb);
        if (!acceptmode(ms,mode))
          mode=0;
        else goto found;
        f++;
      }
    }
    if (!mode) goto request;
found:
    mode2_changed=2;
  }

  else {
    char help[100];

request:   
    sprintf(help," %dx%d",SCREENWIDTH,SCREENHEIGHT);
    strcat(reqtitle, help);
    aslreq[0].ti_Data=(ULONG)reqtitle;
    aslreq[1].ti_Data=mode;
    aslreq[2].ti_Data=ms->SCREENWIDTH;
    aslreq[3].ti_Data=ms->SCREENHEIGHT;
    aslreq[4].ti_Data=ms->MAXWIDTH;
    aslreq[5].ti_Data=ms->MAXHEIGHT;
    aslreq[6].ti_Data=ms->MINDEPTH;
    aslreq[7].ti_Data=ms->MAXDEPTH;
    aslreq[8].ti_Data=propertymask;
    aslreq[10].ti_Data=(ULONG)thehook;
    CNAME(cppc_minwidth)=ms->SCREENWIDTH;
    CNAME(cppc_minheight)=ms->SCREENHEIGHT;
    CNAME(cppc_maxwidth)=ms->MAXWIDTH;
    CNAME(cppc_maxheight)=ms->MAXHEIGHT;
    CNAME(cppc_mindepth)=ms->MINDEPTH;
    CNAME(cppc_maxdepth)=ms->MAXDEPTH;
    if (!AslRequest (video_smr, aslreq)) {
      error ("AslRequest() failed",ms);
      return 0;
    }
    mode = video_smr->sm_DisplayID;

    if (mode2_changed==0) {
      char modeid[512];
      FILE *fil;

      sprintf(modeid,"%i",mode);
      //SetVar(title2,modeid,1024,0);
      //SetVar(modeid2,modeid,1024,0);
      fil=fopen(title2,"w");
      fprintf(fil,modeid);
      fclose(fil);
      fil=fopen(modeid2,"w");
      fprintf(fil,modeid);
      fclose(fil);
    }
    else if (mode2_changed==1) {
      int num;
      char modeid[512];
      FILE *fil;

      if (strcmp(realid,"modeid20")==0) num=20;
      else num=mode_num;
      if (existsmode(modeid3,title2,mode,mode_num)) goto out;
      sprintf(title2,"%s%i",title2,num);
      sprintf(modeid2,"%s%i",modeid2,num);
      sprintf(modeid,"%i",mode);
      //SetVar(title2,modeid,1024,0);
      //SetVar(modeid2,modeid,1024,0);
      fil=fopen(title2,"w");
      fprintf(fil,modeid);
      fclose(fil);
      fil=fopen(modeid2,"w");
      fprintf(fil,modeid);
      fclose(fil);
    }
  }
out:
  ms->ham_int=ham_int;
  ms->wb_int=wb_int;
  ms->pip_int=pip_int;
  ms->dbuf_int=dbuf_int;
  ms->oldstyle_int=oldstyle_int;
  ms->likecgx=likecgx_int;
  if (ms->pip_int) {
    strcpy(ms->pubscreenname,"Workbench");
    strcpy(pubscreenname,ms->pubscreenname);
  }
  else strcpy(ms->pubscreenname,pubscreenname);
  ms->rtgm_int=0;
  ms->mode=mode;

  if (wb_int) {
    locked=1;
    video_screen=LockPubScreen(pubscreenname);
  }
  if ((handle = FindDisplayInfo (mode)) == NULL) {
    error ("Can't FindDisplayInfo() for mode %08x", ms,mode);
    return 0;
  }

  if ((nbytes = GetDisplayInfoData (handle, (UBYTE *)&dispinfo,
                                    sizeof(struct DisplayInfo), DTAG_DISP,
                                    0)) < 40 ) {
    error ("Can't GetDisplayInfoData() for mode %08x, got %d bytes",
           ms,mode, nbytes);
    return 0;
  }

  if ((nbytes = GetDisplayInfoData (handle, (UBYTE *)&dimsinfo,
                                    sizeof(dimsinfo), DTAG_DIMS,
                                    0)) < 66) {
    error ("Can't GetDisplayInfoData() for mode %08x, got %d bytes",
           ms,mode, nbytes);
    return 0;
  }

  if ((ms->dbuf_int)&&(ms->oldstyle_int)) numbuffers=2;
  else if (ms->dbuf_int) numbuffers=3;
  else numbuffers=1;

  ms->numbuffers=numbuffers; 
  ms->SCREENWIDTH=SCREENWIDTH;
  ms->SCREENHEIGHT=SCREENHEIGHT;
  video_oscan_height = dimsinfo.MaxOScan.MaxY - dimsinfo.MaxOScan.MinY + 1;
  ms->video_oscan_height=video_oscan_height;
  video_is_cyber_mode = 0;
  if (CyberGfxBase != NULL)
    video_is_cyber_mode = IsCyberModeID (mode);

  video_is_native_mode = ((GfxBase->LibNode.lib_Version < 39 ||
                          (dispinfo.PropertyFlags & DIPF_IS_EXTRAHALFBRITE) != 0 ||
                          (dispinfo.PropertyFlags & DIPF_IS_AA) != 0 ||
                          (dispinfo.PropertyFlags & DIPF_IS_ECS) != 0 ||
                          (dispinfo.PropertyFlags & DIPF_IS_DBUFFER) != 0) &&
                          !video_is_cyber_mode &&
                          (dispinfo.PropertyFlags & DIPF_IS_FOREIGN) == 0);

  if (!video_is_native_mode) {
    ham_int=0;
    ms->ham_int=0;
  }
  else {
    pip_int=0;
    pipnoclear_int=0;
    ms->pip_int=0;
    if (wb_int) {
      video_is_native_mode=0;
      ham_int=0;
      ms->ham_int=0;
    }
  }
  video_depth = 8;
  ms->video_depth=video_depth;
  ms->video_is_cyber_mode=video_is_cyber_mode;
  ms->video_is_native_mode=video_is_native_mode;
  SCREENWIDTH= dimsinfo.Nominal.MaxX-dimsinfo.Nominal.MinX+1;
  SCREENHEIGHT=dimsinfo.Nominal.MaxY-dimsinfo.Nominal.MinY+1;
  if (video_is_cyber_mode) {
    mydepth=GetCyberIDAttr(CYBRIDATTR_DEPTH,mode);
    SCREENWIDTH=GetCyberIDAttr(CYBRIDATTR_WIDTH,mode);
    SCREENHEIGHT=GetCyberIDAttr(CYBRIDATTR_HEIGHT,mode);
  }
  if (!(video_is_native_mode)) {
   if (reqwidth<SCREENWIDTH) {
     SCREENWIDTH=reqwidth;
     SCREENHEIGHT=reqheight;
     different=1;
   }
  }
  ms->SCREENWIDTH=SCREENWIDTH;
  ms->SCREENHEIGHT=SCREENHEIGHT;
  rect.MinX = 0;
  rect.MinY = 0;
  if ((ms->ham_int)&&(ms->video_is_native_mode)) rect.MaxX = 2*SCREENWIDTH-1;
  else rect.MaxX = SCREENWIDTH - 1;
  rect.MaxY = SCREENHEIGHT - 1;

  ms->format=-1;
  if (video_is_native_mode) {
    ms->c2p_compare_buffer = malloc(SCREENWIDTH*SCREENHEIGHT); /* phx */

    if (!ham_int) ms->format=PIXFMT_LUT8;
    else ms->format=PIXFMT_RGB15;

    if (ms->format==PIXFMT_RGB15) {
      for (i = 0; i < numbuffers; i++) {
        InitRastPort (&video_rastport[i]);
      }
      ms->video_rastport=video_rastport;
      if (wb_int) video_screen=IntuitionBase->FirstScreen;
      else {
        if (SCREENWIDTH<640) mode=mode|HIRESHAM_KEY;
        else mode=mode|SUPERHAM_KEY;
        if ((ms->dbuf_int)&&(ms->oldstyle_int)) {
          if ((video_screen = OpenScreenTags (NULL,
               SA_DisplayID,   mode,
               SA_DClip,       (ULONG)&rect,
               SA_Width,       2*SCREENWIDTH,
               SA_Height,      2*SCREENHEIGHT,
               SA_Depth,       8,
               SA_Title,       FALSE,
               SA_Quiet,       TRUE,
               TAG_DONE,       0)) == NULL) {
            error ("OpenScreen() failed",ms);
            return 0;
          }
        }
        else {
          if ((video_screen = OpenScreenTags (NULL,
               SA_DisplayID,   mode,
               SA_DClip,       (ULONG)&rect,
               SA_Width,       2*SCREENWIDTH,
               SA_Height,      SCREENHEIGHT,
               SA_Depth,       8,
               SA_Title,       FALSE,
               SA_Quiet,       TRUE,
               TAG_DONE,       0)) == NULL) {
            error ("OpenScreen() failed",ms);
            return 0;
          }
        }
      }
      ms->video_screen=video_screen;
    }

    else {
      for (i = 0; i < numbuffers; i++)
        InitRastPort (&video_rastport[i]);
      ms->video_rastport=video_rastport;

      if (wb_int) video_screen=IntuitionBase->FirstScreen;
      else {
        if ((ms->dbuf_int)&&(ms->oldstyle_int)) {
          if ((video_screen = OpenScreenTags (NULL,
               SA_DisplayID,   mode,
               SA_DClip,       (ULONG)&rect,
               SA_Width,       SCREENWIDTH,
               SA_Height,      2*SCREENHEIGHT,
               SA_Depth,       8,
               SA_Title,       FALSE,
               SA_Quiet,       TRUE,
               TAG_DONE,       0)) == NULL) {
            error ("OpenScreen() failed",ms);
            return 0;
          }
        }
        else {
          if ((video_screen = OpenScreenTags (NULL,
               SA_DisplayID,   mode,
               SA_DClip,       (ULONG)&rect,
               SA_Width,       SCREENWIDTH,
               SA_Height,      SCREENHEIGHT,
               SA_Depth,       8,
               SA_Quiet,       TRUE,
               SA_Title,       FALSE,
               TAG_DONE,       0)) == NULL) {
            error ("OpenScreen() failed",ms);
            return 0;
          }
        }
      }
      ms->video_screen=video_screen;
    }
  }

  else {
    if (wb_int) video_screen=IntuitionBase->FirstScreen;
    else {
      if ((ms->dbuf_int)&&(ms->oldstyle_int)) {
        if ((video_screen = OpenScreenTags (NULL,
             SA_DisplayID,   mode,
             SA_DClip,       (ULONG)&rect,
             SA_Width,       SCREENWIDTH,
             SA_Height,      2*SCREENHEIGHT,
             SA_Depth,       mydepth,
             SA_Quiet,       TRUE,
             SA_ShowTitle,FALSE,
             SA_Draggable,FALSE,
             TAG_DONE,       0)) == NULL) {
          error ("OpenScreen() failed",ms);
          return 0;
        }
      }
      else {
        if ((video_screen = OpenScreenTags (NULL,
             SA_DisplayID,   mode,
             SA_DClip,       (ULONG)&rect,
             SA_Width,       SCREENWIDTH,
             SA_Height,      SCREENHEIGHT,
             SA_Depth,       mydepth,
             SA_Quiet,       TRUE,
             SA_Title,       FALSE,
             TAG_DONE,       0)) == NULL) {
          error ("OpenScreen() failed",ms);
          return 0;
        }
      }
    }
    ms->video_screen=video_screen;
  }

  ms->SCREENWIDTH=SCREENWIDTH;
  ms->SCREENHEIGHT=SCREENHEIGHT;

  if (CyberGfxBase&&(video_is_cyber_mode)) {
    video_bitmap_handle = (APTR)LockBitMapTags (video_screen->ViewPort.RasInfo->BitMap,
                                                LBMI_WIDTH,       &width,
                                                LBMI_DEPTH,       &depth,
                                                LBMI_PIXFMT,      &pixfmt,
                                                LBMI_BYTESPERROW, &bpr,
                                                LBMI_BASEADDRESS, &screen,
                                                TAG_DONE);
    if (wb_int==0)
      ms->bpr=bpr;
    UnLockBitMap (video_bitmap_handle);
    ms->screen=screen;

    if (pixfmt==PIXFMT_LUT8) ms->format=PIXFMT_LUT8;
    else if (pixfmt==PIXFMT_RGB16) ms->format=PIXFMT_RGB16;
    else if (pixfmt==PIXFMT_BGR16) ms->format=PIXFMT_BGR16;
    else if (pixfmt==PIXFMT_RGB16PC) ms->format=PIXFMT_RGB16PC;
    else if (pixfmt==PIXFMT_BGR16PC) ms->format=PIXFMT_BGR16PC;
    else if (pixfmt==PIXFMT_RGB15) ms->format=PIXFMT_RGB15;
    else {
      error("Unsupported Screen format",ms);
      return 0;
    }
    if ((ms->dbuf_int)&&(ms->oldstyle_int)) {
      screenb=screen+SCREENHEIGHT*(ms->bpr);
      screenc=0;
      ms->screenb=screenb;
      ms->screenc=screenc;
    }
    if (wb_int==1) {
      if (pixfmt==PIXFMT_LUT8) bpr=SCREENWIDTH;
      else bpr=SCREENWIDTH*2;
      ms->bpr=bpr;
    }
  }
  if (ms->format==-1) {
    error("Unsupported Screen format",ms);
    return 0;
  }

  idcmp = IDCMP_RAWKEY;
  wflags = WFLG_ACTIVATE | WFLG_RMBTRAP | WFLG_NOCAREREFRESH |
           WFLG_SIMPLE_REFRESH;
  idcmp |= IDCMP_MOUSEMOVE | IDCMP_DELTAMOVE | IDCMP_MOUSEBUTTONS;
  wflags |= WFLG_REPORTMOUSE;

  if (!pip_int) {   
    if (wb_int) {
      if ((video_window = OpenWindowTags (NULL,
          WA_Left,         0,
          WA_Top,          0,
          WA_InnerWidth,        SCREENWIDTH,
          WA_InnerHeight,       SCREENHEIGHT,
          WA_IDCMP,        idcmp,
          WA_DragBar, TRUE,
          WA_RMBTrap, TRUE,
          WA_DepthGadget,TRUE,
          WA_Flags,        wflags,
          WA_CustomScreen, video_screen,
          WA_PubScreenName,(ULONG)pubscreenname,
          TAG_DONE)) == NULL) {
        error ("OpenWindow() failed",ms);
        return 0;
      }
    }
    else {
      if ((ms->dbuf_int)&&(ms->oldstyle_int)) {
        int w;
        if ((ms->video_is_native_mode)&&(ms->ham_int)) w=2*SCREENWIDTH;
        else w=SCREENWIDTH;
        if ((video_window = OpenWindowTags (NULL,
            WA_Left,         0,
            WA_Top,          0,
            WA_Width,        w,
            WA_Height,       2*SCREENHEIGHT,
            WA_IDCMP,        idcmp,
            WA_Flags,        wflags,
            WA_Borderless,   TRUE,
            WA_DepthGadget,0,
            WA_CloseGadget,0,
            WA_DragBar,0,
            WA_SizeGadget,0,
            WA_CustomScreen, video_screen,
            WA_DepthGadget,TRUE,
            WA_PubScreenName,(ULONG)pubscreenname,
            TAG_DONE)) == NULL) {
          error ("OpenWindow() failed",ms);
          return 0;
        }
      }
      else {
        int w;
        if ((ms->video_is_native_mode)&&(ms->ham_int)) w=2*SCREENWIDTH;
        else w=SCREENWIDTH;
        if ((video_window = OpenWindowTags (NULL,
            WA_Left,         0,
            WA_Top,          0,
            WA_InnerWidth,        w,
            WA_InnerHeight,       SCREENHEIGHT,
            WA_IDCMP,        idcmp,
            WA_Flags,        wflags,
            WA_CustomScreen, video_screen,
            WA_Borderless,   TRUE,
            WA_DepthGadget,0,
            WA_CloseGadget,0,
            WA_DragBar,0,
            WA_SizeGadget,0,
            WA_PubScreenName,(ULONG)pubscreenname,
            TAG_DONE)) == NULL) {
          error ("OpenWindow() failed",ms);
          return 0;
        }
      }
    }   
  }
  else {
    struct TagItem piptags[]= {
      P96PIP_SourceFormat, 0,
      P96PIP_SourceWidth, 0,
      P96PIP_SourceHeight, 0,
      WA_Title, (ULONG)"PIP",
      WA_Flags,0,
      WA_Activate,TRUE,
      WA_RMBTrap, TRUE,
      WA_Width, 0,
      WA_Height, 0,
      WA_DragBar,TRUE,
      WA_DepthGadget,TRUE,
      WA_IDCMP,0,
      WA_NewLookMenus,TRUE,
      WA_PubScreenName, 0,
      P96PIP_AllowCropping,TRUE,
      TAG_DONE,0
    };
    APTR vbhandle;
    vbhandle = (APTR)LockBitMapTags (video_screen->ViewPort.RasInfo->BitMap,
                                            LBMI_PIXFMT,      (ULONG)&pixfmt,
                                            TAG_DONE);
    UnLockBitMap (vbhandle);

    piptags[0].ti_Data=(ULONG)RGBFB_CLUT;
    piptags[1].ti_Data=(ULONG)SCREENWIDTH;
    piptags[2].ti_Data=(ULONG)SCREENHEIGHT;
    piptags[4].ti_Data=wflags;
    piptags[7].ti_Data=SCREENWIDTH;
    piptags[8].ti_Data=SCREENHEIGHT;
    piptags[11].ti_Data=idcmp;
    piptags[13].ti_Data=(ULONG)pubscreenname;   
    piptags[7].ti_Data=SCREENWIDTH*resize_int;
    piptags[8].ti_Data=SCREENHEIGHT*resize_int;
    if (video_screen->Width+20<piptags[7].ti_Data) piptags[7].ti_Data=video_screen->Width;
    if (video_screen->Height+20<piptags[8].ti_Data) piptags[8].ti_Data=video_screen->Height;
 
    ms->format=PIXFMT_LUT8;
    if (mydepth==8) 
    {
     ms->format=PIXFMT_LUT8;
     pixfmt=PIXFMT_LUT8;
    }
    if (pixfmt==PIXFMT_RGB16) 
    {
      piptags[0].ti_Data=RGBFB_R5G6B5;
      ms->format=pixfmt=PIXFMT_RGB16;
    }
    else if (pixfmt==PIXFMT_RGB16PC) 
    {
      piptags[0].ti_Data=RGBFB_R5G6B5PC;
      ms->format=pixfmt=PIXFMT_RGB16PC;
    }
    else if (pixfmt==PIXFMT_BGR16PC) 
    {
      piptags[0].ti_Data=RGBFB_B5G6R5PC;
      ms->format=pixfmt=PIXFMT_BGR16PC;
    }
    else 
    {
     piptags[0].ti_Data=RGBFB_CLUT;
     ms->format=pixfmt=PIXFMT_LUT8;
    }

    piptags[13].ti_Data=(ULONG)pubscreenname;
    video_window = p96PIP_OpenTagList(piptags);
    if (!(video_window)) {
      UnlockPubScreen(0,video_screen);
      strcpy(pubscreenname,"Workbench");
      video_screen=LockPubScreen(pubscreenname);
      piptags[13].ti_Data=(ULONG)pubscreenname;
      video_window = p96PIP_OpenTagList(piptags);
    }
    if (video_window) {
      struct TagItem gtTags[] = { {P96PIP_SourceBitMap,0},{TAG_DONE, 0} };
      struct RenderInfo renderinfo;
      LONG lock;

      gtTags[0].ti_Data = (ULONG)&bitmap;
      p96PIP_GetTagList(video_window, gtTags);
      lock = p96LockBitMap(bitmap, (UBYTE *) &renderinfo, sizeof(struct RenderInfo));
      screen = (unsigned char *) renderinfo.Memory;
      p96UnlockBitMap(bitmap, lock);
      bpr=p96GetBitMapAttr(bitmap,P96BMA_BYTESPERROW);
      ms->bpr=bpr;
      if (!(pipnoclear_int)) memset(screen,0,SCREENWIDTH*SCREENHEIGHT*p96GetBitMapAttr(bitmap,P96BMA_BYTESPERPIXEL));
    }
    else {
      error("Could not initialize PIP\n",ms,0);
      return 0;
    }
  }

  ms->video_window=video_window;
  ms->video_tmp_bm=&video_tmp_bm;
  if (wb_int) UnlockPubScreen(0,video_screen);
  locked=0;
  InitBitMap (&video_tmp_bm, video_depth, SCREENWIDTH, 1);
  video_temprp=*(video_window->RPort);
  ms->video_temprp=&video_temprp;
  for (depth = 0; depth < video_depth; depth++) {
    video_tmp_bm.Planes[depth]=AllocRaster(SCREENWIDTH,1);
    if (video_tmp_bm.Planes[depth] == 0) {
      error ("AllocRaster() failed",ms);
      return 0;
    }
  }
  video_temprp.Layer = NULL;
  video_temprp.BitMap = &video_tmp_bm;

  ms->wbcolors=(UBYTE *)AllocVec(1024,MEMF_FAST|MEMF_CLEAR);
  if (!(ms->wbcolors)) {
    error("Out of memory",ms);
    return 0;
  }
  ms->pen_obtained=0;
  ms->transtable=(UBYTE *)AllocVec(256*sizeof(UBYTE),MEMF_FAST|MEMF_CLEAR);
  ms->WBColorTable=(unsigned long *)AllocVec(770*sizeof(unsigned long),MEMF_FAST|MEMF_CLEAR);
  ms->ColorTable=(unsigned long *)AllocVec(770*sizeof(unsigned long),MEMF_FAST|MEMF_CLEAR);
  if (!(ms->transtable)) {
    error("Out of memory",ms);
    return 0;
  }
  if (!(ms->WBColorTable)) {
    error("Out of memory",ms);
    return 0;
  }
  if (!(ms->ColorTable)) {
    error("Out of memory",ms);
    return 0;
  }
  ms->WBColorTable[0] = 0x01000000;
  ms->WBColorTable[3*256+1] = 0;
  GetRGB32(((video_screen)->ViewPort).ColorMap,0,256,ms->WBColorTable+1);
  ms->pal_changed=0;
  emptypointer=(UWORD *)AllocVec(sizeof(UWORD)*6,MEMF_CHIP|MEMF_CLEAR);

  ms->emptypointer=emptypointer;
  if (!emptypointer) {
    error("Not enough memory!\n",ms);
    return 0;
  }
  SetPointer (video_window, emptypointer, 1, 16, 0, 0);
  ms->bufnum=0;

  if (video_smr) {
    FreeAslRequest(video_smr);
    video_smr=0;
  }

  if (ms->dbuf_int) {
    ms->bitmapa=(&((ms->video_screen)->RastPort))->BitMap;
    if (video_is_native_mode) {
      if (ham_int) {
        if (ms->oldstyle_int) {
          ULONG *ppr;

          ppr=(ULONG *)(ms->bitmapa)->Planes[6];
          for (depth=0;depth<((SCREENWIDTH*SCREENHEIGHT)/16);depth++)
            *ppr++=0xBBBBBBBB;
          ppr=(ULONG *)(ms->bitmapa)->Planes[7];
          for (depth=0;depth<((SCREENWIDTH*SCREENHEIGHT)/16);depth++)
            *ppr++=0xEEEEEEEE;
          ms->bitmapb=0;
          ms->bitmapc=0;
          ms->screen=(ms->bitmapa)->Planes[0];
          ms->screenb=(ms->screen)+(SCREENWIDTH/4)*SCREENHEIGHT;
          ms->screenc=0;
          ms->Buf1=0;
          ms->Buf2=0;
          ms->Buf3=0;
        }

        else {
          ULONG *ppr;

          ms->Buf1 = AllocScreenBuffer(ms->video_screen, NULL, SB_SCREEN_BITMAP);
          if (!(ms->Buf1)) {
            error("ScreenBuffer allocation failed",ms);
            return 0;
          }
          ms->Buf2 = AllocScreenBuffer(ms->video_screen, NULL, 0);
          if (!(ms->Buf2)) {
            error("ScreenBuffer 2 allocation failed",ms);
            return 0;
          }
          ms->Buf3 = AllocScreenBuffer(ms->video_screen, NULL, 0);
          if (!(ms->Buf3)) {
            error("ScreenBuffer 3 allocation failed",ms);
            return 0;
          }
          ms->bitmapa=(ms->Buf1)->sb_BitMap;
          ms->bitmapb=(ms->Buf2)->sb_BitMap;
          ms->bitmapc=(ms->Buf3)->sb_BitMap;
          ms->screen=(ms->bitmapa)->Planes[0];
          ms->screenb=(ms->bitmapb)->Planes[0];
          ms->screenc=(ms->bitmapc)->Planes[0];
          ppr=(ULONG *)(ms->bitmapa)->Planes[6];
          for (depth=0;depth<((SCREENWIDTH*SCREENHEIGHT)/16);depth++)
            *ppr++=0xBBBBBBBB;
          ppr=(ULONG *)(ms->bitmapa)->Planes[7];
          for (depth=0;depth<((SCREENWIDTH*SCREENHEIGHT)/16);depth++)
            *ppr++=0xEEEEEEEE;

          if (numbuffers>1) {
            ppr=(ULONG *)(ms->bitmapb)->Planes[6];
            for (depth=0;depth<((SCREENWIDTH*SCREENHEIGHT)/16);depth++)
              *ppr++=0xBBBBBBBB;
            ppr=(ULONG *)(ms->bitmapb)->Planes[7];
            for (depth=0;depth<((SCREENWIDTH*SCREENHEIGHT)/16);depth++)
              *ppr++=0xEEEEEEEE;

            if (numbuffers>2) {
              ppr=(ULONG *)(ms->bitmapc)->Planes[6];
              for (depth=0;depth<((SCREENWIDTH*SCREENHEIGHT)/16);depth++)
                *ppr++=0xBBBBBBBB;
              ppr=(ULONG *)(ms->bitmapc)->Planes[7];
              for (depth=0;depth<((SCREENWIDTH*SCREENHEIGHT)/16);depth++)
                *ppr++=0xEEEEEEEE;
            }
          }
        }
      }

      else {
        if (ms->oldstyle_int) {
          ms->Buf1=0;
          ms->Buf2=0;
          ms->Buf3=0;
          ms->bitmapb=0;
          ms->bitmapc=0;
          ms->screen=(ms->bitmapa)->Planes[0];
          ms->screenb=(ms->screen)+(SCREENWIDTH/8)*SCREENHEIGHT;
          ms->screenc=0;
        }
        else {
          ms->Buf1 = AllocScreenBuffer(ms->video_screen, NULL, SB_SCREEN_BITMAP);
          if (!(ms->Buf1)) {
            error("ScreenBuffer allocation failed",ms);
            return 0;
          }
          ms->Buf2 = AllocScreenBuffer(ms->video_screen, NULL, 0);
          if (!(ms->Buf2)) {
            error("ScreenBuffer 2 allocation failed",ms);
            return 0;
          }
          ms->Buf3 = AllocScreenBuffer(ms->video_screen, NULL, 0);
          if (!(ms->Buf3)) {
            error("ScreenBuffer 3 allocation failed",ms);
            return 0;
          }
          ms->bitmapa=(ms->Buf1)->sb_BitMap;
          ms->bitmapb=(ms->Buf2)->sb_BitMap;
          ms->bitmapc=(ms->Buf3)->sb_BitMap;
          ms->screen=(ms->bitmapa)->Planes[0];
          ms->screenb=(ms->bitmapb)->Planes[0];
          ms->screenc=(ms->bitmapc)->Planes[0];
        }
      }
    }

    else {
      if (ms->oldstyle_int) {
        ms->Buf1=0;
        ms->Buf2=0;
        ms->Buf3=0;
        ms->bitmapb=0;
        ms->bitmapc=0;
      }
      else {
        ms->Buf1 = AllocScreenBuffer(ms->video_screen, NULL, SB_SCREEN_BITMAP);
        if (!(ms->Buf1)) {
          error("ScreenBuffer allocation failed",ms);
          return 0;
        }
        ms->Buf2 = AllocScreenBuffer(ms->video_screen, NULL, 0);
        if (!(ms->Buf2)) {
          error("ScreenBuffer 2 allocation failed",ms);
          return 0;
        }
        ms->Buf3 = AllocScreenBuffer(ms->video_screen, NULL, 0);
        if (!(ms->Buf3)) {
          error("ScreenBuffer 3 allocation failed",ms);
          return 0;
        }
        ms->bitmapa=(ms->Buf1)->sb_BitMap;
        ms->bitmapb=(ms->Buf2)->sb_BitMap;
        ms->bitmapc=(ms->Buf3)->sb_BitMap;
        video_bitmap_handle = (APTR)LockBitMapTags(ms->bitmapb,
                                                   LBMI_BASEADDRESS, &screenb,
                                                   TAG_DONE);
        UnLockBitMap (video_bitmap_handle);
        video_bitmap_handle = (APTR)LockBitMapTags (ms->bitmapc,
                                                    LBMI_BASEADDRESS, &screenc,
                                                    TAG_DONE);
        UnLockBitMap (video_bitmap_handle);
        ms->screenb=screenb;
        ms->screenc=screenc;
      }
    }

    if (ms->oldstyle_int) {
      video_rastport[0].BitMap = ms->bitmapa;
      ms->thebitmap=ms->bitmapa;
      SetAPen (&video_rastport[0], (1 << 8) - 1);
      SetBPen (&video_rastport[0], 0);
      SetDrMd (&video_rastport[0], JAM2);
    }
    else {
      video_rastport[0].BitMap = ms->bitmapa;
      video_rastport[1].BitMap = ms->bitmapb;
      video_rastport[2].BitMap = ms->bitmapc;
      ms->thebitmap=ms->bitmapa;
      for (i=0;i<2;i++) {
        SetAPen (&video_rastport[i], (1 << 8) - 1);
        SetBPen (&video_rastport[i], 0);
        SetDrMd (&video_rastport[i], JAM2);
      }
    }
  }

  else {
    ms->bitmapa=((ms->video_window)->RPort)->BitMap;
    if (video_is_native_mode) {
      ms->Buf1=0;
      ms->Buf2=0;
      ms->Buf3=0;
      ms->bitmapb=0;
      ms->bitmapc=0;
      ms->screen=(ms->bitmapa)->Planes[0];
      if (ms->ham_int) {
        ULONG *ppr;
        ppr=(ULONG *)(ms->bitmapa)->Planes[6];
        for (depth=0;depth<((SCREENWIDTH*SCREENHEIGHT)/16);depth++)
          *ppr++=0xBBBBBBBB;
        ppr=(ULONG *)(ms->bitmapa)->Planes[7];
        for (depth=0;depth<((SCREENWIDTH*SCREENHEIGHT)/16);depth++)
          *ppr++=0xEEEEEEEE;
      }
    }
    else {
      ms->Buf1=0;
      ms->Buf2=0;
      ms->Buf3=0;
      ms->bitmapb=0;
      ms->bitmapc=0;
    }

    video_rastport[0].BitMap = ms->bitmapa;
    ms->thebitmap=ms->bitmapa;
    SetAPen (&video_rastport[0], (1 << 8) - 1);
    SetBPen (&video_rastport[0], 0);
    SetDrMd (&video_rastport[0], JAM2);
  }
  
  if (ms->pip_int) {
    struct TagItem gtTags[] = { {P96PIP_SourceBitMap,0},{TAG_DONE, 0} };
    struct RenderInfo renderinfo;
    LONG lock;

    gtTags[0].ti_Data = (ULONG)&bitmap;
    p96PIP_GetTagList(video_window, gtTags);
    lock = p96LockBitMap(bitmap, (UBYTE *) &renderinfo, sizeof(struct RenderInfo));
    screen = (unsigned char *) renderinfo.Memory;
    p96UnlockBitMap(bitmap, lock);
    ms->screen=screen;
    if (!(pipnoclear_int)) memset(screen,0,SCREENWIDTH*SCREENHEIGHT*p96GetBitMapAttr(bitmap,P96BMA_BYTESPERPIXEL));
  }

  if (ms->bpr==0) {
    if (ms->format==PIXFMT_LUT8) ms->bpr=ms->SCREENWIDTH;
    else ms->bpr=2*(ms->SCREENWIDTH);
  }

  if (different&&(ms->video_is_cyber_mode)) {
    UnLockBitMap(LockBitMapTags (ms->bitmapa,
                                 LBMI_BYTESPERROW, &bpr,
                                 TAG_DONE));
    ms->bpr=bpr;
  }

  if (!ms->video_is_cyber_mode) likecgx_int=0;
  if ((ms->wb_int)&&(!P96Base)) likecgx_int=1;
  if (ms->pip_int) likecgx_int=0;
  if (ms->format==PIXFMT_LUT8) likecgx_int=0;
  if (!(ms->wb_int)) likecgx_int=0;
  ms->likecgx=likecgx_int;

  if (likecgx_int) {
    ms->Buf1 = AllocScreenBuffer(ms->video_screen, NULL, 0);
    if (!(ms->Buf1)) {
      error("Back Screen Buffer allocation failed",ms);
      return 0;
    }
    ms->bitmapa=(ms->Buf1)->sb_BitMap;
    video_bitmap_handle = (APTR)LockBitMapTags (ms->bitmapa,
                                                LBMI_BASEADDRESS, &(ms->screen),
                                                LBMI_BYTESPERROW,&(ms->bpr),
                                                TAG_DONE);
    UnLockBitMap (video_bitmap_handle);
  }

  if (pixfmt==PIXFMT_LUT8) ms->video_depth=8;
  else if (pixfmt==PIXFMT_RGB16) ms->video_depth=16;
  else if (pixfmt==PIXFMT_BGR16) ms->video_depth=16;
  else if (pixfmt==PIXFMT_RGB16PC) ms->video_depth=16;
  else if (pixfmt==PIXFMT_BGR16PC) ms->video_depth=16;
  else if (pixfmt==PIXFMT_RGB15) ms->video_depth=15;
  if ((ms->video_depth==15)&&(ms->video_is_native_mode)) ms->video_depth=16;

  return(ms);
}


void LoadColors(struct Mode_Screen *ms, ULONG Table[])
{
  struct TagItem EmptyTags[] = {
    TAG_DONE,0
  };
  int i,r,g,b;
  struct ViewPort *VPort;
  struct Window *Window;
  struct ColorMap *ColorMap;

  Window=ms->video_window;
  ColorMap = Window->WScreen->ViewPort.ColorMap;
  VPort = &Window->WScreen->ViewPort;

  if ((!(ms->wb_int))||(ms->pip_int)) {
    LoadRGB32(VPort,Table);
    return;
  }

  if (!(ms->pen_obtained)) {
    ms->WBColorTable[0] = 0x01000000;
    ms->WBColorTable[3*256+1] = 0;
    GetRGB32(ColorMap,0,256,ms->WBColorTable+1);
    for (i=0;i<(3*256+2);i++)
      ms->ColorTable[i] = ms->WBColorTable[i];
    for (i=0; i<256; i++) {
      r = Table[3*i+1];
      g = Table[3*i+2];
      b = Table[3*i+3];
      r=r&0xFF000000;
      g=g&0xFF000000;
      b=b&0xFF000000;
      ms->transtable[i] = ObtainBestPenA(ColorMap,r,g,b,EmptyTags);
    }
  }

  for (i=0; i<256; i++) {
          ms->ColorTable[3*ms->transtable[i]+1] = Table[3*i+1];
          ms->ColorTable[3*ms->transtable[i]+2] = Table[3*i+2];
          ms->ColorTable[3*ms->transtable[i]+3] = Table[3*i+3];
  }

  LoadRGB32(VPort,ms->ColorTable);
  ms->pen_obtained = TRUE;
  ms->pal_changed=1;
}


void DoubleBufferOld(struct Mode_Screen *ms)
{
  struct ViewPort *vp = &((ms->video_screen)->ViewPort);
  int bufnum=ms->bufnum;

  if (bufnum == 0) {
    vp->RasInfo->RyOffset = ms->SCREENHEIGHT;
    ScrollVPort(vp);
    WaitBOVP(vp);
    ms->bufnum = 1-ms->bufnum;
  }
  else {
    vp->RasInfo->RyOffset = 0;
    ScrollVPort(vp);
    WaitBOVP(vp);
    ms->bufnum = 1-ms->bufnum;
  }
}


void DoubleBuffer(struct Mode_Screen *ms)
{
  struct ViewPort *vp = &((ms->video_screen)->ViewPort);

  if (ms->dbuf_int==0) return;
  if (ms->oldstyle_int) {
    DoubleBufferOld(ms);
    return;
  }

  if (ms->bufnum == 0) {
    ms->thebitmap=ms->bitmapb;
    (ms->Buf2)->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = NULL;
    while (!ChangeScreenBuffer(ms->video_screen, ms->Buf2));
    ms->bufnum = 1;
  }
  else if (ms->bufnum==1) {
    ms->thebitmap=ms->bitmapc;
    (ms->Buf3)->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = NULL;
    while (!ChangeScreenBuffer(ms->video_screen, ms->Buf3));
    ms->bufnum=2;
  }
  else {
    ms->thebitmap=ms->bitmapa;
    (ms->Buf1)->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = NULL;
    while (!ChangeScreenBuffer(ms->video_screen, ms->Buf1));
    ms->bufnum=0;
  }
}


#ifdef __PPC__
int ChunkyInit(struct Mode_Screen *ms,int srcformat)
#else
int ChunkyInit68k(struct Mode_Screen *ms,int srcformat)
#endif
{
  if (ms->video_is_native_mode) ms->algo=c2p;
  if (ms->wb_int) ms->algo=WbChunky8;
  else ms->algo=Chunky8;
  return 1;
}
