/***************************************************************************
    BALANCE, pace: see balance_pace.hpp, balance.hpp and docs/balance-pace.md.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "main.hpp"
#include "frontend/config.hpp"
#include "engine/oroad.hpp"
#include "engine/oinitengine.hpp"
#include "engine/ostats.hpp"
#include "crossover/balance.hpp"
#include "crossover/balance_pace.hpp"
#include "crossover/bike.hpp"
#include "crossover/madpower.hpp"
#include "crossover/shboard.hpp"
#include "crossover/hoboard.hpp"
#include "crossover/orboard.hpp"
#include "crossover/ormode.hpp"

namespace balance
{
    // ------------------------------------------------------------------------------------------------
    // The full factors (docs/balance-pace.md): the course's own vehicle's distance per second at its top
    // speed on its course, over the visiting vehicle's at its top speed on that course before BALANCE, both
    // in the course's own distance units, at the centre line on a level straight.
    // ------------------------------------------------------------------------------------------------
    //
    // Paces at top speed (harness runs at BALANCE OFF, CB_PACE_TRACE, docs/balance-pace.md section 2), per second:
    //   OutRun, path positions:  Ferrari 40.6398 (293.115 km/h: 303 x 293 per 30 Hz tick, CAR_BASE_INC $12F)
    //                            Super Hang-On's bike 33.3170 (241.0-242.0 km/h: 151 x 241 per 60 Hz frame, $85AE)
    //                            MAD POWER 62.5713 (451.169 km/h level, automatic: 0.138687 per km/h a second)
    //   Super Hang-On, positions: its bike 33.3170; the Ferrari 40.6398; MAD POWER 62.5713 (its distance code is the
    //                            one on OutRun's courses: 0.13878 per km/h a second measured here; its level top)
    //   Hang-On, positions:      its bike 38.4320 (278.984 km/h: 151 x 278 per frame, $10F5C); the Ferrari 40.6398;
    //                            Super Hang-On's bike 33.3154; MAD POWER 62.5713 (0.13878 measured here)
    //   OutRunners, segments:    MAD POWER 56.1769 (451.169 km/h x 0.124514, measured on its course: 136 per km/h a
    //                            30 Hz tick, $8000 a segment); the Ferrari 36.4902; Super Hang-On's bike 30.0853
    double pace(Vehicle v, Host h)
    {
        switch (h)
        {
            case OUTRUN:
                if (v == SH_BIKE)   return 1.21979;     // 40.6398 / 33.3170
                if (v == MAD_POWER) return 0.64950;     // 40.6398 / 62.5713
                break;
            case SUPER_HANG_ON:
                if (v == FERRARI)   return 0.81981;     // 33.3170 / 40.6398
                if (v == MAD_POWER) return 0.53246;     // 33.3170 / 62.5713
                break;
            case HANG_ON:
                if (v == FERRARI)   return 0.94567;     // 38.4320 / 40.6398
                if (v == SH_BIKE)   return 1.15358;     // 38.4320 / 33.3154
                if (v == MAD_POWER) return 0.61421;     // 38.4320 / 62.5713
                break;
            case OUTRUNNERS:
                if (v == FERRARI)   return 1.53951;     // 56.1769 / 36.4902
                if (v == SH_BIKE)   return 1.86725;     // 56.1769 / 30.0853
                break;
        }
        return 1.0;                                     // the vehicle's own course (Hang-On's bike: its course only)
    }

    // ------------------------------------------------------------------------------------------------
    // The vehicle and course in force
    // ------------------------------------------------------------------------------------------------
    static bool joined = false;
    static Vehicle j_vehicle = FERRARI;
    static Host j_host = OUTRUN;

    void set_world(Vehicle v, Host h) { joined = true; j_vehicle = v; j_host = h; reset(); }
    void clear_world() { joined = false; reset(); }

    double pace_carry[2] = {};
    void reset_pace() { pace_carry[0] = pace_carry[1] = 0; }

    double pace_now()
    {
        Vehicle v = j_vehicle;
        Host h = j_host;
        if (!joined)
        {
            h = OUTRUN;
            v = bike.enabled ? SH_BIKE : madpower.enabled ? MAD_POWER : FERRARI;
        }
        // cached: called for every traffic object every tick
        static int c_v = -1, c_h = -1, c_b = -1;
        static double c_f = 1.0;
        if ((int) v != c_v || (int) h != c_h || config.crossover.balance != c_b)
        {
            c_v = v; c_h = h; c_b = config.crossover.balance;
            c_f = native(v, h) ? 1.0 : blend(pace(v, h));
        }
        return c_f;
    }

    uint32_t world_speed(uint32_t inc)
    {
        const double f = pace_now();
        if (f == 1.0) return inc;
        return (uint32_t) std::lround((double) inc * f);
    }

    uint32_t own_speed(uint32_t world_inc)
    {
        const double f = pace_now();
        if (f == 1.0) return world_inc;
        return (uint32_t) std::lround((double) world_inc / f);
    }

    uint32_t pace_distance(uint32_t d, double& carry)
    {
        const double f = pace_now();
        if (f == 1.0) return d;
        double x = (double) d * f + carry;
        double r = std::floor(x);
        carry = x - r;
        return (uint32_t) r;
    }

    // ------------------------------------------------------------------------------------------------
    // Testing: CB_PACE_TRACE=<file>
    //   frame host pos world_kmh own_kmh
    // host 0 OutRun (pos: road_pos in path positions), 1 Super Hang-On ($260030, path positions), 2 Hang-On
    // ($C7E030, path positions), 3 OutRunners (segments: $209042 + $209034 / $8000); world_kmh the speed the
    // host moves its world by (OutRun: world_speed(car_increment); Super Hang-On $260078, Hang-On $C7E078,
    // OutRunners $209074 / 136); own_kmh the vehicle's own (car_increment in OutRun's car slot, else the
    // board's own vehicle's: the same as world_kmh); then for OutRun, Super Hang-On and Hang-On the vehicle's X
    // (car_x_pos, $260022, $C7E050), the curve type and radius (road_type / road_curve, $2600AC / $2600B0,
    // $C7E0AC / $C7E0B0: a type over 1 is a curve; OutRun adds its stage, ostats.cur_stage), for OutRunners the car's hill frame index ($2004D9, 1 level)
    // the stage ($2080A2) and the km/h its HUD shows ($208F50)
    // ------------------------------------------------------------------------------------------------
    void pace_trace_frame(int frame)
    {
        static const char* path = getenv("CB_PACE_TRACE");
        if (!path) return;
        static FILE* f = fopen(path, "w");
        if (!f) return;
        const double own_slot = oinitengine.car_increment / 65536.0;
        auto be16 = [](const uint8_t* p) { return (uint32_t)(p[0] << 8 | p[1]); };
        switch (cannonball::state)
        {
            case cannonball::STATE_GAME:
                fprintf(f, "%d 0 %.6f %.4f %.4f %d %d %d %d\n", frame, oroad.road_pos / 65536.0, world_speed(oinitengine.car_increment) / 65536.0,
                        own_slot, oinitengine.car_x_pos, oinitengine.road_type, oinitengine.road_curve, ostats.cur_stage);
                break;
            case cannonball::STATE_SHANGON:
            {
                const uint8_t* r = shboard.cpu1ram;
                double pos = (be16(r + 0x30) << 16 | be16(r + 0x32)) / 65536.0;
                double spd = (be16(r + 0x78) << 16 | be16(r + 0x7A)) / 65536.0;
                fprintf(f, "%d 1 %.6f %.4f %.4f %d %d %d\n", frame, pos, spd, joined ? own_slot : spd, (int16_t) be16(r + 0x22),
                        (int16_t) be16(r + 0xAC), (int16_t) be16(r + 0xB0));
                break;
            }
            case cannonball::STATE_HANGON:
            {
                const uint8_t* r = hoboard.subram + 0x2000;              // $C7E000
                double pos = (be16(r + 0x30) << 16 | be16(r + 0x32)) / 65536.0;
                double spd = (be16(r + 0x78) << 16 | be16(r + 0x7A)) / 65536.0;
                fprintf(f, "%d 2 %.6f %.4f %.4f %d %d %d\n", frame, pos, spd, joined ? own_slot : spd, (int16_t) be16(r + 0x50),
                        (int16_t) be16(r + 0xAC), (int16_t) be16(r + 0xB0));
                break;
            }
            case cannonball::STATE_OUTRUNNERS:
            {
                const ORBoard* b = ormode.board_for_tests();
                if (!b) break;
                auto r16 = [&](uint32_t a) { return (uint32_t)(b->read8(a) | b->read8(a + 1) << 8); };
                uint32_t seg = r16(0x209042), in = r16(0x209034) | r16(0x209036) << 16;
                double spd = r16(0x209074) / 136.0;
                fprintf(f, "%d 3 %.6f %.4f %.4f %d %d %d\n", frame, seg + in / 32768.0, spd, joined ? own_slot : spd,
                        b->read8(0x2004D9), b->read8(0x2080A2), r16(0x208F50));   // hill frame index (1 level), stage, km/h shown
                break;
            }
            default:
                break;
        }
        fflush(f);
    }
}
