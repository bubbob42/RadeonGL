/*
 * r2pm4.h -- the R100/R200 command processor's packet headers ("PM4").
 *
 * Linux 6.6 r100d.h and radeon_reg.h (RADEON_CP_PACKET*_GET_*), Mesa 21.3
 * radeon_cmdbuf.h (CP_PACKET0_ONE); their notices: LICENSE, section C.2.
 *
 *   type 0  register write   bits 31-30 = 0, 29-16 = count, 15 = ONE_REG_WR,
 *                            12-0 = register >> 2; count + 1 values follow,
 *                            to consecutive registers, or all to the one
 *                            register when ONE_REG_WR is set
 *   type 2  filler           0x80000000, one dword, no payload
 *   type 3  command          bits 31-30 = 3, 29-16 = count, 15-8 = opcode;
 *                            count + 1 payload dwords follow
 *
 * Type 1 exists but nothing we follow uses it. A packet is at most
 * 1 + 0x4000 dwords.
 */

#ifndef R2PM4_H
#define R2PM4_H

#define R2_PKT_TYPE(h)          (((h) >> 30) & 3UL)
#define R2_PKT_COUNT(h)         (((h) >> 16) & 0x3fffUL)
#define R2_PKT0_REG(h)          (((h) & 0x1fffUL) << 2)
#define R2_PKT0_ONE_REG_WR(h)   (((h) >> 15) & 1UL)
#define R2_PKT3_OPCODE(h)       (((h) >> 8) & 0xffUL)

#define R2_PKT0(reg, n)         ((((ULONG) (n)) << 16) | (((ULONG) (reg)) >> 2))
#define R2_PKT0_ONE(reg, n)     (R2_PKT0(reg, n) | 0x00008000UL)
#define R2_PKT2                 0x80000000UL
#define R2_PKT3(op, n)          (0xc0000000UL | (((ULONG) (op)) << 8) | (((ULONG) (n)) << 16))

#define R2_PKT_MAX_COUNT        0x3fffUL     /* payload dwords - 1 */

/* Type-3 opcodes (Linux r100d.h PACKET3_*, Mesa R200_CP_CMD_*). */
#define R2_OP_NOP               0x10   /* payload skipped: the ring's wrap filler */
#define R2_OP_3D_LOAD_VBPNTR    0x2f
#define R2_OP_INDX_BUFFER       0x33
#define R2_OP_3D_DRAW_VBUF_2    0x34
#define R2_OP_3D_DRAW_IMMD_2    0x35
#define R2_OP_3D_DRAW_INDX_2    0x36

/* INDX_BUFFER's first payload dword: the value Mesa (r200FireEB) and
 * mirq's emitter both send, and the only one we know. */
#define R2_INDX_BUFFER_CNTL     0x80000810UL

#endif /* R2PM4_H */
