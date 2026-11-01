/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// comndef.h  -- general definitions

#if !defined BYTE_DEFINED
typedef unsigned char	  byte;
#define BYTE_DEFINED 1
#endif

#undef true
#undef false

typedef enum {false, true}  qboolean;

//============================================================================

typedef struct sizebuf_s
{
	qboolean allowoverflow; // if false, do a Sys_Error
	qboolean overflowed;	// set to true if the buffer size failed
	byte	*data;
	int	maxsize;
	int	cursize;

} sizebuf_t;

void SZ_Alloc (sizebuf_t *buf, int startsize);
void SZ_Free (sizebuf_t *buf);
void SZ_Clear (sizebuf_t *buf);
void *SZ_GetSpace (sizebuf_t *buf, int length);
void SZ_Write (sizebuf_t *buf, void *data, int length);
void SZ_Print (sizebuf_t *buf, char *data); // strcats onto the sizebuf

//============================================================================

typedef struct link_s
{
	struct link_s *prev, *next;

} link_t;


void ClearLink (link_t *l);
void RemoveLink (link_t *l);
void InsertLinkBefore (link_t *l, link_t *before);
void InsertLinkAfter (link_t *l, link_t *after);

// (type *)STRUCT_FROM_LINK(link_t *link, type, member)
// ent = STRUCT_FROM_LINK(link,entity_t,order)
// FIXME: remove this mess!
#define STRUCT_FROM_LINK(l,t,m) ((t *)((byte *)l - (int)&(((t *)0)->m)))

//============================================================================

#ifndef NULL
#define NULL ((void *)0)
#endif

#define Q_MAXCHAR ((char)0x7f)
#define Q_MAXSHORT ((short)0x7fff)
#define Q_MAXINT  ((int)0x7fffffff)
#define Q_MAXLONG ((int)0x7fffffff)
#define Q_MAXFLOAT ((int)0x7fffffff)

#define Q_MINCHAR ((char)0x80)
#define Q_MINSHORT ((short)0x8000)
#define Q_MININT  ((int)0x80000000)
#define Q_MINLONG ((int)0x80000000)
#define Q_MINFLOAT ((int)0x7fffffff)

//============================================================================

extern	qboolean    bigendien;

extern	short (*BigShort) (short l);
extern	short (*LittleShort) (short l);
extern	int (*BigLong) (int l);
extern	int (*LittleLong) (int l);
extern	float (*BigFloat) (float l);
#ifdef __PPC__
extern float FloatSwapPPC(float *);
#define LittleFloat(f) FloatSwapPPC(&(f))
#else
extern	float (*LittleFloat) (float l);
#endif

//============================================================================

void MSG_WriteChar (sizebuf_t *sb, int c);
void MSG_WriteByte (sizebuf_t *sb, int c);
void MSG_WriteShort (sizebuf_t *sb, int c);
void MSG_WriteLong (sizebuf_t *sb, int c);
void MSG_WriteFloat (sizebuf_t *sb, float f);
void MSG_WriteString (sizebuf_t *sb, char *s);
void MSG_WriteCoord (sizebuf_t *sb, float f);
void MSG_WriteAngle (sizebuf_t *sb, float f);
void MSG_WritePreciseAngle (sizebuf_t *sb, float f); // JPG - precise aim!!

extern int	msg_readcount;
extern qboolean msg_badread;	// set if a read goes beyond end of message

void MSG_BeginReading (void);
int MSG_ReadChar (void);
int MSG_ReadByte (void);
int MSG_PeekByte (void);	// JPG - need this to check for ProQuake messages
int MSG_ReadShort (void);
int MSG_ReadLong (void);
float MSG_ReadFloat (void);
char *MSG_ReadString (void);

float MSG_ReadCoord (void);
float MSG_ReadAngle (void);
float MSG_ReadPreciseAngle (void); // JPG - precise aim!!

//============================================================================

#if defined(AMIGAOS)

/* Use exec.library CopyMem() */

#include <proto/exec.h>

#if !defined(__PPC__)
#define Q_memcpy(dst,src,cnt) CopyMem(src,dst,cnt)
#else
#define Q_memcpy(dst,src,cnt) memcpy(dst,src,cnt)
#endif

#define Q_memcmp(m1,m2,cnt) memcmp(m1,m2,cnt)
#define Q_memset(dst,fill,cnt) memset(dst,fill,cnt)

/* Both compilers redirect to the real, standard-library string functions
 * here -- there's no reason for a hand-rolled reimplementation when
 * clib2/vbcc's own strcpy/strncpy/etc are right there and already
 * correct. This used to branch on __GNUC__ (GCC got real prototypes for
 * common.c's own custom Q_str* bodies instead), which is what let a real,
 * hardware-confirmed off-by-one bug in Q_strncpy's own implementation
 * (Hunk_AllocName's "Q_strncpy(h->name, name, 8)" corrupting the first
 * byte of its own returned allocation for any 9+ char name) actually run
 * for the first time ever under GCC -- those bodies had never been
 * exercised before since nothing called them under vbcc either. See
 * common.c's own comment on those bodies (now dead code for this
 * project, guarded by !defined(AMIGAOS) instead). */
#include "string.h"

#define Q_strcpy(dst,src) strcpy(dst,src)
#define Q_strncpy(dst,src,cnt) strncpy(dst,src,cnt)
#define Q_strlen(str) strlen(str)
#define Q_strrchr(str,chr) strrchr(str,chr)
#define Q_strcat(dst,src) strcat(dst,src)
#define Q_strcmp(s1,s2) strcmp(s1,s2)
#define Q_strncmp(s1,s2,cnt) strncmp(s1,s2,cnt)

/* vbcc 68k has no inline assembler for these :(
#define Q_atoi(str) atoi(str)
#define Q_atof(str) atof(str)

   Comment out the function def below and function in common.c if using the
   built in function above */
// void Q_memcpy (void *dest, void *src, int count);
// int Q_memcmp (void *m1, void *m2, int count);
// void Q_memset (void *dest, int fill, int count);
// void Q_strcpy (char *dest, char *src);
// void Q_strncpy (char *dest, char *src, int count);
// int Q_strlen (char *str);
// char *Q_strrchr (char *s, char c);
// void Q_strcat (char *dest, char *src);
// int Q_strcmp (char *s1, char *s2);
// int Q_strncmp (char *s1, char *s2, int count);
int Q_atoi (char *str);
float Q_atof (char *str);

#else // no AMIGAOS

void Q_memcpy (void *dest, void *src, int count);
int Q_memcmp (void *m1, void *m2, int count);
void Q_memset (void *dest, int fill, int count);
void Q_strcpy (char *dest, char *src);
void Q_strncpy (char *dest, char *src, int count);
int Q_strlen (char *str);
char *Q_strrchr (char *s, char c);
void Q_strcat (char *dest, char *src);
int Q_strcmp (char *s1, char *s2);
int Q_strncmp (char *s1, char *s2, int count);
int Q_atoi (char *str);
float Q_atof (char *str);

#endif

int Q_strcasecmp (char *s1, char *s2);
int Q_strncasecmp (char *s1, char *s2, int n);

//============================================================================

extern char	com_token[1024];
extern qboolean com_eof;

char *COM_Parse (char *data);


extern int	com_argc;
extern char	**com_argv;

int COM_CheckParm (char *parm);
void COM_Init (char *path);
void COM_InitArgv (int argc, char **argv);

char *COM_SkipPath (char *pathname);
void COM_StripExtension (char *in, char *out);
void COM_FileBase (char *in, char *out, size_t outside);
void COM_DefaultExtension (char *path, char *extension);

char  *va(char *format, ...);
// does a varargs printf into a temp buffer


//============================================================================

extern int com_filesize;
struct cache_user_s;

extern char com_gamedir[MAX_OSPATH];

void COM_WriteFile (char *filename, void *data, int len);
int COM_OpenFile (char *filename, int *hndl);
int COM_FOpenFile (char *filename, FILE **file);
void COM_CloseFile (int h);

byte *COM_LoadStackFile (char *path, void *buffer, int bufsize);
byte *COM_LoadTempFile (char *path);
byte *COM_LoadHunkFile (char *path);
void COM_LoadCacheFile (char *path, struct cache_user_s *cu);


extern struct cvar_s registered;

extern qboolean standard_quake, rogue, hipnotic;
