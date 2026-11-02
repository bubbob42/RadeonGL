/*
 * eclock.h -- microsecond timing from timer.device's E clock.
 *
 * ReadEClock() ticks at about 709 kHz (PAL) or 716 kHz (NTSC); the rate
 * comes back from the first call. All arithmetic is 32-bit integer: a
 * difference of two tick counts is right across one wrap of the low word,
 * and the conversion never multiplies a tick count by a million.
 */

#ifndef PROBE_ECLOCK_H
#define PROBE_ECLOCK_H

#include <exec/types.h>

/* Opens timer.device (UNIT_MICROHZ). FALSE when it cannot. */
BOOL Clock_Open(void);
void Clock_Close(void);

/* Current E-clock low word. */
ULONG Clock_Now(void);

/* Ticks between two Clock_Now() readings. */
ULONG Clock_Since(ULONG start);

/* Ticks -> microseconds (ticks up to about 4000 s). */
ULONG Clock_Us(ULONG ticks);

/* bytes moved in us microseconds -> kilobytes a second (bytes up to 4 MB). */
ULONG Clock_KBps(ULONG bytes, ULONG us);

/* E-clock ticks a second, valid after Clock_Open(). */
extern ULONG Clock_Freq;

#endif /* PROBE_ECLOCK_H */
