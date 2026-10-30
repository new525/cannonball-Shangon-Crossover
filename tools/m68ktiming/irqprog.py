#!/usr/bin/env python3
"""irqprog.py: the interrupt-latency test program (for mame_irq.lua and mtime irq).

usage: irqprog.py <out.bin> [stop]
Writes a 16 KB image of Hang-On's main CPU work RAM ($20C000-$20FFFF): a loop of assorted
instructions (different microcode endings, data-dependent paths, an SR write) run with interrupts
enabled, and a handler at $20C800 for the level 4 autovector (VBLANK, once a frame). The loop's
length is not a divisor of the frame, so the interrupt lands at a different point each frame.
With 'stop', the loop is a single STOP #$2000 (the wait-for-interrupt case).
"""
import struct, sys

BASE, HND, SUB = 0x20c000, 0x20c800, 0x20c900
def L(v): return [(v >> 16) & 0xffff, v & 0xffff]

def program(stop):
    init = [0x2e7c] + L(0x20ff00) + [0x207c] + L(0x20f000) + [0x227c] + L(0x20f010) + [0x247c] + L(0x20f300) + [0x2c7c] + L(0x20fe00) + [0x46fc, 0x2000]
    if stop:
        body = [0x7001, 0x4e72, 0x2000, 0x5280]          # moveq #1,d0; stop #$2000; addq.l #1,d0
    else:
        body = [
            0x7005,                     # moveq #5,d0
            0x4e71,                     # nop
            0x3200,                     # move.w d0,d1
            0x2210,                     # move.l (a0),d1
            0x3281,                     # move.w d1,(a1)
            0xd280,                     # add.l d0,d1
            0xd7c0,                     # adda.l d0,a3
            0x7403, 0xc6c2,             # moveq #3,d2; mulu d2,d3
            0x283c] + L(0x10000) + [0x88fc, 0x0007,   # move.l #$10000,d4; divu #7,d4
            0xeb8d,                     # lsl.l #5,d5
            0x7c03, 0xed6f,             # moveq #3,d6; lsl.w d6,d7
            0x0800, 0x0003, 0x0880, 0x0013,   # btst #3,d0; bclr #19,d0
            0x4a80, 0x6702, 0x4e71,     # tst.l d0; beq.s +2 (not taken); nop
            0x6602, 0x4e71,             # bne.s +2 (taken)
            0x7a02, 0x51cd, 0xfffe,     # moveq #2,d5; dbra d5,* (taken twice, then expires)
            0x4eb9] + L(SUB) + [        # jsr sub (rts)
            0x41f9] + L(0x20f000) + [0x43e8, 0x0010,  # lea $20f000,a0; lea 16(a0),a1
            0x4879] + L(0x20f100) + [0x588f,          # pea $20f100; addq.l #4,a7
            0x48d0, 0x000f, 0x4cd0, 0x000f,           # movem.l d0-d3,(a0); movem.l (a0),d0-d3
            0x57c6, 0x56c6,             # seq d6; sne d6
            0x4291,                     # clr.l (a1)
            0xc141, 0xc141,             # exg d0,d1 (twice)
            0x4846, 0x4886, 0x4446,     # swap d6; ext.w d6; neg.w d6
            0x3368, 0x0004, 0x0008,     # move.w 4(a0),8(a1)
            0x23f9] + L(0x20f000) + L(0x20f010) + [   # move.l $20f000,$20f010
            0x5346, 0xb240,             # subq.w #1,d6; cmp.w d0,d1
            0xe846, 0xe35e,             # asr.w #4,d6; rol.w #1,d6
            0x4686,                     # not.l d6
            0x6000, 0x0002,             # bra.w +2
            0x0c40, 0x0005,             # cmpi.w #5,d0
            0x46fc, 0x2000,             # move #$2000,sr
            0xc1c2,                     # muls d2,d0
            0x4840,                     # swap d0
            0x1218,                     # move.b (a0)+,d1
            0x41f9] + L(0x20f000) + [   # lea $20f000,a0 (undo the postincrement)
            0x2500,                     # move.l d0,-(a2)
            0xd451,                     # add.w (a1),d2
            0x0283] + L(0x00ff00ff) + [ # andi.l #$ff00ff,d3
            0x9280,                     # sub.l d0,d1
            0x2228, 0x0004,             # move.l 4(a0),d1
            0x4e56, 0xfff8, 0x4e5e,     # link a6,#-8; unlk a6
            0x6102, 0x6004,             # bsr.s +2 (to the rts below); bra.s +4
            0x4e75, 0x4e71,             # rts; nop
            0xd389,                     # addx.l -(a1),-(a1)  (a1 -= 8)
            0x5089,                     # addq.l #8,a1
            0xc300,                     # abcd d0,d1
            0x4ad1,                     # tas (a1)
            0x0308, 0x0002,             # movep.w 2(a0),d1
            0xe592,                     # roxl.l #2,d2
            0x7064, 0x81fc, 0x0007,     # moveq #100,d0; divs #7,d0
            0x7005, 0x4180,             # moveq #5,d0; chk d0,d0
            0x45f9] + L(0x20f300) + [   # lea $20f300,a2
        ]
    loop = BASE + 2 * len(init)
    words = init + body
    words += [0x6000, (loop - (BASE + 2 * len(words) + 2)) & 0xffff]
    img = bytearray(0x4000)
    struct.pack_into('>%dH' % len(words), img, 0, *words)
    # handler: a delay of (count & 63) DBRA loops, so the interrupt lands somewhere else each frame
    hnd = [0x48e7, 0x8000, 0x3039] + L(0x20f200) + [0x0240, 0x003f, 0x51c8, 0xfffe, 0x4cdf, 0x0001,
           0x5279] + L(0x20f200) + [0x4e73]
    struct.pack_into('>%dH' % len(hnd), img, HND - BASE, *hnd)
    struct.pack_into('>H', img, SUB - BASE, 0x4e75)       # rts
    return img

if __name__ == '__main__':
    open(sys.argv[1], 'wb').write(program(len(sys.argv) > 2 and sys.argv[2] == 'stop'))
