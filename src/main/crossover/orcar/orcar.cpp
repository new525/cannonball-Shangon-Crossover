/***************************************************************************
    OutRunners' player car: the handling, transcribed from the V70 program. See orcar.hpp and
    docs/OUTRUNNERS-RE.md section 13. Comments give the address of each instruction transcribed;
    "rN" are the V70's registers where the code keeps a value in one.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#include "crossover/orcar/orcar.hpp"

namespace orcar
{

// ------------------------------------------------------------------------------------------------
// The V70's arithmetic (MAME 0.264's V60 core)
// ------------------------------------------------------------------------------------------------
static inline int32_t  sx16(uint32_t v) { return (int16_t)(uint16_t) v; }
static inline int32_t  sx8(uint32_t v)  { return (int8_t)(uint8_t) v; }
static inline uint32_t lo16(uint32_t r, uint32_t v) { return (r & 0xFFFF0000u) | (v & 0xFFFFu); }   // a .h write to a register
static inline uint32_t sar32(uint32_t v, int n) { return (uint32_t)((int32_t) v >> n); }            // sha.w #-n (n < 32)
// div.w: signed, truncating; a division by 0 (or $80000000 / -1) leaves the dividend
static inline uint32_t divw(uint32_t a, uint32_t b)
{
    if (b == 0 || (a == 0x80000000u && b == 0xFFFFFFFFu)) return a;
    return (uint32_t)((int32_t) a / (int32_t) b);
}
static inline uint16_t divh(uint16_t a, uint16_t b)
{
    if (b == 0 || (a == 0x8000u && b == 0xFFFFu)) return a;
    return (uint16_t)((int16_t) a / (int16_t) b);
}
static inline uint32_t divuw(uint32_t a, uint32_t b) { return b ? a / b : a; }
static inline uint16_t divuh(uint16_t a, uint16_t b) { return b ? (uint16_t)(a / b) : a; }
// The approach step, 32-bit: mov.w Rd, R2 / sha.w #E0, R2 / div.w #n, Rd / add.w R2, R2 / inc.w R2 /
// add.w R2, Rd: the difference / n, truncated, plus its sign
static inline uint32_t step32(uint32_t d, uint32_t n)
{
    uint32_t r2 = ((int32_t) d < 0) ? 0xFFFFFFFFu : 0;
    return divw(d, n) + (r2 + r2 + 1);
}
// The same on halfwords (sha.h #F0 / div.h / add.h / inc.h / add.h)
static inline uint16_t step16(uint16_t d, uint16_t n)
{
    uint16_t r1 = ((int16_t) d < 0) ? 0xFFFFu : 0;
    return (uint16_t)(divh(d, n) + (uint16_t)(r1 + r1 + 1));
}

// ROM tables (docs/OUTRUNNERS-RE.md 6.1)
static const uint32_t SINE = 0x100C00, COSINE = 0x101400, ATAN = 0x103400;
static const uint32_t ATAN_MUL = 0x12454, ATAN_ADD = 0x12444;
static const uint32_t CAR_PARAMS = 0xB038, GEAR_TABLES = 0xB2B8, TORQUE_CURVES = 0xB32C;
static const uint32_t ACCEL_MAP = 0xAE37, BRAKE_MAP = 0xAF37;
static const uint32_t SHAKER = 0xB970;              // the shaker's value off the road, per car
static const uint32_t ZONE_RIGHT = 0xB978, ZONE_LEFT = 0xB9B8;   // jump tables of the surface test
static const uint32_t GRID_SEG = 0x9438, GRID_LAT = 0x9318;     // start grid: segment, lateral (per player count, per rank)
static const uint32_t CRASH_PARAMS = 0x82B6;        // per car: restitution, speed decay, push decay, gravity, response table
static const uint32_t WEIGHTS = 0x82A4, RANK_OFFSET = 0x8292;
static const uint32_t ANIM = 0x34138, ANIM_CMDS = 0xF5B8;
static const uint32_t TIMEUP_BRAKE = 0x979C, GOAL_SCRIPT = 0x97AC;
// the sprite frame ($7A7A): base frame, wheelspin frame, slides (left / right), steering frame, body
// palette index, the goal run's stop frame (per car; the slides' and steering frame's by value)
static const uint32_t FRAME_BASE = 0x94D8, FRAME_SPIN = 0x956C, FRAME_SLIDE_L = 0x957C, FRAME_SLIDE_R = 0x967C;
static const uint32_t FRAME_STEER = 0x9528, PAL_INDEX = 0x977C, FRAME_STOP = 0x978C;
// the sounds: the car's engine ($12292[car]), its tyre sounds when the wheels lock ($AE2F[car]) and
// off the road ($B968[car])
static const uint32_t SND_ENGINE = 0x12292, SND_LOCK = 0xAE2F, SND_GRAVEL = 0xB968;

int32_t ORCar::sin_(uint32_t i) const { return sx16(r16(SINE + 2 * i)); }
int32_t ORCar::cos_(uint32_t i) const { return sx16(r16(COSINE + 2 * i)); }

// $12318: the angle (12-bit) of the vector (r0 - r2, r1 - r3), from the arc tangent table
static uint32_t atan2_12318(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                            uint16_t (*rd16)(const void*, uint32_t), const void* ctx)
{
    uint32_t r4 = 0;                                                // 12318
    r0 -= r2;                                                       // 1231B
    if ((int32_t) r0 < 0) { r0 = 0u - r0; r4 = (r4 & ~0xFFu) | ((r4 + 1) & 0xFF); }    // 12320
    r1 -= r3;                                                       // 12325
    if ((int32_t) r1 < 0) { r1 = 0u - r1; r4 = (r4 & ~0xFFu) | ((r4 + 2) & 0xFF); }    // 1232A
    if ((int64_t)(int32_t) r0 - (int32_t) r1 < 0) { uint32_t t = r0; r0 = r1; r1 = t; r4 = (r4 & ~0xFFu) | ((r4 + 4) & 0xFF); }  // 12330
    if (r0 == 0) return r0;                                         // 1233B tb
    r1 <<= 10;                                                      // 1233F
    r2 = r0 >> 1;                                                   // 12342
    r1 += r2;                                                       // 12349
    r1 = divuw(r1, r0);                                             // 1234C
    r0 = rd16(ctx, ATAN + 2 * r1);                                  // 12352
    r0 = lo16(r0, (uint16_t)(r0 * rd16(ctx, ATAN_MUL + 2 * (r4 & 0xFF))));     // 1235A
    r0 = lo16(r0, (uint16_t)(r0 + rd16(ctx, ATAN_ADD + 2 * (r4 & 0xFF))));     // 12362
    r0 = lo16(r0, r0 & 0xFFF);                                      // 1236A
    return r0;
}

static uint16_t rom16(const void* ctx, uint32_t a)
{
    const uint8_t* rom = (const uint8_t*) ctx;
    return (uint16_t)(rom[a & 0x1FFFFF] | rom[(a + 1) & 0x1FFFFF] << 8);
}

// ------------------------------------------------------------------------------------------------
// The tick ($715C)
// ------------------------------------------------------------------------------------------------
void ORCar::tick(const TickInput& in, const Road& road)
{
    unsupported_pc = 0;
    snd = SoundOut();
    if (in.link) { unsupported_pc = 0x71B5; return; }              // $20E623: the linked mode's paths
    s.rec_markers = in.rec_markers;
    s.done_flags = in.flags94;
    s.shaker = 0;                                                   // 7160: mov.b #0, D0[R24](R0)
    // 7167 bsr 76AF: race order (its result, 68[$208000], is in TickInput.ref_rec_seg)
    // 716A jsr A304: the inputs
    if (in.demo) { unsupported_pc = 0xA325; return; }               // A304: the demo driver
    s.in_accel = in.accel;                                          // A30C
    s.in_brake = in.brake;                                          // A314
    s.in_steer = in.steer;                                          // A31C
    // 7170 jsr 124A4: the collision boxes (the board's); a contact reported since the last tick
    if (in.hit_at_entry) s.flags |= 4;
    auto set_state = [&](uint8_t st) { s.state = st; s.sub = 0; s.st_e = 0; s.st_f = 0; };  // mov.w #n, C[R20]

    switch (s.state)                                                // 7176: table $7184
    {
    case 0:
        state0(in, road);
        return;
    case 1:                                                         // 72F4
        if (--s.timer != 0)
        {
            s.in_brake = 0xFF;                                      // 72F9
            s.in_steer = 0x80;                                      // 7300
            handling(in, road);                                     // 7307 br 7358
            return;
        }
        s.state++;                                                  // 7309
        s.moving = 0xFF;                                            // 730C
        // 7311: (not the demo) $20FB5D and $20831C := $FF (not the car's motion)
        // fall into state 2
        [[fallthrough]];
    case 2:                                                         // 7325
        if (in.time_up) { set_state(3); goto state3; }              // 7325 -> 739B
        if (in.goal) { set_state(4); goto state4; }                 // 732B -> 7467
        if (s.grace != 0 && --s.grace != 0) goto t7342;             // 7332
        s.flags &= 0xFFF7;                                          // 733C
    t7342:
        // 7342 jsr 127DB: the car-to-car test (the board's); 7348: the contact flag
        if (in.hit) s.flags |= 4;
        if (s.flags & 4) { collision(in, road); return; }           // 7348 -> 7F87
        handling(in, road);                                         // 734F
        return;
    case 3:
    state3:                                                         // 73A0
        if (in.goal) { set_state(4); goto state4; }                 // 73A0 -> 7467
        s.in_accel = 0;                                             // 73A7
        s.in_brake = r8_(TIMEUP_BRAKE + (uint8_t)(s.car + s.car + s.trans));    // 73AD-73B8
        switch (s.sub)                                              // 73C3: table $73D0
        {
        case 0:                                                     // 73E0
            s.sub++;
            s.timer = 0xB4;
            clear_contact();                                        // 73EA bsr 7A4B
            [[fallthrough]];
        case 1:                                                     // 73ED
            if (--s.timer != 0) { handling(in, road); return; }     // 73F2: 734F or 745A, both run 7358
            s.sub++;                                                // 73FA
            s.timer = 0x3C;
            s.game_over = 0xFF;                                     // 7404
            // 740A-7423: the GAME OVER object (not the car)
            goto t7434;
        case 2:
        t7434:                                                      // 7434
            if (--s.timer != 0) { handling(in, road); return; }
            s.sub++;                                                // 7439
            s.done_flags |= 0x10;                                   // 743C: or.h #10, 94[R25]
            {
                // 7444-7454: the time-up's sound by the stage ($2080A4 > 3: $37, else $36), through $1966E: its
                // table $197E2 gives the command (the entry's top byte; the rest to F30[R25], not the car's)
                uint8_t r0 = in.stage > 3 ? 0x37 : 0x36;
                sound_queue((uint8_t)(r32(0x197E2 + 4u * r0) >> 24));
            }
            handling(in, road);
            return;
        case 3:                                                     // 745A
            handling(in, road);
            return;
        default:
            unsupported_pc = 0x73C3;
            return;
        }
    case 4:
    state4:                                                         // 746C: table $7478
        switch (s.sub)
        {
        case 0:                                                     // 7490
            s.sub++;
            s.goal_flag = 0xFF;                                     // 7493
            s.rec_markers++;                                        // 7499: inc.b 60[24[R25]]
            s.goal_seg = s.seg_count2;                              // 749D
            clear_contact();                                        // 74A4
            s.flags |= 8;                                           // 74A7
            handling(in, road);                                     // 74AB
            return;
        case 1:                                                     // 74AE
        {
            uint32_t r0 = in.finish_time;                           // 74B2
            if (r0 == 0) { handling(in, road); return; }            // 74B8 tb
            s.rec_finish = (uint16_t) r0;                           // 74BC
            // 74C1: 4[R23] := time x 10 (not the car's motion)
            s.rec_goal = 0xFF;                                      // 74C8
            s.sub++;                                                // 74CE
            uint32_t p = r32(GOAL_SCRIPT + 4 * in.rank);            // 74D1-74DB
            s.goal_seg = (uint16_t)(s.goal_seg + r16(p));           // 74E3
            s.goal_lat = (int16_t) r16(p + 2);                      // 74E9
            s.goal_speed = r16(p + 4);                              // 74EF
            s.goal_steer = s.in_steer;                              // 74F5
            s.goal_accel = s.in_accel;                              // 74FD
            goto t7505;
        }
        case 2:
        t7505:                                                      // 7505
            if (s.goal_seg > s.seg_count2) { goal_drive(); handling(in, road); return; }   // 750C bnh
            s.sub++;                                                // 7514
            s.goal_speed = 0;                                       // 7517
            anim_set_f5d2(r16(FRAME_STOP + 2u * s.car));            // 751D-7529: the car's stop frame
            goto t752f;
        case 3:
        t752f:                                                      // 752F
            s.in_accel = 0x40;
            s.in_brake = 0;
            s.in_steer = 0x80;
            goal_drive();                                           // 7543
            sound_tyre(r8_(SND_LOCK + s.car));                      // 7546-7552
            if (s.speed != 0)                                       // 7558
            {
                a39c(in, road);                                     // 75B2-75BB
                s.wheel[1] = 2;                                     // 75C1
                s.wheel[2] = 2;
                s.wheel[0] = 3;
                s.braking = 2;                                      // 75D3
                goto t75d8;
            }
            s.sub++;                                                // 755D
            s.timer = 0x5A;
            s.in_accel = 0;                                         // 7567
            s.in_brake = 0x80;
            s.in_steer = 0x80;
            goto t757b;
        case 4:                                                     // 7567
            s.in_accel = 0;
            s.in_brake = 0x80;
            s.in_steer = 0x80;
        t757b:                                                      // 757B
            if (--s.timer == 0) { s.sub++; s.done_flags |= 0x10; }  // 7580-7583
            a39c(in, road);                                         // 75A1
            goto t75d8;
        case 5:                                                     // 758D
            s.in_accel = 0;
            s.in_brake = 0x80;
            s.in_steer = 0x80;
            a39c(in, road);                                         // 75A1
        t75d8:
            camera_flag(in);                                        // 75DE bsr 7B74 (75D8: A89E, the camera)
            // 75E1-75F0: $79A0, the car record, the road; 75F6 jsr 121E8: the engine's parameters
            s.snd_engine = (uint8_t)(s.rpm >> 8);
            s.snd_ctrl = 0x20;
            anim_step_f579();                                       // 75FC jmp F579
            return;
        default:
            unsupported_pc = 0x7470;
            return;
        }
    case 5:                                                         // 819D
        crash_tick(in);                                             // both ends go to 734F
        handling(in, road);
        return;
    default:
        unsupported_pc = 0x717A;
        return;
    }
}

// State 0 ($719C): the car's place on the grid and its handling's set-up
void ORCar::state0(const TickInput& in, const Road& road)
{
    // 719C-71DD: the camera's parameters (10-18[R25]), $79A0
    s.state++;                                                      // 71E3
    // 71E6: 0[R23] := R20; 71EA jsr FF28: the course's first segments (the ring)
    uint32_t r0 = 0x10;                                             // 71F9
    s.seg_count = 0;                                                // 7200
    uint32_t r1 = in.rank;                                          // 7205-7209
    uint32_t r2 = 0;                                                // 720F
    if (!in.demo) r2 = in.players_m1;                               // 7212-7221
    uint32_t r3 = r32(GRID_SEG + 4 * r2);                           // 7223
    r0 = lo16(r0, r0 + r16(r3 + 2 * r1));                           // 722B
    s.seg_count2 = (uint16_t) r0;                                   // 722F
    r0 = lo16(r0, r0 * 0x20);                                       // 7233
    s.ring_off = (uint16_t) r0;                                     // 7238
    uint16_t off = (uint16_t)(r0 & 0xFFF);                          // 723C: R11 = $20A000 + it
    r3 = r32(GRID_LAT + 4 * r2);                                    // 723F
    r1 = r32(r3 + 4 * r1);                                          // 7247
    s.lateral = (int16_t) r1;                                       // 724B: mov.w R1, 38[R23] (38 and 3A)
    s.seg_rest = (uint16_t)(r1 >> 16);
    Segment seg = road.segment(off);
    s.heading = seg.heading;                                        // 724F
    s.pitch_init = seg.pitch;                                       // 7255
    r1 += (uint32_t) seg.x;                                         // 725B
    s.wx = (int32_t) r1;                                            // 725E
    s.wz = seg.z;                                                   // 7262
    s.wy = seg.y;                                                   // 7267
    s.pal_index = r16(PAL_INDEX + 2u * s.car);                      // 726C-7270: the body's palette index
    s.timer = 8;                                                    // 7278
    if (!in.demo) s.timer = 0x95;                                   // 7285 (728C, 7294: the camera)
    // 729C jsr A278: the car's parameters
    s.params = r32(CAR_PARAMS + 4 * s.car);                         // A27C
    uint32_t i = (uint32_t) s.trans + 2u * s.car;                   // A284-A288
    s.torque_ptr = r32(TORQUE_CURVES + 4 * i);                      // A28C
    uint32_t p = r32(GEAR_TABLES + 4 * i);                          // A296
    s.gears_m1 = r8_(p++);                                           // A29E
    s.gears_ptr = p;                                                // A2A3
    s.ratio = r8_(p);                                                // A2A7
    // A2AC: 58[R23] := R12 (above); A2B0 jmp B764: the surface zones
    b764(road);
    // 72A2 jsr A2B6: the torque table, one curve per gear interpolated to 256 entries
    {
        uint32_t r10 = 0;                                           // A2B6: $209300
        uint32_t r11 = s.torque_ptr;                                // A2BB
        uint32_t r9 = (uint32_t) s.gears_m1 + 1;                    // A2BF
        uint16_t* t = &s.torque[0][0];
        do
        {
            uint32_t r12 = r11 + 4;                                 // A2C5
            do
            {
                uint32_t a0 = r16(r11), a1 = r16(r11 + 2); r11 += 4;    // A2C9, A2CC
                uint32_t b0 = r16(r12), b1 = r16(r12 + 2); r12 += 4;    // A2CF, A2D2
                a1 <<= 16; b1 <<= 16;                               // A2D5, A2D9
                b1 -= a1;                                           // A2DD
                b0 -= a0;                                           // A2E0
                b1 = divw(b1, b0);                                  // A2E3
                do
                {
                    if (r10 < 8 * 256) t[r10] = (uint16_t)(a1 >> 16);   // A2E6-A2ED
                    else { unsupported_pc = 0xA2ED; return; }
                    r10++;
                    a1 += b1;                                       // A2F0
                } while (--b0 != 0);                                // A2F3 dbr
            } while (sx16(r16(r12)) >= 0);                          // A2F7 test.h [R12]; bp
            r11 = r12 + 4;                                          // A2FB
        } while (--r9 != 0);                                        // A2FF
    }
    // 72A8 bsr 7A10: the world position from the grid position
    s.pos_hi = 0;                                                   // 7A10
    s.lateral = (int16_t)(uint16_t) s.wx;                           // 7A15: mov.h 20[R20], 38[R23]
    {
        uint16_t r11 = s.ring_off;                                  // 7A1B-7A20
        int32_t r13 = (uint16_t) s.lateral;                         // 7A24: movz.hw
        int32_t r17 = 0x80;                                         // 7A28
        int32_t r15 = s.pos_hi;                                     // 7A2F
        uint16_t r12 = (uint16_t)((r11 + 0x20) & 0xAFFF);           // 7A36-7A3A
        a7cc(road, r11, r12, r13, r15, r17);                        // 7A3F jsr A7A3 (-> A7CC)
    }
    // 7A45 jmp A89E (camera); 72AB-72BF: $79A0, $FF8E, $102CE
    // 72C5 jsr 7F5D: the crash parameters
    p = r32(CRASH_PARAMS + 4 * s.car);                              // 7F5D-7F61
    s.crash_restitution = r16(p);                                   // 7F69
    s.crash_speed_decay = r16(p + 2);                               // 7F6F
    s.crash_push_decay = r16(p + 4);                                // 7F75
    s.crash_gravity = r16(p + 6);                                   // 7F7B
    s.crash_table = p + 8;                                          // 7F81
    // 72CB-72E0: the race's countdown
    // 72E5 jsr 1219C: the engine's parameters at the start
    s.snd_car = r8_(SND_ENGINE + s.car);                            // 121B5
    s.snd_box = s.trans ? 0x30 : 0x10;                              // 121C2-121CF
    s.snd_engine = 1;                                               // 121D6
    s.snd_ctrl = 0xA0;                                              // 121DE
    frame_select(in);                                               // 72EB jsr 7A7A
    // 72F1 br 77BD: the car record
}

// $7358: the handling and the tick's tail
void ORCar::handling(const TickInput& in, const Road& road)
{
    a39c(in, road);                                                 // 7358-7361
    // 7367 jsr A89E: the camera
    if (s.state == 5) s.wheel[0] = 0;                               // 736D
    // 737A bsr 7B74: the camera's dip and lag; its flag for the sprite object only ($209145)
    camera_flag(in);
    // 737D-738C: $79A0, $77BD (the car record), $FF8E, $102CE
    // 7392 jsr 121E8: the engine's parameters
    s.snd_engine = (uint8_t)(s.rpm >> 8);                           // 121FD-12205
    s.snd_ctrl = 0x20;                                              // 1220C
    frame_select(in);                                               // 7398 br 7A7A
}

// $7A7A: the car's sprite frame (38[R20]) and sub-frame (40[R20]): with a contact, the animation
// (the crash's frames) steps; sliding, a slide frame by its direction and amount; spinning the
// wheels with the force forward, the wheelspin frame; otherwise the base frame for the hill, its
// sub-frame the steering frame, which follows the steering angle one step a tick
void ORCar::frame_select(const TickInput& in)
{
    uint32_t r9 = s.car;                                            // 7A7A
    if (s.flags & 4) { anim_step_f579(); return; }                  // 7A7E -> 7B39 jmp F579
    if (s.sliding)                                                  // 7A85
    {
        uint32_t r8 = (uint16_t) s.slide;                           // 7ADA movz.hw B8[R20]
        r9 = lo16(r9, r9 << 4);                                     // 7ADF shl.h #4
        uint8_t b8 = (uint8_t) r8;
        if ((int8_t) s.in_steer < 0)                                // 7AE2 test.b D2[R20]; bge 7AFC
        {
            if ((int8_t) b8 < 0) b8 = (uint8_t)(0u - b8);           // 7AE8-7AEC
            r9 = (r9 & ~0xFFu) | ((r9 + b8) & 0xFF);                // 7AEF add.b
            r9 = r16(FRAME_SLIDE_L + 2 * (r9 & 0xFFFF));            // 7AF2
        }
        else
        {
            if ((int8_t) b8 > 0) b8 = (uint8_t)(0u - b8);           // 7AFC-7B00
            b8 = (uint8_t)(0u - b8);                                // 7B03
            r9 = (r9 & ~0xFFu) | ((r9 + b8) & 0xFF);                // 7B06 add.b
            r9 = r16(FRAME_SLIDE_R + 2 * (r9 & 0xFFFF));            // 7B09
        }
        goto t7b11;
    }
    if (s.force > 0 && s.spin)                                      // 7A8A-7A93: test.h 94[R20]; ble
    {
        r9 = r16(FRAME_SPIN + 2 * r9);                              // 7AD0
        goto t7b11;
    }
    {
        uint16_t r0 = r16(FRAME_BASE + 2 * r9);                     // 7A95
        r0 = (uint16_t)(r0 + hill_index(in.camera_pitch_now, in));  // 7A9D-7AA0
        s.anim_frame = r0;                                          // 7AA3
        uint8_t t = r8_(FRAME_STEER + (uint32_t)(int32_t) s.steer_angle);   // 7AA7-7AAC (a signed index)
        uint8_t f = s.steer_frame;                                  // 7AB4
        if (t != f) f = (uint8_t)(t < f ? f - 1 : f + 1);           // 7AB9-7AC4 (bl: unsigned)
        s.anim_step = f;                                            // 7AC6
        s.steer_frame = f;                                          // 7ACA
        return;                                                     // 7ACF
    }
t7b11:
    r9 = (uint16_t)(r9 + hill_index(in.camera_pitch_now, in));      // 7B11-7B14
    if (s.anim_frame != (uint16_t) r9)                              // 7B17
    {
        s.anim_frame = (uint16_t) r9;                               // 7B1D
        uint8_t b = r8_(r32(ANIM + 4 * (r9 & 0xFFFF)));             // 7B21-7B29
        s.anim_rate = b;                                            // 7B2C
        s.anim_count = b;                                           // 7B30
        s.anim_step = 0;                                            // 7B34
    }
    anim_step_f579();                                               // 7B39 jmp F579
}

// $7B74 (the camera, state E0[R20]: 0 $7B93, 1 $7B97, 2 $7D3D), the part that sets $209145: when the
// speed grew by more than $A0 since the last tick (not spinning, no contact)
void ORCar::camera_flag(const TickInput& in)
{
    if (in.cam_state > 1) return;                                   // 7D3D: not transcribed (the flag is not written)
    if (in.cam_e1) return;                                          // 7B97 -> 7D28
    s.squat = 0;                                                    // 7B9E
    int16_t r0 = (int16_t)(0 - s.speed_copy);                       // 7BA4 neg.h 2C[R20]
    int16_t r1 = s.neg_speed;                                       // 7BA8
    s.neg_speed = r0;                                               // 7BAD
    r0 = (int16_t)(r0 - r1);                                        // 7BB2
    if (s.flags & 4) return;                                        // 7BBA test1 #2, [R20]
    if (r0 < 0 && !s.spin && r0 < -0xA0) s.squat = 0xFF;            // 7BC0, 7BD6-7BE5
}

// $F5D2: a frame's animation from its start (34, 36, 43[R20], the sprite's scale and a count, are
// not the car's motion)
void ORCar::anim_set_f5d2(uint16_t frame)
{
    s.anim_frame = frame;                                           // F5D2
    uint8_t b = r8_(r32(ANIM + 4u * frame));                        // F5D6-F5DE
    s.anim_rate = b;                                                // F5E1
    s.anim_count = b;                                               // F5E5
    s.anim_step = 0;                                                // F5FC
}

// $12124: a sound command into the player's queue ($208800: read index, write index, 32 bytes; the
// frame interrupt's $3F8DA sends one a frame to $700700). The attract demo's filter does not apply
void ORCar::sound_queue(uint8_t cmd)
{
    s.sq[s.sq_wr & 0x1F] = cmd;                                     // 1214A-12152
    s.sq_wr = (uint8_t)((s.sq_wr + 1) & 0x1F);                      // 12156-1215C
    if (snd.ncmd < 8) snd.cmd[snd.ncmd] = cmd;
    snd.ncmd++;
}

// $12235: the tyre/skid sound's number to the sound board ($700718)
void ORCar::sound_tyre(uint8_t num)
{
    snd.tyre = num;                                                 // 1224A
    snd.tyre_writes++;
}

// $1222F: the same, unless the car is off the road
void ORCar::sound_tyre_if_on_road(uint8_t num)
{
    if (s.offroad) return;                                          // 1222F-12234
    sound_tyre(num);
}

// $7A4B
void ORCar::clear_contact()
{
    s.push = 0;                                                     // 7A4B
    s.dspeed = 0;                                                   // 7A51
    s.jump = 0;                                                     // 7A57
    s.jump_v = 0;                                                   // 7A5D
    s.push = 0;                                                     // 7A63
    s.flags &= 0xFFFB;                                              // 7A69
    s.sliding = 0;                                                  // 7A6F
    s.spin = 0;                                                     // 7A74
}

// $7602: the goal run's script: speed to F4 by 1/16, steering towards the lateral target F2
void ORCar::goal_drive()
{
    s.in_brake = 0;                                                 // 7602
    s.in_accel = 0x40;                                              // 7608
    uint32_t r0 = s.speed;                                          // 760F
    uint32_t r1 = s.goal_speed;                                     // 7613
    r1 -= r0;                                                       // 7618
    if (r1 != 0) r0 += step32(r1, 0x10);                            // 761D-7636
    s.speed = (uint16_t) r0;                                        // 7639
    uint8_t b0 = s.goal_steer;                                      // 763D
    int16_t r1h = s.goal_lat;                                       // 7642
    int32_t d = (int32_t) s.lateral - r1h;                          // 7647-764B: sub.h (exact sign for blt)
    if ((uint16_t) d == 0) goto t7678;                              // 764E be
    if (d < 0) goto t7687;                                          // 7650 blt
    if ((int16_t)(uint16_t) d <= 8) goto t7678;                     // 7652 cmp.h #8, R2; ble
    b0++;                                                           // 7657
    if ((int8_t) b0 > (int8_t) 0xCF) b0 = 0xCF;                     // 7659
    if ((int8_t) b0 < (int8_t) 0x90) b0 = 0x90;                     // 7663
    s.goal_steer = b0;                                              // 766D
    s.in_steer = b0;
    return;
t7678:
    s.goal_steer = 0x80;                                            // 7678
    s.in_steer = 0x80;
    return;
t7687:
    if ((int16_t)(uint16_t) d >= -8) goto t7678;                    // 7687 cmp.h #FFF8, R2; bge
    b0--;                                                           // 768E
    if ((int8_t) b0 < 0x10) b0 = 0x10;                              // 7690
    if ((int8_t) b0 > 0x6F) b0 = 0x6F;                              // 769A
    s.goal_steer = b0;                                              // 76A4
    s.in_steer = b0;
}

// ------------------------------------------------------------------------------------------------
// Collisions ($7F87) and the crash ($819D)
// ------------------------------------------------------------------------------------------------
void ORCar::collision(const TickInput& in, const Road& road)
{
    uint32_t r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r19;
    switch (in.hit_kind)                                            // 7F87: table $7F94
    {
    case 3:                                                         // 7FA4: a minor contact
        s.flags &= 0xFFFB;
        s.minor = 0xF;                                              // 7FAA
        handling(in, road);                                         // 7FB0 br 734F
        return;
    case 0:                                                         // 7FB3: scenery
        r19 = s.crash_table;
        r5 = s.speed32;                                             // 7FB8
        if (r5 >= 0xFFFF) r5 = 0xFFFF;                              // 7FBD
        r0 = 0u - r5;                                               // 7FCB
        r5 >>= 7;                                                   // 7FCE
        r8 = r16(RANK_OFFSET + 2 * in.rank);                        // 7FD2-7FDC
        r5 += r8;                                                   // 7FE4
        if (r5 >= 0x1F0) r5 = 0x1F0;                                // 7FE7
        r5 = lo16(r5, r5 & 0x1F0);                                  // 7FF7
        r19 += r5;                                                  // 7FFC
        r8 = r16(r19 + 4);                                          // 7FFF
        r0 *= r8;                                                   // 8003
        r0 = sar32(r0, 8);                                          // 8006
        s.dspeed = (int32_t) r0;                                    // 800A
        r8 = (uint16_t) s.lateral >> 15;                            // 800F-8013
        s.push = (int16_t) r16(r19 + 6 + 2 * r8);                   // 8017
        s.jump_v = (int16_t) r16(r19 + 0xA);                        // 801F
        break;                                                      // 8026 br 8150
    case 1:                                                         // 8029: an object with its own position
    case 2:                                                         // 8074: a car
        if (in.hit_kind == 1)
        {
            r2 = road.segment(s.ring_off).half_width;               // 8030-8039
            r3 = (uint32_t) sx8((uint8_t) in.obj_width);            // 803D
            r2 *= r3;                                               // 8041
            r0 = (uint32_t)(int32_t) s.lateral;                     // 8044
            r1 = (uint32_t)(int32_t) in.obj_lateral;                // 8048
            r1 += r2;                                               // 804C
            r2 = (uint32_t)(int32_t) s.latvel;                      // 804F
            r3 = (uint32_t)(int32_t) in.obj_latvel;                 // 8053
            r4 = s.speed_copy;                                      // 8057
            r5 = in.obj_speed;                                      // 805B
            r5 <<= 8;                                               // 805F
            r6 = in.obj_weight;                                     // 8062
            r0 += r0; r1 += r1; r2 += r2; r3 += r3;                 // 8066-806F
        }
        else
        {
            r0 = (uint32_t)(int32_t) s.lateral;                     // 8081
            r1 = (uint32_t)(int32_t) in.car_lateral;                // 8085
            r2 = (uint32_t)(int32_t) s.latvel;                      // 8089
            r3 = (uint32_t)(int32_t) in.car_latvel;                 // 808D
            r4 = (uint16_t)(s.speed_copy >> 1);                     // 8091, 8099
            r5 = (uint16_t)(in.car_speed >> 1);                     // 8095, 809D
            r6 = r16(WEIGHTS + 2 * in.car_model);                   // 80A1-80A5
        }
        r7 = r16(WEIGHTS + 2 * s.car);                              // 80AD-80B1
        r19 = s.crash_table;                                        // 80B9
        r5 -= r4;                                                   // 80BE
        r4 = r5;                                                    // 80C1
        if ((int32_t) r5 < 0) { r5 = 0u - r5; r5 += r5; }           // 80C4-80CB (a rear hit counts double)
        r5 >>= 8;                                                   // 80CE
        r5 *= r6;                                                   // 80D2
        r5 = divw(r5, r7);                                          // 80D5
        r8 = r16(RANK_OFFSET + 2 * in.rank);                        // 80D8-80E2
        r5 += r8;                                                   // 80EA
        if (r5 >= 0x1F0) r5 = 0x1F0;                                // 80ED
        r5 = lo16(r5, r5 & 0x1F0);                                  // 80FD
        r19 += r5;                                                  // 8102
        r8 = r16(r19 + 0xE);                                        // 8105
        r4 *= r8;                                                   // 8109
        r4 = sar32(r4, 8);                                          // 810C
        r4 *= r6;                                                   // 8110
        r4 = sar32(r4, 8);                                          // 8113
        s.dspeed = (int32_t) r4;                                    // 8117
        r0 -= r1;                                                   // 811C
        r3 -= r2;                                                   // 811F
        r3 <<= 1;                                                   // 8122
        r0 += r3;                                                   // 8125
        r8 = (0u - r0) >> 31;                                       // 8128, 812B
        r9 = (uint32_t) sx16(r16(r19 + 8));                         // 812F
        r0 *= r9;                                                   // 8133
        r0 = sar32(r0, 7);                                          // 8136
        r0 *= r6;                                                   // 813A
        r0 = divw(r0, r7);                                          // 813D
        // 8140 movt.wh R0, CE[R20]; on overflow BRK (a trap on the V70; MAME 0.264 skips it)
        s.push = (int16_t)(uint16_t) r0;
        s.jump_v = (int16_t) r16(r19 + 0xA);                        // 8149
        break;
    default:
        unsupported_pc = 0x7F8B;
        return;
    }
    // 8150: the crash
    s.state = 5; s.sub = 0; s.st_e = 0; s.st_f = 0;                 // 8150 mov.w #5, C[R20]
    s.flags |= 8;                                                   // 8155
    s.crash_snd = r8_(r19 + 0xD);                                   // 8159
    s.crash_snd_wait = 0xA;                                         // 8160
    sound_queue((uint8_t) r16(r19 + 0xC));                          // 8166-816A
    r9 = r16(r19 + 2 * r8);                                         // 8170: the crash frame (by side)
    r1 = hill_index(in.camera_pitch, in);                           // 8175 bsr 7B3F
    r9 = (uint16_t)(r9 + r1);                                       // 8178
    s.anim_frame = (uint16_t) r9;                                   // 817B
    {
        uint32_t p = r32(ANIM + 4 * r9);                            // 817F
        uint8_t b = r8_(p);                                          // 8187
        s.anim_rate = b;                                            // 818A
        s.anim_count = b;                                           // 818E
        s.anim_step = 0;                                            // 8192
    }
    s.crash_dust = 0xFF;                                            // 8197 (the sprite object's crash dust)
    // fall into 819D
    crash_tick(in);                                                 // both ends go to 734F
    handling(in, road);
}

// $819D, state 5. Returns false when the crash ended ($8263: back to state 2)
bool ORCar::crash_tick(const TickInput& in)
{
    if (in.goal) goto t8263;                                        // 819D
    s.shaker = 0x10;                                                // 81A4-81A8
    if (s.crash_snd_wait != 0 && --s.crash_snd_wait == 0 && s.crash_snd != 0)   // 81B0-81C1
        sound_queue(s.crash_snd);                                   // 81C5
    if (s.anim_rate == 0) goto t8263;                               // 81CB
    {
        s.in_accel = 0;                                             // 81D1
        s.in_brake = 0;                                             // 81D7
        s.in_steer = 0x80;                                          // 81DD
        uint32_t r9 = (uint32_t) sx16((uint16_t) s.push);           // 81E4
        uint32_t r8 = s.crash_push_decay;                           // 81E9
        r9 *= r8;                                                   // 81EE
        r9 = divw(r9, 0x100);                                       // 81F1
        s.push = (int16_t)(uint16_t) r9;                            // 81F8
        r9 = (uint32_t) s.dspeed;                                   // 81FD
        r8 = s.crash_speed_decay;                                   // 8202
        r9 *= r8;                                                   // 8207
        r9 = divw(r9, 0x100);                                       // 820A
        s.dspeed = (int32_t) r9;                                    // 8211
        r9 = (uint32_t) sx16((uint16_t) s.jump_v);                  // 8216
        r8 = (uint32_t) s.jump;                                     // 821B
        uint32_t r6 = s.crash_gravity;                              // 8220
        r9 -= r6;                                                   // 8225
        uint32_t r7 = r8;                                           // 8228
        r8 += r9;                                                   // 822B
        r7 ^= r8;                                                   // 822E
        if ((int32_t) r7 < 0)                                       // 8231 bp: the height changed sign: a bounce
        {
            r9 += r6;                                               // 8233
            r8 = 0;                                                 // 8236
            r7 = s.crash_restitution;                               // 8239
            r9 *= r7;                                               // 823E
            r9 = divw(r9, 0xFFFFFF00);                              // 8241
            if (r9 != 0) sound_queue(0x31);                         // 8248-8250: a sound when it bounces
        }
        s.jump_v = (int16_t)(uint16_t) r9;                          // 8256
        s.jump = (int32_t) r8;                                      // 825B
        return true;                                                // 8260 br 734F
    }
t8263:
    s.state = 2; s.sub = 0; s.st_e = 0; s.st_f = 0;                 // 8263 mov.w #2, C[R20]
    s.flags &= 0xFFFB;                                              // 8268
    s.grace = 0xF;                                                  // 826E
    s.push = 0;                                                     // 8276
    s.jump_v = 0;                                                   // 827B
    s.jump = 0;                                                     // 8280
    s.dspeed = 0;                                                   // 8285
    s.in_accel = 0;                                                 // 828A
    return false;                                                   // 828F br 734F
}

// $F579: one step of the car's animation (the crash frames: the crash ends when it stops)
void ORCar::anim_step_f579()
{
    if (s.anim_rate == 0) return;                                   // F579
    s.anim_count--;                                                 // F57E
    if ((int8_t) s.anim_count > 0) return;                          // F581 bgt
    s.anim_count = s.anim_rate;                                     // F583
    s.anim_step++;                                                  // F589
    uint32_t r9 = s.anim_step;                                      // F58C
    uint32_t r8 = r32(ANIM + 4u * s.anim_frame);                    // F590-F5A1
    int32_t v = (int32_t) r32(r8 + 4 + 4 * r9);                     // F5A4
    if (v > 0) return;                                              // F5A8 ble
    uint32_t target = r32(ANIM_CMDS + 4u * (uint32_t)(-v));         // F5AB-F5B0
    switch (target)
    {
    case 0xF5C4:                                                    // stop
        s.anim_rate = 0;
        s.anim_step--;
        return;
    case 0xF5FC:                                                    // loop
        s.anim_step = 0;
        return;
    case 0xF5CD:                                                    // another frame
    {
        r9 = r32(r8 + 8 + 4 * r9);
        s.anim_frame = (uint16_t) r9;                               // F5D2
        uint8_t b = r8_(r32(ANIM + 4 * r9));
        s.anim_rate = b;                                            // F5E1
        s.anim_count = b;                                           // F5E5
        // F5E9-F5F5: 43, 34, 36[R20] (the sprite's scale)
        s.anim_step = 0;                                            // F5FC
        return;
    }
    default:
        unsupported_pc = 0xF5B5;
        return;
    }
}

// $7B3F: the hill index of the car's frame, from the camera's pitch and the road's under the car
uint8_t ORCar::hill_index(uint16_t camera_pitch, const TickInput& in)
{
    uint16_t r1 = 0;                                                // 7B3F
    uint16_t r4 = camera_pitch;                                     // 7B42
    r4 = (uint16_t)(r4 - 0xB0);                                     // 7B46
    uint16_t r2 = in.own_rec_pitch;                                 // 7B4B
    r4 = (uint16_t)(r4 << 4);                                       // 7B50
    r2 = (uint16_t)(r2 << 4);                                       // 7B53
    uint16_t r3 = 5;                                                // 7B56
    r4 = (uint16_t)(r4 - r2);                                       // 7B59
    if (!(r4 & 0x8000))                                             // 7B5C bn
    {
        r4 = divuh(r4, 0x300);                                      // 7B5E
        r1 = r4;                                                    // 7B63
        if (r1 >= r3) r1 = r3;                                      // 7B66 bl
    }
    s.hill = (uint8_t) r1;                                          // 7B6E
    return (uint8_t) r1;
}

// ------------------------------------------------------------------------------------------------
// The handling ($A39C-$A89D)
// ------------------------------------------------------------------------------------------------
void ORCar::a39c(const TickInput& in, const Road& road)
{
    uint16_t r11 = s.ring_off;                                      // R11 = $20A000 + 40[R23] (7358)
    uint32_t r12 = s.params;                                        // A39C
    s.wheel[0] = 0; s.wheel[1] = 0; s.wheel[2] = 0;                 // A3A0-A3AC
    if (s.speed != 0 && s.shift_smoke != 0)                         // A3B2, A3B7
    {
        s.wheel[1] = 0xB; s.wheel[2] = 0xB; s.wheel[0] = 3;          // A3BD-A3C9
    }
    Segment seg = road.segment(r11);
    uint32_t r0 = s.speed;                                          // A3CF
    if (r0 != 0)                                                    // A3D3 tb
    {
        r0 *= r0;                                                   // A3D7
        r0 >>= 16;                                                  // A3DA
        // A3DE-A3E6: R1 lateral, R2 the segment's half-width, R3 the car's (parameter +1A)
        r0 = b79b(in, road, r0, s.lateral, seg.half_width, r16(r12 + 0x1A));    // A3EA
        if (unsupported_pc) return;
    }
    r0 += 0x1800;                                                   // A3F0
    uint32_t r7 = seg.pitch;                                        // A3F7
    s.rec_pitch = (uint16_t) r7;                                    // A3FB: 54[24[R25]]
    r7 = (uint32_t) sin_(r7);                                       // A400
    r7 = sar32(r7, 1);                                              // A408
    r7 -= r0;                                                       // A40C: slope - drag
    uint32_t r8 = s.in_accel;                                       // A40F
    s.braking = 0;                                                  // A414
    uint32_t r9 = s.in_brake;                                       // A419
    r9 = r8_(BRAKE_MAP + r9);                                       // A41E
    if (r9 != 0) { r8 = 0; s.braking = 2; }                         // A426 tb
    s.brake_val = (uint16_t) r9;                                    // A432
    r9 *= r32(r12 + 0x1C);                                          // A437: brake force
    r8 = (uint32_t) sx8(r8_(ACCEL_MAP + r8));                       // A43B
    uint32_t gear = s.gear;                                         // A443-A44A: $209300 + gear << 9
    uint32_t r1 = (uint16_t) s.speed >> 8;                          // A450-A454
    uint32_t r2 = (gear < 8) ? s.torque[gear][r1] : 0;              // A458
    if (gear >= 8) { unsupported_pc = 0xA458; return; }
    r8 *= r2;                                                       // A45C
    r8 = sar32(r8, 6);                                              // A45F: engine force
    uint32_t r19 = s.ratio;                                         // A463
    aac9(in, r7, r8, r9, r19);                                      // A467
    // A46A: test.b 623[R24] (the linked mode, refused in tick())
    r0 = s.speed;                                                   // A477
    uint32_t r6 = r0;                                               // A47B
    s.speed_copy = (uint16_t) r6;                                   // A47E
    r0 = s.in_steer;                                                // A482
    r0 = (r0 & 0xFF) >> 3;                                          // A487
    r0 = (uint32_t) sx8(r8_(r32(r12) + r0));                        // A48B: the car's steering table
    r0 *= 0x14;                                                     // A490
    r0 = divw(r0, 0x10);                                            // A497
    if (r6 > 0xA000) { }                                            // A49E bh
    else if (r6 > 0x2000)                                           // A4A7 bh
    {
        r7 = r6 + 0x6000;                                           // A4BC
        r0 *= r7;                                                   // A4C6
        r0 = divw(r0, 0x10000);                                     // A4C9
    }
    else
    {
        r0 *= r6;                                                   // A4B0
        r0 = divw(r0, 0x4000);                                      // A4B3
    }
    s.steer_angle = (int16_t)(uint16_t) r0;                         // A4D0
    // A4D5-A50E: the push out of the curve: the change of the road's heading at the car
    uint16_t h7 = s.heading, h8 = s.heading_prev;                   // A4D5, A4DA
    h7 = (uint16_t)(h7 << 4);                                       // A4DF
    h8 = (uint16_t)(h8 << 4);                                       // A4E2
    h8 = (uint16_t)(h8 - h7);                                       // A4E5
    h8 = (uint16_t)(h8 * 0x20);                                     // A4E8
    h8 = divh(h8, 0x100);                                           // A4ED
    if (s.state == 5) h8 = (uint16_t)((int16_t) h8 >> 4);           // A4F2-A4F9
    r8 = (uint32_t) sx16(h8);                                       // A4FD
    r6 = (uint32_t) s.catchup + 0x100;                              // A500-A504
    r8 <<= 8;                                                       // A50B
    r8 = divw(r8, r6);                                              // A50E
    curve_term = (int16_t) h8; curve_push_raw = (int16_t) r8;       // crossover: BALANCE's curve push
    if (push_fn) r8 = (uint32_t) push_fn(sx16((uint16_t) r8));
    curve_push = (int16_t) r8;
    s.sliding = 0;                                                  // A511
    r1 = s.speed_copy;                                              // A516
    r1 *= r0;                                                       // A51A: speed x steering angle
    r2 = r32(r12 + 0x20);                                           // A51D: the grip limit
    bool slide = false;
    if ((int64_t)(int32_t) r1 - (int32_t) r2 >= 0) slide = true;    // A521 bge
    else
    {
        r2 = 0u - r2;                                               // A526
        if ((int64_t)(int32_t) r1 - (int32_t) r2 <= 0) slide = true;    // A529 ble
        else r8 = lo16(r8, r8 + r0);                                // A52E: within the grip
    }
    if (slide)
    {
        r1 -= r2;                                                   // A534
        s.sliding = 0xFF;                                           // A537
        r1 *= r32(r12 + 0x24);                                      // A53C
        r1 = divw(r1, 0xFFC00000u);                                 // A540
        s.slide = (int16_t)(uint16_t) r1;                           // A547
        r8 = lo16(r8, r8 + r1);                                     // A54C
        r8 = lo16(r8, r8 + r0);                                     // A54F
        if (s.wheel[0] == 0) { s.wheel[1] = 3; s.wheel[2] = 3; s.wheel[0] = 3; }   // A552-A564
        r0 = (r0 & ~0xFFu) | 3;                                     // A56A
        sound_tyre_if_on_road(3);                                   // A56D jsr 1222F
        s.latvel = (int16_t)(uint16_t) r8;                          // A573
        r0 = lo16(r0, r8);                                          // A577
    }
    else
    {
        // A5CA: test1 #8, 90[R25] (the demo, refused in tick()); after the goal ($20FB5E) by 1/4
        if (in.own_rec_goal != 0)                                   // A5D2
        {
            r0 = lo16(r0, (uint16_t) s.latvel);                     // A57C
            uint16_t d = (uint16_t)(r8 - r0);                       // A580
            if (d != 0) r0 = lo16(r0, r0 + step16(d, 4));           // A583-A59A
            s.latvel = (int16_t)(uint16_t) r0;                      // A59D
        }
        else
        {
            r0 = lo16(r0, r8);                                      // A5D8
            s.latvel = (int16_t)(uint16_t) r0;                      // A5DB
        }
    }
    // A5DF: test.b 623[R24]
    // A5EE: the advance along the road: speed x cos(angle of (lateral velocity << 8, speed)) >> 14
    r0 = (uint32_t) sx16(r0);                                       // A5EE
    r0 <<= 8;                                                       // A5F1
    r1 = s.speed_copy;                                              // A5F4
    r9 = r1;                                                        // A5F8
    r0 = atan2_12318(r0, r1, 0, 0, rom16, rom);               // A5FB-A601
    r0 = (uint32_t) cos_(r0);                                       // A607
    r9 *= r0;                                                       // A60F
    r9 >>= 14;                                                      // A612
    uint32_t r15 = s.pos;                                           // A616
    r6 = r9;                                                        // A61A
    r15 += r6;                                                      // A61D
    if (s.goal_flag == 0)                                           // A620
    {
        // A626: the catch-up: the gap in segments to the car one place ahead (68[R25], $76AF), up to
        // $20: the advance grows by up to 31/128 (none for the leader, whose 68[R25] is itself)
        uint16_t g = (uint16_t)(in.ref_rec_seg - in.own_rec_seg);   // A626-A62B
        if ((int16_t) g > 0x20) g = 0x20;                           // A630 ble
        int32_t gm1 = (int32_t)(int16_t) g - 1;                     // A63C sub.h #1 (exact sign for bge)
        g = (uint16_t)(g - 1);
        if (gm1 < 0) g = 0;                                         // A63F bge
        s.gap = g;                                                  // A644
        uint32_t c = s.catchup;                                     // A648: movz.hw
        int32_t dd = (int32_t)(int16_t) g - (int32_t)(int16_t)(uint16_t) c;    // A64C sub.h R6, R7
        if ((uint16_t) dd != 0)                                     // A64F be
        {
            if (dd > 0) c = lo16(c, c + 1);                         // A651 bgt -> A657
            else c = lo16(c, c - 1);                                // A653
        }
        s.catchup = (uint16_t) c;                                   // A659
        c *= r9;                                                    // A65D
        c >>= 7;                                                    // A660
        r15 += c;                                                   // A664
    }
    // A667: test.b 623[R24]
    int32_t r13 = sx16((uint16_t)(s.lateral + s.latvel + s.push));  // A66E-A67B
    s.lateral = (int16_t) r13;                                      // A67E
    uint16_t r12o;
    int32_t r17;
    Segment s11 = road.segment(r11);
    for (;;)
    {
        r12o = (uint16_t)((r11 + 0x20) & 0xAFFF & 0xFFF);           // A682-A686
        Segment s12 = road.segment(r12o);
        uint32_t a = (uint16_t)(s11.heading - s12.heading) & 0xFFF; // A68B-A693
        uint32_t v = (uint32_t) sin_(a);                            // A698
        v *= (uint32_t) r13;                                        // A6A0
        uint32_t cy = (v >> 5) & 1;                                 // A6A3 sha.w #FA: the last bit shifted out
        v = sar32(v, 6);
        v = v + 0x8000 + cy;                                        // A6A7 addc.w: the segment's length here
        r17 = (int32_t) v;
        if ((int64_t)(int32_t) r15 - r17 < 0) break;                // A6AE blt
        r15 -= (uint32_t) r17;                                      // A6B3: into the next segment
        r11 = r12o;                                                 // A6B6
        s11 = s12;
        s.seg_count++;                                              // A6B9
        s.seg_count2++;                                             // A6BC
        s.ring_off2 = (uint16_t)((s.ring_off2 + 0x20) & 0xAFFF);    // A6BF-A6C7
        s.ring_off = (uint16_t)((s.ring_off + 0x20) & 0xAFFF);      // A6CF-A6D6
    }
    s.pos = r15;                                                    // A6DF
    int32_t r15s = (int32_t) sar32(r15, 8);                         // A6E3
    r17 = (int32_t) sar32((uint32_t) r17, 8);                       // A6E7
    s.pos_hi = (uint16_t) r15s;                                     // A6EB
    // A770: test.b 623[R24]; A77C: the lateral limits: the half-width + $60
    {
        uint16_t h6 = s11.half_width;                               // A77C
        uint16_t h7l = (uint16_t)(0u - h6);                         // A780
        h6 = (uint16_t)(h6 + 0x60);                                 // A783
        h7l = (uint16_t)(h7l - 0x60);                               // A788
        bool store = false;
        if ((int32_t)(int16_t) r13 - (int32_t)(int16_t) h6 >= 0) { r13 = (int32_t) lo16((uint32_t) r13, h6); store = true; }     // A78D blt
        else if (!((int32_t)(int16_t) r13 - (int32_t)(int16_t) h7l > 0)) { r13 = (int32_t) lo16((uint32_t) r13, h7l); store = true; }  // A797 bgt
        if (store) s.lateral = (int16_t)(uint16_t) r13;             // A79F
    }
    // A7A3: test.b 623[R24]
    a7cc(road, r11, r12o, r13, r15s, r17);
}

// $A7CC-$A89D: the world position (rotation $104F6) and the heading interpolated in the segment
void ORCar::a7cc(const Road& road, uint16_t r11, uint16_t r12, int32_t r13, int32_t r15, int32_t r17)
{
    Segment s11 = road.segment(r11), s12 = road.segment(r12);
    uint32_t r0 = 0;                                                // A7CC
    uint32_t r1 = (uint32_t) r13;                                   // A7CF
    uint32_t r2 = (uint32_t) r15;                                   // A7D2
    r1 <<= 7;                                                       // A7D5
    r2 <<= 7;                                                       // A7D8
    uint32_t r4 = s11.pitch;                                        // A7DB
    uint32_t r3 = s11.heading;                                      // A7DF
    {
        // 104F6: rotation by the heading (R3) and the pitch (R4)
        uint32_t r5 = (uint32_t) sin_(r4);                          // 104F6
        uint32_t r6 = (uint32_t) cos_(r4);                          // 104FE
        r4 = (uint32_t) cos_(r3);                                   // 10506
        r3 = (uint32_t) sin_(r3);                                   // 1050E
        uint32_t r7 = r0;                                           // 10516
        r0 *= r6;                                                   // 10519
        uint32_t r8 = 0u - r2;                                      // 1051C
        r8 *= r5;                                                   // 1051F
        r0 += r8;                                                   // 10522
        r0 = sar32(r0, 14);                                         // 10525
        r8 = r7;                                                    // 10529
        r8 *= r3;                                                   // 1052C
        r8 = sar32(r8, 14);                                         // 1052F
        r8 *= r5;                                                   // 10533
        uint32_t r9 = 0u - r1;                                      // 10536
        r1 *= r4;                                                   // 10539
        r1 += r8;                                                   // 1053C
        r8 = r2;                                                    // 1053F
        r8 *= r3;                                                   // 10542
        r8 = sar32(r8, 14);                                         // 10545
        r8 *= r6;                                                   // 10549
        r1 += r8;                                                   // 1054C
        r1 = sar32(r1, 14);                                         // 1054F
        r7 *= r4;                                                   // 10553
        r7 = sar32(r7, 14);                                         // 10556
        r7 *= r5;                                                   // 1055A
        r9 *= r3;                                                   // 1055D
        r9 += r7;                                                   // 10560
        r2 *= r4;                                                   // 10563
        r2 = sar32(r2, 14);                                         // 10566
        r2 *= r6;                                                   // 1056A
        r2 += r9;                                                   // 1056D
        r2 = sar32(r2, 14);                                         // 10570
    }
    { uint32_t t = r0; r0 = r1; r1 = t; }                           // A7E9 xch.w
    r0 += r0;                                                       // A7EC
    r2 += r2;                                                       // A7EF
    r3 = (uint32_t) s11.x;                                          // A7F2
    r3 <<= 8;                                                       // A7F6
    r0 += r3;                                                       // A7F9
    r0 = sar32(r0, 8);                                              // A7FC
    s.wx = (int32_t) r0;                                            // A800
    r3 = (uint32_t) s11.z;                                          // A804
    r3 <<= 8;                                                       // A808
    r2 += r3;                                                       // A80B
    r2 = sar32(r2, 8);                                              // A80E
    s.wz = (int32_t) r2;                                            // A812
    r0 = (uint32_t) s11.y;                                          // A816
    r1 = (uint32_t) s12.y;                                          // A81A
    uint32_t r18 = (uint32_t) r17;                                  // A81E
    r18 -= (uint32_t) r15;                                          // A821
    r0 *= r18;                                                      // A824
    r1 *= (uint32_t) r15;                                           // A827
    r1 += r0;                                                       // A82A
    r1 = divw(r1, (uint32_t) r17);                                  // A82D
    r1 += (uint32_t) s.jump;                                        // A830
    s.wy_prev = s.wy;                                               // A835 xch.w R1, 24[R20]
    s.wy = (int32_t) r1;
    s.seg_rest = (uint16_t) r18;                                    // A83E
    r0 = s11.heading;                                               // A842
    r1 = s12.heading;                                               // A846
    r0 = lo16(r0, r0 << 4);                                         // A84A
    r1 = lo16(r1, r1 << 4);                                         // A84D
    uint32_t q = (r0 & 0xFFFF) >> 14;                               // A850-A853
    if (q == 0 || q == 3) { r0 = (uint32_t) sx16(r0); r1 = (uint32_t) sx16(r1); }  // A870 (table $A860)
    else { r0 &= 0xFFFF; r1 &= 0xFFFF; }                            // A878
    r0 *= r18;                                                      // A87E
    r1 *= (uint32_t) r15;                                           // A881
    r0 += r1;                                                       // A884
    r0 = divw(r0, (uint32_t) r17);                                  // A887
    r0 = sar32(r0, 4);                                              // A88A
    r0 = lo16(r0, r0 & 0xFFF);                                      // A88E
    s.heading_prev = s.heading;                                     // A893 xch.h R0, 8E[R20]; A898
    s.heading = (uint16_t) r0;
}

// $AAC9: the speed integration, wheelspin / locked wheels and the engine speed
void ORCar::aac9(const TickInput& in, uint32_t r7, uint32_t r8, uint32_t r9, uint32_t r19)
{
    uint32_t r12 = s.params;
    uint32_t r0, r1;
    r9 += r7;                                                       // AAC9
    r9 += r8;                                                       // AACC
    r9 = sar32(r9, 6);                                              // AACF
    s.force = (int16_t)(uint16_t) r9;                               // AAD3
    uint32_t r18 = r19;                                             // AAD8
    r18 *= 0xFFFF;                                                  // AADB
    r18 >>= 8;                                                      // AAE2: top speed in this gear
    bool spin_path;                                                 // the integration of $AB80 (spinning) or $AC55
    if (s.spin != 0)                                                // AAE6
    {
        r8 = r9;                                                    // AAEC
        if ((int32_t) r9 >= (int32_t) r32(r12 + 0x10)) goto ab52;   // AAEF bge: still spinning
        if (s.speed == 0) goto ac55;                                // AAF5
        if ((int32_t) r9 <= (int32_t) 0xFFFFFB80u) goto ab0f;       // AAFB ble: still locked
        goto ac55;
    }
    if ((int32_t) r9 >= (int32_t) r32(r12 + 0xC))                   // AC3F bge
    {
        r8 = r9;                                                    // AB4A
        s.spin = 0xFF;                                              // AB4D
        goto ab52;
    }
    if (s.speed == 0) goto ac55;                                    // AC46
    if ((int32_t) r9 <= (int32_t) 0xFFFFFC80u)                      // AC4B ble: hard braking
    {
        r8 = r9;                                                    // AB07
        s.spin = 0xFF;                                              // AB0A
        goto ab0f;
    }
    goto ac55;
ab0f:
    if (in.brake == 0) goto ab80;                                   // AB0F test.b 102[R25]
    if (!s.sliding) sound_tyre(r8_(SND_LOCK + s.car));              // AB15-AB2E: unless sliding
    if (s.wheel[0] == 0) { s.wheel[1] = 2; s.wheel[2] = 2; s.wheel[0] = 3; }   // AB30-AB42
    goto ab80;
ab52:
    if (!s.sliding) sound_tyre_if_on_road(1);                       // AB52-AB62: unless sliding
    r9 = r32(r12 + 0x14);                                           // AB64: the force while spinning
    if (s.wheel[0] == 0) { s.wheel[1] = 1; s.wheel[2] = 1; s.wheel[0] = 3; }   // AB68-AB7A
ab80:
    spin_path = true;
    goto integrate;
ac55:
    s.spin = 0;                                                     // AC55
    spin_path = false;
integrate:                                                          // AB80 / AC5A (the same code)
    r0 = s.speed;
    if ((int64_t) r0 + (int64_t) s.dspeed < 0) r0 = 0;              // AB84 bge
    else r0 += (uint32_t) s.dspeed;
    r1 = r0;                                                        // AB8E
    r1 += r9;                                                       // AB91
    if ((int64_t)(int32_t) r1 - (int32_t) r18 > 0)                  // AB94 ble: above the gear's top speed
    {
        r1 = r18;                                                   // AB99
        r1 -= r0;                                                   // AB9C
        if (r1 != 0) r0 += step32(r1, 0x1E);                        // AB9F-ABBA: by 1/30
        if (r0 >= 0xFFFF) r0 = 0xFFFF;                              // ABBD bl
    }
    else
    {
        if ((int32_t) r1 < 0) r1 = 0;                               // ABCF
        r1 -= r0;                                                   // ABD6
        if (r1 != 0) r0 += step32(r1, 2);                           // ABD9-ABF0: by 1/2
    }
    s.speed = (uint16_t) r0;                                        // ABF3 / ACCD
    s.speed32 = r0;                                                 // ABF7 / ACD1
    if (r0 == 0)                                                    // ABFC / ACD6 tb
    {
        s.moving = 0;                                               // ACE7
        r0 = s.in_accel;                                            // ACEC
        r0 <<= 8;                                                   // ACF1
        spin_path = false;                                          // the engine follows by 1/3
    }
    else
    {
        s.moving = 0xFF;                                            // AC00 / ACDA
        r0 <<= 8;                                                   // AC05 / ACDF
        r0 = divuw(r0, r19);                                        // AC08 / ACE2
    }
    r1 = s.rpm;                                                     // AC0B / ACF4
    r0 -= r1;                                                       // AC0F / ACF8
    if (r0 != 0) r1 += step32(r0, spin_path ? 0xA : 3);             // AC14-AC29 (1/10) / ACFD-AD12 (1/3)
    if (r1 >= 0xFFFF) r1 = lo16(r1, 0xFFFF);                        // AC2C / AD15 bl
    s.rpm = (uint16_t) r1;                                          // AC3A / AD23
}

// $B79B: the surface under the wheels, off-road and the minor contact's drag. R0 = the drag term,
// R1 the lateral position, R2 the segment's half-width, R3 the car's.
uint32_t ORCar::b79b(const TickInput& in, const Road& road, uint32_t r0, int32_t r1, uint32_t r2, uint32_t r3)
{
    uint32_t r13 = s.zone_ptr[0];                                   // B79B
    uint32_t r14 = s.zone_ptr[1];                                   // B7A0
    if (sx16(road.zone16(r13)) < 0 && sx16(road.zone16(r14)) < 0 && s.seg_count2 >= s.zone_base)   // B7A5-B7B3
    {
        b764(road);                                                 // B7B5: the next node's zones
        r13 = s.zone_ptr[0];                                        // B7B8
        r14 = s.zone_ptr[1];                                        // B7BD
    }
    uint32_t r4 = s.seg_count2;                                     // B7C2
    uint32_t r5 = s.zone_base_prev;                                 // B7C6
    uint32_t r6 = road.zone16(r13);                                 // B7CA
    r6 += r5;                                                       // B7CD
    if (r4 >= r6) { r13 += 8; s.zone_ptr[0] = r13; }                // B7D0-B7D8
    r6 = road.zone16(r14);                                          // B7DD
    r6 += r5;
    if (r4 >= r6) { r14 += 8; s.zone_ptr[1] = r14; }                // B7E3-B7EB
    if (s.minor != 0)                                               // B7F0
    {
        s.minor--;                                                  // B7F6
        r0 *= r32(s.params + 8);                                    // B7FA
        r0 >>= 4;                                                   // B7FE
        b93d(in);                                                   // B802 br B93D
        return r0;
    }
    uint16_t h4 = (uint16_t) r1;                                    // B805
    h4 = (uint16_t)(h4 - r3);                                       // B808: the left side
    uint16_t h3 = (uint16_t)(r3 + r1);                              // B80B: the right side
    uint32_t c = 0;                                                 // B80E-B811
    uint16_t h5 = (uint16_t)(0u - r2);                              // B814
    uint16_t h6 = 0x8000;                                           // B817
    h3 = (uint16_t)(h3 + h6);                                       // B81C
    h4 = (uint16_t)(h4 + h6);                                       // B81F
    h5 = (uint16_t)(h5 + h6);                                       // B822
    h6 = (uint16_t)(h6 + r2);                                       // B825
    if (h6 < h3) c++;                                               // B828-B82B
    if (h4 < h5) c++;                                               // B82E-B831
    s.offroad = 0;                                                  // B834
    if (c != 0)                                                     // B839 tb
    {
        s.offroad = 0xFF;                                           // B83D
        r0 *= r32(s.params + 8);                                    // B842
        r0 >>= 4;                                                   // B846
        b93d(in);                                                   // B84A
    }
    uint16_t z15 = road.zone16(r13 + 2), z16 = road.zone16(r13 + 4);   // B84D, B851
    uint16_t z17 = road.zone16(r14 + 2), z18 = road.zone16(r14 + 4);   // B855, B859
    // B85D-B892: the right wheels
    {
        uint16_t lo = (uint16_t)(h3 - r2), hi = (uint16_t)(h3 + r2);   // B860-B866
        uint32_t i = (uint32_t)(z15 < lo) << 3 | (uint32_t)(z16 < lo) << 2 | (uint32_t)(hi < z17) << 1 | (uint32_t)(hi < z18);
        switch (r32(ZONE_RIGHT + 4 * i))
        {
        case 0xB8CB: break;
        case 0xB8CC: s.wheel[0] |= 1; s.wheel[1] = road.zone8(r13 + 6); break;
        case 0xB8DA: s.wheel[0] |= 1; s.wheel[1] = road.zone8(r13 + 7); break;
        case 0xB8E8: s.wheel[0] |= 1; s.wheel[1] = road.zone8(r14 + 6); break;
        case 0xB8F6: s.wheel[0] |= 1; s.wheel[1] = road.zone8(r14 + 7); break;
        default: unsupported_pc = 0xB892; return r0;
        }
    }
    // B894-B8C9: the left wheels
    {
        uint16_t lo = (uint16_t)(h4 - r2), hi = (uint16_t)(h4 + r2);   // B897-B89D
        uint32_t i = (uint32_t)(z15 < lo) << 3 | (uint32_t)(z16 < lo) << 2 | (uint32_t)(hi < z17) << 1 | (uint32_t)(hi < z18);
        switch (r32(ZONE_LEFT + 4 * i))
        {
        case 0xB904: break;
        case 0xB905: s.wheel[0] |= 2; s.wheel[2] = road.zone8(r13 + 6); break;
        case 0xB913: s.wheel[0] |= 2; s.wheel[2] = road.zone8(r13 + 7); break;
        case 0xB921: s.wheel[0] |= 2; s.wheel[2] = road.zone8(r14 + 6); break;
        case 0xB92F: s.wheel[0] |= 2; s.wheel[2] = road.zone8(r14 + 7); break;
        default: unsupported_pc = 0xB8C9; return r0;
        }
    }
    return r0;
}

// $B764: the course node's surface zones
void ORCar::b764(const Road& road)
{
    uint8_t node = road.node();                                     // B764
    s.zone_node = node;                                             // B769
    uint16_t r5 = road.node_length(node);                           // B76E
    r5 = (uint16_t)(r5 + road.node_extra());                        // B774
    s.zone_base_prev = s.zone_base;                                 // B779
    s.zone_base = (uint16_t)(s.zone_base + r5);                     // B77F
    // B783-B794: the node's two lists from $B9F8
    s.zone_ptr[0] = road.zone_list(node, 0);
    s.zone_ptr[1] = road.zone_list(node, 1);
}

// $B93D: off the road (or a minor contact): the shaker; the gravel sound ($12235)
void ORCar::b93d(const TickInput&)
{
    s.shaker = r8_(SHAKER + s.car);                                 // B949
    sound_tyre(r8_(SND_GRAVEL + s.car));                            // B955-B95D: the gravel sound
}

// ------------------------------------------------------------------------------------------------
// The gear box ($C379; state 1 $C3C4)
// ------------------------------------------------------------------------------------------------
void ORCar::gearbox_tick(const TickInput& in)
{
    snd = SoundOut();
    s.gearbox_state = in.gearbox_state;
    switch (s.gearbox_state)                                        // C379: table $C384
    {
    case 0:                                                         // C390
        s.gearbox_state++;
        // C393-C3BE: the HUD's gear box sprite
        return;
    case 1:
        break;
    default:                                                        // ED50: drawn only
        return;
    }
    if (s.shift_smoke != 0)                                         // C3C4
    {
        s.shift_smoke--;                                            // C3CA
        sound_tyre_if_on_road(1);                                   // C3CE-C3D1
    }
    uint32_t r0 = s.gears_m1;                                       // C3D7
    uint32_t r1 = s.gear;                                           // C3DB
    uint8_t r2 = in.shift_edges;                                    // C3DF
    if ((r2 & 0x10) && (uint8_t) r0 != (uint8_t) r1)                // C3E4, C3E9
    {
        r1 = (r1 & ~0xFFu) | ((r1 + 1) & 0xFF);                     // C3EE
        if (s.rpm >= 0xF000) s.shift_smoke = 0x1E;                  // C3F0 bl
    }
    if ((r2 & 0x20) && r1 != 0)                                     // C400, C405 tb
    {
        uint32_t a = r8_(s.gears_ptr + r1);                         // C409
        r1 = (r1 & ~0xFFu) | ((r1 - 1) & 0xFF);                     // C40E
        uint32_t b = r8_(s.gears_ptr + r1);                         // C410
        uint32_t r4 = s.rpm;                                        // C415
        r4 *= a;                                                    // C419
        r4 = divuw(r4, b);                                          // C41C
        if (r4 >= 0xFFFF) s.shift_smoke = 0x1E;                     // C41F bl: over-revving
    }
    s.gear = (uint8_t) r1;                                          // C42F
    s.ratio = r8_(s.gears_ptr + r1);                                // C433
    s.gearbox_hud = (uint8_t) r1;                                   // C43A
    // C43E-C484: the HUD's rev indicator
    if (in.goal || s.game_over) s.gearbox_state++;                  // C488-C494
}

} // namespace orcar
