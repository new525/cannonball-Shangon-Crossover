/***************************************************************************
    Super Hang-On's cabinet vibration motor (bit 3 of the main CPU's output port $140001, MAME's
    segaorun.cpp "Vibration motor"), as the game drives it: the routine at $1CBE, called by the
    vertical blank interrupt every frame (60 Hz) and written to the port at $12FE with the rest of
    the output byte ($60410).

      $1CBE  lea     $60410, A0
             tst.w   $6040A       ; set when a game starts ($115A), cleared at its end ($53BA)
             beq     off
             tst.w   $260000      ; the game mode (1 the start line, 2 racing; 0 after time over)
             beq     off
             move.w  $2600EC, D0  ; set after the goal ($612E: $2600EA set and the course
             or.b    $60050, D0   ;  position past $23); $60050 the interrupt's own skip flag
             bne     off
             lea     $1D1A, A1    ; the pattern table
             move.w  $260078, D0  ; the speed's integer km/h
             andi.w  #$1F0, D0
             lsr.w   #3, D0       ; a word per 16 km/h
             move.w  $60414, D1   ; the frame counter, counted only while the motor may run
             addq.w  #1, D1
             move.w  D1, $60414
             andi.w  #$F, D1
             move.w  (A1,D0.w), D0
             btst    D1, D0       ; one bit of the band's 16-frame pattern
             beq     off
             bset    #3, (A0)
             rts
      off:   bclr    #3, (A0)
             rts

    The table is read through the FD1089B's data decryption (rom_data). Only its first 18 words are a
    table (0-287 km/h); a speed of 288 or more reads the code that follows it, which the game does too,
    so those 14 words are here as the data view of those bytes.

    Checked against the emulated board: a Super Hang-On race on its course (4,606 frames, the start
    line, racing at 0-227 km/h and the coast after time over): this function on the RAM as the frame
    began gives the board's bit in every frame.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>

namespace shvibration
{
    static const uint16_t PATTERN[32] =
    {
        // 0-191 km/h                                                           192-207 ... 272-287
        0x92a4, 0x92a4, 0x92a4, 0x92a4, 0x92a4, 0x92a4, 0x92a4, 0x92a4, 0x92a4, 0x92a4, 0x92a4, 0x92a4,
        0xa4aa, 0xaa55, 0xaaaa, 0xd555, 0x6db6, 0xeeee,
        // 288 km/h and up: the code after the table ($1D3E-$1D59), read as data
        0xb471, 0x6006, 0xd806, 0x5308, 0xcc0e, 0x15b9, 0x8848, 0x813a, 0x5fb9, 0x6012, 0x6040, 0x1db1,
        0x6040, 0xb986,
    };

    // One frame of the routine while the motor may run: the counter advances and the band's pattern
    // gives the bit. speed: the integer km/h ($260078's high word)
    inline int frame(uint16_t speed, uint16_t& counter)
    {
        counter++;
        return (PATTERN[(speed & 0x1F0) >> 4] >> (counter & 15)) & 1;
    }
}
