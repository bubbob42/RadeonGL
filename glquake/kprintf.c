/*
** This is a replacement for the amiga.lib kprintf for PowerPC.
** It uses the WarpUp SPrintF function to do debug output
** to the serial connector (or Sushi, if it's installed).
*/

#include <proto/exec.h>
#ifdef __STORM__
#include <clib/powerpc_protos.h>
#else
#include <powerpc/powerpc_protos.h>
#endif
#include <stdarg.h>

int kprintf(char *format, ...)
{

	char msg[1024];
	va_list marker;

	va_start(marker, format);
	vsprintf(msg, format, marker);
	va_end(marker);

#ifndef __STORM__
	SPrintF(msg, 0);
#endif
	return 1;   /* fake something... */
}
