/***************************************************************************
    Units shared by the joins: the conversions between the games' lateral, speed and distance
    units, and the values of OutRun's own code that more than one join uses.

    One definition each, with its source. The joins that use them: crossover/madpower (MAD POWER on
    OutRun's courses), crossover/orvehicle (the Ferrari and Super Hang-On's bike on OutRunners'),
    crossover/shferrari and crossover/hovehicle (the Ferrari, MAD POWER and Super Hang-On's bike on
    Super Hang-On's and Hang-On's), crossover/bike (Super Hang-On's bike on OutRun's), and the test
    code (crossover/harness, crossover/balance_*).

    Super Hang-On's and Hang-On's lateral X and speed are in OutRun's units (crossover/bike.hpp), so
    they take OutRun's conversions. The boards' RAM addresses are in crossover/shaddr.hpp, hoaddr.hpp
    and oraddr.hpp.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>

namespace units
{
    // --------------------------------------------------------------------------------------------
    // Time
    // --------------------------------------------------------------------------------------------

    // Frames a second of the program's time that every emulated board runs (Super Hang-On's, Hang-On's,
    // OutRunners' and its sound board alone), and Super Hang-On's bike code on the other games' courses:
    // 60. (Super Hang-On's and Hang-On's screens are 60.05 Hz on the hardware, crossover/shboard.hpp,
    // hoboard.hpp; the program gives each of their frames 1/60 s.)
    constexpr int BOARD_FPS = 60;

    // Board frames in one 30 Hz tick: OutRun's logic and OutRunners' car code run at 30 Hz (OutRunners'
    // player task every other frame, docs/OUTRUNNERS-RE.md 1), Super Hang-On's and Hang-On's every frame
    constexpr int FRAMES_PER_TICK = 2;

    // --------------------------------------------------------------------------------------------
    // OutRun <-> OutRunners (docs/CROSSOVER-OUTRUNNERS.md 7.2, 8.2, 9.2, 10)
    // --------------------------------------------------------------------------------------------

    // Lateral: OutRunners' world units per OutRun X unit, 11/6. From the cars' half-widths: MAD POWER's 44
    // (its parameter +$1A and its collision box $12928) against the Ferrari's 24 (OFerrari::check_wheels:
    // one wheel off 24 inside the road's edge, both 24 outside it). OutRun's X is + = left, OutRunners'
    // lateral ($209038) + = right, so the conversions also change the sign.
    //   OutRunners' lateral = -X x LAT_OUTRUNNERS / LAT_OUTRUN
    constexpr int LAT_OUTRUNNERS = 11, LAT_OUTRUN = 6;

    // Speed: OutRunners' speed units per km/h. Its speed ($209074) is its advance per 30 Hz tick in course
    // units, a segment $8000 long on the centre line; its HUD shows speed / 136 as km/h ($C2E9 divu.h #88).
    // OutRun's car_increment and Super Hang-On's $260078 are 16.16 km/h.
    constexpr int OUTRUNNERS_SPEED_PER_KMH = 136;

    // Distance: OutRun's advance per km/h per 30 Hz tick in its road position (16.16, the integer part
    // indexing its path): OFerrari::CAR_BASE_INC = $12F (OFerrari::update_road_pos, $6ABA). Super Hang-On's
    // and Hang-On's are 151 per km/h per 60 Hz frame, read from their ROMs ($85AE, $10F5C).
    constexpr int OUTRUN_POS_PER_KMH = 303;

    // Distance: OutRunners' segment in its course units, on the centre line ($A6A7, docs/OUTRUNNERS-RE.md 2.2)
    constexpr int OUTRUNNERS_SEGMENT = 0x8000;

    // Distance: the crossover gives a km/h the same distance in both games, so at the same km/h one
    // OutRunners segment is 303/272 OutRun path positions (OutRun 303 per km/h per tick in 16.16 positions,
    // OutRunners 136 per km/h per tick in $8000 a segment), and one position 272/303 of a segment
    constexpr double OUTRUN_POS_PER_SEGMENT = (double) OUTRUN_POS_PER_KMH / (2.0 * OUTRUNNERS_SPEED_PER_KMH);
    constexpr double OUTRUNNERS_SEGMENT_PER_POS = (double) OUTRUNNERS_SPEED_PER_KMH * 65536.0
                                                / ((double) OUTRUN_POS_PER_KMH * (double) OUTRUNNERS_SEGMENT);

    // --------------------------------------------------------------------------------------------
    // OutRun's own values
    // --------------------------------------------------------------------------------------------

    // The road's edge beyond road_width, in car_x_pos units: OFerrari::check_wheels puts one wheel off at
    // |x| > w + $D4 and all wheels off at |x| > w + $104; with a symmetric track that gives the edge at the
    // midpoint w + $EC (and a half-track of $18). The asphalt half-width around a road's centre when the
    // two roads are apart (road_width > $FF).
    constexpr int OUTRUN_ROAD_EDGE = 0xEC;

    // The road's flat heights: $2400 per entry (ORoad::set_y_2044 with OutRun's flat horizon, measured on
    // its first stage). The car's incline frames, its crash and its smoke stand on these.
    constexpr int32_t OUTRUN_FLAT_STEP = 0x2400;

    // The spin chance of a contact with one of OutRun's cars: traffic property +5, 1 for its cars and 3 for
    // its lorries (ROM $4CFA); a spin starts when random & mask == mask (OInitEngine::init_crash_bonus).
    // The boards' rivals and OutRunners' cars get the cars' value.
    constexpr uint16_t OUTRUN_CAR_COLLISION_MASK = 1;
}
