/***************************************************************************
    Balancing a vehicle against a course from another game: contact (see balance.hpp).

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "main.hpp"
#include "engine/outrun.hpp"
#include "engine/oinitengine.hpp"
#include "engine/ocrash.hpp"
#include "crossover/balance.hpp"
#include "crossover/harness.hpp"
#include "crossover/shmode.hpp"
#include "crossover/shboard.hpp"
#include "crossover/shferrari.hpp"
#include "crossover/homode.hpp"
#include "crossover/hoboard.hpp"
#include "crossover/hovehicle.hpp"
#include "crossover/ormode.hpp"
#include "crossover/orboard.hpp"
#include "crossover/orvehicle.hpp"

namespace balance
{
    // --------------------------------------------------------------------------------------------
    // The rule (docs/balance-contact.md). Each game answers a contact with another vehicle by taking a
    // share of its speed off; how much, it decides by its own rule, written here as the speed it keeps:
    //
    //   OutRun, the Ferrari ($50DE, OTraffic::check_collision): the speed is set to the other's speed - 80
    //   Super Hang-On and Hang-On, the bike ($7510 / $A928, then the speed task's $6178/$61E6 and $85DA/$8656):
    //       a knockback of 14 a frame of contact (kept within 40) that cuts the throttle and brakes 5 x 0.775
    //       km/h a frame while it counts down; it ends above the speed task's 80 km/h floor
    //   OutRunners, MAD POWER ($8029 / $8074, then state 5 $819D): a bounce off the other by their speeds and
    //       weights; against a car of its weight it ends about at the other's speed, against Kenya's
    //       elephants (the only other vehicles of a one-player race) well below it
    //
    // With FULL, a visiting vehicle keeps the fraction of its speed that the course's own vehicle would keep
    // in the same contact: the same other vehicle, met at the same fraction of the vehicle's top speed. The
    // visiting vehicle's own response keeps its shape (the Ferrari's skid and spin chance, the bike's
    // knockback, MAD POWER's bounce); the speed it takes off is multiplied by
    //
    //   full = (1 - kept by the course's vehicle) / (1 - kept by the visiting vehicle's own response)
    //
    // and the applied factor is blend(full). The visiting vehicle's own response is known when the contact
    // starts for the Ferrari (a cut); for the bike and MAD POWER, whose responses run over several frames,
    // its model below gives what it would keep.
    // --------------------------------------------------------------------------------------------

    // Top speeds, km/h in each vehicle's own unit (measured with the accelerator held on a level road:
    // the Ferrari 293.1 on OutRun's first stage, the bike 242.0 on Super Hang-On's course, Hang-On's bike
    // 276.6 on its course, MAD POWER 451.2 with its automatic box on Hang-On's level road)
    static const double TOP[4] = { 293.0, 242.0, 276.6, 451.2 };

    // The knockback's speed loss at top speed on its own board, km/h (measured, rails runs, offsets 0-40
    // across the hit span: Super Hang-On 10 contacts, 39 knock frames 154.6, 26 frames 100.6, mean 145;
    // Hang-On 4 contacts, all 39 frames, 155)
    static const double KNOCK_LOSS_SH = 145.0, KNOCK_LOSS_HO = 155.0;
    // The bike's speed task brakes only from 80 km/h ($61B8 / $8622): the knockback leaves it at about 78
    static const double KNOCK_FLOOR = 78.0;
    // Super Hang-On's bike on the other courses: its knockback's loss at top speed there, km/h (measured,
    // 241 -> 84 on OutRun's, Hang-On's and OutRunners' courses: their tests report a contact once an OutRun
    // or OutRunners tick, which gives 14 on both Super Hang-On frames of the tick, so 40 frames of knock)
    static const double KNOCK_LOSS_GUEST[4] = { 157.0, 145.0, 157.0, 157.0 };

    // MAD POWER against Kenya's elephants (kind 1, $8029), the speed it keeps, fitted to its own contacts (24
    // contacts with elephants at 152-227 km/h, met at 221-451 km/h; least squares, rms 9.7 km/h): 1.385 x the
    // elephant's speed - 0.52 x its own, not below 0
    static double kept_or_elephant(double v, double u) { return std::max(0.0, 1.385 * u - 0.52 * v); }

    // The speed the course's own vehicle keeps (km/h) when it meets the other (u km/h) at v km/h
    static double kept_host(Host h, double v, double u)
    {
        switch (h)
        {
            case OUTRUN:        return std::min(v, std::max(0.0, u - 80));
            case SUPER_HANG_ON: return std::min(v, std::max(KNOCK_FLOOR, v - KNOCK_LOSS_SH));
            case HANG_ON:       return std::min(v, std::max(KNOCK_FLOOR, v - KNOCK_LOSS_HO));
            case OUTRUNNERS:    return std::min(v, kept_or_elephant(v, u));
        }
        return v;
    }

    // The speed the visiting vehicle's own response keeps (km/h): the Ferrari's cut is exact (it is passed);
    // the bike's knockback as measured on that course, its braking taken on below the speed task's floor
    // (contact_step: a scaled knockback brakes at its own rate for all its frames); MAD POWER against a car
    // of its own weight ($8074: the traffic and rivals count as cars of its race) keeps 0.711 x the other's
    // speed + 0.121 x its own + 4.5 km/h (least squares over its 362 contacts at OFF on OutRun's, Super
    // Hang-On's and Hang-On's courses, met at 120-450 km/h: rms 12 km/h, 4.3 points of speed lost)
    static double kept_mp_car(double v, double u) { return 0.711 * u + 0.121 * v + 4.5; }
    static double kept_own(Vehicle v, Host h, double before, double u)
    {
        switch (v)
        {
            case SH_BIKE:   return std::min(before, std::max(0.0, before - KNOCK_LOSS_GUEST[h]));
            case MAD_POWER: return std::min(before, std::max(0.0, kept_mp_car(before, u)));
            default:        return std::max(0.0, u - 80);
        }
    }

    // The full factor for a contact: the visiting vehicle v at before km/h meets another at u km/h on h's
    // course; own_after is what its own response keeps (< 0: its model above)
    static double full_factor(Vehicle v, Host h, double before, double u, double own_after = -1)
    {
        if (native(v, h) || before <= 0) return 1.0;
        const Vehicle hv = h == OUTRUN ? FERRARI : h == SUPER_HANG_ON ? SH_BIKE : h == HANG_ON ? HO_BIKE : MAD_POWER;
        const double p = before / TOP[v];                       // the same fraction of the top speed
        const double vh = p * TOP[hv];
        const double lost_host = 1.0 - kept_host(h, vh, u) / vh;
        const double lost_own  = 1.0 - (own_after >= 0 ? std::min(before, own_after) : kept_own(v, h, before, u)) / before;
        if (lost_own < 0.02 || lost_host <= 0) return 1.0;      // a contact that takes (almost) nothing off
        return std::max(0.2, std::min(5.0, lost_host / lost_own));
    }

    // The other vehicle's speed in a typical contact of each course (km/h): OutRun's traffic cruises at
    // 200 + a random -32..31 (OTraffic::spawn_car, $4BAC: traffic_orig_speed); the rivals met on Super
    // Hang-On's and Hang-On's courses (rival_speed, measured means of the rails runs: 136 and 190); Kenya's
    // elephants 152-227, 203 on average in the rails runs (their speed word +$2C << 8, as $805B reads it)
    static const double TYPICAL_OTHER[4] = { 200.0, 136.0, 190.0, 203.0 };

    // The full factor of a typical contact at top speed (the table in docs/balance-contact.md)
    double contact(Vehicle v, Host h)
    {
        return full_factor(v, h, TOP[v], TYPICAL_OTHER[h]);
    }

    // The course being driven
    static Host host_now()
    {
        if (shmode.active) return SUPER_HANG_ON;
        if (homode.active) return HANG_ON;
        if (ormode.active) return OUTRUNNERS;
        return OUTRUN;
    }

    // The contact going on: its applied factor, and for the Ferrari the speed it leaves (a contact goes on
    // while the hosts report it every tick; OutRun sets the same speed again each tick of an overlap)
    static double factor_now = 1.0;
    static int    begin_frame = -1000;      // cannonball::frame of the last report
    static int    cut_kmh = -1;
    static int    step_frame = -100;        // testing: the last frame of a response (contact_step)
    static int    base_kmh = 0, own_kmh = -1;   // contact_cut: the speed a contact started from, OutRun's own cut

    void reset_contact()
    {
        factor_now = 1.0;
        begin_frame = -1000;
        cut_kmh = -1;
        step_frame = -100;
        base_kmh = 0; own_kmh = -1;
    }

    void contact_begin(Vehicle v, double before_kmh, int other_kmh)
    {
        const bool going_on = cannonball::frame - begin_frame <= 4;
        begin_frame = cannonball::frame;
        if (going_on) return;
        cut_kmh = -1;
        factor_now = blend(full_factor(v, host_now(), before_kmh, other_kmh));
        if (contact_tracing()) contact_trace_hit("begin", other_kmh, before_kmh, factor_now);
    }

    double contact_step(Vehicle v, double before, double after)
    {
        step_frame = harness.frame;
        if (factor_now == 1.0) return after;
        // the bike (16.16 km/h): the knockback brakes 5 x $C666 a frame ($61D4 and the three steps of $61E6)
        // while its speed task is at or above 80 km/h ($61B8); below that the task accelerates. Scaled, it
        // brakes on.
        if (v == SH_BIKE && before < 80.0 * 65536) after = before - 5.0 * 0xC666;
        return before + (after - before) * factor_now;
    }

    int contact_cut(Vehicle v, int before_kmh, int after_kmh, int other_kmh)
    {
        // a contact goes on, or another vehicle is met within it (OutRun's cut is then the last one's): the
        // speed it started from stays the base of the cut
        const bool going_on = cannonball::frame - begin_frame <= 4;
        begin_frame = cannonball::frame;
        step_frame = harness.frame;
        if (!going_on) base_kmh = before_kmh;
        if (!going_on || after_kmh != own_kmh)
        {
            own_kmh = after_kmh;
            before_kmh = base_kmh;
            factor_now = blend(full_factor(v, host_now(), before_kmh, other_kmh, after_kmh));
            cut_kmh = factor_now == 1.0 ? -1
                    : std::max(0, (int) std::lround(before_kmh - (before_kmh - after_kmh) * factor_now));
            if (contact_tracing()) contact_trace_hit("begin", other_kmh, before_kmh, factor_now);
        }
        const int s = cut_kmh < 0 ? after_kmh : cut_kmh;
        if (contact_tracing()) contact_trace_hit("cut", other_kmh, before_kmh, s);
        return s;
    }

    // --------------------------------------------------------------------------------------------
    // Testing: CB_CONTACT_LOG=<file> writes the vehicle's speed every frame and each contact
    //   F <frame> <km/h, the vehicle's own unit> then, as they apply: K <knock> (the bikes at home),
    //     S <state> (MAD POWER at home), X <X>, R <1 in a scaled response>, C <the Ferrari's spin x 100 + crash state>
    //   H <frame> <what> <the other's km/h> <km/h before> <the applied factor ('begin'), the speed set ('cut'),
    //     OutRun's spin mask ('traffic')>; MAD POWER at home: H <frame> kind<n> <the other's km/h> <km/h> <km/h>
    // --------------------------------------------------------------------------------------------
    static FILE* trace_file()
    {
        static FILE* f = getenv("CB_CONTACT_LOG") ? fopen(getenv("CB_CONTACT_LOG"), "w") : nullptr;
        return f;
    }

    bool contact_tracing() { return trace_file() != nullptr; }

    void contact_trace_hit(const char* what, int other_kmh, double before, double after)
    {
        FILE* f = trace_file();
        if (f) fprintf(f, "H %d %s %d %.3f %.3f\n", harness.frame, what, other_kmh, before, after);
    }

    static uint32_t rl(const uint8_t* p) { return (uint32_t) p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

    void contact_trace_frame()
    {
        FILE* f = trace_file();
        if (!f) return;
        if (ormode.active && !orvehicle.enabled) return;            // MAD POWER at home: contact_trace_or
        if (shmode.active && !shferrari.enabled)
        {   // Super Hang-On's bike at home: speed $260078 (16.16 km/h), knock $260088, X $260022
            const uint8_t* m = shboard.cpu1ram;
            fprintf(f, "F %d %.3f K %d X %d\n", harness.frame, (int32_t) rl(m + 0x78) / 65536.0, (int16_t)(m[0x88] << 8 | m[0x89]),
                    (int16_t)(m[0x22] << 8 | m[0x23]));
            return;
        }
        if (homode.active && !hovehicle.enabled)
        {   // Hang-On's bike at home: speed $C7E078 (16.16 km/h), knock $C7E088, X $C7E050
            const uint8_t* m = hoboard.subram + 0x2000;
            fprintf(f, "F %d %.3f K %d X %d\n", harness.frame, (int32_t) rl(m + 0x78) / 65536.0, (int16_t)(m[0x88] << 8 | m[0x89]),
                    (int16_t)(m[0x50] << 8 | m[0x51]));
            return;
        }
        fprintf(f, "F %d %.3f X %d R %d C %d\n", harness.frame, (int32_t) oinitengine.car_increment / 65536.0, oinitengine.car_x_pos,
                harness.frame - step_frame <= 2 ? 1 : 0, ocrash.spin_control1 * 100 + ocrash.crash_state);
    }

    // MAD POWER on OutRunners' own courses: speed $209074 (km/h x 136), state +$0C of the car object $200400;
    // a contact is the change to state 5 (docs/OUTRUNNERS-RE.md 2.7, 4.3) with its kind $20046E
    const ORBoard* test_or_board = nullptr;

    void contact_trace_or(const ORBoard& b, bool own_car)
    {
        test_or_board = &b;
        FILE* f = trace_file();
        if (!f || !own_car) return;
        auto r16 = [&](uint32_t a) { return (uint16_t)(b.read8(a) | b.read8(a + 1) << 8); };
        static int state_prev = -1;
        int state = r16(0x20040C);
        double kmh = r16(0x209074) / 136.0;
        if (state == 5 && state_prev != 5)
        {
            int kind = b.read8(0x20046E), other = -1;
            if (kind == 2) other = r16(0x20FC00 + 0x80 * b.read8(0x200468) + 0x4C) / 136;
            if (kind == 1) other = (int)(((uint32_t) r16((0x200000 | r16(0x200468)) + 0x2C) << 8) / 136);
            fprintf(f, "H %d kind%d %d %.3f %.3f\n", harness.frame, kind, other, kmh, kmh);
        }
        state_prev = state;
        fprintf(f, "F %d %.3f S %d\n", harness.frame, kmh, state);
    }

    // Testing (CB_RAILS=<X>): the vehicle rides Super Hang-On's, Hang-On's or OutRunners' road at X (put back there as soon
    // as no contact response, skid or crash runs, so the recovery is on the road), and each rival coming up to
    // the contact window is put in its path at the next lateral offset of CB_RAILS_OFFS (default
    // 0,8,16,24,32,40), so the board's own test and the vehicle's own response decide the contact. The timer
    // is held. Called by the harness before each frame.
    void contact_test_rails()
    {
        static const char* env = getenv("CB_RAILS");
        if (!env) return;
        if (ormode.active)
        {   // OutRunners' courses: the vehicle held at X (OutRun's units; MAD POWER's lateral $209038 = -X x 11/6)
            // while its speed is not falling (no response); Kenya's elephants run their own way
            ORBoard* b = const_cast<ORBoard*>(test_or_board);
            if (!b) return;
            static int32_t hist[8] = {0};
            static int hi = 0, quiet_or = 0;
            const bool own = !orvehicle.enabled;
            const int32_t now = own ? (b->read8(0x209074) | b->read8(0x209075) << 8) : oinitengine.car_increment;
            const int32_t then = hist[hi & 7];
            hist[hi++ & 7] = now;
            const bool busy = own ? b->read8(0x20040C) == 5
                                  : (ocrash.skid_counter != 0 || ocrash.spin_control1 != 0 || ocrash.crash_counter != 0);
            quiet_or = (now < then || busy) ? 0 : quiet_or + 1;
            if (quiet_or > 0)
            {
                if (own)
                {
                    int16_t lat = (int16_t)(-atoi(env) * 11 / 6);
                    b->write8(0x209038, (uint8_t) lat); b->write8(0x209039, (uint8_t)(lat >> 8));
                }
                else oinitengine.car_x_pos = (int16_t) atoi(env);
            }
            return;
        }
        static std::vector<int> offs;
        static int next = 0;
        if (offs.empty())
        {
            const char* o = getenv("CB_RAILS_OFFS") ? getenv("CB_RAILS_OFFS") : "0,8,16,24,32,40";
            for (const char* p = o; *p; ) { offs.push_back(atoi(p)); while (*p && *p != ',') p++; if (*p) p++; }
        }
        static uint32_t slot_set[256] = {0};
        static int slot_off[256] = {0};
        uint8_t *ram, *v;
        uint32_t task, from, to, xoff;
        int dfield, dlo, dhi;
        const bool guest = (shmode.active && shferrari.enabled) || (homode.active && hovehicle.enabled);
        if (shmode.active)
        { ram = shboard.workram; v = shboard.cpu1ram; task = 0x74B6; from = 0x3000; to = 0x8000; dfield = 0x14; dlo = 0x40; dhi = 0xA0; xoff = 0x22; }
        else if (homode.active)
        { ram = hoboard.workram; v = hoboard.subram + 0x2000; task = 0xA8D0; from = 0x800; to = 0x4000; dfield = 0x0C; dlo = 0x60; dhi = 0xD0; xoff = 0x50; }
        else return;
        auto rw = [](const uint8_t* p) { return (int)(int16_t)(p[0] << 8 | p[1]); };
        auto ww = [](uint8_t* p, int x) { p[0] = (uint8_t)(x >> 8); p[1] = (uint8_t) x; };
        ram[0x500] = 0x50;                                          // the timer
        static int quiet = 0;
        const int lane = atoi(env);
        if (!guest)
        {
            quiet = (rw(v + 0x88) != 0 || rw(v + 0x94) != 0) ? 0 : quiet + 1;     // knock, crash
            if (quiet > 0) ww(v + xoff, lane);                      // X ($260022 / $C7E050)
        }
        else
        {   // no response: the speed not lower than 8 frames ago, and no skid, spin or crash of the Ferrari
            static int32_t hist[8] = {0};
            static int hi = 0;
            const int32_t now = oinitengine.car_increment, then = hist[hi & 7];
            hist[hi++ & 7] = now;
            quiet = (now < then || ocrash.skid_counter != 0 || ocrash.spin_control1 != 0 || ocrash.crash_counter != 0) ? 0 : quiet + 1;
            if (quiet > 0) oinitengine.car_x_pos = (int16_t) lane;
        }
        for (uint32_t a = from; a < to; a += 0x40)
        {
            uint8_t* s = ram + a;
            uint32_t t = (uint32_t) s[2] << 24 | s[3] << 16 | s[4] << 8 | s[5];
            if (!(s[0] & 0x80) || t != task) continue;
            int d = rw(s + dfield);
            int k = (a / 0x40) & 255;
            if (d < dlo || d >= dhi) { if (d < dlo) slot_set[k] = 0; continue; }
            if (slot_set[k] != a) { slot_set[k] = a; slot_off[k] = offs[next++ % offs.size()]; }
            ww(s + 0x34, lane + slot_off[k]);
        }
    }
}
