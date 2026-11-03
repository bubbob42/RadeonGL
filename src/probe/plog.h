/*
 * plog.h -- the probes' output: every line to the console and, when a LOG
 * file is given, to that file as well. The file is what the host reads
 * after a run on the real machine; the console is what the user watches.
 *
 * Formats are RawDoFmt's: %ld %lu %lx %s %lc, with the l on every number.
 *
 * While a P96 bitmap is locked, nothing may be printed to a console: the
 * console handler renders through the same board, and a held lock keeps
 * P96 off the chip (MEASURED: LockBench), so a Write() to a console
 * window could wait for an unlock that never comes. Between
 * Log_Hold(TRUE) and Log_Hold(FALSE) lines still go to the LOG file at
 * once, but the console's copy waits in a buffer (16 KB; what does not fit
 * is counted and reported) and is written at the release, after a Flush
 * of what VFPrintf still buffers. While held, pr_WindowPtr is -1: a DOS
 * requester for the LOG file's volume would draw on the locked board, so
 * the write fails instead. P96Board_Lock and P96Board_Unlock (p96board.h)
 * hold and release.
 */

#ifndef PROBE_PLOG_H
#define PROBE_PLOG_H

#include <exec/types.h>

/* path NULL or empty: console only. Returns FALSE when the file could not
 * be opened (console output still works). */
BOOL Log_Open(CONST_STRPTR path);
void Log_Close(void);

void Log_Printf(CONST_STRPTR fmt, ...);

/* A line of dashes with a title, for the sections of a report. */
void Log_Section(CONST_STRPTR title);

/* TRUE: hold the console's copy from now on (nests); FALSE: one level
 * less, and at the outermost level write what was held. */
void Log_Hold(BOOL hold);

#endif /* PROBE_PLOG_H */
