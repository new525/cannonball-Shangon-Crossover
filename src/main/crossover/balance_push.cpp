/***************************************************************************
    BALANCE, curve push: see balance.hpp, balance_push.hpp and docs/balance-push.md.

    Each vehicle's curve push comes from its own game's code (OutRun's set_curve_adjust for the
    Ferrari, Super Hang-On's centrifugal task for its bike, OutRunners' heading change for MAD POWER),
    tuned there to that game's courses. On another game's course the push the vehicle gives is
    multiplied by blend(full), where full makes its demand (the push at its top speed as a fraction of
    its steering's largest lateral speed, both per tick) what it is on its own game's courses.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#include <cmath>
#include <cstdlib>

#include "crossover/balance_push.hpp"
#include "crossover/harness.hpp"
#include "crossover/shmode.hpp"
#include "crossover/homode.hpp"
#include "crossover/ormode.hpp"
#include "crossover/shboard.hpp"
#include "crossover/hoboard.hpp"
#include "crossover/orboard.hpp"
#include "crossover/shferrari.hpp"
#include "crossover/hovehicle.hpp"
#include "crossover/orvehicle.hpp"
#include "engine/oferrari.hpp"
#include "engine/oinitengine.hpp"
#include "engine/oroad.hpp"
#include "engine/oinputs.hpp"

namespace balance
{
    // Full factor per (vehicle, course): r = D_own / D_host, the vehicle's demand on its own game's courses
    // over its demand on this course (docs/balance-push.md). D is the demand averaged along the course's
    // curves (bins of 8 positions or segments where it is 0.03 or more: the demand summed over the curves over
    // their length), measured with CB_PUSH_LOG at BALANCE OFF: the push each vehicle's own code gives at its
    // top speed (no supercharger; MAD POWER automatic) over its full-lock steering per tick (the Ferrari 18 per
    // 30 Hz tick, either bike 15 per 60 Hz frame, MAD POWER 26 per tick). Own courses: the Ferrari 0.792
    // (OutRun's 15 stages), Super Hang-On's bike 0.713 (its four courses), MAD POWER 1.029 (OutRunners' Kenya
    // and full course).
    // MAD POWER's push per tick is the heading change it crosses per tick, which BALANCE's pace scales (at
    // FULL measured 0.659, 0.540, 0.621 of OFF against pace 0.650, 0.532, 0.614), so its factor is
    // r / pace(v, h) and blend() of it gives r ^ strength with the pace in force. The others' push per tick
    // does not change with pace (0.995-1.004 of OFF measured).
    // 1 on a vehicle's own course, and where a vehicle never races (Hang-On's bike away from its course).
    double push(Vehicle v, Host h)
    {
        if (native(v, h)) return 1.0;
        switch (h)
        {
            case OUTRUN:            // D_host over OutRun's 15 stages
                if (v == SH_BIKE)   return 0.990;               // 0.713 / 0.721; stage by stage 0.83-1.45
                if (v == MAD_POWER) return 0.810 / pace(v, h);  // 1.029 / 1.271; stage by stage 0.66-1.24
                break;
            case SUPER_HANG_ON:     // over its four courses
                if (v == FERRARI)   return 0.936;               // 0.792 / 0.846; course by course 0.86-0.98
                if (v == MAD_POWER) return 0.795 / pace(v, h);  // 1.029 / 1.296; course by course 0.75-0.84
                break;
            case HANG_ON:           // over its course
                if (v == FERRARI)   return 0.847;               // 0.792 / 0.935
                if (v == MAD_POWER) return 0.827 / pace(v, h);  // 1.029 / 1.244
                if (v == SH_BIKE)   return 0.993;               // 0.713 / 0.718
                break;
            case OUTRUNNERS:        // over Kenya and the full course
                if (v == FERRARI)   return 1.334;               // 0.792 / 0.594; Kenya 1.14, full course 1.39
                if (v == SH_BIKE)   return 1.268;               // 0.713 / 0.562; Kenya 1.27, full course 1.27
                break;
        }
        return 1.0;
    }

    Host host()
    {
        if (ormode.active) return OUTRUNNERS;
        if (homode.active) return HANG_ON;
        if (shmode.active) return SUPER_HANG_ON;
        return OUTRUN;
    }

    static double push_carry[4] = {};
    void reset_push() { for (double& c : push_carry) c = 0; }

    int push_scaled(Vehicle v, int p)
    {
        double* carry = push_carry;
        const double f = blend(push(v, host()));
        if (f == 1.0) return p;
        if (p == 0) return 0;                                   // no push, none given (the fraction waits)
        double want = p * f + carry[v];
        long out = std::lround(want);
        carry[v] = want - out;
        return (int) out;
    }

    // ------------------------------------------------------------------------------------------------
    // Measurement log
    // ------------------------------------------------------------------------------------------------
    FILE* push_log()
    {
        static FILE* f = getenv("CB_PUSH_LOG") ? fopen(getenv("CB_PUSH_LOG"), "w") : nullptr;
        return f;
    }

    static ORBoard* or_board = nullptr;

    // CB_PUSH_HOLD=1 (with CB_PUSH_LOG): a board's own vehicle is held at the road's centre and its timer
    // held, so that it covers its course at top speed while its push is logged (the push it computes
    // depends on its position along the course and its speed, not on its lateral position);
    // CB_PUSH_HOLD=2: the timer only (the vehicle steered by the harness's "steerto <x> road")
    static int hold() { static int h = getenv("CB_PUSH_HOLD") ? atoi(getenv("CB_PUSH_HOLD")) : 0; return h; }

    static inline uint16_t sh_rw(uint32_t a)
    {
        const uint8_t* p = nullptr;
        if (a >= 0x260000 && a < 0x268000) p = shboard.cpu1ram + (a - 0x260000);
        else if (a >= 0x060000 && a < 0x068000) p = shboard.workram + (a - 0x060000);
        return p ? (uint16_t)(p[0] << 8 | p[1]) : 0;
    }
    static inline uint16_t ho_rw(uint32_t a)
    {
        const uint8_t* p = nullptr;
        if (a >= 0xC7C000 && a < 0xC80000) p = hoboard.subram + (a - 0xC7C000);
        else if (a >= 0x20C000 && a < 0x210000) p = hoboard.workram + (a - 0x20C000);
        return p ? (uint16_t)(p[0] << 8 | p[1]) : 0;
    }
    static inline uint16_t or_rw(ORBoard* b, uint32_t a) { return (uint16_t)(b->read8(a) | b->read8(a + 1) << 8); }

    // Super Hang-On's and Hang-On's position on the course ($260030, $C7E030: the integer part),
    // OutRunners' segment count ($209042), OutRun's road position
    int log_pos()
    {
        switch (host())
        {
            case SUPER_HANG_ON: return (int16_t) sh_rw(0x260030);
            case HANG_ON:       return (int16_t) ho_rw(0xC7E030);
            case OUTRUNNERS:    return or_board ? or_rw(or_board, 0x209042) : -1;
            default:            return (int) (oroad.road_pos >> 16);
        }
    }

    void ferrari_curve_adjust()
    {
        FILE* log = push_log();
        const int16_t x0 = oinitengine.car_x_pos;
        oferrari.set_curve_adjust();
        const int raw = (int16_t)(oinitengine.car_x_pos - x0);
        const int out = push_scaled(FERRARI, raw);
        if (out != raw) oinitengine.car_x_pos = (int16_t)(x0 + out);
        if (log)
        {
            int16_t xd = oroad.road_x[170] - oroad.road_x[511];      // as set_curve_adjust reads it
            if (oinitengine.rd_split_state && x0 < 0) xd = -xd;
            fprintf(log, "F %d %d %d %d %d %d %d %d %d %d %d %d %d\n", harness.frame, (int) host(), log_pos(), (int)(oinitengine.car_increment >> 16),
                    x0, xd, raw, out, oinputs.steering_adjust, oferrari.steering_old, oinitengine.road_type, oinitengine.road_curve,
                    oferrari.wheel_state);
        }
    }

    // Super Hang-On's centrifugal task ($63CE) as it computes its push, from the board's RAM: the road
    // table ($260800 + $2607FE) at $A6 (with the supercharger $8C) less at $1B8, / 64, x speed, / the
    // task's divisor (+$1E), plus 1 in the quotient's direction; nothing if X would pass 500
    void push_frame_sh()
    {
        FILE* log = push_log();
        if (!log || shferrari.enabled) return;
        const uint32_t a0 = 0x260800 + (int16_t) sh_rw(0x2607FE);
        const bool sc = (shboard.workram[0x0412] != 0);
        const int16_t dx = (int16_t)(sh_rw(a0 + (sc ? 0x8C : 0xA6)) - sh_rw(a0 + 0x1B8));
        const int16_t speed = (int16_t) sh_rw(0x260078), x = (int16_t) sh_rw(0x260022);
        const int16_t div = (int16_t) sh_rw(0x063A40 + 0x1E);
        int p = 0;
        int16_t q = (int16_t)(dx / 0x40);
        if (q && div)
        {
            int16_t r = (int16_t)((int32_t) speed * q / div);
            p = r + (r < 0 ? -1 : 1);
            if (std::abs(x + p) > 500) p = 0;
        }
        fprintf(log, "S %d %d %d %d %d %d %d %d %d %d\n", harness.frame, (int) SUPER_HANG_ON, (int16_t) sh_rw(0x260030), speed, x, dx, p, p,
                (int16_t) sh_rw(0x064080 + 0x34), sc ? 1 : 0);
        if (hold() && sh_rw(0x060408))      // in a game: X at the centre ($260022), the timer ($60500) held
        {
            if (hold() == 1) { shboard.cpu1ram[0x22] = 0; shboard.cpu1ram[0x23] = 0; }
            shboard.workram[0x500] = 0x50;
        }
    }

    // Hang-On's centrifugal task ($879A): its road table ($C7E800) at $4C less at $DE, / 64, x speed
    // ($C7E078), / the task's divisor ($20CA80 + $1E), plus 1 in the quotient's direction; nothing if X
    // ($C7E050) would pass $10F64 (500)
    void push_frame_ho()
    {
        FILE* log = push_log();
        if (!log || hovehicle.enabled) return;
        const int16_t dx = (int16_t)(ho_rw(0xC7E84C) - ho_rw(0xC7E8DE));
        const int16_t speed = (int16_t) ho_rw(0xC7E078), x = (int16_t) ho_rw(0xC7E050);
        const int16_t div = (int16_t) ho_rw(0x20CA80 + 0x1E);
        int p = 0;
        int16_t q = (int16_t)(dx / 0x40);
        if (q && div)
        {
            int16_t r = (int16_t)((int32_t) speed * q / div);
            p = r + (r < 0 ? -1 : 1);
            if (std::abs(x + p) > 500) p = 0;
        }
        fprintf(log, "H %d %d %d %d %d %d %d %d %d %d %d\n", harness.frame, (int) HANG_ON, (int16_t) ho_rw(0xC7E030), speed, x, dx, p, p,
                (int16_t) ho_rw(0x20D080 + 0x34), (int16_t) ho_rw(0xC7E068), div);
        if (hold() && ho_rw(0x20C408))      // in a game: X at the centre ($C7E050), the timer ($20C500) held
        {
            if (hold() == 1) { hoboard.subram[0x2050] = 0; hoboard.subram[0x2051] = 0; }
            hoboard.workram[0x500] = 0x50;
        }
    }

    // OutRunners' car ($A4D5-$A50E): the push of its next tick from the heading at the car ($20048E) and
    // the one before ($200490), divided by 1 + catch-up ($20906A) / 256. Logged once per car tick (when
    // the position in the segment, $209034, changes).
    void push_frame_or(ORBoard* b)
    {
        or_board = b;
        FILE* log = push_log();
        if (!log || orvehicle.enabled) return;
        if (hold() && b->read8(0x20040C) == 2)  // racing: lateral at the centre ($209038), the countdown ($20FB6A) held
        {
            if (hold() == 1) { b->write8(0x209038, 0); b->write8(0x209039, 0); }
            b->write8(0x20FB6A, 1200 & 0xFF); b->write8(0x20FB6B, 1200 >> 8);
        }
        static uint32_t last = 0xFFFFFFFF;
        const uint32_t pos = or_rw(b, 0x209034) | (uint32_t) or_rw(b, 0x209036) << 16;
        if (pos == last) return;
        last = pos;
        const uint16_t cur = or_rw(b, 0x20048E), prev = or_rw(b, 0x200490);
        int16_t h8 = (int16_t)(uint16_t)((uint16_t)((uint16_t)(prev << 4) - (uint16_t)(cur << 4)) * 0x20);
        h8 = (int16_t)(h8 / 0x100);
        const int catchup = or_rw(b, 0x20906A);
        const int p = (int)(((int32_t) h8 << 8) / (catchup + 0x100));
        fprintf(log, "R %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n", harness.frame, (int) OUTRUNNERS, or_rw(b, 0x209042), or_rw(b, 0x209074),
                (int16_t) or_rw(b, 0x209038), h8, p, p, (int16_t) or_rw(b, 0x200496), (int16_t) or_rw(b, 0x20042E),
                b->read8(0x209067), (int16_t) or_rw(b, 0x2004B8), catchup, b->read8(0x20040C));
    }
}
