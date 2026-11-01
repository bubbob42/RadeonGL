#include <emul/emulinterface.h>

static ULONG Call68k(struct EmulCaos *ec,void *function) =
  "\tlwz\t0,92(2)\n"
  "\tmtlr\t0\n"
  "\tstw\t4,0(3)\n"
  "\tblrl";


/* prototypes from sys_MulDiv64PPC.s */
extern void PPCDivu64p(int *,int *);
extern void PPCDivs64p(int *,int *);
extern void PPCModu64p(int *,int *);
extern void PPCMods64p(int *,int *);
extern void PPCMulu64p(int *,int *);
extern void PPCMuls64p(int *,int *);
