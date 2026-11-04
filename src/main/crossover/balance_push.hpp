/***************************************************************************
    BALANCE, curve push (see balance.hpp and docs/balance-push.md): where each visiting vehicle's
    curve push is scaled, and the measurement log.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include <cstdio>

#include "crossover/balance.hpp"

class ORBoard;

namespace balance
{
    // The course being raced: OutRunners', Hang-On's or Super Hang-On's board when its mode is
    // active, otherwise OutRun's
    Host host();

    // The push the vehicle's own code has just given (its own lateral units, one tick of its own),
    // multiplied by blend(push(v, host())). The fraction rounded away is carried to the next call,
    // so the average is exact. With a factor of 1 the push is returned as it is.
    int push_scaled(Vehicle v, int push);

    // OFerrari::set_curve_adjust with the Ferrari's factor for this course
    void ferrari_curve_adjust();

    // Measurement (CB_PUSH_LOG=<file>): one line per tick of each vehicle's push, see
    // docs/balance-push.md. Null when the switch is off.
    FILE* push_log();
    int log_pos();                          // the course position the log uses for this course
    // per board frame, from the modes: the course's own vehicle (Super Hang-On's bike, Hang-On's bike,
    // MAD POWER) read from the board's RAM
    void push_frame_sh();
    void push_frame_ho();
    void push_frame_or(ORBoard* b);
}
