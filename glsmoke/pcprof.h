/*
 * pcprof.h -- a PC sampler for the test clients (GLBench, GLQuake).
 *
 * A child process at a higher priority wakes every interval_us
 * (timer.device) and, with the calling task switched out just then, copies
 * the longwords at its saved stack pointer: exec keeps the task's FPU
 * state, its registers and its PC there. tools/profile.py finds the PC's
 * slot afterwards. On the 060 the FPU state comes first, and its FPIAR (the
 * last FPU instruction's address) points into the code too: MEASURED
 * 2026-11-03, a profile that took it for the PC put every sample on an FPU
 * store. So enough longwords must be kept to reach the PC (64 on the
 * A4000D: the PC was longword 31).
 *
 * The file: 'PROF', anchor_lib, anchor_prog, the sample count, then per
 * sample the task's state and the longwords (the record size follows from
 * the file size). The anchors are two code addresses whose symbols
 * profile.py knows (a library hook, the program's main), from which it
 * finds where the library and the program were loaded.
 *
 * GCC only (the clients that use it link the MiniGL SDK's GCC archive).
 */

#ifndef PCPROF_H
#define PCPROF_H

#include <exec/types.h>

/* Start sampling the calling task. FALSE: no memory or no process (then
 * PcProf_Stop does nothing). */
BOOL PcProf_Start(ULONG interval_us, ULONG max_samples, ULONG longs);

/* Stop, write the samples to path with the two anchors, free everything.
 * Prints a line with the count. Safe to call when nothing was started. */
void PcProf_Stop(const char *path, ULONG anchor_lib, ULONG anchor_prog);

#endif /* PCPROF_H */
