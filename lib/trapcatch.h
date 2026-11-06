/*
 * trapcatch.h -- a debug catcher for CPU traps while RadeonGL draws (only
 * with RGL_DEFS=-DRGL_TRAPCATCH, 68040/68060 build; the release has none
 * of it). Included once, by rgl.c.
 *
 * The user's GLQuake died with guru BF00 0002 (an access fault) in play,
 * never in a timedemo (2026-11-06). RadeonGLTrap selects:
 *
 *   1  the context's task: its trap code records the trap, then the old
 *      trap code (the guru, as before)
 *   2  the access fault vector (VBR + 8), any task, user or supervisor
 *      mode: records, then the old vector (68060.library, MMULib)
 *   3  both; and a user-mode trap of the context's task does not end in the
 *      guru: the task is parked (Wait(0) for good), the machine goes on,
 *      and TrapFind reads the records at once
 *
 * A record: 'RGLX', count, task, trap number ('VEC' from the vector), SR,
 * PC, format and vector, fault address (frame + 8), FSLW (frame + 12),
 * where, where2, the four bitmaps' CPU addresses at the last lock,
 * pending, d0-d7, a0-a6, USP, eight longwords of the frame, 'DONE'. Each
 * set has copies: the top of chip and of fast RAM, and fixed places in
 * the middle of both (MEASURED 2026-11-06: after a crash and reboot
 * neither top copy survived -- the alert and the boot reuse those).
 */

#ifndef RADEONGL_TRAPCATCH_H
#define RADEONGL_TRAPCATCH_H

#if defined(RGL_TRAPCATCH) && defined(RGL_CPU_040)

#define TRAP_REC_BYTES 512
#define TRAP_COPIES 6

ULONG *rgl_trap_rec;                    /* the task trap's record */
ULONG *rgl_trap_copies[TRAP_COPIES + 1];  /* and its copies, NULL-terminated */
ULONG *rgl_alert_rec;                   /* exec Alert()'s record (modes 2, 3) */
ULONG *rgl_alert_copies[TRAP_COPIES + 1];
APTR rgl_alert_old;                     /* Alert() before (MMULib patches it too) */
ULONG *rgl_vec_rec;                     /* the vector's record */
ULONG *rgl_vec_copies[TRAP_COPIES + 1];
APTR rgl_trap_old;                      /* the task's trap code before */
APTR rgl_vec_old;                       /* the access fault vector before */
ULONG rgl_trap_park;                    /* mode 3: park the task */
/* VBR + 8. The VBR may well be 0 (the A4000D: a NULL vbr pointer once
 * passed for "not installed" while vector 8 was already ours) -- so an
 * address, and a flag. */
static volatile ULONG *trap_vec;
static BOOL trap_vec_on;
static struct Task *trap_task;
static ULONG *trap_blocks[3 * (TRAP_COPIES + 1)];
static BOOL trap_alert_on;
static ULONG trap_nblocks;

void RglTrapEntry(void);
void RglVecEntry(void);
void RglPark(void);
void RglAlertEntry(void);
ULONG RglGetVbr(void);

__asm__(
"        .text\n"
"        .even\n"
"        .globl  _RglGetVbr\n"
"_RglGetVbr:\n"
"        .word   0x4e7a,0x0801\n"               /* MOVEC VBR,D0 */
"        rte\n"
"\n"
/* the common part. In: a0 = record, a1 = the exception frame (SR first),
 * d0 = trap number, d1 = the copies (NULL-terminated); d0-d7/a0-a6 of the
 * code that trapped already in the record. Out: d0-d7/a1-a6 as they were,
 * a0 = record. */
"RglRecTail:\n"
"        move.l  d0,12(a0)\n"
"        move.l  usp,a2\n"
"        move.l  a2,124(a0)\n"
"        move.l  4.w,a2\n"
"        move.l  276(a2),8(a0)\n"               /* ThisTask */
"        moveq   #0,d2\n"
"        move.w  (a1),d2\n"
"        move.l  d2,16(a0)\n"
"        move.l  2(a1),20(a0)\n"
"        moveq   #0,d2\n"
"        move.w  6(a1),d2\n"
"        move.l  d2,24(a0)\n"
"        move.l  8(a1),28(a0)\n"
"        move.l  12(a1),32(a0)\n"
"        move.l  _rgl_where,36(a0)\n"
"        move.l  _rgl_where2,40(a0)\n"
"        lea     _rgl_bm,a2\n"
"        move.l  (a2)+,44(a0)\n"
"        move.l  (a2)+,48(a0)\n"
"        move.l  (a2)+,52(a0)\n"
"        move.l  (a2)+,56(a0)\n"
"        move.l  _rgl_pending,60(a0)\n"
"        move.l  (a1),128(a0)\n"
"        move.l  4(a1),132(a0)\n"
"        move.l  8(a1),136(a0)\n"
"        move.l  12(a1),140(a0)\n"
"        move.l  16(a1),144(a0)\n"
"        move.l  20(a1),148(a0)\n"
"        move.l  24(a1),152(a0)\n"
"        move.l  28(a1),156(a0)\n"
"        move.l  #0x52474C58,(a0)\n"
"        addq.l  #1,4(a0)\n"
"        move.l  #0x444F4E45,160(a0)\n"
"        move.l  d1,a2\n"
"1:      move.l  (a2)+,d2\n"
"        beq     3f\n"
"        move.l  d2,a3\n"
"        moveq   #0,d3\n"
"2:      move.l  (a0,d3.l),(a3,d3.l)\n"
"        addq.l  #4,d3\n"
"        cmp.w   #420,d3\n"
"        bne     2b\n"
"        bra     1b\n"
"3:      .word   0xf478\n"                      /* CPUSHA DC */
"        movem.l 64(a0),d0-d7\n"
"        movem.l 100(a0),a1-a6\n"
"        rts\n"
"\n"
/* the task's trap code: the trap number, then the frame, on the stack */
"        .globl  _RglTrapEntry\n"
"_RglTrapEntry:\n"
"        move.l  a0,-(sp)\n"
"        move.l  _rgl_trap_rec,a0\n"
"        cmp.w   #0,a0\n"
"        beq     9f\n"
"        movem.l d0-d7/a0-a6,64(a0)\n"
"        move.l  (sp),96(a0)\n"
"        lea     8(sp),a1\n"
"        move.l  4(sp),d0\n"
"        move.l  #_rgl_trap_copies,d1\n"
"        bsr     RglRecTail\n"
"        tst.l   _rgl_trap_park\n"
"        beq     9f\n"
"        btst    #5,8(sp)\n"                    /* trapped in supervisor mode? */
"        bne     9f\n"
"        move.l  #_RglPark,10(sp)\n"            /* asleep instead of the guru */
"        move.l  (sp)+,a0\n"
"        addq.l  #4,sp\n"
"        rte\n"
"9:      move.l  (sp)+,a0\n"
"        move.l  _rgl_trap_old,-(sp)\n"
"        rts\n"
"\n"
/* the access fault vector: the frame on the stack */
"        .globl  _RglVecEntry\n"
"_RglVecEntry:\n"
"        move.l  a0,-(sp)\n"
"        move.l  _rgl_vec_rec,a0\n"
"        cmp.w   #0,a0\n"
"        beq     8f\n"
"        movem.l d0-d7/a0-a6,64(a0)\n"
"        move.l  (sp),96(a0)\n"
"        lea     4(sp),a1\n"
"        move.l  #0x56454300,d0\n"
"        move.l  #_rgl_vec_copies,d1\n"
"        bsr     RglRecTail\n"
"8:      move.l  (sp)+,a0\n"
"        move.l  _rgl_vec_old,-(sp)\n"
"        rts\n"
"\n"
/* exec Alert(), d7 = the alert, a6 = SysBase, user or supervisor mode:
 * the alert as trap number, the caller as PC, $A1E7 as format, the
 * registers, the breadcrumbs and 64 longwords of the stack (from +164: the
 * caller's frames, a bus error's exception frame among them), then
 * CacheClearU() and the Alert() before. */
"        .globl  _RglAlertEntry\n"
"_RglAlertEntry:\n"
"        move.l  a0,-(sp)\n"
"        move.l  _rgl_alert_rec,a0\n"
"        cmp.w   #0,a0\n"
"        beq     7f\n"
"        movem.l d0-d7/a0-a6,64(a0)\n"
"        move.l  (sp),96(a0)\n"
"        move.l  #0x52474C58,(a0)\n"
"        move.l  d7,12(a0)\n"
"        clr.l   16(a0)\n"
"        move.l  4(sp),20(a0)\n"
"        move.l  #0xA1E7,24(a0)\n"
"        move.l  4.w,a2\n"
"        move.l  276(a2),8(a0)\n"
"        move.l  _rgl_where,36(a0)\n"
"        move.l  _rgl_where2,40(a0)\n"
"        lea     _rgl_bm,a2\n"
"        move.l  (a2)+,44(a0)\n"
"        move.l  (a2)+,48(a0)\n"
"        move.l  (a2)+,52(a0)\n"
"        move.l  (a2)+,56(a0)\n"
"        move.l  _rgl_pending,60(a0)\n"
"        lea     164(a0),a2\n"
"        lea     4(sp),a3\n"
"        moveq   #63,d2\n"
"5:      move.l  (a3)+,(a2)+\n"
"        dbra    d2,5b\n"
"        addq.l  #1,4(a0)\n"
"        move.l  #0x444F4E45,160(a0)\n"
"        lea     _rgl_alert_copies,a2\n"
"1:      move.l  (a2)+,d2\n"
"        beq     3f\n"
"        move.l  d2,a3\n"
"        moveq   #0,d3\n"
"2:      move.l  (a0,d3.l),(a3,d3.l)\n"
"        addq.l  #4,d3\n"
"        cmp.w   #420,d3\n"
"        bne     2b\n"
"        bra     1b\n"
"3:      move.l  a0,-(sp)\n"                  /* CacheClearU may use d0-d1/a0-a1 */
"        move.l  4.w,a6\n"
"        jsr     -636(a6)\n"                   /* CacheClearU */
"        move.l  (sp)+,a0\n"
"        movem.l 64(a0),d0-d7\n"
"        movem.l 100(a0),a1-a6\n"
"7:      move.l  (sp)+,a0\n"
"        move.l  _rgl_alert_old,-(sp)\n"
"        rts\n"
"\n"
/* a parked task, in user mode: asleep for good */
"        .globl  _RglPark\n"
"_RglPark:\n"
"        move.l  4.w,a6\n"
"        moveq   #0,d0\n"
"        jsr     -318(a6)\n"
"        bra     _RglPark\n"
);

static ULONG *TrapBlock(ULONG *at, ULONG flags)
{
    ULONG *p;

    if (trap_nblocks >= sizeof(trap_blocks) / sizeof(trap_blocks[0]))
        return NULL;
    p = (at != NULL) ? (ULONG *) AllocAbs(TRAP_REC_BYTES, at)
                     : (ULONG *) AllocMem(TRAP_REC_BYTES, flags | MEMF_CLEAR);
    if (p == NULL)
        return NULL;
    if (at != NULL)
    {
        ULONG i;
        for (i = 0; i < TRAP_REC_BYTES / 4; i++)
            p[i] = 0;
    }
    trap_blocks[trap_nblocks++] = p;
    return p;
}

/* A record and its copies: the top of chip RAM (the record), the top of
 * fast RAM, chip RAM at 640 KB, 1152 KB and 1536 KB, the middle and three
 * quarters of the biggest fast RAM region. */
static ULONG *TrapSet(ULONG **copies, ULONG off)
{
    struct MemHeader *mh;
    struct MemHeader *big;
    ULONG *r;
    ULONG n;
    ULONG lo;
    ULONG hi;

    r = TrapBlock(NULL, MEMF_CHIP | MEMF_REVERSE);
    if (r == NULL)
        return NULL;
    r[0] = 0x52474C58UL;
    n = 0;
    copies[n] = TrapBlock(NULL, MEMF_FAST | MEMF_REVERSE);
    if (copies[n] != NULL) n++;
    copies[n] = TrapBlock((ULONG *) (0x000A0000UL + off), 0);
    if (copies[n] != NULL) n++;
    copies[n] = TrapBlock((ULONG *) (0x00120000UL + off), 0);
    if (copies[n] != NULL) n++;
    copies[n] = TrapBlock((ULONG *) (0x00180000UL + off), 0);
    if (copies[n] != NULL) n++;
    big = NULL;
    Forbid();
    for (mh = (struct MemHeader *) SysBase->MemList.lh_Head; mh->mh_Node.ln_Succ != NULL;
         mh = (struct MemHeader *) mh->mh_Node.ln_Succ)
        if ((mh->mh_Attributes & MEMF_FAST)
            && (big == NULL || (ULONG) mh->mh_Upper - (ULONG) mh->mh_Lower
                               > (ULONG) big->mh_Upper - (ULONG) big->mh_Lower))
            big = mh;
    lo = (big != NULL) ? (ULONG) big->mh_Lower : 0;
    hi = (big != NULL) ? (ULONG) big->mh_Upper : 0;
    Permit();
    if (big != NULL)
    {
        copies[n] = TrapBlock((ULONG *) ((((lo + hi) / 2) & ~4095UL) + off), 0);
        if (copies[n] != NULL) n++;
        copies[n] = TrapBlock((ULONG *) (((lo + (hi - lo) / 4 * 3) & ~4095UL) + off), 0);
        if (copies[n] != NULL) n++;
    }
    copies[n] = NULL;
    return r;
}

/* RadeonGLTrap=1, 2 or 3: the catcher for the calling task / the system. */
static void Trap_Install(void)
{
    char flag[4];

    if (trap_task != NULL)
        return;
    if (!(GetVar((STRPTR) "RadeonGLTrap", (STRPTR) flag, sizeof(flag), 0) > 0
          && flag[0] >= '1' && flag[0] <= '3'))
        return;
    trap_task = FindTask(NULL);
    trap_nblocks = 0;
    if (flag[0] != '2')
    {
        rgl_trap_park = (flag[0] == '3') ? 1UL : 0UL;
        rgl_trap_rec = TrapSet(rgl_trap_copies, 0);
        if (rgl_trap_rec == NULL)
            return;
    }
    if (flag[0] != '1')
    {
        rgl_alert_rec = TrapSet(rgl_alert_copies, 0x2000);
        rgl_vec_rec = TrapSet(rgl_vec_copies, 0x1000);
    }
    CacheClearU();
    if (rgl_vec_rec != NULL)
    {
        trap_vec = (volatile ULONG *) (Supervisor((ULONG (*)()) RglGetVbr) + 8);
        Disable();
        rgl_vec_old = (APTR) *trap_vec;
        *trap_vec = (ULONG) RglVecEntry;
        trap_vec_on = TRUE;
        Enable();
        CacheClearU();
    }
    if (rgl_alert_rec != NULL)
    {
        Forbid();
        rgl_alert_old = SetFunction((struct Library *) SysBase, -108, (APTR) RglAlertEntry);
        trap_alert_on = TRUE;
        Permit();
    }
    if (rgl_trap_rec != NULL)
    {
        Forbid();
        rgl_trap_old = trap_task->tc_TrapCode;
        trap_task->tc_TrapCode = (APTR) RglTrapEntry;
        Permit();
    }
}

static void Trap_Remove(void)
{
    ULONG i;

    if (trap_task == NULL)
        return;
    if (trap_alert_on)
    {
        Forbid();
        /* only if nobody patched it after us */
        if (*(APTR *) ((UBYTE *) SysBase - 108 + 2) == (APTR) RglAlertEntry)
            SetFunction((struct Library *) SysBase, -108, rgl_alert_old);
        rgl_alert_rec = NULL;
        trap_alert_on = FALSE;
        Permit();
    }
    if (trap_vec_on)
    {
        Disable();
        if (*trap_vec == (ULONG) RglVecEntry)
            *trap_vec = (ULONG) rgl_vec_old;
        rgl_vec_rec = NULL;
        trap_vec_on = FALSE;
        Enable();
    }
    if (rgl_trap_rec != NULL)
    {
        Forbid();
        trap_task->tc_TrapCode = rgl_trap_old;
        rgl_trap_rec = NULL;
        Permit();
    }
    /* the magic goes first: a freed record must not be found after a reboot */
    for (i = 0; i < trap_nblocks; i++)
    {
        trap_blocks[i][0] = 0;
        FreeMem(trap_blocks[i], TRAP_REC_BYTES);
    }
    trap_nblocks = 0;
    rgl_trap_copies[0] = NULL;
    rgl_vec_copies[0] = NULL;
    rgl_alert_copies[0] = NULL;
    CacheClearU();
    trap_task = NULL;
}

#else

#define Trap_Install() ((void) 0)
#define Trap_Remove() ((void) 0)

#endif

#endif /* RADEONGL_TRAPCATCH_H */
