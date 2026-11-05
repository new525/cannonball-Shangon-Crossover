/***************************************************************************
    OutRun's Ferrari as the vehicle on Super Hang-On's courses. See shferrari.hpp.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#include "crossover/shferrari.hpp"
#include "crossover/shboard.hpp"
#include "crossover/m68k/m68k.h"
#include "crossover/madpower.hpp"
#include "crossover/balance_pace.hpp"
#include "crossover/balance_push.hpp"
#include "crossover/balance.hpp"

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <cmath>

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

SHFerrari shferrari;

// ------------------------------------------------------------------------------------------------
// Super Hang-On addresses (main CPU view; see crossover/shophys and the bike's notes)
// ------------------------------------------------------------------------------------------------
static const uint32_t V_MODE = 0x260000, V_STAGE_DONE = 0x260010, V_X = 0x260022, V_POS = 0x260030,
    V_SPR_LEAN = 0x260056, V_SPR_X = 0x260058, V_SPR_Y = 0x26005A, V_SPEED = 0x260078,
    V_CRASH_COUNT = 0x260084, V_KNOCK = 0x260088, V_CRASH = 0x260094, V_OFFROAD = 0x260098,
    V_CURVE_NEXT = 0x2600A4, V_CURVE_NEXT_R = 0x2600A8, V_CURVE = 0x2600AC, V_CURVE_R = 0x2600B0,
    V_GOAL = 0x2600EA, V_RETIRE = 0x2600EC, V_RETIRE2 = 0x2600E2, V_FRAME_TGL = 0x2607FE,
    V_ROADY = 0x261200;
static const uint32_t W_INGAME = 0x060408, W_SC_ACTIVE = 0x060412, W_TIMER = 0x060500,
    W_SPRITES = 0x061400, W_DRAWLISTS = 0x061C00, W_PKT = 0x060640;
static const uint32_t SUB_PATH_PTR = 0x2604F0;          // the course's road path (second CPU's $0604F0)

// object slots ($63800 + 64 * n) and the tasks expected in them
static const uint32_t SLOT_SOUND = 0x063840, SLOT_SPEED = 0x063A00, SLOT_CENTRIFUGAL = 0x063A40,
    SLOT_BIKE = 0x064080;
static const uint32_t SLOT_CRASH[4] = { 0x0638C0, 0x063900, 0x063940, 0x063980 };
static const uint32_t TASK_SOUND = 0x5B98, TASK_SPEED = 0x610A, TASK_CENTRIFUGAL = 0x63CE, TASK_BIKE = 0x7AE6;

// hooked main-CPU addresses
static const uint32_t PC_DISPATCH = 0x1212;              // jsr (a0): a5 = slot, a0 = task
static const uint32_t PC_DISPATCH_NEXT = 0x1214;
static const uint32_t PC_SPRITE_DONE1 = 0x2B66, PC_SPRITE_DONE2 = 0x2B78;   // sub_02994 after the entry is written
static const uint32_t PC_RIVAL_CMP = 0x750A;             // cmp.w d0,d1: |x - $160| against allowance d0
static const uint32_t PC_RIVAL_HIT = 0x7510, PC_RIVAL_HIT_END = 0x753C;
static const uint32_t PC_RIVAL_SOUND = 0x7554, PC_RIVAL_SOUND_END = 0x7558;
static const uint32_t PC_DUST_DRAW = 0x811E, PC_DUST_DRAW_END = 0x8122;
static const uint32_t PC_SOUND_QUEUE = 0x28B0;           // sub_028b0: d0 = command
// BALANCE's pace (crossover/balance_pace): $260078 is the speed the world moves by (the rivals $7716, the roadside
// objects $6F4C, $725E and $B63C, the score $5C4E, the road's stripes); the speedometer shows the vehicle's own
static const uint32_t PC_HUD_SPEED = 0x1566, PC_HUD_SPEED_NEXT = 0x156C;   // move.w $260078,d0: x 279/240, shown
static const uint32_t PC_HUD_TOP = 0x1588, PC_HUD_TOP_LOW = 0x15B0, PC_HUD_TOP_HIGH = 0x1592;   // cmpi.w #$F0,$260078; blt
static const uint32_t PC_SCENERY_CAP = 0x6F58, PC_SCENERY_CAP_END = 0x6F66;   // the roadside objects' speed held to 279
static const int SCENERY_TABLE_TOP = 349;                // the approach table $C348 (speed / 2) has 350 entries

// ROM tables
static const uint32_t ROM_SPR_LEAN = 0xCA4E;             // the bike's lean frames (definition pointers)
static const uint32_t ROM_RIVAL_SPEED = 0x870E;          // rivals' speeds by difficulty, course, stage
static const uint32_t ROM_RIVAL_DIV = 0x85C4;

// OutRun: the Ferrari treats Super Hang-On's rivals as its own traffic. OutRun's per-type spin chance
// (traffic property +5) is 1 for its cars and 3 for its trucks (ROM $4CFA); the rivals are bikes, so
// they get the cars' value.
static const uint16_t RIVAL_COLLISION_MASK = 1;

// OutRun single road of half-width $40 (OutRun road_width units): OFerrari::check_wheels puts one
// wheel off at |x| > $40 + $D4 and both off at |x| > $40 + $104, i.e. the edge midpoint at $12C = 300,
// Super Hang-On's own road edge (sub_07F30: off road at |X| > 300). set_ferrari_bounds then keeps
// the car within $40 + $1F4 = 564 of the centre, as far past that edge as OutRun allows.
static const int32_t ROAD_WIDTH = 0x40;

// ------------------------------------------------------------------------------------------------
static inline uint8_t* loc(uint32_t a)
{
    a &= 0xFFFFFF;
    if (a >= 0x260000 && a < 0x268000) return shboard.cpu1ram + (a - 0x260000);
    if (a >= 0x060000 && a < 0x068000) return shboard.workram + (a - 0x060000);
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
    const std::vector<uint8_t>& r = shboard.rom_data;
    return a + 1 < r.size() ? (uint16_t)(r[a] << 8 | r[a + 1]) : 0;
}
static inline uint32_t ROML(uint32_t a) { return (uint32_t) ROMW(a) << 16 | ROMW(a + 2); }

static bool dbg() { static int d = getenv("CB_SHFERRARI_DEBUG") ? 1 : 0; return d; }
static int16_t sh_path_word(void*, uint32_t off);

// MAD POWER (docs/CROSSOVER-OUTRUNNERS.md section 10): lateral units OutRun's (Super Hang-On's) x 11/6; Super
// Hang-On's road edge for its bike (sub_07F30: off the road at |X| > 300)
static const int LAT_OR = 11, LAT_OUT = 6, SPEED_PER_KMH = 136, SH_EDGE = 300;

// ------------------------------------------------------------------------------------------------
// Start and end
// ------------------------------------------------------------------------------------------------
void SHFerrari::begin()
{
    if (!enabled) return;
    balance::set_world(vehicle == MAD_POWER ? balance::MAD_POWER : balance::FERRARI, balance::SUPER_HANG_ON);
    mem.rom = shboard.rom_data.data(); mem.rom_len = shboard.rom_data.size();
    mem.work = shboard.workram;
    mem.shared = shboard.cpu1ram; mem.shared_len = sizeof shboard.cpu1ram;
    mem.offroad_fn = nullptr; mem.offroad_ctx = nullptr;
    mem.sound_fn = nullptr; mem.sound_ctx = nullptr;

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
    std::memset(rival_lat, 0, sizeof rival_lat);
    std::memset(crash_entry, 0, sizeof crash_entry);
    board_frame = 0;
    if (vehicle == MAD_POWER)
    {
        // OutRunners' car in OutRun's car slot; OutRun's engine and tyre sounds off (its board plays them)
        madpower.begin_board(sh_path_word);
        osoundint.engine_data[sound::ENGINE_PITCH_H] = 0;
        osoundint.engine_data[sound::ENGINE_PITCH_L] = 0;
        osoundint.engine_data[sound::ENGINE_VOL] = 0;
    }

    static const uint32_t pcs[] = { PC_DISPATCH, PC_SPRITE_DONE1, PC_SPRITE_DONE2, PC_RIVAL_CMP, PC_RIVAL_HIT,
                                    PC_RIVAL_SOUND, PC_DUST_DRAW, PC_SOUND_QUEUE, PC_HUD_SPEED, PC_HUD_TOP,
                                    PC_SCENERY_CAP };
    shboard.set_pc_hooks(pcs, (int)(sizeof pcs / sizeof pcs[0]), &SHFerrari::hook, this);
}

void SHFerrari::end()
{
    balance::clear_world();
    shboard.set_pc_hooks(nullptr, 0, nullptr, nullptr);
    if (enabled && vehicle == MAD_POWER) madpower.end_board();
    shvideo.overlay = nullptr;
    osoundint.engine_data[sound::ENGINE_PITCH_H] = 0;
    osoundint.engine_data[sound::ENGINE_PITCH_L] = 0;
    osoundint.engine_data[sound::ENGINE_VOL] = 0;
    osoundint.queue_sound(sound::STOP_SLIP);
    osoundint.queue_sound(sound::STOP_SAFETYZONE);
}

// ------------------------------------------------------------------------------------------------
// Hooks
// ------------------------------------------------------------------------------------------------
void SHFerrari::hook(void* ctx, uint32_t pc) { ((SHFerrari*) ctx)->on_pc(pc); }

void SHFerrari::on_pc(uint32_t pc)
{
    switch (pc)
    {
        case PC_DISPATCH:
        {
            uint32_t a5 = shboard.main_reg(M68K_REG_A5) & 0xFFFFFF, a0 = shboard.main_reg(M68K_REG_A0) & 0xFFFFFF;
            if (in_sound_task) { in_sound_task = false; silence_packet(); }
            if (a5 == SLOT_SOUND && a0 == TASK_SOUND) board_frame++;
            bool was = racing;
            racing = RW(W_INGAME) != 0 && (RW(V_MODE) == 1 || RW(V_MODE) == 2);
            if (was && !racing && vehicle == MAD_POWER) madpower.engine_off();      // the race is over
            if (racing && !was)                                   // on the start line: where the game put its vehicle
            {
                oinitengine.car_x_pos = oinitengine.car_x_old = (int16_t) RW(V_X);
                crash_count = RW(V_CRASH_COUNT);
                if (dbg()) fprintf(stderr, "shferrari: race from X %d\n", oinitengine.car_x_pos);
            }
            if (on_task(a5, a0)) shboard.set_main_reg(M68K_REG_PC, PC_DISPATCH_NEXT);
            break;
        }

        case PC_SPRITE_DONE1:
        case PC_SPRITE_DONE2:
            // the bike slot's sprite entry: tag it as the place for OutRun's sprites
            if (racing && (shboard.main_reg(M68K_REG_A5) & 0xFFFFFF) == SLOT_BIKE)
            {
                uint32_t e = W_SPRITES + RW(SLOT_BIKE + 6) * 12;
                WW(e + 4, (uint16_t)(RW(e + 4) | 0x4000));              // hide: the board draws nothing
                WW(e + 6, SHVideo::TAG_ADDR);
            }
            break;

        case PC_RIVAL_CMP:
            // d0 = allowance + 10 (the bike's part, $7506), d1 = |rival x - $160|. OutRun's rule for the
            // Ferrari against a car: hit within 0.6875 of its width either side (OTraffic::check_collision:
            // w/2 + w/8 + w/16), i.e. 11/8 of the half-width the allowance gives.
            if (racing)
            {
                if (vehicle == MAD_POWER) rival_lateral(shboard.main_reg(M68K_REG_A5) & 0xFFFFFF);
                // (MAD POWER is the Ferrari's width, docs/CROSSOVER-OUTRUNNERS.md 9.4: the same rule)
                int32_t half = (int16_t)(shboard.main_reg(M68K_REG_D0) & 0xFFFF) - 10;
                int32_t w = half * 11 / 8;
                uint32_t d0 = shboard.main_reg(M68K_REG_D0);
                shboard.set_main_reg(M68K_REG_D0, (d0 & 0xFFFF0000) | (uint16_t) w);
            }
            break;

        case PC_RIVAL_HIT:
            // contact: OutRun's traffic response instead of Super Hang-On's knockback
            if (racing)
            {
                rival_contact(shboard.main_reg(M68K_REG_A5) & 0xFFFFFF);
                shboard.set_main_reg(M68K_REG_PC, PC_RIVAL_HIT_END);    // d0 = $89: marks the contact
            }
            break;

        case PC_RIVAL_SOUND:
            // a contact starts: OutRun's rebound sound instead of Super Hang-On's
            if (racing)
            {
                if (vehicle == FERRARI) osoundint.queue_sound(sound::REBOUND);  // MAD POWER: its own crash sounds
                shboard.set_main_reg(M68K_REG_PC, PC_RIVAL_SOUND_END);
            }
            break;

        case PC_DUST_DRAW:
            if (racing) shboard.set_main_reg(M68K_REG_PC, PC_DUST_DRAW_END);
            break;

        case PC_HUD_SPEED:
        case PC_HUD_TOP:
        case PC_SCENERY_CAP:
            pace_hook(pc);
            break;

        case PC_SOUND_QUEUE:
            // the sound task's effects for the bike: squeal $8A/$8B, off-road $8C/$8D
            if (racing && in_sound_task)
            {
                uint8_t c = (uint8_t) shboard.main_reg(M68K_REG_D0);
                if (c >= 0x8A && c <= 0x8D)
                {
                    uint32_t sp = shboard.main_reg(M68K_REG_A7);
                    uint32_t ret = shboard.read(0, sp, 4, 0);
                    shboard.set_main_reg(M68K_REG_A7, sp + 4);              // rts
                    shboard.set_main_reg(M68K_REG_PC, ret);
                }
            }
            break;
    }
}

bool SHFerrari::on_task(uint32_t a5, uint32_t entry)
{
    if (a5 == SLOT_SOUND && entry == TASK_SOUND) { in_sound_task = racing; return false; }
    if (!racing)
    {
        // The bike's crash objects stay off for the rest of the game (the goal sequence, mode 3, and after):
        // they start a crash whenever the scenery count $260084 differs from their copy ($9FE6), which Super
        // Hang-On's scenery test kept counting for the vehicle while they were off; a crash would switch the
        // bike's task back on ($A09C), and it would stop the goal run ($7E2C: speed 0) before its end
        if (RW(W_INGAME))
            for (int i = 0; i < 4; i++) if (a5 == SLOT_CRASH[i] && entry == crash_entry[i]) return true;
        return false;
    }
    if (a5 == SLOT_SPEED && entry == TASK_SPEED)             { speed_task(a5); return true; }
    if (a5 == SLOT_CENTRIFUGAL && entry == TASK_CENTRIFUGAL) return true;
    if (a5 == SLOT_BIKE && entry == TASK_BIKE)               { bike_task(a5); return true; }
    for (int i = 0; i < 4; i++) if (a5 == SLOT_CRASH[i]) { crash_entry[i] = entry; return true; }
    return false;
}

// The packet's player part ($060641-$060643: mode, engine pitch, throttle), written by the sound task
// for the sound board's bike engine: the engine the sound board plays when no race runs.
void SHFerrari::silence_packet()
{
    WB(W_PKT + 1, 0);
    WB(W_PKT + 2, 0);
    WB(W_PKT + 3, 0);
}

// ------------------------------------------------------------------------------------------------
// Slot 8: speed and track progress
// ------------------------------------------------------------------------------------------------
void SHFerrari::speed_task(uint32_t a5)
{
    if (RW(V_MODE) == 1)                                          // $63A4: start line, nothing moves
    {
        WW(V_FRAME_TGL, RW(V_FRAME_TGL) ^ 0x400);
        return;
    }
    if (RW(V_GOAL))
    {
        // sub_0B430 (d0 = 4): Super Hang-On steers its vehicle to the centre after the goal...
        int16_t x = oinitengine.car_x_pos;
        if ((x < 0 ? -x : x) >= 8) oinitengine.car_x_pos = (int16_t)(x + (x > 0 ? -4 : 4));
        // ...and takes it off the course a little way on ($6124)
        if ((int16_t) RW(V_POS) > 0x23) { WW(V_RETIRE, 0xFFFF); WW(V_RETIRE2, 0xFFFF); }
    }
    WL(V_SPEED, balance::world_speed(oinitengine.car_increment));   // the speed the world moves by (BALANCE's pace)
    WB(W_SC_ACTIVE, 0);
    WW(V_X, (uint16_t) oinitengine.car_x_pos);
    // OutRun's distance for its last tick, spread over the board frames until the next one
    uint32_t d = owed_frames > 0 ? owed / (uint32_t) owed_frames : owed;
    owed -= d;
    if (owed_frames > 0) owed_frames--;
    sho_track_advance(&mem, a5, d);
}

// BALANCE's pace, when it changes anything: the speedometer shows the vehicle's own speed (in the race the car's,
// after it the board's speed taken back to it); the roadside objects approach at the world's speed up to the end
// of their table (Super Hang-On holds its own bike's to its 279, $85A6: the speed it never passes)
void SHFerrari::pace_hook(uint32_t pc)
{
    if (balance::pace_now() == 1.0) return;
    uint32_t d0 = shboard.main_reg(M68K_REG_D0);
    uint16_t own = (uint16_t)((racing ? (uint32_t) oinitengine.car_increment : balance::own_speed(RL(V_SPEED))) >> 16);
    switch (pc)
    {
        case PC_HUD_SPEED:
            shboard.set_main_reg(M68K_REG_D0, (d0 & 0xFFFF0000) | own);
            shboard.set_main_reg(M68K_REG_PC, PC_HUD_SPEED_NEXT);
            break;
        case PC_HUD_TOP:
            shboard.set_main_reg(M68K_REG_PC, (int16_t) own < 0xF0 ? PC_HUD_TOP_LOW : PC_HUD_TOP_HIGH);
            break;
        case PC_SCENERY_CAP:
            if ((int16_t)(d0 & 0xFFFF) > SCENERY_TABLE_TOP) d0 = (d0 & 0xFFFF0000) | SCENERY_TABLE_TOP;
            shboard.set_main_reg(M68K_REG_D0, d0);
            shboard.set_main_reg(M68K_REG_PC, PC_SCENERY_CAP_END);
            break;
    }
}

// ------------------------------------------------------------------------------------------------
// Slot 34: the vehicle
// ------------------------------------------------------------------------------------------------
void SHFerrari::bike_task(uint32_t a5)
{
    if (slot_depth < 0) slot_depth = RW(a5 + 0x14);

    if (RW(V_RETIRE))                                             // $7E1E: off the course after the goal
    {
        if (vehicle == MAD_POWER && (RB(a5) & 0x80)) madpower.engine_off();
        WB(0x064140, (uint8_t)(RB(0x064140) | 0x80));
        WB(a5, (uint8_t)(RB(a5) & 0x7F));
        WL(V_SPEED, 0);
        oinitengine.car_increment = 0;
        register_sprite(a5);
        return;
    }

    if (sub_frame == 0) tick();
    sub_frame ^= 1;

    // what the rest of Super Hang-On reads about its vehicle
    WW(V_X, (uint16_t) oinitengine.car_x_pos);
    if (vehicle == MAD_POWER)                                     // Super Hang-On's own rule for its vehicle ($7F30)
        WW(V_OFFROAD, std::abs((int) oinitengine.car_x_pos) > SH_EDGE ? 0xFFFF : 0);
    else
        WW(V_OFFROAD, oferrari.wheel_state == OFerrari::WHEELS_OFF ? 0xFFFF : 0);
    WW(V_KNOCK, 0);
    WW(V_CRASH, 0);
    WB(V_SPR_LEAN, 0); WB(V_SPR_LEAN + 1, 0);
    WW(V_SPR_X, 0x160); WW(V_SPR_Y, 0xDF);

    // its object: drawn as OutRun's sprites (tagged in sub_02994). At its own depth in the sprite list,
    // or, while OutRun's crash throws the car up the road, at the crashed car's depth.
    WL(a5 + 0x1A, ROML(ROM_SPR_LEAN));
    WW(a5 + 0x0A, 0x160);
    WW(a5 + 0x0C, 0xDF);
    int depth = slot_depth;
    const oentry& c = osprites.jump_table[OSprites::SPRITE_CRASH];
    if ((c.control & OSprites::ENABLE) && ocrash.crash_counter)
    {
        int k = std::min<int>(c.priority & 0x1FF, 0x1FD);         // OutRun road index (0x1FD = the car)
        depth = std::min(slot_depth, 0x80 + k * 0xBE / 0x1FD);
    }
    WW(a5 + 0x14, (uint16_t) depth);

    if (RW(V_MODE) == 1)
    {
        // $7E38: on the start line the vehicle blinks until the race starts
        uint8_t n = (uint8_t)(RB(a5 + 0x38) - 1), b39;
        WB(a5 + 0x38, n);
        if (n == 0) { WB(a5 + 0x38, 0x0C); WB(a5 + 0x39, (uint8_t)(RB(a5 + 0x39) + 1)); }
        b39 = RB(a5 + 0x39);
        if ((int8_t) b39 > 0x0A || !(b39 & 1)) register_sprite(a5);
        return;
    }
    register_sprite(a5);
}

// sub_07A72: the object into the draw list of its depth ($61C00 + (depth - $80) * 32: count, slots)
void SHFerrari::register_sprite(uint32_t a5)
{
    uint32_t list = W_DRAWLISTS + (uint32_t)(int32_t)(int16_t)(uint16_t)((RW(a5 + 0x14) - 0x80) << 5);
    uint16_t n = RW(list);
    WW(list, (uint16_t)(n + 1));
    WW(list + 2 + 2 * n, (uint16_t)(a5 - 0x063800));
}

// ------------------------------------------------------------------------------------------------
// OutRun's car logic, one tick (OutRun's game logic runs at 30 Hz: every other board frame)
// ------------------------------------------------------------------------------------------------
void SHFerrari::world_to_outrun()
{
    if (getenv("CB_SHF_FREEZE_TIME")) WB(W_TIMER, 0x50);         // testing: reach the goal
    outrun.tick_frame = true;
    outrun.game_state = RW(V_MODE) == 2 ? GS_INGAME : GS_START1;
    oinitengine.road_type       = (int16_t) RW(V_CURVE);           // same list format in both games
    oinitengine.road_curve      = (int16_t) RW(V_CURVE_R);
    oinitengine.road_type_next  = (int16_t) RW(V_CURVE_NEXT);
    oinitengine.road_curve_next = (int16_t) RW(V_CURVE_NEXT_R);
    oinitengine.rd_split_state  = OInitEngine::SPLIT_NONE;
    oroad.road_ctrl  = ORoad::ROAD_BOTH_P0;
    oroad.road_width = ROAD_WIDTH << 16;
    oferrari.road_width_old = ROAD_WIDTH;
    oroad.road_pos = RL(V_POS);
    ostats.cur_stage = RW(V_STAGE_DONE) ? 1 : 0;                   // OFerrari's start-line steering limit
    ostats.time_counter = RB(W_TIMER);                              // 0 = time over: no accelerator
    // a scenery hit (Super Hang-On's test, $6EFA) -> OutRun's crash
    uint16_t cc = RW(V_CRASH_COUNT);
    if (cc != crash_count) { olevelobjs.collision_sprite = 1; crash_count = cc; }
    fill_road_heights();
    for (int i = 0; i < 512; i++) oroad.road_x[i] = (int16_t) road_x[i];
}

void SHFerrari::tick()
{
    if (vehicle == MAD_POWER) { tick_madpower(); return; }
    world_to_outrun();
    uint32_t pos0 = oroad.road_pos;

    // OInitEngine::update_engine, the car block
    oferrari.move();
    if (oferrari.car_ctrl_active)
    {
        balance::ferrari_curve_adjust();        // oferrari.set_curve_adjust(), BALANCE's curve push
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
            s = balance::contact_cut(balance::FERRARI, oinitengine.car_increment >> 16, s, hits[i].speed);
            oinitengine.car_increment = (s << 16) | (oinitengine.car_increment & 0xFFFF);
            oferrari.car_inc_old = s;
            otraffic.collision_traffic++;
        }
        if (dbg()) fprintf(stderr, "shferrari: rival contact side %d rival %d km/h -> skid %d\n", hits[i].side, hits[i].speed, ocrash.skid_counter);
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
            fprintf(stderr, "shferrari: tick %d x %d spd %d gear %d acc %d wheels %d crash %d/%d skid %d pos %08x curve %d/%d push %d spr %d\n", n,
                    oinitengine.car_x_pos, oinitengine.car_increment >> 16, oinputs.gear, oinputs.acc_adjust, oferrari.wheel_state,
                    ocrash.crash_counter, ocrash.coll_count1, ocrash.skid_counter, RL(V_POS), oinitengine.road_type, oinitengine.road_curve,
                    oroad.road_x[170], osprites.sprite_count);
        static uint16_t last_mode = 0xFFFF;
        if (RW(V_MODE) != last_mode) { fprintf(stderr, "shferrari: mode %d\n", RW(V_MODE)); last_mode = RW(V_MODE); }
    }
}

// MAD POWER: OutRunners' car code (crossover/madpower) in OutRun's car slot, the world as for the Ferrari
void SHFerrari::tick_madpower()
{
    madpower.board_pitch = hill_pitch();
    world_to_outrun();
    uint32_t pos0 = oroad.road_pos;
    // a scenery hit (Super Hang-On's test, every frame of an overlap): MAD POWER's own response (kind 0)
    if (olevelobjs.collision_sprite) { madpower.scenery_hit(); olevelobjs.collision_sprite = 0; }
    // a rival's contact: a car of its race (kind 2): its lateral position and lateral speed (Super Hang-On's
    // $34, the same units and centre as X, + = right as OutRunners' lateral; its change a frame x 2 for the
    // 30 Hz tick), its speed
    for (int i = 0; i < n_hits; i++)
        madpower.rival_hit((int16_t)(hits[i].lat * LAT_OR / LAT_OUT), (int16_t)(hits[i].latvel * 2 * LAT_OR / LAT_OUT),
                           (uint16_t) std::max(0, std::min(0xFFFF, hits[i].speed * SPEED_PER_KMH)));
    n_hits = 0;
    madpower.accel_cut = ostats.time_counter == 0;               // time over: no accelerator, as the Ferrari
    madpower.tick();

    // its distance for this tick, given to the track over the next two board frames
    owed += oroad.road_pos - pos0;
    owed_frames = 2;

    if (dbg())
    {
        static int n = 0;
        if ((++n % 30) == 0)
            fprintf(stderr, "shferrari: MAD POWER tick %d x %d spd %d pos %08x curve %d/%d\n", n, oinitengine.car_x_pos,
                    oinitengine.car_increment >> 16, RL(V_POS), oinitengine.road_type, oinitengine.road_curve);
    }
}

// The road's pitch at the vehicle from Super Hang-On's hills (docs/CROSSOVER-OUTRUNNERS.md 10.1). Its second CPU
// runs a hill as a vertical path (its object at $060420: +4 state, 2 while a hill runs; +8 the distance into it,
// positions in the high word; +$C the path, from the table $1A4C by the hill number $06004C): unit vectors of
// 4096, x the height (+ = up) and y the distance along, one a position, like the road's curve path. Its camera
// takes its direction from the two vectors at the vehicle ($1650-$1674) and projects the vectors ahead with a
// focal length of 64 pixels ($174A-$1778, then / 256 into the height table $061200, sub_01494); OutRunners'
// is 192 pixels: a hill that looks as steep has tan(pitch) = height / along x 64 / 192.
int16_t SHFerrari::hill_pitch() const
{
    if (RB(0x260424) != 2) return 0;                              // no hill running: level
    uint32_t a = (RL(0x26042C) + (uint32_t) RW(0x260428) * 4u) & 0xFFFFFF;
    const std::vector<uint8_t>& r = shboard.sub_rom;
    auto sw = [&](uint32_t o) { return o + 1 < r.size() ? (int)(int16_t)(r[o] << 8 | r[o + 1]) : 0; };
    int up = sw(a) + sw(a + 4), along = sw(a + 2) + sw(a + 6);
    if (along <= 0) return 0;
    double t = (double) up / along * 64.0 / 192.0;
    return (int16_t) std::lround(-std::atan(t) * 4096.0 / (2.0 * M_PI));
}

// OutRun's road builder (ORoad::setup_x_data, verified as sho_road_build_outrun) over Super Hang-On's
// path: OFerrari::set_curve_adjust reads its slice 170 (and 511, which the builder never writes).
// Rebuilt when the position changes, keeping the slices it does not reach, as OutRun does.
static int16_t sh_path_word(void*, uint32_t off)
{
    uint32_t base = RL(SUB_PATH_PTR);
    const std::vector<uint8_t>& r = shboard.sub_rom;
    uint32_t a = (base + off) & 0xFFFFFF;
    return a + 1 < r.size() ? (int16_t)(r[a] << 8 | r[a + 1]) : 0;
}

void SHFerrari::build_road_x()
{
    uint32_t pos = RL(V_POS) >> 16;
    if (pos == road_x_pos) return;
    road_x_pos = pos;
    sho_path_reader pr = { sh_path_word, nullptr };
    sho_road_build_outrun(&pr, pos << 2, road_x);
}

// The road heights OutRun's crash and smoke objects stand on (oroad.road_y, from road_p0): Super Hang-On's
// depth -> height table ($261200, same format: height above the bottom line << 4), with OutRun's road
// index k (0 = horizon, $1FD = the car; 0-$1FF) at Super Hang-On's depth k * $BE / $1FD (the bike's mapping,
// crossover/bike.cpp depth_to_index, the other way round).
void SHFerrari::fill_road_heights()
{
    int16_t* ry = oroad.road_y + oroad.road_p0;
    for (int k = 0; k < 0x200; k++)
    {
        int d = std::min(k * 0xBE / 0x1FD, 0xFF);
        ry[k] = (int16_t) RW(V_ROADY + 2 * d);
    }
    // OutRun keeps a list of hill crests from +$280 for hiding sprites behind them (OSprites::do_sprite);
    // empty = a flat road: the car's sprites are not clipped by OutRun's hills (Super Hang-On's are
    // drawn as Super Hang-On draws them)
    ry[0x280] = ry[0x281] = 0;
}

// ------------------------------------------------------------------------------------------------
// Rivals
// ------------------------------------------------------------------------------------------------
void SHFerrari::rival_contact(uint32_t a5)
{
    for (int i = 0; i < n_hits; i++) if (hits[i].slot == a5) return;     // once per OutRun tick
    if (n_hits >= 8) return;
    int16_t x = (int16_t)(RW(a5 + 0x0A) - 0x160);                  // screen x from the centre (+ = right)
    int16_t lat = (int16_t) RW(a5 + 0x34), v = 0;
    for (const RivalLat& r : rival_lat) if (r.slot == a5) v = r.latvel;
    hits[n_hits++] = { a5, x < 0 ? -1 : 1, rival_speed(a5), lat, v };
    if (dbg()) fprintf(stderr, "shferrari: rival contact: slot %06x screen dx %d lateral $34 %d (v %d) depth %x, X %d\n", a5, x, lat, v,
                       RW(a5 + 0x14), oinitengine.car_x_pos);
}

// A rival's lateral position ($34, steered by its task $75A0-$765E) at each frame's contact test, for its
// lateral speed (its change since the last frame)
void SHFerrari::rival_lateral(uint32_t a5)
{
    int16_t lat = (int16_t) RW(a5 + 0x34);
    RivalLat* e = nullptr;
    for (RivalLat& r : rival_lat) if (r.slot == a5) { e = &r; break; }
    if (!e) for (RivalLat& r : rival_lat) if (r.slot == 0 || board_frame - r.frame > 2) { e = &r; e->slot = a5; e->frame = 0; break; }
    if (!e) return;
    if (e->frame != board_frame)
    {
        e->latvel = e->frame + 1 == board_frame ? (int16_t)(lat - e->lat) : 0;
        e->lat = lat;
        e->frame = board_frame;
    }
}

// A rival's speed in km/h. Its distance changes per frame ($7716-$773A) by
//   d0 + ((player speed - S) * 10) >> 4,   S = $870E[difficulty, course, stage], d0 = $36 (+ $32 term)
// so it keeps pace with the player at player speed S - 1.6 * d0.
int SHFerrari::rival_speed(uint32_t a5)
{
    uint32_t d1 = (RW(0x060450) & 6) * 36 + RW(0x260012) * 18 + RW(V_STAGE_DONE) * 2;
    int s = (int16_t) ROMW(ROM_RIVAL_SPEED + d1);
    int d0 = (int16_t) RW(a5 + 0x36);
    int t32 = RW(a5 + 0x32);
    if (t32)
    {
        uint16_t div = ROMW(ROM_RIVAL_DIV);
        int q = div ? t32 / div : 0;
        d0 += std::min(q, 0x26);
    }
    return s - d0 * 16 / 10;
}

// ------------------------------------------------------------------------------------------------
// Display
// ------------------------------------------------------------------------------------------------
void SHFerrari::after_frame()
{
    if (!enabled) return;
    std::memset(overlay, 0, sizeof overlay);
    if (racing && vehicle == MAD_POWER) madpower.to_overlay(overlay, shvideo.width, SHVideo::H, shvideo.xoff);
    else if (racing) video.sprite_layer->render_to(overlay, shvideo.width, SHVideo::H, shvideo.xoff, 8);
    shvideo.overlay = racing ? overlay : nullptr;
    if (dbg())
    {
        static int n = 0;
        int px = 0;
        for (int i = 0; i < shvideo.width * SHVideo::H; i++) px += overlay[i] != 0;
        if ((++n % 300) == 0)
            fprintf(stderr, "shferrari: overlay %d px, placed last frame %d\n", px, shvideo.overlay_placed);
    }
}

void SHFerrari::colours(uint32_t* rgb)
{
    for (uint32_t i = 0; i < 0x1000; i++) rgb[0x2000 + i] = video.own_rgb((uint16_t) i);
}
