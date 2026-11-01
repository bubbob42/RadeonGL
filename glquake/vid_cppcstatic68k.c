#include <libraries/asl.h>
#include <proto/graphics.h>
#include "amigacompiler.h"

extern int cppc_minwidth;
extern int cppc_minheight;
extern int cppc_maxwidth;
extern int cppc_maxheight;
extern int cppc_mindepth;
extern int cppc_maxdepth;



ULONG ASM filterfunc(REG(a0,struct Hook *hook),REG(a1,void *mode),
                     REG(a2,struct ScreenModeRequester *req))
{
  struct DimensionInfo dimsinfo;
  int width;
  int height;

  if (!GetDisplayInfoData(NULL, (UBYTE *)&dimsinfo,
                          sizeof(struct DimensionInfo), DTAG_DIMS,
                          (ULONG)mode)) {
    return FALSE;
  }

  width=dimsinfo.Nominal.MaxX-dimsinfo.Nominal.MinX+1;
  height=dimsinfo.Nominal.MaxY-dimsinfo.Nominal.MinY+1;

  if ((dimsinfo.MaxDepth < cppc_mindepth)||(dimsinfo.MaxDepth>cppc_maxdepth)||
      (cppc_minwidth>width)||(cppc_maxwidth<width)||(cppc_minheight>height)||
      (cppc_maxheight<height)) {
    return FALSE;
  }

  return TRUE;
}
