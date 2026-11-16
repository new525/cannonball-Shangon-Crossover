/***************************************************************************
    Hang-On's RAM addresses that more than one file uses (the main CPU's view: work RAM at $20C000,
    the second CPU's RAM at $C7C000; 68000 byte order). See docs/CROSSOVER-HANGON.md section 3.

    The variables listed here are at Super Hang-On's offsets (crossover/shaddr.hpp) from $C7E000, except
    X at +$50. Addresses a single file uses stay in that file (crossover/hovehicle.cpp).

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>

namespace hoaddr
{
    // The two RAMs, $4000 bytes each (HOBoard::workram, HOBoard::subram)
    const uint32_t WORK = 0x20C000, SUB = 0xC7C000, RAM_SIZE = 0x4000;

    // The game's variables (the second CPU's RAM, from V_BASE)
    const uint32_t V_BASE         = 0xC7E000;
    const uint32_t V_POS          = 0xC7E030;           // .l track position, 16.16 path positions
    const uint32_t V_X            = 0xC7E050;           // .w lateral position (+ = left); road edge +/-260
    const uint32_t V_SPEED        = 0xC7E078;           // .l speed, 16.16 km/h
    const uint32_t V_KNOCK        = 0xC7E088;           // .w knockback timer
    const uint32_t V_CRASH        = 0xC7E094;           // .w crashing
    const uint32_t V_OFFROAD      = 0xC7E098;           // .w nonzero when off the road (|X| > 260)
    const uint32_t V_CURVE        = 0xC7E0AC;           // .w curve type (> 1 = a curve)
    const uint32_t V_CURVE_R      = 0xC7E0B0;           // .w curve radius

    // Work RAM
    const uint32_t W_INGAME       = 0x20C408;           // .w nonzero when a game is being played
    const uint32_t W_MODE         = 0x20C420;           // .w the game flow's mode: 1 start line, 2 race, 3 goal
    const uint32_t W_TIMER        = 0x20C500;           // .b race timer; 0 = time over

    // Object slots ($20C800 + 64 x n): the centrifugal task's ($879A, slot 10) and the bike's ($AF10, slot 34)
    const uint32_t SLOT_CENTRIFUGAL = 0x20CA80, SLOT_BIKE = 0x20D080;

    // The bike's X is kept within +/-500 ($10F64, sub_0B556; the centrifugal task $879A gives no push
    // that would take it past)
    const int X_LIMIT = 500;

    // The bike's place on the screen in the objects' coordinates (its slot's +$A, +$C; the rivals' screen x is
    // compared with it, $A922)
    const int BIKE_SX = 0x160, BIKE_SY = 0xDF;

    // The rivals' task (a slot's +2; testing: the harness's CB_CHASE, CB_RAILS)
    const uint32_t TASK_RIVAL = 0xA8D0;
}
