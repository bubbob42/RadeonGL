/*
 * plog.c -- see plog.h.
 */

#include <stdarg.h>

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "plog.h"

/* RawDoFmt's character procedure: the character in d0, the data in a3 */
#if defined(__SASC)
# define PUTCH_QUAL __asm
# define REG(r, a) register __##r a
#elif defined(__GNUC__)
# define PUTCH_QUAL
# define REG(r, a) a __asm(#r)
#else
# error "plog.c needs register-argument support (SAS/C or GCC)."
#endif

#define HOLD_BYTES 16384UL

struct Held
{
    ULONG len;
    ULONG lost;
    char buf[HOLD_BYTES];
};

static BPTR log_File = 0;
static ULONG log_Depth = 0;
static struct Held log_Held;
static APTR log_WindowPtr;      /* pr_WindowPtr as found, while held */

BOOL Log_Open(CONST_STRPTR path)
{
    if (path == NULL || path[0] == '\0')
        return TRUE;
    log_File = Open(path, MODE_NEWFILE);
    return (BOOL)(log_File != 0);
}

void Log_Close(void)
{
    while (log_Depth != 0)
        Log_Hold(FALSE);
    if (log_File)
    {
        Close(log_File);
        log_File = 0;
    }
}

/* Appends one character to the held text (RawDoFmt ends with a NUL, which
 * is not kept). */
static void PUTCH_QUAL HoldChar(REG(d0, UBYTE ch), REG(a3, struct Held *h))
{
    if (ch == 0)
        return;
    if (h->len < HOLD_BYTES)
        h->buf[h->len++] = (char)ch;
    else
        h->lost++;
}

/* On the 68k a va_list is a pointer to the arguments on the stack, which
 * is exactly the array VFPrintf() and RawDoFmt() want. Varargs functions
 * take their arguments on the stack under both compilers' register
 * conventions. */
void Log_Printf(CONST_STRPTR fmt, ...)
{
    va_list ap;
    BPTR out;

    va_start(ap, fmt);
    if (log_Depth != 0)
        RawDoFmt(fmt, (APTR)ap, (void (*)())HoldChar, &log_Held);
    else
    {
        out = Output();
        if (out)
            VFPrintf(out, fmt, (APTR)ap);
    }
    va_end(ap);
    if (log_File)
    {
        va_start(ap, fmt);
        VFPrintf(log_File, fmt, (APTR)ap);
        va_end(ap);
    }
}

void Log_Section(CONST_STRPTR title)
{
    Log_Printf("\n--- %s ---\n", title);
}

void Log_Hold(BOOL hold)
{
    BPTR out;

    struct Process *me;

    me = (struct Process *)FindTask(NULL);
    if (hold)
    {
        /* the LOG file is still written while the board is locked: a DOS
         * requester there (disk full, volume missing) would draw on the
         * locked board and deadlock, so DOS fails the write instead */
        if (log_Depth++ == 0 && me->pr_Task.tc_Node.ln_Type == NT_PROCESS)
        {
            log_WindowPtr = me->pr_WindowPtr;
            me->pr_WindowPtr = (APTR)-1;
        }
        return;
    }
    if (log_Depth == 0 || --log_Depth != 0)
        return;
    if (me->pr_Task.tc_Node.ln_Type == NT_PROCESS)
        me->pr_WindowPtr = log_WindowPtr;
    out = Output();
    /* the held text comes after what VFPrintf still buffers */
    if (out)
        Flush(out);
    if (out && log_Held.len != 0)
        Write(out, log_Held.buf, (LONG)log_Held.len);
    if (out && log_Held.lost != 0)
        FPrintf(out, "(%lu more bytes of output while the board was locked: see the LOG file)\n",
                log_Held.lost);
    log_Held.len = 0;
    log_Held.lost = 0;
}
