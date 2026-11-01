#include <exec/types.h>
#include <exec/ports.h>
#include <exec/io.h>
#include <exec/memory.h>
#include <devices/clipboard.h>

struct cbbuf {
  ULONG size;     /* size of memory allocation            */
  ULONG count;    /* number of characters after stripping */
  UBYTE *mem;     /* pointer to memory containing data    */
};

#define MAKE_ID(a,b,c,d) ((a<<24L) | (b<<16L) | (c<<8L) | d)
#define ID_FORM MAKE_ID('F','O','R','M')
#define ID_FTXT MAKE_ID('F','T','X','T')
#define ID_CHRS MAKE_ID('C','H','R','S')

#ifndef CBIO
/* prototypes */
extern struct IOClipReq *CBOpen         ( ULONG );
extern void             CBClose         (struct IOClipReq *);
extern int              CBQueryFTXT     (struct IOClipReq *);
extern struct cbbuf     *CBReadCHRS     (struct IOClipReq *);
extern void             CBReadDone      (struct IOClipReq *);
extern void             CBFreeBuf       (struct cbbuf *);
#endif
