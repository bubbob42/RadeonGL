/*
 * eclock.c -- see eclock.h.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/timer.h>

#include "eclock.h"

/* struct Device * under both compilers' proto/timer.h. */
struct Device *TimerBase = NULL;

ULONG Clock_Freq = 709379;

static struct MsgPort *clk_Port = NULL;
static struct timerequest *clk_Req = NULL;
static BOOL clk_Open = FALSE;

BOOL Clock_Open(void)
{
    struct EClockVal ev;

    if (clk_Open)
        return TRUE;
    clk_Port = CreateMsgPort();
    if (clk_Port == NULL)
        return FALSE;
    clk_Req = (struct timerequest *)CreateIORequest(clk_Port, sizeof(struct timerequest));
    if (clk_Req == NULL)
    {
        DeleteMsgPort(clk_Port);
        clk_Port = NULL;
        return FALSE;
    }
    if (OpenDevice(TIMERNAME, UNIT_MICROHZ, (struct IORequest *)clk_Req, 0) != 0)
    {
        DeleteIORequest((struct IORequest *)clk_Req);
        DeleteMsgPort(clk_Port);
        clk_Req = NULL;
        clk_Port = NULL;
        return FALSE;
    }
    TimerBase = clk_Req->tr_node.io_Device;
    clk_Open = TRUE;
    Clock_Freq = ReadEClock(&ev);
    if (Clock_Freq == 0)
        Clock_Freq = 709379;
    return TRUE;
}

void Clock_Close(void)
{
    if (!clk_Open)
        return;
    CloseDevice((struct IORequest *)clk_Req);
    DeleteIORequest((struct IORequest *)clk_Req);
    DeleteMsgPort(clk_Port);
    clk_Req = NULL;
    clk_Port = NULL;
    TimerBase = NULL;
    clk_Open = FALSE;
}

ULONG Clock_Now(void)
{
    struct EClockVal ev;

    ReadEClock(&ev);
    return ev.ev_lo;
}

ULONG Clock_Since(ULONG start)
{
    return Clock_Now() - start;
}

/* seconds first, then the remainder (< Clock_Freq, so remainder * 1000
 * stays far below 2^32) in two steps of a thousand. */
ULONG Clock_Us(ULONG ticks)
{
    ULONG s;
    ULONG r;
    ULONG ms;
    ULONG rest;

    s = ticks / Clock_Freq;
    r = ticks - s * Clock_Freq;
    ms = (r * 1000UL) / Clock_Freq;
    rest = (r * 1000UL) - ms * Clock_Freq;      /* < Clock_Freq */
    return s * 1000000UL + ms * 1000UL + (rest * 1000UL) / Clock_Freq;
}

/* KB/s = kb * 1000000 / us, in two steps so kb * 1000 (kb <= 4096) and the
 * remainder * 1000 stay inside 32 bits. */
ULONG Clock_KBps(ULONG bytes, ULONG us)
{
    ULONG kb;
    ULONG q;
    ULONG r;

    if (us == 0)
        us = 1;
    kb = bytes >> 10;
    q = (kb * 1000UL) / us;
    r = (kb * 1000UL) - q * us;
    return q * 1000UL + (r * 1000UL) / us;
}
