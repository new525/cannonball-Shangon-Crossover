/***************************************************************************
    BALANCE, pace: the distance a vehicle covers in the course's world per unit of its own speed.
    See balance.hpp for the factors and docs/balance-pace.md for the method and measurements.

    The vehicle keeps its own speed (its physics, sounds, curve push input and speedometer); where
    its motion enters the course's world (the distance the host's position advances by, and the
    speed the host moves traffic, rivals, scenery, the road's stripes and its distance score by),
    the host gets the vehicle's own speed x the pace factor.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include "crossover/balance.hpp"

namespace balance
{
    // The course whose program runs the world around OutRun's car slot, and the vehicle in that slot:
    // set by the joins for their race (crossover/shferrari, hovehicle, orvehicle) and cleared at its end.
    // Without one, the course is OutRun's and the vehicle the one OutRun's engine runs (bike.enabled,
    // madpower.enabled, else the Ferrari).
    void set_world(Vehicle v, Host h);
    void clear_world();

    // blend(pace(vehicle, course)) for the vehicle and course in force: 1 at OFF and on the vehicle's own course
    double pace_now();

    // A speed (16.16 km/h) as the world moves by it: speed x pace_now() (the speed itself at 1)
    uint32_t world_speed(uint32_t inc);
    // The vehicle's own speed for a world speed (the inverse; the speed itself at 1)
    uint32_t own_speed(uint32_t world_inc);

    // A distance the vehicle has covered (in the world's units) x pace_now(), with the fraction carried in
    // `carry` from call to call (the distance itself at 1)
    uint32_t pace_distance(uint32_t d, double& carry);

    // Testing (CB_PACE_TRACE=<file>): per rendered frame, the host's position in its own units, the speed the
    // world moves by and the vehicle's own speed (called from crossover/harness)
    void pace_trace_frame(int frame);
}
