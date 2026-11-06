/***************************************************************************
    Balancing a vehicle against a course from another game (the race setup's BALANCE).

    Each game's courses are made for its own vehicle: OutRun's for the Ferrari, Super Hang-On's
    for its bike, Hang-On's for its bike, OutRunners' for MAD POWER. A vehicle from another game
    keeps its own handling, sounds and speedometer; BALANCE pulls what it does to the course
    towards what the course's own vehicle does, by a factor per aspect:

      factor = full ^ strength

    where full is the factor that matches the course's own vehicle (worked out per vehicle and
    course from measurements, documented with each table) and strength is 0 (OFF: the vehicles
    as they are), 0.5 (HALF: the geometric midpoint) or 1 (FULL). On a vehicle's own course
    every factor is 1.

    Aspects:
      pace     the distance the vehicle covers in the course's world per unit of its own speed
      push     the curve push the vehicle feels from the course's curves
      contact  the speed it loses to other vehicles

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

class ORBoard;

namespace balance
{
    enum Vehicle { FERRARI = 0, SH_BIKE = 1, HO_BIKE = 2, MAD_POWER = 3 };
    enum Host    { OUTRUN = 0, SUPER_HANG_ON = 1, HANG_ON = 2, OUTRUNNERS = 3 };

    // 0, 0.5 or 1 from config.crossover.balance (0 OFF, 1 HALF, 2 FULL)
    double strength();

    // full ^ strength(): 1 for OFF, and for full == 1
    double blend(double full);

    // The vehicle's own course (every factor 1)
    bool native(Vehicle v, Host h);

    // The carries the aspects keep between ticks (rounding fractions, a contact going on) cleared, so that a
    // race starts as the first one did: at every race start (OInitEngine::init) and each join's begin/end
    void reset();
    void reset_pace();                      // (the parts of reset(), each in its file)
    void reset_push();
    void reset_contact();
    extern double pace_carry[2];            // the Ferrari's and the bike's pace_distance carries (balance_pace.cpp)

    double pace(Vehicle v, Host h);         // crossover/balance_pace.cpp
    double push(Vehicle v, Host h);         // crossover/balance_push.cpp
    double contact(Vehicle v, Host h);      // crossover/balance_contact.cpp

    // Contact with another vehicle (balance_contact.cpp): the speed the vehicle's own response takes off,
    // scaled. A contact begins (or goes on) with contact_begin; each tick of the response then gives its
    // speed change to contact_step. contact_cut is both, for a response that is a single cut.
    void   contact_begin(Vehicle v, double before_kmh, int other_kmh);
    double contact_step(Vehicle v, double before, double after);
    int    contact_cut(Vehicle v, int before_kmh, int after_kmh, int other_kmh);

    // Testing (CB_CONTACT_LOG=<file>): the vehicle's speed every frame and each contact
    bool contact_tracing();
    void contact_trace_frame();
    void contact_trace_hit(const char* what, int other_kmh, double before, double after);
    void contact_trace_or(const ::ORBoard& board, bool own_car);
    extern const ::ORBoard* test_or_board;
    void contact_test_rails();      // the harness's CB_CHASE on OutRunners' courses
}
