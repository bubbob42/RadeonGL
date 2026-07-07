#ifndef PROJECT_DEBUG_H
#define PROJECT_DEBUG_H

/*
 * Serial-debug tracing.
 *
 * In the debug build (compiled with -DLMDEBUG, which also links debug.lib --
 * "gmake debug") DBG() sends a formatted line to the serial port via KPrintF().
 * Under FS-UAE the serial port is exposed as tcp://127.0.0.1:1234, so the trace
 * is read on the host with `telnet 127.0.0.1 1234` (the same channel MuForce
 * uses; see fsuae/mutools). In a normal build DBG() expands to nothing, so the
 * release binaries carry no tracing and do not link debug.lib.
 *
 * Every line is prefixed with "<project>: " so the trace is easy to pick out of
 * mixed serial output from other programs. The project name is injected by the
 * Makefile as -DDBG_TAG=<name> (GCC) / DEFINE DBG_TAG=<name> (SAS/C) and
 * stringised below; it defaults to "app" if the build does not set it.
 *
 * The double-parenthesis call form keeps it C89-compatible (no variadic
 * macros):
 *
 *     DBG(("start: %ld rows\n", (LONG)rows));
 *     DBG(("name=%s\n", (LONG)name));
 *
 * KPrintF() formats with the exec RawDoFmt() codes, NOT the C ones: integers
 * are 16-bit unless you use the l modifier, so always pass 32-bit values and
 * write %ld / %lx; %s takes a STRPTR. Cast every argument to LONG (or STRPTR
 * for %s) so the widths on the stack match the format string.
 */

#ifdef LMDEBUG
#include <clib/debug_protos.h>

#ifndef DBG_TAG
#define DBG_TAG app
#endif
#define DBG_STR_(x) #x
#define DBG_STR(x) DBG_STR_(x)

#define DBG(a) do { \
        KPrintF((CONST_STRPTR)(DBG_STR(DBG_TAG) ": ")); \
        KPrintF a; \
    } while (0)
#else
#define DBG(a) ((void)0)
#endif

#endif /* PROJECT_DEBUG_H */
