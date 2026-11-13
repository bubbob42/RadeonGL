/*
 * rglport.h -- RadeonGL's public port: is anyone drawing on the Radeon?
 *
 * For programs that share the card with games and want to stay off it
 * while one runs (Herald's request R3, 2026-11-12). While minigl.library
 * (RadeonGL) is loaded, a public message port named "RadeonGL" carries
 * the numbers below. It takes no messages: it only exists to be found.
 *
 * Read it under Forbid(), and copy what you need before Permit():
 *
 *     struct RglPort *p;
 *     ULONG games = 0;
 *
 *     Forbid();
 *     p = (struct RglPort *) FindPort(RGLPORT_NAME);
 *     if (p != NULL && p->rp_Version >= 1)
 *         games = p->rp_ScreenContexts;
 *     Permit();
 *
 * No port: RadeonGL is not loaded (or an older release without it), so
 * nobody draws with it. The port goes away when the library is expunged,
 * which is why nothing may be read from it after Permit().
 *
 * Newer versions only add fields at the end, and raise rp_Version.
 */

#ifndef RADEONGL_RGLPORT_H
#define RADEONGL_RGLPORT_H

#include <exec/types.h>
#include <exec/ports.h>
#include <devices/timer.h>

#define RGLPORT_NAME    "RadeonGL"
#define RGLPORT_VERSION 1

struct RglPort
{
    struct MsgPort rp_Port;         /* ln_Name RGLPORT_NAME, PA_IGNORE */
    UWORD rp_Version;               /* RGLPORT_VERSION of the library */
    UWORD rp_Reserved;
    ULONG rp_Size;                  /* sizeof (struct RglPort) of the library */
    const char *rp_IdString;        /* the library's $VER line */
    ULONG rp_Contexts;              /* contexts on the Radeon now (every kind) */
    ULONG rp_ScreenContexts;        /* of them, on a GL screen of their own: games */
    ULONG rp_Flushes;               /* flushes to the Radeon since the library loaded */
    struct EClockVal rp_LastFlush;  /* ReadEClock() at the last one; zero: none yet */
};

#endif /* RADEONGL_RGLPORT_H */
