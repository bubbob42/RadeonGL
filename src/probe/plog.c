/*
 * plog.c -- see plog.h.
 */

#include <stdarg.h>

#include <exec/types.h>
#include <dos/dos.h>
#include <proto/dos.h>

#include "plog.h"

static BPTR log_File = 0;

BOOL Log_Open(CONST_STRPTR path)
{
    if (path == NULL || path[0] == '\0')
        return TRUE;
    log_File = Open(path, MODE_NEWFILE);
    return (BOOL)(log_File != 0);
}

void Log_Close(void)
{
    if (log_File)
    {
        Close(log_File);
        log_File = 0;
    }
}

/* On the 68k a va_list is a pointer to the arguments on the stack, which
 * is exactly the array VFPrintf() wants. Varargs functions take their
 * arguments on the stack under both compilers' register conventions. */
void Log_Printf(CONST_STRPTR fmt, ...)
{
    va_list ap;
    BPTR out;

    out = Output();
    va_start(ap, fmt);
    if (out)
        VFPrintf(out, fmt, (APTR)ap);
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
