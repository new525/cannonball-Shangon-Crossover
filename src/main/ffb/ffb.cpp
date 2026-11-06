/***************************************************************************
    Force feedback effects. See ffb.hpp.

    Levels are percentages of the device's full force before the master strength; the shapes,
    cadences and thresholds are CannonBall-DX's (its ooutputs_base.cpp and ffeedback_base.cpp,
    commit 09edd62), on ticks instead of milliseconds: a tick is 1/30 s.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#include "ffb/ffb.hpp"
#include "ffb/ffbdevice.hpp"

#include <cstdio>
#include <cstdlib>
#include "frontend/config.hpp"

namespace ffb
{
    static bool opened = false;         // init() found a device
    static bool enabled = false;        // config.controls.haptic at init / enable()
    static bool focused = true;
    static bool ticked = false;

    // state carried between ticks
    static int  shift_dir = 0, shift_age = 0;       // a gear shift's kick
    static int  rev_ramp = 0;                       // the start's rev shake, 0-100
    static int  offroad_dir = 0;                    // the side the vehicle left the road on (+ left)
    static int  offroad_counter = 0;                // the rumble pattern's phase for vehicles without a motor command
    static int  crash_last_state = -1, crash_last_spin = -1, landing = 0;
    static int  rattle_phase = 0;
    static int  slip_hold = 0;                      // ticks the slip stays on after the state drops it

    static int clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
    static int pct(int value, int percent) { return (value * percent + (value >= 0 ? 50 : -50)) / 100; }
    static const ffb_settings_t& cfg() { return config.controls.ffb; }

    // OutRun's off-road motor patterns (OOutputs' MOTOR_VALUES_OFFROAD1/2: 0 off, 8 centre, 5/B etc. a bank),
    // for the vehicles that have no motor command of their own
    static const uint8_t OFFROAD_BOTH[32] =
    {
        0x8, 0x8, 0x5, 0x5, 0x8, 0x8, 0xB, 0xB, 0x8, 0x8, 0x4, 0x4, 0x8, 0x8, 0xC, 0xC,
        0x8, 0x8, 0x3, 0x3, 0x8, 0x8, 0xD, 0xD, 0x8, 0x8, 0x2, 0x2, 0x8, 0x8, 0xE, 0xE,
    };
    static const uint8_t OFFROAD_ONE[32] =
    {
        0x8, 0x5, 0x8, 0xB, 0x8, 0x5, 0x8, 0xB, 0x8, 0x4, 0x8, 0xC, 0x8, 0x4, 0x8, 0xC,
        0x8, 0x3, 0x8, 0xD, 0x8, 0x3, 0x8, 0xD, 0x8, 0x2, 0x8, 0xE, 0x8, 0x2, 0x8, 0xE,
    };

    void init()
    {
        enabled = config.controls.haptic != 0;
        if (!enabled) return;
        opened = ffbdev::init(config.controls.pad_id);
        if (!opened) return;
        apply_settings();
        idle();
    }

    void close()
    {
        if (opened) ffbdev::close();
        opened = false;
    }

    bool active() { return opened && enabled && focused; }

    void enable(bool on)
    {
        enabled = on;
        if (on && !opened)
        {
            opened = ffbdev::init(config.controls.pad_id);
            if (opened) apply_settings();
        }
        if (!on && opened) ffbdev::stop();
    }

    void apply_settings()
    {
        if (!opened) return;
        ffbdev::set_gain(clamp(cfg().strength, 0, 100));
    }

    void focus(bool has_focus)
    {
        if (focused == has_focus) return;
        focused = has_focus;
        if (!has_focus && opened) ffbdev::stop();
    }

    void stop()
    {
        ticked = true;
        if (opened) ffbdev::stop();
    }

    bool ticked_this_frame() { return ticked; }
    void frame_done() { ticked = false; }

    static void reset_race_state()
    {
        shift_dir = shift_age = 0;
        rev_ramp = 0;
        offroad_dir = 0;
        slip_hold = 0;
        crash_last_state = crash_last_spin = -1;
        landing = 0;
    }

    // a spring level: a percentage of the SPRING setting
    static int spring_level(int percent) { return pct(clamp(cfg().centering_strength, 0, 100), percent); }

    // the motor command as a constant force: linear in the motor's speed, 0 for off and centre
    static int motor_level(int cmd, int strength)
    {
        if (cmd <= 0 || cmd == 8 || cmd > 15) return 0;
        return pct((cmd - 8) * 100 / 7, strength);
    }

    // ---------------------------------------------------------------------------------------------
    // The scenery crash: DX's choreography (ooutputs_base.cpp apply_crash_ffb_force) and its spring
    // levels (ffeedback_base.cpp tuned_centering_for_source). primary is the impact's side. The
    // level is 0 and spring_pct 100 for a crash kind without effects.
    // ---------------------------------------------------------------------------------------------
    static int crash_level(const State& s, int& spring_pct)
    {
        const ffb_settings_t& c = cfg();
        const int primary = s.crash_dir < 0 ? -100 : 100;
        const int rebound = -primary;
        const int age = s.crash_counter;
        const int state = s.crash_state;
        const bool spin_changed = crash_last_spin >= 0 && s.crash_spin != crash_last_spin;
        int level = 0;
        spring_pct = 100;

        switch (s.crash)
        {
            case CRASH_BUMP:
                spring_pct = c.sp_crash_bump;
                if (age <= 2)      level = pct(primary, c.crash_bump);
                else if (age == 3) level = pct(pct(rebound, 29), c.crash_bump);
                break;

            case CRASH_SPIN:
            case CRASH_TUMBLE:
                // the side loads through states 1-4 (DX), and through a traffic contact's spin, which OutRun
                // counts with crash_counter at crash_state 0 (OCrash::spin_control1; DX left that one silent)
                spring_pct = state <= 4 ? c.sp_crash_spin : c.sp_crash_recovery;
                if (age <= 2)        level = pct(primary, c.crash_spin_impact);
                else if (state <= 4) level = pct(((age / 4) & 1) ? primary : rebound, c.crash_spin);
                break;

            case CRASH_FLIP:
                if (state <= 1)
                {
                    spring_pct = c.sp_crash_flip_start;
                    if (age <= 2) level = pct(primary, c.crash_flip_impact);
                    else          level = pct(((age / 4) & 1) ? primary : rebound, c.crash_flip);
                }
                else if (state == 2)
                {
                    spring_pct = c.sp_crash_flip_airborne;
                    if (s.crash_spin > 0)
                        level = pct(pct((s.crash_spin & 1) ? primary : rebound, spin_changed ? 100 : 71), c.crash_flip);
                }
                else
                {
                    spring_pct = state <= 4 ? c.sp_crash_flip_transition : (state == 5 ? c.sp_crash_flip_landing : c.sp_crash_flip_recovery);
                    if (crash_last_state == 2 && state >= 5) landing = 3;   // the airborne state ends on the ground: the landing
                    if (landing == 3)      level = pct(primary, c.crash_flip_landing);
                    else if (landing == 2) level = pct(pct(rebound, 57), c.crash_flip_landing);
                    else if (landing == 1) level = pct(pct(primary, 14), c.crash_flip_landing);
                    if (landing > 0) landing--;
                }
                break;

            default:
                break;
        }
        crash_last_state = state;
        crash_last_spin = s.crash_spin;
        return level;
    }

    static bool crash_effects_on(int crash)
    {
        const ffb_settings_t& c = cfg();
        switch (crash)
        {
            case CRASH_BUMP:   return c.crash_bump > 0;
            case CRASH_SPIN:
            case CRASH_TUMBLE: return c.crash_spin_impact > 0 || c.crash_spin > 0;
            case CRASH_FLIP:   return c.crash_flip_impact > 0 || c.crash_flip > 0 || c.crash_flip_landing > 0;
            default:           return false;
        }
    }

    // DX's off-road: the motor table's rumble, its +/-6 peak as 100%, scaled by the rumble strength and a
    // speed envelope, plus a constant pull away from the road
    static int offroad_level(const State& s)
    {
        const ffb_settings_t& c = cfg();
        const bool full = s.wheels == OFF;
        // the pull is away from the road: the side the vehicle left on (DX's fallback for both wheels leaving
        // at once pulled the other way; here it is the side of the road the vehicle is on, as for one wheel)
        if (s.wheels == LEFT_OFF)       offroad_dir = 1;
        else if (s.wheels == RIGHT_OFF) offroad_dir = -1;
        else if (offroad_dir == 0)      offroad_dir = s.side >= 0 ? 1 : -1;

        int cmd = s.motor;
        if (cmd < 0)
        {   // no motor command: OutRun's own pattern (OOutputs::do_motor_offroad) from the speed
            const int index = s.speed <= 0x32 ? 0 : s.speed <= 0x50 ? 1 : s.speed <= 0x6E ? 2 : 3;
            cmd = (full ? OFFROAD_BOTH : OFFROAD_ONE)[(index << 3) + (offroad_counter & 7)];
            offroad_counter++;
        }
        const int speed_pct = s.speed <= 0x32 ? 20 : s.speed <= 0x50 ? 40 : s.speed <= 0x6E ? 70 : 100;
        int rumble = (cmd <= 0 || cmd == 8 || cmd > 15) ? 0 : (cmd - 8) * 100 / 6;
        rumble = pct(pct(rumble, full ? c.offroad_rumble_full : c.offroad_rumble_one_wheel), speed_pct);
        const int pull = pct(100 * offroad_dir, full ? c.offroad_pull_full : c.offroad_pull_one_wheel);
        return clamp(rumble + pull, -100, 100);
    }

    // DX's rough surface rattle: light taps left and right, quicker at speed
    static int rattle_level(const State& s)
    {
        static const int8_t SLOW[16] = { -1, 0, 1, 0, 0, -1, 0, 1, 0, 0, -1, 0, 1, 0, 0, -1 };
        static const int8_t FAST[16] = { -1, 0, 1, 0, -1, 0, 1, 0, -1, 0, 1, 0, -1, 0, 1, 0 };
        if (s.speed < 0x50) return 0;
        const int8_t* pattern = s.speed < 0xB0 ? SLOW : FAST;
        return pct(100 * pattern[rattle_phase & 15], cfg().sand);
    }

    // DX's gear shift: a kick in the shift's direction for two ticks, a smaller rebound on the third
    static int shift_level()
    {
        if (!shift_dir) return 0;
        int level = 0;
        if (shift_age <= 1)      level = pct(100 * shift_dir, cfg().gear_shift);
        else if (shift_age == 2) level = pct(pct(-100 * shift_dir, 29), cfg().gear_shift);
        else                     shift_dir = 0;
        shift_age++;
        return level;
    }

    void tick(const State& s)
    {
        ticked = true;
        rattle_phase++;
        if (!active()) return;
        const ffb_settings_t& c = cfg();

        static const bool log_state = getenv("CB_FFB_LOG_STATE") != nullptr;
        if (log_state && ffbdev::logging())
        {
            char line[200];
            snprintf(line, sizeof line, "state phase %d speed %d wheels %d side %d slip %d rough %d skid %d crash %d/%d/%d/%d dir %d shift %d start %d/%d revs %d motor %d bit %d",
                     s.phase, s.speed, s.wheels, s.side, s.slipping, s.rough, s.skid, s.crash, s.crash_state, s.crash_counter, s.crash_spin,
                     s.crash_dir, s.gear_shift, s.start_steer, s.start_pedal, s.revs, s.motor, s.motor_bit);
            ffbdev::log_line(line);
        }

        if (s.crash == CRASH_NONE) { crash_last_state = crash_last_spin = -1; landing = 0; }
        if (s.phase == IDLE) { shift_dir = shift_age = 0; rev_ramp = 0; }
        if (s.wheels == ON) offroad_dir = 0;
        if (s.gear_shift && s.phase == RACE && c.gear_shift > 0) { shift_dir = s.gear_shift; shift_age = 0; }

        // the vehicles' slip flags can flicker from tick to tick (MAD POWER's sliding byte at full lock):
        // the slip is held for a few ticks after it drops
        bool slipping = s.slipping;
        if (s.phase == RACE && s.slipping) slip_hold = 4;
        else if (slip_hold > 0 && s.phase == RACE && s.wheels == ON) { slip_hold--; slipping = true; }
        else slip_hold = 0;

        const int motor_base = motor_level(s.motor, c.motor);
        int level = 0;
        int spring = spring_level(c.low_speed);
        int periodic = s.motor_bit > 0 ? c.motor_vibration : 0, period = 45;    // a one-bit vibration motor: a sine while it is on

        if (s.phase == START)
        {
            if (s.start_steer >= 0 && c.start_steering > 0)
                level = -pct(clamp(s.start_steer, 0, 100), c.start_steering);
            else
                level = motor_base;
            if (s.start_pedal >= 0 && c.start_rev_shake > 0)
            {   // DX: the pedal as a 0-100 target, reached 5 a tick, left 7 a tick
                const int target = (clamp(s.start_pedal, 0, 255) * 100 + 0x7F) / 0xFF;
                if (rev_ramp < target)      rev_ramp = rev_ramp + 5 > target ? target : rev_ramp + 5;
                else if (rev_ramp > target) rev_ramp = rev_ramp - 7 < target ? target : rev_ramp - 7;
                if (rev_ramp > periodic) periodic = pct(rev_ramp, c.start_rev_shake);
            }
            else
                rev_ramp = 0;
        }
        else if (s.phase == RACE)
        {
            if (s.crash != CRASH_NONE)
            {
                int sp = 100;
                const int fx = crash_level(s, sp);
                level = crash_effects_on(s.crash) ? fx : motor_base;
                spring = spring_level(sp);
            }
            else if (s.skid != 0)
            {
                level = c.traffic_skid > 0 ? pct(s.skid > 0 ? -100 : 100, c.traffic_skid) : motor_base;
                spring = spring_level(c.sp_traffic_skid);
            }
            else if (s.wheels != ON)
            {
                level = offroad_level(s);
            }
            else
            {
                level = motor_base;
                if (s.rough && !slipping) level += rattle_level(s);
                // the spring grows with speed, and gives way while the tyres slide
                const int lo = spring_level(c.low_speed), hi = spring_level(c.high_speed);
                const int start = c.speed_start, full = c.speed_full > start ? c.speed_full : start + 1;
                spring = lo + (hi - lo) * clamp(s.speed - start, 0, full - start) / (full - start);
                if (slipping) spring = pct(spring, c.sliding);
                // the periodic channel: the tyre slip, else the engine's shake with the revs
                if (slipping && c.tyre_slip > periodic)
                    periodic = c.tyre_slip;
                else if (!slipping && s.revs >= 0 && c.engine > 0 && periodic == 0)
                {   // DX: the revs in eight steps (one haptic update per step, not per tick), the magnitude's
                    // envelope 18-100% over them, the period the idle period shortened in proportion
                    const int bucket = clamp((((clamp(s.revs, 0, 0x130) * 100 + 0x98) / 0x130) * 7 + 50) / 100, 0, 7);
                    const int revs = 0x1F + bucket * (0x130 - 0x1F) / 7;
                    periodic = pct(c.engine, 18 + (bucket * 82 + 3) / 7);
                    period = clamp((c.engine_period * 0x1F + revs / 2) / revs, 10, 500);
                }
            }
            level += shift_level();
        }
        else
        {
            level = motor_base;         // Cannonball's original behaviour outside a race: the motor centres the wheel
        }

        ffbdev::set_constant(clamp(c.invert ? -level : level, -100, 100));
        ffbdev::set_spring(clamp(spring, 0, 100));
        ffbdev::set_periodic(clamp(periodic, 0, 100), period);
    }

    void idle()
    {
        ticked = true;
        if (!active()) return;
        reset_race_state();
        ffbdev::set_constant(0);
        ffbdev::set_periodic(0, 0);
        ffbdev::set_spring(spring_level(cfg().low_speed));
    }
}
