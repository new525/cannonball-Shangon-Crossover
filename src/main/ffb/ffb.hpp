/***************************************************************************
    Force feedback effects: what the wheel does, computed from the vehicle's state.

    Two layers on one device (ffb/ffbdevice):
      - the arcade base: the vehicle's own cabinet motor output, as the game's code drives it.
        OutRun's deluxe cabinet motor (OOutputs::do_motors, the commands the game writes to its
        motor register: a banking direction and speed, 0-15 with 8 the centre) becomes a constant
        force on the wheel, as Cannonball's force feedback always did, now linear in the motor
        speed. OutRunners' cabinet has a vibration motor in the wheel (one bit of its I/O chip's
        port D, MAME's Steering_Wheel_motor): a vibration. Super Hang-On's and Hang-On's ride-on
        cabinets have no motor: those bikes have no base.
      - CannonBall-DX's synthesised effects, each an option with its own strength: a loose
        surface's rattle, tyre slip, off-road pull, the gear shift's kick, the traffic contact's
        yank, the scenery crash's impacts and loads, the start line's steering load and rev shake,
        the engine's shake, and a centering spring that grows with speed and is lowered while the
        vehicle slides, skids or crashes. Their strengths and the spring's levels are
        config.controls.ffb (res/config.xml, controls/analog/haptic), the DX defaults.
    Where a synthesised effect stands in for a motor pattern of the same event (the scenery
    crash, the traffic skid, the start line), the motor base is muted for that event while the
    effect's strength is above 0.

    The vehicles fill a State once per 30 Hz tick and call tick(): OutRun's Ferrari from OutRun's
    variables (OOutputs::ffb_tick, on every course), Super Hang-On's bike from its own
    (crossover/bike), MAD POWER from its (crossover/madpower), the boards' own vehicles from their
    RAM (shmode, homode, ormode). Menus and attract mode call idle() (the spring at its low-speed
    level); losing the window's focus, a pause and the exit call stop().

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>

namespace ffb
{
    enum Phase { IDLE = 0, START = 1, RACE = 2 };
    enum Crash { CRASH_NONE = 0, CRASH_BUMP = 1, CRASH_SPIN = 2, CRASH_FLIP = 3,
                 CRASH_TUMBLE = 4 };     // a vehicle without OutRun's crash types (the bikes, MAD POWER): one impact, then the spin loads
    enum Wheels { ON = 0, LEFT_OFF = 1, RIGHT_OFF = 2, OFF = 3 };    // OFerrari::wheel_state's values

    struct State
    {
        int  phase = IDLE;          // IDLE: menus, attract, the game outside a race; START: the start line; RACE
        int  speed = 0;             // km/h (OutRun's car_increment >> 16 scale; the spring's thresholds are in it)
        int  wheels = ON;           // which wheels are off the road
        int  side = 0;              // the side of the road the vehicle is on, + left (OutRun's X sign): the pull when both wheels leave at once
        bool slipping = false;      // the tyres slide on the road (OutRun: is_slipping, the start's wheelspin)
        bool rough = false;         // a loose surface under the wheels (sand or stone strips, debris)
        int  skid = 0;              // a traffic contact's skid: + the vehicle skids left, - right, 0 none
        int  crash = CRASH_NONE;    // a scenery crash and its kind
        int  crash_state = 0;       // OCrash::crash_state (1 impact, 2 airborne flip, 3-4 slide, 5+ over); CRASH_TUMBLE: 1 while tumbling, 5 when over
        int  crash_counter = 0;     // ticks since the crash began (OCrash::crash_counter)
        int  crash_spin = 0;        // OCrash::crash_spin_count (changes with each flip rotation)
        int  crash_dir = 0;         // the impact's side: -1 left, +1 right
        int  gear_shift = 0;        // +1 shifted up this tick, -1 down
        int  start_steer = -1;      // the start line's intro: the steering load's shape 0-100 (-1: none this tick)
        int  start_pedal = -1;      // the start line's countdown: the accelerator 0-255 (-1: none)
        int  revs = -1;             // engine revs for the engine shake, 0-0x130 (OFerrari::revs >> 16; -1: none)
        int  motor = -1;            // the arcade motor command, OutRun's 0-15 (8 centre, 0 off; -1: none)
        int  motor_bit = -1;        // a one-bit vibration motor (OutRunners' wheel): 0/1 (-1: none)
    };

    void init();                    // after SDL and the controls: opens the device if controls.haptic is on
    void close();
    bool active();                  // enabled in the config and a device (or the log double) is there
    void enable(bool on);           // the menu's FORCE FEEDBACK entry
    void apply_settings();          // after the menu changed the strength or the spring

    void tick(const State& s);      // one 30 Hz tick with the vehicle's state
    void idle();                    // a tick without a vehicle (menus, attract, the boards' run-ups)
    void stop();                    // every force off (focus lost, a pause)
    void focus(bool has_focus);     // the window's focus: without it nothing plays
    bool ticked_this_frame();       // for main: tick() or idle() was called since frame_done()
    void frame_done();
}
