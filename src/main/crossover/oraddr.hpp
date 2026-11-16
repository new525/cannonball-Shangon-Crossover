/***************************************************************************
    OutRunners' work RAM addresses that more than one file reads (player 1; the V70's view, little-
    endian, through ORBoard::read8 / write8). From docs/OUTRUNNERS-RE.md section 2 (the player car's
    state), 2.8 (the car records) and docs/CROSSOVER-OUTRUNNERS.md 7.4.

    Addresses a single file uses stay in that file (crossover/orvehicle.cpp, ormode.cpp, orradio.cpp).
    The units of the speed and the lateral position are in crossover/units.hpp.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>

namespace oraddr
{
    const uint32_t A_STATE     = 0x208080;      // game state: 2 credit, 6 game
    const uint32_t A_STAGE     = 0x2080A2;      // stage number from the course script's header

    // The car object (R20 in player 1's task) and its fields
    const uint32_t OBJ_CAR     = 0x200400;      // flags: bit 0 active, bit 2 collision this tick, bit 3 crashing
    const uint32_t O_STATE     = 0x0C;          // car state: 1 start line, 2 driving, 3 time up, 4 goal run, 5 a contact's bounce
    const uint32_t O_LATVEL    = 0x2E;          // s16: lateral velocity per tick
    const uint32_t O_ARG       = 0x68;          // what was hit: the other car's number (kind 2), the object's address (kind 1)
    const uint32_t O_KIND      = 0x6E;          // collision kind: 0 scenery, 1 an object with its own position, 2 a car, 3 minor
    const uint32_t O_PUSH      = 0xCE;          // s16: the contact's lateral push (+ = right), decayed each tick of state 5
    const uint32_t O_HILL      = 0xD9;          // the car's hill frame index ($7B3F): 0 up, 1 level, 2.. down

    // The car's variables (R23 = $209000)
    const uint32_t A_SEGPOS    = 0x209034;      // u32: position within the segment, 0 .. the segment's length ($8000 on a straight)
    const uint32_t A_LAT       = 0x209038;      // s16: lateral position from the centre line, + = right
    const uint32_t A_SEGCNT    = 0x209042;      // u16: segment counter (the position along the course)
    const uint32_t A_SLIDING   = 0x209067;      // u8: $FF sliding
    const uint32_t A_SPEED     = 0x209074;      // u16: speed, the advance per 30 Hz tick; km/h x 136

    const uint32_t A_COUNTDOWN = 0x20FB6A;      // u16: the race countdown (1/30 s), master copy

    // The race's car array: $80 per car, player 1's first ($E88C copies the cars' records $20FB00.. here)
    const uint32_t CARS        = 0x20FC00;
    const uint32_t CAR_SIZE    = 0x80;
    const uint32_t CAR_LAT     = 0x44;          // s16: lateral position
    const uint32_t CAR_SEGCNT  = 0x46;          // u16: segment counter
    const uint32_t CAR_SPEED   = 0x4C;          // u16: speed (km/h x 136)
}
