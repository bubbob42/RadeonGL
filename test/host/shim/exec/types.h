#ifndef EXEC_TYPES_H
#define EXEC_TYPES_H

/*
 * Host stand-in for <exec/types.h>, for the core modules' unit tests ONLY.
 * The widths are the Amiga's: LONG and ULONG are 32 bits here too (on the
 * host `long` is 64 bits, which would hide every 32-bit overflow).
 */

typedef int            LONG;
typedef unsigned int   ULONG;
typedef short          WORD;
typedef unsigned short UWORD;
typedef signed char    BYTE;
typedef unsigned char  UBYTE;
typedef short          BOOL;
typedef void          *APTR;
typedef unsigned char *STRPTR;
typedef const unsigned char *CONST_STRPTR;

#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef NULL
#define NULL ((void *) 0)
#endif

typedef char exec_types_long_is_32[(sizeof(LONG) == 4) ? 1 : -1];

#endif
