/***************************************************************************
    OutRun's Ferrari as the vehicle on Hang-On's course. See hoferrari.hpp.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#include "crossover/hoferrari.hpp"
#include "crossover/hoboard.hpp"
#include "crossover/m68k/m68k.h"

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <algorithm>

#include "video.hpp"
#include "hwvideo/hwsprites.hpp"
#include "frontend/config.hpp"
#include "engine/outrun.hpp"
#include "engine/oinitengine.hpp"
#include "engine/oroad.hpp"
#include "engine/oferrari.hpp"
#include "engine/oinputs.hpp"
#include "engine/ostats.hpp"
#include "engine/ocrash.hpp"
#include "engine/osprites.hpp"
#include "engine/osmoke.hpp"
#include "engine/obonus.hpp"
#include "engine/otraffic.hpp"
#include "engine/olevelobjs.hpp"
#include "engine/oentry.hpp"
#include "engine/audio/osoundint.hpp"

extern "C" {
#include "crossover/shophys/sho_road.h"
}

HOFerrari hoferrari;

// ------------------------------------------------------------------------------------------------
// Hang-On addresses (main CPU view), see docs/CROSSOVER-HANGON.md
// ------------------------------------------------------------------------------------------------
static const uint32_t V_STAGE = 0xC7E010, V_POS = 0xC7E030, V_X = 0xC7E050, V_SPR_LEAN = 0xC7E056,
    V_SPR_X = 0xC7E058, V_SPR_Y = 0xC7E05A, V_DIST = 0xC7E074, V_SPEED = 0xC7E078,
    V_CRASH_COUNT = 0xC7E084, V_KNOCK = 0xC7E088, V_ODO_STEP = 0xC7E08C, V_ODO = 0xC7E090,
    V_CRASH = 0xC7E094, V_OFFROAD = 0xC7E098, V_CURVE_NEXT = 0xC7E0A4, V_CURVE_NEXT_R = 0xC7E0A8,
    V_CURVE = 0xC7E0AC, V_CURVE_R = 0xC7E0B0, V_CHECK = 0xC7E0E2, V_GOAL = 0xC7E0EA,
    V_RETIRE = 0xC7E0EC, V_EXIT_DONE = 0xC7E0EE, V_DRAW_COUNT = 0xC7E500, V_DRAW_LIST = 0xC7E502,
    V_PATH = 0xC7F0F0;
static const uint32_t W_INGAME = 0x20C408, W_MODE = 0x20C420, W_DIP = 0x20C450, W_TIMER = 0x20C500,
    W_PKT = 0x20C640, W_SPRITES = 0x20D400;

// object slots ($20C800 + 64 * n) and the tasks expected in them
static const uint32_t SLOT_BASE = 0x20C800;
static const uint32_t SLOT_SOUND = 0x20C880, SLOT_SPEED = 0x20CA40, SLOT_CENTRIFUGAL = 0x20CA80,
    SLOT_BIKE = 0x20D080;
static const uint32_t SLOT_CRASH[4] = { 0x20C900, 0x20C940, 0x20C980, 0x20C9C0 };
static const uint32_t TASK_SOUND = 0x8140, TASK_SPEED = 0x8566, TASK_CENTRIFUGAL = 0x879A, TASK_BIKE = 0xAF10;

// hooked main-CPU addresses
static const uint32_t PC_DISPATCH = 0x119A, PC_DISPATCH_NEXT = 0x119C;
static const uint32_t PC_SPRITE_SHOWN = 0x4026, PC_SPRITE_HIDDEN = 0x4038;     // sub_03F84 after sub_04110
static const uint32_t PC_RIVAL_CMP = 0xA922;             // cmp.w d0,d1: |x - $160| against allowance + 10
static const uint32_t PC_RIVAL_HIT = 0xA928, PC_RIVAL_HIT_END = 0xA95A;
static const uint32_t PC_RIVAL_SOUND = 0xA978, PC_RIVAL_SOUND_END = 0xA97C;
static const uint32_t PC_DUST_DRAW = 0xB76A, PC_DUST_DRAW_END = 0xB76E;
static const uint32_t PC_SOUND_QUEUE = 0x3F2E;           // sub_03F2E: d0 = command

// ROM tables
static const uint32_t ROM_SPR_LEAN = 0xB45C;             // the bike's lean frames (definition pointers)
static const uint32_t ROM_RIVAL_SPEED = 0x10FA4;         // rivals' speeds by difficulty and stage
static const uint32_t ROM_RIVAL_DIV = 0x10F72;

// OutRun: the rivals are bikes, so they get the spin chance of OutRun's cars (see shferrari.cpp)
static const uint16_t RIVAL_COLLISION_MASK = 1;

// OutRun single road of half-width $18: OFerrari::check_wheels puts one wheel off at |x| > $18 + $D4 and
// both off at |x| > $18 + $104, i.e. the edge midpoint at $104 = 260, Hang-On's own road edge (sub_0B57C:
// off road at |X| > 260; Super Hang-On's is 300, with OutRun's half-width $40).
static const int32_t ROAD_WIDTH = 0x18;

// Hang-On's bike: draw-list depth (and screen line) of the vehicle, its screen position
static const int BIKE_DEPTH = 0xE0, BIKE_SX = 0x160, BIKE_SY = 0xDF;

// OutRun's flat road heights (ORoad::set_y_2044 with OutRun's flat horizon: $2400 per entry, measured on
// its first stage): the car's incline frames, its crash and smoke stand on these
static const int32_t FLAT_STEP = 0x2400;

// ------------------------------------------------------------------------------------------------
static inline uint8_t* loc(uint32_t a)
{
    a &= 0xFFFFFF;
    if (a >= 0xC7C000 && a < 0xC80000) return hoboard.subram + (a - 0xC7C000);
    if (a >= 0x20C000 && a < 0x210000) return hoboard.workram + (a - 0x20C000);
    static uint8_t dummy[4];
    return dummy;
}
static inline uint16_t RW(uint32_t a) { uint8_t* p = loc(a); return (uint16_t)(p[0] << 8 | p[1]); }
static inline uint32_t RL(uint32_t a) { return (uint32_t) RW(a) << 16 | RW(a + 2); }
static inline uint8_t  RB(uint32_t a) { return *loc(a); }
static inline void WW(uint32_t a, uint16_t v) { uint8_t* p = loc(a); p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t) v; }
static inline void WL(uint32_t a, uint32_t v) { WW(a, (uint16_t)(v >> 16)); WW(a + 2, (uint16_t) v); }
static inline void WB(uint32_t a, uint8_t v) { *loc(a) = v; }
static inline uint16_t ROMW(uint32_t a)
{
    const std::vector<uint8_t>& r = hoboard.main_rom;
    return a + 1 < r.size() ? (uint16_t)(r[a] << 8 | r[a + 1]) : 0;
}
static inline uint32_t ROML(uint32_t a) { return (uint32_t) ROMW(a) << 16 | ROMW(a + 2); }

// The car's X as Hang-On's program and road see it: within Hang-On's own limit for its bike ($10F64 = 500,
// sub_0B556), a little inside OutRun's bound for this road ($18 + $1F4 = 524)
static inline uint16_t ho_x() { return (uint16_t) std::max(-500, std::min((int) oinitengine.car_x_pos, 500)); }

static bool dbg() { static int d = getenv("CB_HOFERRARI_DEBUG") ? 1 : 0; return d; }

// ------------------------------------------------------------------------------------------------
// Start and end
// ------------------------------------------------------------------------------------------------
void HOFerrari::begin()
{
    if (!enabled) return;

    // OutRun's objects for the car, as Outrun::init_jump_table sets them up (minus the course)
    outrun.tick_frame = true;
    osprites.init();                        // jump table; OFerrari::init and OCrash::init
    osprites.clear_palette_data();
    otraffic.init();
    otraffic.collision_traffic = 0;
    otraffic.traffic_count = 0;
    oroad.stage_lookup_off = 0;             // OutRun's first stage: its smoke colours
    osmoke.init();
    osmoke.setup_smoke_sprite(true);
    obonus.init();
    ocrash.clear_crash_state();
    olevelobjs.collision_sprite = 0;
    olevelobjs.spray_counter = 0;
    olevelobjs.sprite_collision_counter = 0;
    oferrari.reset_car();
    oferrari.car_ctrl_active = true;
    oferrari.init_ingame();
    for (int s : { (int) OSprites::SPRITE_SMOKE1, (int) OSprites::SPRITE_SMOKE2 })
        osprites.jump_table[s].control |= OSprites::ENABLE;
    oinitengine.car_x_pos = oinitengine.car_x_old = 0;
    oinitengine.car_increment = 0;
    oinitengine.rd_split_state = OInitEngine::SPLIT_NONE;
    osoundint.engine_data[sound::ENGINE_VOL] = 0x3F;   // as OInitEngine::init
    video.sprite_layer->reset();

    std::fill(road_x, road_x + 512, 0);
    road_x_pos = 0xFFFFFFFF;
    racing = false;
    sub_frame = 0;
    in_sound_task = false;
    crash_count = RW(V_CRASH_COUNT);
    slot_depth = -1;
    owed = 0; owed_frames = 0;
    n_hits = 0;
    std::memset(overlay, 0, sizeof overlay);
    hovideo.tag_active = true;

    static const uint32_t pcs[] = { PC_DISPATCH, PC_SPRITE_SHOWN, PC_SPRITE_HIDDEN, PC_RIVAL_CMP, PC_RIVAL_HIT,
                                    PC_RIVAL_SOUND, PC_DUST_DRAW, PC_SOUND_QUEUE };
    hoboard.set_pc_hooks(pcs, (int)(sizeof pcs / sizeof pcs[0]), &HOFerrari::hook, this);
}

void HOFerrari::end()
{
    hoboard.set_pc_hooks(nullptr, 0, nullptr, nullptr);
    hovideo.overlay = nullptr;
    hovideo.tag_active = false;
    if (!enabled) return;
    osoundint.engine_data[sound::ENGINE_PITCH_H] = 0;
    osoundint.engine_data[sound::ENGINE_PITCH_L] = 0;
    osoundint.engine_data[sound::ENGINE_VOL] = 0;
    osoundint.queue_sound(sound::STOP_SLIP);
    osoundint.queue_sound(sound::STOP_SAFETYZONE);
}

// ------------------------------------------------------------------------------------------------
// Hooks
// ------------------------------------------------------------------------------------------------
void HOFerrari::hook(void* ctx, uint32_t pc) { ((HOFerrari*) ctx)->on_pc(pc); }

void HOFerrari::on_pc(uint32_t pc)
{
    switch (pc)
    {
        case PC_DISPATCH:
        {
            uint32_t a5 = hoboard.main_reg(M68K_REG_A5) & 0xFFFFFF, a0 = hoboard.main_reg(M68K_REG_A0) & 0xFFFFFF;
            if (in_sound_task) { in_sound_task = false; silence_packet(); }
            bool was = racing;
            uint16_t mode = RW(W_MODE);
            racing = RW(W_INGAME) != 0 && mode >= 1 && mode <= 3;
            if (racing && !was)                                   // on the start line: where the game put its vehicle
            {
                oinitengine.car_x_pos = oinitengine.car_x_old = (int16_t) RW(V_X);
                crash_count = RW(V_CRASH_COUNT);
                if (dbg()) fprintf(stderr, "hoferrari: race from X %d\n", oinitengine.car_x_pos);
            }
            if (on_task(a5, a0)) hoboard.set_main_reg(M68K_REG_PC, PC_DISPATCH_NEXT);
            break;
        }

        case PC_SPRITE_SHOWN:
        case PC_SPRITE_HIDDEN:
            if (racing && (hoboard.main_reg(M68K_REG_A5) & 0xFFFFFF) == SLOT_BIKE) tag_sprite(pc == PC_SPRITE_HIDDEN);
            break;

        case PC_RIVAL_CMP:
            // d0 = allowance + 10, d1 = |rival x - $160|. OutRun's rule for the Ferrari against a car: hit
            // within 11/8 of the half-width the allowance gives (as on Super Hang-On's courses)
            if (racing)
            {
                int32_t half = (int16_t)(hoboard.main_reg(M68K_REG_D0) & 0xFFFF) - 10;
                int32_t w = half * 11 / 8;
                uint32_t d0 = hoboard.main_reg(M68K_REG_D0);
                hoboard.set_main_reg(M68K_REG_D0, (d0 & 0xFFFF0000) | (uint16_t) w);
            }
            break;

        case PC_RIVAL_HIT:
            // contact: OutRun's traffic response instead of Hang-On's push
            if (racing)
            {
                rival_contact(hoboard.main_reg(M68K_REG_A5) & 0xFFFFFF);
                hoboard.set_main_reg(M68K_REG_PC, PC_RIVAL_HIT_END);    // d0 = $89: marks the contact
            }
            break;

        case PC_RIVAL_SOUND:
            if (racing)
            {
                osoundint.queue_sound(sound::REBOUND);
                hoboard.set_main_reg(M68K_REG_PC, PC_RIVAL_SOUND_END);
            }
            break;

        case PC_DUST_DRAW:
            if (racing) hoboard.set_main_reg(M68K_REG_PC, PC_DUST_DRAW_END);
            break;

        case PC_SOUND_QUEUE:
            // the sound task's effects for the bike: squeal $8A/$8B, off-road $8C/$8D
            if (racing && in_sound_task)
            {
                uint8_t c = (uint8_t) hoboard.main_reg(M68K_REG_D0);
                if (c >= 0x8A && c <= 0x8D)
                {
                    uint32_t sp = hoboard.main_reg(M68K_REG_A7);
                    uint32_t ret = hoboard.read(0, sp, 4, 0);
                    hoboard.set_main_reg(M68K_REG_A7, sp + 4);              // rts
                    hoboard.set_main_reg(M68K_REG_PC, ret);
                }
            }
            break;
    }
}

bool HOFerrari::on_task(uint32_t a5, uint32_t entry)
{
    if (a5 == SLOT_SOUND && entry == TASK_SOUND) { in_sound_task = racing; return false; }
    if (!racing) return false;
    if (a5 == SLOT_SPEED && entry == TASK_SPEED)             { speed_task(a5); return true; }
    if (a5 == SLOT_CENTRIFUGAL && entry == TASK_CENTRIFUGAL) return true;
    if (a5 == SLOT_BIKE && entry == TASK_BIKE)               { bike_task(a5); return true; }
    for (uint32_t s : SLOT_CRASH) if (a5 == s) return true;
    return false;
}

// The packet's player part ($20C641-$20C643: mode, engine pitch, throttle), written by the sound task
// for the sound board's bike engine
void HOFerrari::silence_packet()
{
    WB(W_PKT + 1, 0);
    WB(W_PKT + 2, 0);
    WB(W_PKT + 3, 0);
}

// ------------------------------------------------------------------------------------------------
// Slot 9: speed and track progress
// ------------------------------------------------------------------------------------------------
void HOFerrari::speed_task(uint32_t a5)
{
    if (RW(W_MODE) == 1) return;                                  // $8566: start line, nothing moves
    if (RW(V_GOAL) && (int16_t) RW(V_POS) > 0x1C)                 // $8572: past the goal line, every frame:
    {                                                             // stopped, retired, the goal signalled
        WL(V_SPEED, 0);
        WW(V_RETIRE, 0xFFFF);
        WW(V_CHECK, 0xFFFF);
        oinitengine.car_increment = 0;
        owed = 0; owed_frames = 0;
        WL(V_DIST, 0);
        WW(V_ODO_STEP, 0);
        return;
    }
    WL(V_SPEED, (uint32_t) oinitengine.car_increment);            // the HUD's speed, the objects' approach
    WW(V_X, ho_x());

    // OutRun's distance for its last tick, spread over the board frames until the next one
    uint32_t d = owed_frames > 0 ? owed / (uint32_t) owed_frames : owed;
    owed -= d;
    if (owed_frames > 0) owed_frames--;
    // $86F2-$8730: track position, odometer (the road's rumble-strip phase, sub_0184A)
    WL(V_DIST, d);
    WL(V_POS, RL(V_POS) + d);
    uint16_t kmh = (uint16_t)(oinitengine.car_increment >> 16);
    uint16_t q = kmh / 0x1D, r = kmh % 0x1D;
    uint16_t rem = (uint16_t)(RW(a5 + 0x22) + r);
    if ((int16_t) rem >= 0x1D) { rem -= 0x1D; q++; }
    WW(a5 + 0x22, rem);
    WW(V_ODO_STEP, q);
    WW(V_ODO, (uint16_t)(RW(V_ODO) + q));
}

// ------------------------------------------------------------------------------------------------
// Slot 34: the vehicle
// ------------------------------------------------------------------------------------------------
void HOFerrari::bike_task(uint32_t a5)
{
    if (slot_depth < 0) slot_depth = RW(a5 + 0x14);

    // what the rest of Hang-On reads about its vehicle
    WW(V_X, ho_x());
    WW(V_OFFROAD, oferrari.wheel_state == OFerrari::WHEELS_OFF ? 0xFFFF : 0);
    WW(V_KNOCK, 0);
    WW(V_CRASH, 0);
    WB(V_SPR_LEAN, 0); WB(V_SPR_LEAN + 1, 0);
    WW(V_SPR_X, BIKE_SX); WW(V_SPR_Y, BIKE_SY);

    // its object: drawn as OutRun's sprites (tagged after sub_03F84 writes its entry)
    WL(a5 + 0x1A, ROML(ROM_SPR_LEAN));
    WW(a5 + 0x0A, BIKE_SX);
    WW(a5 + 0x0C, BIKE_SY);
    WW(a5 + 0x14, (uint16_t) car_depth());

    if (sub_frame == 0) tick();
    sub_frame ^= 1;
    WW(V_X, ho_x());
    WW(V_OFFROAD, oferrari.wheel_state == OFerrari::WHEELS_OFF ? 0xFFFF : 0);
    WW(a5 + 0x14, (uint16_t) car_depth());

    if (RW(V_RETIRE))
    {
        // $B380: after the goal Hang-On's bike rides off up the road while $C7E0EA counts down by 4; then
        // $C7E0EE tells the game flow (mode 3, $73EE) that it has gone. The Ferrari stays where it stopped.
        if ((int16_t) RW(V_GOAL) > 0)
        {
            WW(V_GOAL, (uint16_t)(RW(V_GOAL) - 4));
            register_sprite(a5);
        }
        else
        {
            WW(V_EXIT_DONE, 0xFFFF);
            WW(V_GOAL, 0);
            WW(V_RETIRE, 0);
        }
        return;
    }

    if (RW(W_MODE) == 1)
    {
        // $B3F6: on the start line the vehicle blinks until the race starts
        uint8_t n = (uint8_t)(RB(a5 + 0x38) - 1), b39;
        WB(a5 + 0x38, n);
        if (n == 0) { WB(a5 + 0x38, 0x0C); WB(a5 + 0x39, (uint8_t)(RB(a5 + 0x39) + 1)); }
        b39 = RB(a5 + 0x39);
        if ((int8_t) b39 > 0x0A || !(b39 & 1)) register_sprite(a5);
        return;
    }
    register_sprite(a5);
}

// The car's depth in Hang-On's sprite list: the bike's own, or, while OutRun's crash throws the car up the
// road, the screen line of the crashed car (in Hang-On depth is the screen line, $80 = horizon).
int HOFerrari::car_depth() const
{
    int depth = slot_depth < 0 ? BIKE_DEPTH : slot_depth;
    const oentry& c = osprites.jump_table[OSprites::SPRITE_CRASH];
    if ((c.control & OSprites::ENABLE) && ocrash.crash_counter)
    {
        int k = std::min<int>(c.priority & 0x1FF, 0x1FF);
        int y = 223 - (oroad.road_y[oroad.road_p0 + k] >> 4);
        depth = std::min(depth, std::max(y, 0x80));
    }
    return depth;
}

// sub_0AD3A: the object into the draw list ($C7E500 count, $C7E502: depth.w, slot offset.w)
void HOFerrari::register_sprite(uint32_t a5)
{
    uint16_t n = RW(V_DRAW_COUNT);
    WW(V_DRAW_COUNT, (uint16_t)(n + 1));
    WW(V_DRAW_LIST + 4 * n, RW(a5 + 0x14));
    WW(V_DRAW_LIST + 4 * n + 2, (uint16_t)(a5 - SLOT_BASE));
}

// The bike's 10-byte sprite entry ($20D400 + 10 * rank, words 0-4 of the sprite chip's entry): word 3, the
// sprite data address, becomes the overlay tag. Hang-On has no hide bit; an entry it clipped ($00EF) is
// given lines again so the overlay keeps its place.
void HOFerrari::tag_sprite(bool hidden)
{
    uint32_t e = W_SPRITES + RW(SLOT_BIKE + 6) * 10;
    if (hidden) WW(e, 0xDF80);
    WW(e + 6, HOVideo::TAG_ADDR);
}

// ------------------------------------------------------------------------------------------------
// OutRun's car logic, one tick (OutRun's game logic runs at 30 Hz: every other board frame)
// ------------------------------------------------------------------------------------------------
void HOFerrari::world_to_outrun()
{
    if (getenv("CB_HOF_FREEZE_TIME")) WB(W_TIMER, 0x50);         // testing: reach the goal
    outrun.tick_frame = true;
    outrun.game_state = RW(W_MODE) == 1 ? GS_START1 : GS_INGAME;
    oinitengine.road_type       = (int16_t) RW(V_CURVE);           // same list format in the three games
    oinitengine.road_curve      = (int16_t) RW(V_CURVE_R);
    oinitengine.road_type_next  = (int16_t) RW(V_CURVE_NEXT);
    oinitengine.road_curve_next = (int16_t) RW(V_CURVE_NEXT_R);
    oinitengine.rd_split_state  = OInitEngine::SPLIT_NONE;
    oroad.road_ctrl  = ORoad::ROAD_BOTH_P0;
    oroad.road_width = ROAD_WIDTH << 16;
    oferrari.road_width_old = ROAD_WIDTH;
    oroad.road_pos = RL(V_POS);
    ostats.cur_stage = RW(V_STAGE) ? 1 : 0;                         // OFerrari's start-line steering limit
    ostats.time_counter = RB(W_TIMER);                              // 0 = time over: no accelerator
    if (RW(V_RETIRE)) ostats.time_counter = 0;                      // stopped after the goal
    // a scenery hit (Hang-On's test, $9E68-$9EAA) -> OutRun's crash
    uint16_t cc = RW(V_CRASH_COUNT);
    if (cc != crash_count) { olevelobjs.collision_sprite = 1; crash_count = cc; }
    fill_road_heights();
    for (int i = 0; i < 512; i++) oroad.road_x[i] = (int16_t) road_x[i];
}

void HOFerrari::tick()
{
    world_to_outrun();
    uint32_t pos0 = oroad.road_pos;

    // OInitEngine::update_engine, the car block
    oferrari.move();
    if (RW(V_RETIRE)) oinitengine.car_increment = 0;
    if (oferrari.car_ctrl_active)
    {
        oferrari.set_curve_adjust();
        oferrari.set_ferrari_x();
        oferrari.do_skid();
        oferrari.check_wheels();
        oferrari.set_ferrari_bounds();
    }
    oferrari.do_sound_score_slip();
    if (olevelobjs.spray_counter > 0) olevelobjs.spray_counter--;
    if (olevelobjs.sprite_collision_counter > 0) olevelobjs.sprite_collision_counter--;

    // OTraffic::check_collision's response, for the rivals touched since the last tick
    for (int i = 0; i < n_hits; i++)
    {
        int d0 = hits[i].side < 0 ? -OCrash::SKID_RESET : OCrash::SKID_RESET;
        if (config.engine.bumper) d0 /= 3;
        d0 += ocrash.skid_counter;
        if (d0 <= OCrash::SKID_MAX && d0 >= -OCrash::SKID_MAX) ocrash.skid_counter = d0;
        otraffic.collision_mask = RIVAL_COLLISION_MASK;
        if (outrun.game_state == GS_INGAME)
        {
            int s = std::max(0, hits[i].speed - 80);
            oinitengine.car_increment = (s << 16) | (oinitengine.car_increment & 0xFFFF);
            oferrari.car_inc_old = s;
            otraffic.collision_traffic++;
        }
        if (dbg()) fprintf(stderr, "hoferrari: rival contact side %d rival %d km/h -> skid %d\n", hits[i].side, hits[i].speed, ocrash.skid_counter);
    }
    n_hits = 0;

    // Outrun::jump_table, the car's part
    oinitengine.init_crash_bonus();
    oferrari.tick();
    if (oferrari.state != OFerrari::FERRARI_END_SEQ)
    {
        ocrash.tick();
        osmoke.draw_ferrari_smoke(&osprites.jump_table[OSprites::SPRITE_SMOKE1]);
        oferrari.draw_shadow();
        osmoke.draw_ferrari_smoke(&osprites.jump_table[OSprites::SPRITE_SMOKE2]);
    }
    osprites.sprite_copy();
    osprites.update_sprites();              // Outrun::vint: the list to the sprite chip, palettes

    // OutRun's distance for this tick, given to the track over the next two board frames
    owed += oroad.road_pos - pos0;
    owed_frames = 2;
    build_road_x();

    if (dbg())
    {
        static int n = 0;
        if ((++n % 30) == 0)
            fprintf(stderr, "hoferrari: tick %d x %d spd %d gear %d acc %d wheels %d crash %d/%d skid %d pos %08x curve %d/%d push %d spr %d eng %02x%02x/%02x\n", n,
                    oinitengine.car_x_pos, oinitengine.car_increment >> 16, oinputs.gear, oinputs.acc_adjust, oferrari.wheel_state,
                    ocrash.crash_counter, ocrash.coll_count1, ocrash.skid_counter, RL(V_POS), oinitengine.road_type, oinitengine.road_curve,
                    oroad.road_x[170], osprites.sprite_count, osoundint.engine_data[sound::ENGINE_PITCH_H],
                    osoundint.engine_data[sound::ENGINE_PITCH_L], osoundint.engine_data[sound::ENGINE_VOL]);
        static uint16_t last_mode = 0xFFFF;
        if (RW(W_MODE) != last_mode) { fprintf(stderr, "hoferrari: mode %d\n", RW(W_MODE)); last_mode = RW(W_MODE); }
    }
}

// OutRun's road builder (ORoad::setup_x_data, verified as sho_road_build_outrun) over Hang-On's path (the
// second CPU's $7F0F0, a pointer into its ROM; same path format as Super Hang-On's): OFerrari::set_curve_adjust
// reads its slice 170. Rebuilt when the position changes, keeping the slices it does not reach, as OutRun does.
static int16_t ho_path_word(void*, uint32_t off)
{
    uint32_t base = RL(V_PATH);
    const std::vector<uint8_t>& r = hoboard.sub_rom;
    uint32_t a = (base + off) & 0x7FFFF;
    return a + 1 < r.size() ? (int16_t)(r[a] << 8 | r[a + 1]) : 0;
}

void HOFerrari::build_road_x()
{
    uint32_t pos = RL(V_POS) >> 16;
    if (pos == road_x_pos) return;
    road_x_pos = pos;
    sho_path_reader pr = { ho_path_word, nullptr };
    sho_road_build_outrun(&pr, pos << 2, road_x);
}

// The road heights OutRun's crash and smoke objects stand on (oroad.road_y, from road_p0; height above the
// bottom line << 4). Hang-On's road is flat; these are OutRun's own heights for a flat road, which keep the
// car's incline frames level (OFerrari reads the slope near the car).
void HOFerrari::fill_road_heights()
{
    int16_t* ry = oroad.road_y + oroad.road_p0;
    int32_t h = 0;
    for (int k = 0x1FF; k >= 0; k--)
    {
        h += FLAT_STEP;
        ry[k] = (int16_t)((h << 4) >> 16);
    }
    // OutRun's list of hill crests from +$280 (OSprites::do_sprite): empty, a flat road
    ry[0x280] = ry[0x281] = 0;
}

// ------------------------------------------------------------------------------------------------
// Rivals
// ------------------------------------------------------------------------------------------------
void HOFerrari::rival_contact(uint32_t a5)
{
    for (int i = 0; i < n_hits; i++) if (hits[i].slot == a5) return;     // once per OutRun tick
    if (n_hits >= 8) return;
    int16_t x = (int16_t)(RW(a5 + 0x0A) - 0x160);                  // screen x from the centre (+ = right)
    hits[n_hits++] = { a5, x < 0 ? -1 : 1, rival_speed(a5) };
}

// A rival's speed in km/h ($AB3C-$ABA6). Its position changes per frame by
//   d0 + ((player speed - S) * 10) >> 4,   S = $10FA4[difficulty, stage], d0 = $36 + min($32 / 5, $26)
// (d0 = 10 once it has been knocked), so it keeps pace with the player at player speed S - 1.6 * d0.
int HOFerrari::rival_speed(uint32_t a5)
{
    uint32_t d1 = (RW(W_DIP) & 6) * 8 + RW(V_STAGE) * 2;
    int s = (int16_t) ROMW(ROM_RIVAL_SPEED + d1);
    int d0;
    if (RB(a5 + 0x3E)) d0 = 10;
    else
    {
        d0 = (int16_t) RW(a5 + 0x36);
        uint16_t div = ROMW(ROM_RIVAL_DIV);
        int q = div ? RW(a5 + 0x32) / div : 0;
        d0 += std::min(q, 0x26);
    }
    return s - d0 * 16 / 10;
}

// ------------------------------------------------------------------------------------------------
// Display
// ------------------------------------------------------------------------------------------------
void HOFerrari::after_frame()
{
    if (!enabled) return;
    std::memset(overlay, 0, sizeof overlay);
    if (racing) video.sprite_layer->render_to(overlay, hovideo.width, HOVideo::H, hovideo.xoff, 8);
    hovideo.overlay = racing ? overlay : nullptr;
}

void HOFerrari::colours(uint32_t* rgb)
{
    for (uint32_t i = 0; i < 0x1000; i++) rgb[0x2000 + i] = video.own_rgb((uint16_t) i);
}
