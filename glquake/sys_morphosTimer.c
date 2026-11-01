/*
** GetSysTimerPPC emulation for MorphOS by Frank Wille <frank@phoenix.owl.de>
** Bus clock determination code is based on powerpc.library source
** by Sam Jordan. Thanks Sam!
*/

#pragma amiga-align
#include <devices/timer.h>
#pragma default-align
#include "sys_timer.h"
#include "sys_morphos.h"


static char gap1[32];
static struct ciatimer ctim;
static char gap2[32];
static ULONG TicksPerSec[2];


#ifdef __VBCC__
ULONG mftbu(void) = "\tmftbu\t3";
ULONG mftbl(void) = "\tmftbl\t3";
#else
#error Please write an external assembler module for mftbl/mftbu
#endif



void InitSysTimePPC(void)
{
  static ULONG one[2] = { 1,0 };
  struct EmulCaos MyCaos;
  unsigned long clk;

  MyCaos.reg_a0 = (ULONG)&ctim;
  if (Call68k(&MyCaos,(void *)ReserveCIA)) {
    /* This should run in supervisor mode, for a better precision, */
    /* but it seems sufficient for Quake... */
    clk = MeasureBusClock(&ctim);
    MyCaos.reg_a0 = (ULONG)&ctim;
    Call68k(&MyCaos,(void *)FreeCIA);
  }
  TicksPerSec[0] = 0;
  TicksPerSec[1] = CorrectBusClock(clk) >> 2; /* PPC timer counts every 4th */
}


void GetSysTimePPC(struct timeval *tv)
{
  static ULONG mill[2] = { 0,1000000 };
  ULONG r[2],secs[2];

  secs[0] = r[0] = mftbu();
  secs[1] = r[1] = mftbl();
  PPCDivu64p((int *)secs,(int *)TicksPerSec);
  tv->tv_secs = secs[1];
  PPCModu64p((int *)r,(int *)TicksPerSec);
  PPCMulu64p((int *)r,(int *)mill);
  PPCDivu64p((int *)r,(int *)TicksPerSec);
  tv->tv_micro = r[1];
}
