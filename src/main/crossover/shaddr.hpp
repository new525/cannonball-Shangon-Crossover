/***************************************************************************
    Super Hang-On's RAM addresses that more than one file uses (the main CPU's view: work RAM at
    $060000, the second CPU's RAM through the 315-5195 mapper at $260000; 68000 byte order).

    The transcription of its vehicle tasks (crossover/shophys, C) defines the addresses of its
    interface in its headers (SHO_V_*, SHO_A5_* the task slots, SHO_PKT); those are taken from
    there. The transcription's .c files keep their own private defines, as they are checked against
    MAME on their own. Addresses a single file uses stay in that file (crossover/bike.cpp,
    shferrari.cpp).

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>

extern "C" {
#include "crossover/shophys/shophys.h"
#include "crossover/shophys/sho_crash.h"
}

namespace shaddr
{
    // The two RAMs, $8000 bytes each (SHBoard::workram, SHBoard::cpu1ram; the bike's own copies in sho_mem)
    const uint32_t WORK = 0x060000, SHARED = 0x260000, RAM_SIZE = 0x8000;

    // The game's variables (the second CPU's RAM)
    const uint32_t V_MODE         = 0x260000;           // .w game mode (1, 2 = racing ...)
    const uint32_t V_STAGE_DONE   = 0x260010;           // .w nonzero after the first stage
    const uint32_t V_COURSE       = 0x260012;           // .w course 0..3
    const uint32_t V_X            = SHO_V_X;            // $260022 .w lateral position (+ = left); road edge +/-300
    const uint32_t V_POS          = 0x260030;           // .l track position, 16.16 path positions
    const uint32_t V_THROTTLE     = 0x26006C;           // .w throttle 0..127
    const uint32_t V_SPEED        = SHO_V_SPEED;        // $260078 .l speed, 16.16 km/h
    const uint32_t V_CRASH_COUNT  = SHO_V_CRASH_COUNT;  // $260084 .w scenery hits; a change starts a crash
    const uint32_t V_KNOCK        = SHO_V_KNOCK;        // $260088 .w knockback timer (signed, counts to 0)
    const uint32_t V_CRASH        = SHO_V_CRASH;        // $260094 .w $FFFF while crashing
    const uint32_t V_OFFROAD      = 0x260098;           // .w $FFFF when off the road (|X| > 300)
    const uint32_t V_CURVE_NEXT   = 0x2600A4;           // .w the next curve's type and
    const uint32_t V_CURVE_NEXT_R = 0x2600A8;           // .w radius (as V_CURVE, V_CURVE_R)
    const uint32_t V_CURVE        = 0x2600AC;           // .w curve type (> 1 = a curve; 3 against 2 = its direction)
    const uint32_t V_CURVE_R      = 0x2600B0;           // .w curve radius
    const uint32_t V_ROADBUF      = 0x2607FE;           // .w the road table in use: bit 10 toggled every run of the speed task
    const uint32_t V_ROADTAB      = 0x260800;           // the road tables: the one in use at V_ROADTAB + the word at V_ROADBUF
    const uint32_t V_ROADY        = SHO_V_ROADY;        // $261200 .w[256] road height per depth (second CPU), << 4

    // Work RAM
    const uint32_t W_INGAME       = 0x060408;           // .w nonzero when a game is being played
    const uint32_t W_SC_ACTIVE    = 0x060412;           // .b $FF while supercharging
    const uint32_t W_TIMER        = 0x060500;           // .b race timer, BCD; 0 = time over

    // The bike's X is kept within +/-500 of its road's centre (ROM $85B6, sub_07F0E; the centrifugal task
    // $63CE gives no push that would take it past)
    const int X_LIMIT = 500;

    // The bike's place on the screen in the objects' coordinates (its slot's +$A, +$C; the rivals' screen x is
    // compared with it, $750A)
    const int BIKE_SX = 0x160, BIKE_SY = 0xDF;

    // The rivals' task (a slot's +2; testing: the harness's CB_CHASE, CB_RAILS)
    const uint32_t TASK_RIVAL = 0x74B6;
}
