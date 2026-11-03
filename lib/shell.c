/*
 * shell.c -- the minigl.library shell: ROMTag, the four library vectors and
 * the one public vector, LibGetDispatchTable (-30), that every client of
 * dispatch ABI 5 calls through MiniGLOpen().
 *
 * Structure from Steffen Haeuser's shell (MiniGL_Library_68k, branch
 * MiniGL_Classic, libraryroot/library/minigl_library.c; Hyperion MiniGL Open
 * Source Licence). Changed here:
 *   - LibInit frees the library base when it fails (the original leaks it);
 *   - LibClose does not clear the current context for every closer: a
 *     context belongs to the task that created it, and only that task's
 *     LAST close (a client that died or forgot mglDeleteContext) frees it
 *     -- the opens are counted per task;
 *   - nothing runs MGLInit inside LibOpen;
 *   - LibInit refuses a CPU the build cannot run on (the 68040/68060 build
 *     holds FPU instructions a 68881/68882 does not know).
 *
 * This file must stay first in the link: LibNoExec is the first code in the
 * hunk, so running the library as a program returns at once.
 */

#define MINIGL_LIBRARY_BUILD 1
#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <exec/semaphores.h>
#include <dos/dos.h>
#include <intuition/intuitionbase.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <libraries/minigl_dispatch.h>

#include "backend.h"

#define LIBNAME  "minigl.library"
#define VERSION  29
#define REVISION 1

struct MiniGLBase
{
    struct Library         lib;
    BPTR                   segList;
    struct SignalSemaphore lock;
};

/* Running the library file as a command lands here. */
LONG LibNoExec(void)
{
    return -1;
}

static const char LibName[] = LIBNAME;

/* Opens per task (LibOpen and LibClose run under Forbid). A task beyond
 * the table is not counted: its first close counts as its last. */
#define OPENERS 32
static struct Task *opener[OPENERS];
static UWORD opens[OPENERS];

/* The CPU this build needs: RGL_CPU_040 (Makefile) for the 68040/68060
 * build, else a 68020 or 68030 with a 68881/68882. Either needs an FPU. */
static BOOL CpuFits(struct ExecBase *sb)
{
    UWORD f;

    f = sb->AttnFlags;
    if (!(f & (AFF_68881 | AFF_FPU40)))
        return FALSE;
#ifdef RGL_CPU_040
    return (BOOL) ((f & AFF_68040) != 0);
#else
    return (BOOL) ((f & AFF_68020) != 0);
#endif
}

/* The bases the NDK inline headers use. */
struct ExecBase *SysBase = NULL;
struct DosLibrary *DOSBase = NULL;
struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;

extern const MGLDispatchTable MiniGLDispatchTable;

static void CloseBases(void)
{
    if (IntuitionBase)
        CloseLibrary((struct Library *)IntuitionBase);
    if (GfxBase)
        CloseLibrary((struct Library *)GfxBase);
    if (DOSBase)
        CloseLibrary((struct Library *)DOSBase);
    IntuitionBase = NULL;
    GfxBase = NULL;
    DOSBase = NULL;
}

static void FreeBase(struct MiniGLBase *base)
{
    FreeMem((UBYTE *)base - base->lib.lib_NegSize,
            (ULONG)(base->lib.lib_NegSize + base->lib.lib_PosSize));
}

static const MGLDispatchTable *LibGetDispatchTable(void)
{
    return &MiniGLDispatchTable;
}

static struct MiniGLBase *LibInit(
    register struct MiniGLBase *base __asm("d0"),
    register BPTR seglist __asm("a0"),
    register struct ExecBase *sysbase __asm("a6"))
{
    SysBase = sysbase;
    if (!CpuFits(sysbase))
    {
        FreeBase(base);
        return NULL;
    }
    base->lib.lib_Node.ln_Type = NT_LIBRARY;
    base->lib.lib_Node.ln_Name = (char *)LibName;
    base->lib.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    base->lib.lib_Version = VERSION;
    base->lib.lib_Revision = REVISION;
    base->lib.lib_IdString = (char *)Back_IdString;
    base->segList = seglist;
    InitSemaphore(&base->lock);

    DOSBase = (struct DosLibrary *)OpenLibrary("dos.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39);
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    if (DOSBase == NULL || GfxBase == NULL || IntuitionBase == NULL)
    {
        CloseBases();
        FreeBase(base);
        return NULL;
    }
    Back_Init();
    return base;
}

static BPTR LibExpunge(register struct MiniGLBase *base __asm("a6"));

static struct MiniGLBase *LibOpen(register struct MiniGLBase *base __asm("a6"))
{
    struct Task *me;
    ULONG i;
    ULONG free_slot;

    me = FindTask(NULL);
    free_slot = OPENERS;
    for (i = 0; i < OPENERS; i++)
    {
        if (opener[i] == me)
            break;
        if (opener[i] == NULL && free_slot == OPENERS)
            free_slot = i;
    }
    if (i == OPENERS)
        i = free_slot;
    if (i < OPENERS)
    {
        opener[i] = me;
        opens[i]++;
    }
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= (UBYTE)~LIBF_DELEXP;
    return base;
}

static BPTR LibClose(register struct MiniGLBase *base __asm("a6"))
{
    struct Task *me;
    ULONG i;
    BOOL last;

    me = FindTask(NULL);
    last = TRUE;
    for (i = 0; i < OPENERS; i++)
        if (opener[i] == me)
        {
            if (opens[i] > 1)
            {
                opens[i]--;
                last = FALSE;
            }
            else
            {
                opens[i] = 0;
                opener[i] = NULL;
            }
            break;
        }
    if (last)
        Back_ClientGone(me);
    if (base->lib.lib_OpenCnt)
        base->lib.lib_OpenCnt--;
    if (base->lib.lib_OpenCnt == 0 && (base->lib.lib_Flags & LIBF_DELEXP))
        return LibExpunge(base);
    return 0;
}

static BPTR LibExpunge(register struct MiniGLBase *base __asm("a6"))
{
    BPTR seg;

    if (base->lib.lib_OpenCnt)
    {
        base->lib.lib_Flags |= LIBF_DELEXP;
        return 0;
    }
    seg = base->segList;
    Remove((struct Node *)base);
    CloseBases();
    FreeBase(base);
    return seg;
}

static ULONG LibReserved(void)
{
    return 0;
}

static const APTR LibVectors[] =
{
    (APTR)LibOpen,
    (APTR)LibClose,
    (APTR)LibExpunge,
    (APTR)LibReserved,
    (APTR)LibGetDispatchTable,      /* -30: the only public entry */
    (APTR)-1
};

static const struct
{
    ULONG size;
    const APTR *vectors;
    const ULONG *data;
    APTR init;
} InitTable =
{
    sizeof(struct MiniGLBase),
    LibVectors,
    NULL,
    (APTR)LibInit
};

const struct Resident ROMTag =
{
    RTC_MATCHWORD,
    (struct Resident *)&ROMTag,
    (APTR)(&ROMTag + 1),
    RTF_AUTOINIT,
    VERSION,
    NT_LIBRARY,
    0,
    (char *)LibName,
    (char *)Back_IdString,
    (APTR)&InitTable
};
