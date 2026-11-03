/*
 * pcprof.c -- see pcprof.h.
 */

#include <exec/types.h>
#include <exec/io.h>
#include <exec/tasks.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "pcprof.h"

struct Prof
{
    struct Message msg;
    struct Task *target;
    volatile ULONG stop;
    ULONG interval;
    ULONG max;
    ULONG longs;
    ULONG n;
    ULONG waiting;              /* samples that found the target waiting */
    ULONG *data;                /* max x (1 + longs): state, then the longwords */
};

static struct Prof *prof;
static struct MsgPort *prof_reply;

static void Sampler(void)
{
    struct Process *me;
    struct Prof *p;
    struct MsgPort *port;
    struct timerequest *tr;
    ULONG *sp;
    ULONG *d;
    ULONG k;

    me = (struct Process *) FindTask(NULL);
    WaitPort(&me->pr_MsgPort);
    p = (struct Prof *) GetMsg(&me->pr_MsgPort);
    port = CreateMsgPort();
    tr = port ? (struct timerequest *) CreateIORequest(port, sizeof(*tr)) : NULL;
    if (tr != NULL && OpenDevice((STRPTR) TIMERNAME, UNIT_MICROHZ, (struct IORequest *) tr, 0) == 0)
    {
        while (!p->stop && p->n < p->max)
        {
            tr->tr_node.io_Command = TR_ADDREQUEST;
            tr->tr_time.tv_secs = p->interval / 1000000UL;
            tr->tr_time.tv_micro = p->interval % 1000000UL;
            DoIO((struct IORequest *) tr);
            Disable();
            d = p->data + p->n * (1 + p->longs);
            d[0] = p->target->tc_State;
            if (p->target->tc_State == TS_WAIT)
                p->waiting++;
            sp = (ULONG *) p->target->tc_SPReg;
            for (k = 0; k < p->longs; k++)
                d[1 + k] = sp[k];
            p->n++;
            Enable();
        }
        CloseDevice((struct IORequest *) tr);
    }
    if (tr != NULL)
        DeleteIORequest((struct IORequest *) tr);
    if (port != NULL)
        DeleteMsgPort(port);
    Forbid();
    ReplyMsg(&p->msg);
}

static void Free(void)
{
    if (prof != NULL)
    {
        if (prof->data != NULL)
            FreeVec(prof->data);
        FreeVec(prof);
        prof = NULL;
    }
    if (prof_reply != NULL)
    {
        DeleteMsgPort(prof_reply);
        prof_reply = NULL;
    }
}

BOOL PcProf_Start(ULONG interval_us, ULONG max_samples, ULONG longs)
{
    struct TagItem t[5];
    struct Process *child;

    prof = (struct Prof *) AllocVec(sizeof(*prof), MEMF_ANY | MEMF_CLEAR);
    prof_reply = CreateMsgPort();
    if (prof == NULL || prof_reply == NULL)
    {
        Free();
        return FALSE;
    }
    prof->data = (ULONG *) AllocVec(max_samples * (1 + longs) * 4, MEMF_ANY);
    if (prof->data == NULL)
    {
        Free();
        return FALSE;
    }
    prof->interval = interval_us;
    prof->max = max_samples;
    prof->longs = longs;
    prof->target = FindTask(NULL);
    prof->msg.mn_ReplyPort = prof_reply;
    prof->msg.mn_Length = sizeof(struct Message);
    t[0].ti_Tag = NP_Entry;     t[0].ti_Data = (ULONG) Sampler;
    t[1].ti_Tag = NP_Name;      t[1].ti_Data = (ULONG) "PC sampler";
    t[2].ti_Tag = NP_Priority;  t[2].ti_Data = (ULONG) (LONG) (prof->target->tc_Node.ln_Pri + 20);
    t[3].ti_Tag = NP_StackSize; t[3].ti_Data = 8192;
    t[4].ti_Tag = TAG_DONE;     t[4].ti_Data = 0;
    child = CreateNewProc(t);
    if (child == NULL)
    {
        Free();
        return FALSE;
    }
    PutMsg(&child->pr_MsgPort, &prof->msg);
    return TRUE;
}

void PcProf_Stop(const char *path, ULONG anchor_lib, ULONG anchor_prog)
{
    BPTR f;
    ULONG hdr[4];

    if (prof == NULL)
        return;
    prof->stop = 1;
    WaitPort(prof_reply);
    (void) GetMsg(prof_reply);
    f = Open((STRPTR) path, MODE_NEWFILE);
    if (f != 0)
    {
        hdr[0] = 0x50524f46UL;                      /* 'PROF' */
        hdr[1] = anchor_lib;
        hdr[2] = anchor_prog;
        hdr[3] = prof->n;
        Write(f, hdr, sizeof(hdr));
        Write(f, prof->data, (LONG) (prof->n * (1 + prof->longs) * 4));
        Close(f);
    }
    Printf("profile: %ld samples (%ld with the task waiting) to %s\n", (LONG) prof->n,
           (LONG) prof->waiting, (LONG) path);
    Free();
}
