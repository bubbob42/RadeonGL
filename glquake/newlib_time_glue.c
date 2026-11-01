/* newlib_time_glue.c -- syscall glue this newlib AmigaOS port doesn't
 * itself provide. lib_a-tzset_r.o (inside newlib's own libc.a)
 * references bare `timezone`/`daylight`/`_tzname` directly, but
 * sys-include/time.h's own indirection scheme for them
 * (__timezone/__daylight/__tzname) is half-disabled -- the connecting
 * macros for `timezone`/`daylight` are commented out, and nothing
 * anywhere defines the double-underscore storage either way -- so the
 * precompiled object and the shipped header don't agree, and neither
 * path is actually satisfied by anything in the archive (confirmed via
 * exhaustive nm search of libc.a/libg.a). lib_a-gettimeofdayr.o
 * likewise needs a real _gettimeofday(). clib2 never hits this: its own
 * libc.a implements ctime()/time() via AmigaOS DateStamp() directly
 * (time_convert_datestamp.o), bypassing the POSIX timezone globals
 * entirely. Only linked into the newlib build variant.
 *
 * struct timeval's tv_secs/tv_micro fields (sys/_timeval.h) are union-
 * aliased with the POSIX tv_sec/tv_usec names -- binary-identical to
 * what exec.library's GetSysTime() fills in, the same already-proven
 * call this project's own sys_amiga.c uses for Sys_FloatTime()/
 * Sys_Milliseconds(), so no manual field conversion is needed. */
#include <exec/types.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/timer.h>
#include <sys/time.h>

/* time.h's own #define _tzname (*__tzname) is active (unlike the
 * commented-out timezone/daylight macros) -- undef it so this
 * definition creates a real symbol literally named `_tzname`, matching
 * what lib_a-tzset_r.o actually references, instead of being rewritten
 * into a nonsensical `char *(*__tzname)[2]`. */
#undef _tzname

long timezone = 0;
int daylight = 0;
char *_tzname[2] = { "GMT", "GMT" };

int _gettimeofday(struct timeval *tv, void *tz)
{
	GetSysTime(tv);
	return 0;
}
