/*
 * plog.h -- the probes' output: every line to the console and, when a LOG
 * file is given, to that file as well. The file is what the host reads
 * after a run on the real machine; the console is what the user watches.
 *
 * Formats are RawDoFmt's: %ld %lu %lx %s %lc, with the l on every number.
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

#endif /* PROBE_PLOG_H */
