**
**  MMU hack for Amiga 68k
**  Written by Frank Wille <frank@phoenix.owl.de>
**


        XDEF    _MMUHackOn
        XDEF    _MMUHackOff

        mc68040


_MMUHackOn:
        movem.l d0-a6,-(sp)
        move.l  $4.w,a6
        jsr     -150(a6)                ;SuperState
        move.l  d0,-(sp)
        jsr     -120(a6)                ;Disable
        cpusha  bc
        movec   DTT0,d0
        move.l  d0,OldDTT0
        movec   DTT1,d0
        move.l  d0,OldDTT1
        move.l  #$c040,d0
        movec   d0,DTT0
        move.l  #$0fc020,d0
        movec   d0,DTT1
        jsr     -126(a6)                ;Enable
        move.l  (sp)+,d0
        jsr     -156(a6)                ;UserState
        movem.l (sp)+,d0-a6
        rts

_MMUHackOff:
        movem.l d0-a6,-(sp)
        move.l  $4.w,a6
        jsr     -150(a6)                ;SuperState
        move.l  d0,-(sp)
        jsr     -120(a6)                ;Disable
        cpusha  bc
        move.l  OldDTT0,d0
        movec   d0,DTT0
        move.l  OldDTT1,d0
        movec   d0,DTT1
        jsr     -126(a6)                ;Enable
        move.l  (sp)+,d0
        jsr     -156(a6)                ;UserState
        movem.l (sp)+,d0-a6
        rts

OldDTT0:
        dc.l    0
OldDTT1:
        dc.l    0
