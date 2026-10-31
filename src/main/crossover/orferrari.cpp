/***************************************************************************
    OutRun's Ferrari as the vehicle on OutRunners' courses. See orferrari.hpp, and
    docs/CROSSOVER-OUTRUNNERS.md section 7 for the addresses and conversions.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#include "crossover/orferrari.hpp"
#include "crossover/orboard.hpp"

#include <cmath>
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

ORFerrari orferrari;

// ------------------------------------------------------------------------------------------------
// OutRunners addresses (player 1; docs/OUTRUNNERS-RE.md)
// ------------------------------------------------------------------------------------------------
static const uint32_t P1_BASE = 0x208000;               // R25 in player 1's task
static const uint32_t A_STATE = 0x208080;               // game state: 6 game
static const uint32_t A_STAGE = 0x2080A2;               // stage number from the course script's header
static const uint32_t A_LISTPTR = 0x2080C0;             // the sprite list pointer (the last entry written)
static const uint32_t A_CARNUM = 0x208001;              // the player's car number (0 for player 1 alone)
static const uint32_t OBJ_CAR = 0x200400;               // the car object: flags (bit 2 collision), state +$0C
static const uint32_t O_STATE = 0x0C, O_LATVEL = 0x2E, O_KIND = 0x6E, O_ARG = 0x68, O_PUSH = 0xCE, O_HILL = 0xD9,
    O_SPRBD = 0xBD;
static const uint32_t A_SEGCNT = 0x209042, A_SEGPOS = 0x209034, A_SEGOFF = 0x209040, A_LAT = 0x209038,
    A_SPEED = 0x209074, A_RINGEND = 0x2090AA;
static const uint32_t A_COUNTDOWN = 0x20FB6A;           // the race countdown (1/30 s), master copy
static const uint32_t CARS = 0x20FC00;                  // the race's car array, $80 per car
static const uint32_t RING = 0x20A000;                  // the segment ring: 128 records of $20
static const uint32_t SEG_HEADING = 0x00, SEG_HALFWIDTH = 0x12;
static const uint32_t SND_ENGINE = 0x700710;            // shared RAM: player 1's engine pitch, $700712 on/off

// hooked V70 addresses
static const uint32_t PC_HANDLING = 0xA39C;             // the car's handling (states 1-4)
static const uint32_t PC_SPEED = 0xA477;                // movz.hw 74[R23], R0: the speed is used from here
static const uint32_t PC_ADVANCE = 0xA5EE;              // movs.hw R0, R0: R0 = lateral velocity for the advance
static const uint32_t PC_LATERAL = 0xA66E;              // mov.h 38[R23], R13: the lateral position is moved
static const uint32_t PC_COLLISION = 0x7348;            // test1 #2, [R20]: collision flagged -> $7F87
static const uint32_t PC_SPRITES_DONE = 0x9E48;         // $99B8 after the car's parts are written
static const uint32_t PC_PART_SPAWNED = 0x14971;        // $14960 loop: mov.w R20, 14[R21] (a game-over part)
static const uint32_t PC_OBJECT_DRAWN = 0xED6F;         // $ED50 after an object's sprites are written
static const uint32_t ROUTINE_PART_BODY = 0x149A1;      // the game-over part that draws the car's body
static const uint32_t PC_ENGINE = 0x121E8, PC_ENGINE_RET = 0x12215;     // engine parameters ($7392)
static const uint32_t PC_TYRES = 0x1222F, PC_TYRES_RET = 0x12234;       // tyre/skid parameter

// V60Cpu register numbers
static const int REG_R0 = 0, REG_R10 = 10, REG_R19 = 19, REG_R20 = 20, REG_R21 = 21, REG_R25 = 25, REG_PC = 32;

// ------------------------------------------------------------------------------------------------
// Conversions (docs/CROSSOVER-OUTRUNNERS.md 7.2)
// ------------------------------------------------------------------------------------------------
// Speed: OutRunners' speed is its advance per 30 Hz tick in course units (a segment is $8000 long on
// the centre line); its HUD shows speed / 136 as km/h ($C2E9). The Ferrari's km/h x 136.
static const int SPEED_PER_KMH = 136;
// Lateral: OutRunners' world units (the road's half-width is the segment's +$12, 440 for width code 5;
// MAD POWER's half-width 44, parameter +$1A and its collision box $12928) against OutRun's X (the
// Ferrari's half-width 24: OFerrari::check_wheels puts one wheel off 24 inside the road's edge and both
// 24 outside it). X x 44/24 = X x 11/6, with OutRun's X + = left and OutRunners' + = right.
static const int LAT_OR = 11, LAT_OUT = 6;
// OutRunners keeps its car within the segment's half-width + $60 ($A77C-$A79A)
static const int WORLD_MARGIN = 0x60;
// OutRun's road edge for a single road of road_width w: wheels off from w + $D4 to w + $104, the edge
// at w + $EC (OFerrari::check_wheels, ROAD_BOTH with w <= $FF)
static const int OUT_EDGE = 0xEC;
// Distance: OutRun advances CAR_BASE_INC = 303 per km/h per tick in its road position (16.16, the
// integer part indexing its path), OutRunners 136 per km/h per tick in $8000 per segment. At the same
// km/h one OutRun path position is 272/303 of a segment.
static const double SEG_PER_POS = 136.0 * 65536.0 / (303.0 * 32768.0);
static const int PATH_N = 72;                           // OutRun's builder reads 68 positions
// MAD POWER's hill frame index on a level road ($7B3F: $2004D9 = 1 on the pre-stage's level start)
static const int HILL_LEVEL = 1;
// OutRun's flat road heights (as crossover/hovehicle): $2400 per entry
static const int32_t FLAT_STEP = 0x2400;
// OutRun's collision gate after a scenery hit (OLevelObjs::COLLISION_RESET, ticks)
static const int16_t COLLISION_RESET = 4;
// The rivals are cars: OutRun's spin chance for its cars (traffic property +5: 1 cars, 3 lorries)
static const uint16_t CAR_COLLISION_MASK = 1;
// OutRun's Ferrari: bottom anchor of its sprite at screen y 221 (OFerrari::setup_ferrari_sprite), x 0 =
// the middle of OutRun's 320-pixel screen
static const int FERRARI_REF_X = 160, FERRARI_REF_Y = 221;

static bool dbg() { static int d = getenv("CB_ORF_DEBUG") ? 1 : 0; return d; }

// ------------------------------------------------------------------------------------------------
// Board memory (the V70 is little-endian)
// ------------------------------------------------------------------------------------------------
static ORBoard* B = nullptr;
static inline uint8_t  RB(uint32_t a) { return B->read8(a); }
static inline uint16_t RW(uint32_t a) { return (uint16_t)(B->read8(a) | B->read8(a + 1) << 8); }
static inline uint32_t RL(uint32_t a) { return (uint32_t) RW(a) | (uint32_t) RW(a + 2) << 16; }
static inline void WB(uint32_t a, uint8_t v) { B->write8(a, v); }
static inline void WW(uint32_t a, uint16_t v) { B->write8(a, (uint8_t) v); B->write8(a + 1, (uint8_t)(v >> 8)); }
static inline int16_t s12(uint16_t v) { v &= 0xFFF; return (int16_t)(v & 0x800 ? v - 0x1000 : v); }

static inline uint32_t ring_rec(uint32_t k)   // the k-th segment record from the car's
{
    return RING + ((RW(A_SEGOFF) + 0x20 * k) & 0xFFF);
}

// ------------------------------------------------------------------------------------------------
// Start and end
// ------------------------------------------------------------------------------------------------
void ORFerrari::begin(ORBoard* b)
{
    board = nullptr;
    if (!enabled || !b) return;
    board = b;
    B = b;

    // OutRun's objects for the car, as Outrun::init_jump_table sets them up (minus the course)
    outrun.tick_frame = true;
    osprites.init();
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
    // where the game put its car on the start line
    lat_prev = (int16_t) RW(A_LAT);
    oinitengine.car_x_pos = oinitengine.car_x_old = (int16_t)(-lat_prev * LAT_OUT / LAT_OR);
    oinitengine.car_increment = 0;
    oinitengine.rd_split_state = OInitEngine::SPLIT_NONE;
    osoundint.engine_data[sound::ENGINE_VOL] = 0x3F;   // as OInitEngine::init
    video.sprite_layer->reset();

    tick_no = 0;
    last_tick_frame = 0xFFFFFFFF;
    car_obj = body_part = 0;
    minor_ticks = 0;
    speed_prev = 0;
    n_hits = 0;
    layer.assign((size_t) IMG_W * IMG_H, 0);
    for (auto& i : img) i.assign((size_t) IMG_W * IMG_H, 0);

    // MAD POWER's engine off ($1219C started it with $700712 = $A0)
    WB(SND_ENGINE, 0);
    WB(SND_ENGINE + 2, 0);

    board->video().set_overlay([this](uint16_t id, S32Video::OverlayImage& o) {
        o.px = img[id % IMG_COUNT].data(); o.w = IMG_W; o.h = IMG_H; o.ref_x = FERRARI_REF_X; o.ref_y = FERRARI_REF_Y;
        return true;
    });
    static const uint32_t pcs[] = { PC_HANDLING, PC_SPEED, PC_ADVANCE, PC_LATERAL, PC_COLLISION, PC_SPRITES_DONE,
                                    PC_PART_SPAWNED, PC_OBJECT_DRAWN, PC_ENGINE, PC_TYRES };
    board->set_pc_hooks(pcs, (int)(sizeof pcs / sizeof pcs[0]), &ORFerrari::hook, this);
    if (dbg()) fprintf(stderr, "orferrari: race from lateral %d\n", lat_prev);
}

void ORFerrari::end()
{
    if (board)
    {
        board->set_pc_hooks(nullptr, 0, nullptr, nullptr);
        board->video().set_overlay(nullptr);
    }
    board = nullptr;
    if (!enabled) return;
    osoundint.engine_data[sound::ENGINE_PITCH_H] = 0;
    osoundint.engine_data[sound::ENGINE_PITCH_L] = 0;
    osoundint.engine_data[sound::ENGINE_VOL] = 0;
    osoundint.queue_sound(sound::STOP_SLIP);
    osoundint.queue_sound(sound::STOP_SAFETYZONE);
}

int ORFerrari::car_state() const { return RB(OBJ_CAR + O_STATE); }

bool ORFerrari::racing() const
{
    int s = car_state();
    return RB(A_STATE) == 6 && s >= 1 && s <= 4;
}

bool ORFerrari::driving() const { return enabled && board && racing(); }

// ------------------------------------------------------------------------------------------------
// Hooks
// ------------------------------------------------------------------------------------------------
void ORFerrari::hook(void* ctx, uint32_t pc) { ((ORFerrari*) ctx)->on_pc(pc); }

void ORFerrari::on_pc(uint32_t pc)
{
    if (pc == PC_SPRITES_DONE)
    {
        // player 1's own car (R19 = its entry in the car array), not drawn as another player's car
        uint32_t own = CARS + 0x80u * RB(A_CARNUM);
        uint32_t r20 = board->main_reg(REG_R20);
        if ((board->main_reg(REG_R19) & 0xFFFFFF) == own && RB(r20 + O_SPRBD) == 0)
        {
            car_obj = r20;
            tag_sprites(r20, RL(A_LISTPTR) + 0x10, board->main_reg(REG_R10), true);
        }
        return;
    }
    if (pc == PC_PART_SPAWNED)
    {
        // the game-over animation ($14900, which the car's sprite object becomes at $9A01) splits the car
        // into objects (R21, parent R20): the body's ($149A1) carries on as the Ferrari, the others hide
        if (car_obj && (board->main_reg(REG_R20) & 0xFFFFFF) == car_obj && RL(board->main_reg(REG_R21) + 4) == ROUTINE_PART_BODY)
            body_part = board->main_reg(REG_R21) & 0xFFFFFF;
        return;
    }
    if (pc == PC_OBJECT_DRAWN)
    {
        // $ED50, any object's sprites written ($ED6D) from 10[$2080C0] up to R10: the car's game-over parts
        uint32_t r20 = board->main_reg(REG_R20) & 0xFFFFFF;
        if (car_obj && RL(r20 + 0x14) == car_obj)
            tag_sprites(r20, RL(A_LISTPTR) + 0x10, board->main_reg(REG_R10), r20 == body_part);
        return;
    }
    if ((board->main_reg(REG_R25) & 0xFFFFFF) != P1_BASE) return;    // player 2's task
    switch (pc)
    {
        case PC_ENGINE:                                     // MAD POWER's engine: off
            WB(SND_ENGINE, 0);
            WB(SND_ENGINE + 2, 0);
            board->set_main_reg(REG_PC, PC_ENGINE_RET);
            return;
        case PC_TYRES:                                      // MAD POWER's tyres: silent
            board->set_main_reg(REG_PC, PC_TYRES_RET);
            return;
    }
    if (!racing()) return;
    int cs = car_state();
    switch (pc)
    {
        case PC_COLLISION:
            collision();
            break;

        case PC_HANDLING:
            // once per OutRunners tick (player 1's task runs every other frame)
            if (board->frame() != last_tick_frame)
            {
                last_tick_frame = (uint32_t) board->frame();
                tick();
            }
            break;

        case PC_SPEED:
            if (cs != 4)
            {
                uint32_t inc = (uint32_t) oinitengine.car_increment;
                uint32_t v = (inc >> 16) * SPEED_PER_KMH + (((inc & 0xFFFF) * SPEED_PER_KMH) >> 16);
                WW(A_SPEED, (uint16_t) std::min<uint32_t>(v, 0xFFFF));
            }
            break;

        case PC_ADVANCE:
            // the advance along the course is speed x cos(angle of the lateral velocity to the speed): the
            // Ferrari's speed is OutRun's speed along its road
            if (cs != 4) board->set_main_reg(REG_R0, 0);
            break;

        case PC_LATERAL:
            if (cs != 4)
            {
                int16_t lat = world_lateral();
                int16_t lv = (int16_t)(lat - lat_prev);
                lat_prev = lat;
                // $A66E: lateral = $209038 + lateral velocity $20042E + collision push $2004CE
                WW(OBJ_CAR + O_LATVEL, (uint16_t) lv);
                WW(OBJ_CAR + O_PUSH, 0);
                WW(A_LAT, (uint16_t)(lat - lv));
            }
            break;
    }
}

// The Ferrari's lateral position in OutRunners' units (+ = right), within the world's limit
int16_t ORFerrari::world_lateral() const
{
    int hw = RW(ring_rec(0) + SEG_HALFWIDTH);
    int lim = hw + WORLD_MARGIN;
    int lat = -(int) oinitengine.car_x_pos * LAT_OR / LAT_OUT;
    return (int16_t) std::max(-lim, std::min(lat, lim));
}

// ------------------------------------------------------------------------------------------------
// Collisions: OutRunners' tests decide that something was hit (and what), OutRun's rules what follows
// ------------------------------------------------------------------------------------------------
void ORFerrari::collision()
{
    uint16_t fl = RW(OBJ_CAR);
    if (!(fl & 4)) return;
    WW(OBJ_CAR, (uint16_t)(fl & ~4));                       // OutRunners' bounce ($7F87) does not run
    int kind = RB(OBJ_CAR + O_KIND);
    int own = (int16_t) RW(A_LAT);
    switch (kind)
    {
        case 0:                                             // scenery: OutRun's crash
            if (n_hits < 8) hits[n_hits++] = { 0, 0, 0 };
            break;
        case 3:                                             // minor contact: OutRunners' own answer is 15 ticks
            minor_ticks = 15;                               // of off-road drag ($7FAA, $B7F0); OutRun's off-road
            break;
        case 2:                                             // another car of the race ($200468 = its number)
        {
            uint32_t rec = CARS + 0x80u * RB(OBJ_CAR + O_ARG);
            int other = (int16_t) RW(rec + 0x44);
            int kmh = RW(rec + 0x4C) / SPEED_PER_KMH;
            if (n_hits < 8) hits[n_hits++] = { 2, other < own ? -1 : 1, kmh };
            break;
        }
        case 1:                                             // an object with its own position ($8029)
        {
            uint32_t obj = 0x200000 | RW(OBJ_CAR + O_ARG);
            int hw = RW(ring_rec(0) + SEG_HALFWIDTH);
            int other = (int16_t) RW(obj + 0x20) + hw * (int8_t) RB(obj + 0x3F);
            int kmh = (int)((uint32_t) RW(obj + 0x2C) << 8) / SPEED_PER_KMH;
            if (n_hits < 8) hits[n_hits++] = { 1, other < own ? -1 : 1, kmh };
            if (dbg()) fprintf(stderr, "orferrari: object %06x lateral %d speed word %04x (%d km/h)\n", obj, other, RW(obj + 0x2C), kmh);
            break;
        }
    }
    if (dbg()) fprintf(stderr, "orferrari: collision kind %d at lateral %d\n", kind, own);
}

// ------------------------------------------------------------------------------------------------
// OutRun's car logic, one tick
// ------------------------------------------------------------------------------------------------
void ORFerrari::world_to_outrun()
{
    int cs = car_state();
    outrun.tick_frame = true;
    outrun.game_state = cs == 1 ? GS_START1 : GS_INGAME;
    oinitengine.rd_split_state = OInitEngine::SPLIT_NONE;
    oroad.road_ctrl = ORoad::ROAD_BOTH_P0;
    // OutRun's road width so that its road edge (w + $EC) is OutRunners' (the half-width x 6/11)
    int hw = RW(ring_rec(0) + SEG_HALFWIDTH);
    int w = std::max(0, hw * LAT_OUT / LAT_OR - OUT_EDGE);
    oroad.road_width = (uint32_t) w << 16;
    oferrari.road_width_old = (int16_t) w;
    // OutRun's road position: the distance from the course's start in OutRun positions (its first
    // positions limit the steering on its first stage, OFerrari::set_ferrari_x)
    uint64_t units = (uint64_t) RW(A_SEGCNT) * 0x8000u + std::min<uint32_t>(RL(A_SEGPOS), 0x7FFF);
    oroad.road_pos = (uint32_t)(units * 2 * 303 / 272);
    ostats.cur_stage = RB(A_STAGE);
    // the time: 0 = time over, no accelerator (OutRun's rule); OutRunners' time-up state is 3
    ostats.time_counter = (RW(A_COUNTDOWN) && cs != 3) ? 1 : 0;
    static const bool freeze = getenv("CB_ORF_FREEZE_TIME") != nullptr;
    if (freeze) WW(A_COUNTDOWN, 1200);      // testing: reach the goal
    build_road();
}

void ORFerrari::tick()
{
    world_to_outrun();
    const int cs = car_state();
    tick_no++;

    // a scenery hit -> OutRun's crash (OFerrari::setup_ferrari_sprite starts it, OCrash takes a hit
    // during the crash as OutRun does), with OutRun's own gate: no new hit while its counter runs
    // (OLevelObjs::sprite_normal; OutRunners flags every tick of an overlap)
    for (int i = 0; i < n_hits; i++)
        if (hits[i].kind == 0 && olevelobjs.sprite_collision_counter == 0)
        {
            olevelobjs.collision_sprite++;
            olevelobjs.sprite_collision_counter = COLLISION_RESET;
        }

    if (cs == 4)
    {
        // The goal run: OutRunners' script moves the car (speed to a target, then a stop; steering to a
        // lateral target). The Ferrari follows it: OutRun's engine with the accelerator held while the
        // script speeds up (as OutRun's own goal run holds it), its speed and position the script's.
        uint16_t spd = RW(A_SPEED);
        oinputs.acc_adjust = spd > speed_prev ? 0xFF : 0;
        oinputs.brake_adjust = 0;
        oinputs.steering_adjust = 0;
        speed_prev = spd;
        oferrari.move();
        oinitengine.car_increment = (int32_t)(((uint32_t) spd << 16) / SPEED_PER_KMH);
        oferrari.car_inc_old = (uint16_t)(spd / SPEED_PER_KMH);
        oinitengine.car_x_pos = (int16_t)(-(int16_t) RW(A_LAT) * LAT_OUT / LAT_OR);
        lat_prev = (int16_t) RW(A_LAT);
        check_wheels();
    }
    else
    {
        speed_prev = RW(A_SPEED);
        // OInitEngine::update_engine, the car block
        oferrari.move();
        if (oferrari.car_ctrl_active)
        {
            oferrari.set_curve_adjust();
            oferrari.set_ferrari_x();
            oferrari.do_skid();
            check_wheels();
            if (minor_ticks > 0)
            {
                minor_ticks--;
                oferrari.set_wheels(OFerrari::WHEELS_OFF);
            }
            oferrari.set_ferrari_bounds();
            // and within OutRunners' limit for its car
            int hw = RW(ring_rec(0) + SEG_HALFWIDTH);
            int lim = (hw + WORLD_MARGIN) * LAT_OUT / LAT_OR;
            oinitengine.car_x_pos = (int16_t) std::max(-lim, std::min((int) oinitengine.car_x_pos, lim));
        }
    }
    oferrari.do_sound_score_slip();
    if (olevelobjs.spray_counter > 0) olevelobjs.spray_counter--;
    if (olevelobjs.sprite_collision_counter > 0) olevelobjs.sprite_collision_counter--;

    // OTraffic::check_collision's response, for the cars and moving objects touched
    for (int i = 0; i < n_hits; i++)
    {
        if (hits[i].kind == 0) continue;
        int d0 = hits[i].side < 0 ? -OCrash::SKID_RESET : OCrash::SKID_RESET;
        if (config.engine.bumper) d0 /= 3;
        d0 += ocrash.skid_counter;
        if (d0 <= OCrash::SKID_MAX && d0 >= -OCrash::SKID_MAX) ocrash.skid_counter = d0;
        otraffic.collision_mask = CAR_COLLISION_MASK;
        if (outrun.game_state == GS_INGAME)
        {
            int s = std::max(0, hits[i].speed - 80);
            oinitengine.car_increment = (s << 16) | (oinitengine.car_increment & 0xFFFF);
            oferrari.car_inc_old = s;
            otraffic.collision_traffic++;
            osoundint.queue_sound(sound::REBOUND);
        }
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
    render_overlay();

    static const char* trace = getenv("CB_ORF_TRACE");
    if (trace)                              // testing: one line per tick
    {
        static FILE* tf = fopen(trace, "w");
        if (tf)
        {
            fprintf(tf, "%u %llu %d %.3f %d %d %d %d %d %d %d %d %d %u %u %d %d %d %d %d %02x%02x %d\n", tick_no,
                    (unsigned long long) board->frame(), cs, oinitengine.car_increment / 65536.0, oinitengine.car_x_pos,
                    (int16_t) RW(A_LAT), RW(ring_rec(0) + SEG_HALFWIDTH), oinitengine.road_type, oinitengine.road_curve,
                    (int16_t)(oroad.road_x[170] - oroad.road_x[511]), oferrari.wheel_state, ocrash.crash_counter,
                    ocrash.skid_counter, RW(A_SEGCNT), RW(A_SPEED), RB(0x20060F), RB(0x208349), RB(0x20834A + std::max(0, RB(0x208349) - 1)),
                    RW(A_COUNTDOWN), RB(OBJ_CAR + O_HILL), RB(SND_ENGINE + 2), RB(SND_ENGINE), oinputs.gear);
            fflush(tf);
        }
    }
    if (dbg() && (tick_no % 15) == 0)
        fprintf(stderr, "orferrari: tick %u state %d kmh %d x %d lat %d hw %d curve %d/%d push %d hill %d wheels %d crash %d skid %d seg %u gear %d\n",
                tick_no, cs, oinitengine.car_increment >> 16, oinitengine.car_x_pos, (int16_t) RW(A_LAT),
                RW(ring_rec(0) + SEG_HALFWIDTH), oinitengine.road_type, oinitengine.road_curve,
                (int16_t)(oroad.road_x[170] - oroad.road_x[511]), RB(OBJ_CAR + O_HILL), oferrari.wheel_state,
                ocrash.crash_counter, ocrash.skid_counter, RW(A_SEGCNT), oinputs.gear);
}

// OutRun's wheel rule at OutRunners' road edge. OFerrari::check_wheels (a single road, ROAD_BOTH with
// road_width w <= $FF): one wheel off when |X| > w + $D4, both when |X| > w + $104, the edge at w + $EC.
// OutRun's roads are never narrower than w = 0 (edge 236 = OutRunners' 433); for OutRunners' narrower
// roads (half-widths down to 264) the same comparison is made here with the edge where it is.
void ORFerrari::check_wheels()
{
    int hw = RW(ring_rec(0) + SEG_HALFWIDTH);
    int w = hw * LAT_OUT / LAT_OR - OUT_EDGE;
    if (w >= 0) { oferrari.check_wheels(); return; }
    oferrari.wheel_state = OFerrari::WHEELS_ON;
    oferrari.wheel_traction = OFerrari::TRACTION_ON;
    int x = oinitengine.car_x_pos, edge = w + 0x104;
    if (x >= 0)
    {
        if (x > edge) oferrari.set_wheels(OFerrari::WHEELS_OFF);
        else if (x > edge - 0x30) oferrari.set_wheels(OFerrari::WHEELS_LEFT_OFF);
    }
    else
    {
        x = -x;
        if (x > edge) oferrari.set_wheels(OFerrari::WHEELS_OFF);
        else if (x > edge - 0x30) oferrari.set_wheels(OFerrari::WHEELS_RIGHT_OFF);
    }
}

// ------------------------------------------------------------------------------------------------
// The road for OutRun's car code: curvature from OutRunners' segment headings, OutRun's road builder
// over a path made from them, road heights
// ------------------------------------------------------------------------------------------------
static int16_t path_xy[PATH_N][2];

static int16_t path_word(void*, uint32_t off)
{
    uint32_t p = off >> 2;
    if (p >= (uint32_t) PATH_N) p = PATH_N - 1;
    return path_xy[p][(off >> 1) & 1];
}

void ORFerrari::build_road()
{
    // segments generated ahead of the car's ($2090AA counts them as $209042 does)
    int ahead = (int16_t)(RW(A_RINGEND) - RW(A_SEGCNT));
    ahead = std::max(1, std::min(ahead, 127));
    auto heading = [&](int k) { return (int) (RW(ring_rec((uint32_t) std::min(k, ahead)) + SEG_HEADING) & 0xFFF); };

    // OutRun's path: one unit vector (|v| = 4096) per OutRun road position, its angle OutRunners'
    // heading (12-bit, + = turning right; OutRun's path angle atan2(x, y) grows in its right curves)
    // at that distance ahead, the heading interpolated along each segment as OutRunners does at the car
    // ($A842-$A893)
    double f = std::min<uint32_t>(RL(A_SEGPOS), 0x8000) / 32768.0;
    for (int p = 0; p < PATH_N; p++)
    {
        double s = f + p * SEG_PER_POS;
        int k = (int) s;
        double fr = s - k;
        int h0 = heading(k), dh = s12((uint16_t)(heading(k + 1) - h0));
        double a = (h0 + dh * fr) * (2.0 * M_PI / 4096.0);
        path_xy[p][0] = (int16_t) std::lround(4096.0 * std::sin(a));
        path_xy[p][1] = (int16_t) std::lround(4096.0 * std::cos(a));
    }
    uint16_t rx[512];
    for (int i = 0; i < 512; i++) rx[i] = 0;
    sho_path_reader pr = { path_word, nullptr };
    sho_road_build_outrun(&pr, 0, rx);
    for (int i = 0; i < 512; i++) oroad.road_x[i] = (int16_t) rx[i];

    // OutRun's curve type and radius at the car (the radius in OutRun positions: in OutRun's own data
    // the path turns by 1/radius radians per position, measured on its first stage)
    auto curve = [&](int k, int16_t& type, int16_t& radius) {
        int dh = s12((uint16_t)(heading(k + 1) - heading(k)));
        if (!dh) { type = OInitEngine::ROAD_STRAIGHT; radius = 0; return; }
        double kappa = std::abs(dh) * (2.0 * M_PI / 4096.0) / SEG_PER_POS;
        type = dh > 0 ? OInitEngine::ROAD_RIGHT : OInitEngine::ROAD_LEFT;
        radius = (int16_t) std::max(1L, std::min(0x7FFFL, std::lround(1.0 / kappa)));
    };
    curve(0, oinitengine.road_type, oinitengine.road_curve);
    curve(1, oinitengine.road_type_next, oinitengine.road_curve_next);

    // The road heights OutRun's crash and smoke stand on: OutRun's flat road (as on Hang-On's course).
    // Near the car, the slope OutRun's incline frames read (OFerrari: road_y[$1E8] - road_y[$1F0], $12 on a
    // level road, $13 and up the uphill frame, below $12 the downhill one) from OutRunners' own decision
    // for its car's hill frame ($7B3F: $2004D9, 1 on a level road, 0 the car pitched up against the camera,
    // 2-5 pitched down)
    int incline = std::max(-1, std::min(1, HILL_LEVEL - (int) RB(OBJ_CAR + O_HILL)));
    int16_t* ry = oroad.road_y + oroad.road_p0;
    int32_t h = 0;
    for (int k = 0x1FF; k >= 0; k--)
    {
        h += FLAT_STEP;
        ry[k] = (int16_t)(((h << 4) >> 16) + (k <= 0x1E8 ? incline : 0));
    }
    ry[0x280] = ry[0x281] = 0;              // OutRun's list of hill crests: empty
}

// ------------------------------------------------------------------------------------------------
// Display: OutRun's sprites into the board's sprite list
// ------------------------------------------------------------------------------------------------
void ORFerrari::render_overlay()
{
    std::fill(layer.begin(), layer.end(), 0);
    video.sprite_layer->render_to(layer.data(), IMG_W, IMG_H, 0, 8);
    std::vector<uint16_t>& o = img[tick_no % IMG_COUNT];
    for (size_t i = 0; i < layer.size(); i++)
    {
        uint16_t v = layer[i];
        if (!v) { o[i] = 0; continue; }
        if (v == S16_PALETTE_ENTRIES) { o[i] = S32Video::OVL_SHADOW; continue; }  // a shadow over nothing
        uint32_t c = video.own_rgb(v);
        o[i] = (uint16_t)(0x8000 | ((c >> 3) & 0x1F) << 10 | ((c >> 11) & 0x1F) << 5 | ((c >> 19) & 0x1F));
    }
    if (dbg() && (tick_no % 60) == 1)
    {
        int x0 = IMG_W, x1 = -1, y0 = IMG_H, y1 = -1;
        for (int y = 0; y < IMG_H; y++)
            for (int x = 0; x < IMG_W; x++)
                if (o[(size_t) y * IMG_W + x]) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
        fprintf(stderr, "orferrari: overlay bbox x %d-%d y %d-%d\n", x0, x1, y0, y1);
    }
}

// $99B8 has written the car's parts ($9F22: 16-byte System 32 entries, each at R10 and at R10 + $3000)
// from 10[$2080C0] up to R10. The first is the body: it becomes the overlay's tag, its X the middle of
// MAD POWER's body and its Y the body's bottom line (where MAD POWER stands on the road); the Ferrari is
// drawn with its own bottom there. The other parts (wheels, smoke, effects) get no source data.
void ORFerrari::tag_sprites(uint32_t r20, uint32_t start, uint32_t end, bool body)
{
    S32Video& v = board->video();
    if (end <= start || end - start > 0x400) return;
    for (uint32_t copy : { 0u, 0x3000u })
    {
        for (uint32_t e = start; e < end; e += 0x10)
        {
            uint32_t wi = ((e + copy) & 0x1FFFF) >> 1;
            uint16_t* w = &v.m_spriteram[wi];
            if (w[0] >> 14) continue;                       // not a sprite
            if (e == start && body)
            {
                int dstw = w[3] & 0x3FF, dsth = w[2] & 0x3FF;
                int xpos = (int16_t)(w[5] << 4) >> 4, ypos = (int16_t)(w[4] << 4) >> 4;
                int ax = w[0] & 3, ay = (w[0] >> 2) & 3;
                int left = xpos - (ax == 1 ? dstw - 1 : ax == 2 ? 0 : (dstw - 1) / 2);
                int top = ypos - (ay == 1 ? dsth - 1 : ay == 2 ? 0 : (dsth - 1) / 2);
                // the tag keeps the entry's X/Y offset flags and its inline-palette bits (which tell the list
                // walk how many entries it takes)
                // MAD POWER's scale as drawn ($99B8: 34[R20], 8.8, the projection's $19A340[depth] >> 4; 1 when
                // the car stands on the start line, smaller while the camera flies in at the start and pulls
                // back at speed): the Ferrari is scaled the same, from OutRun's size
                int scale = std::max(1, std::min(0xFFF, (int) RW(r20 + 0x34) * 4));
                uint16_t t[8] = { (uint16_t)(w[0] & 0x3030), 0, (uint16_t)(scale << 4 | (tick_no % IMG_COUNT)), (uint16_t)(w[3] & 0x0800),
                                  (uint16_t)((top + dsth - 1) & 0xFFF), (uint16_t)((left + (dstw - 1) / 2) & 0xFFF),
                                  S32Video::OVL_TAG, w[7] };
                if (dbg() && copy == 0 && (tick_no % 60) == 2)
                    fprintf(stderr, "orferrari: body x %d y %d w %d h %d align %d/%d flags %04x -> tag %d,%d scale %d\n", xpos, ypos, dstw, dsth, ax, ay, w[0], left + (dstw - 1) / 2, top + dsth - 1, scale);
                for (int k = 0; k < 8; k++) v.spriteram_w(wi + k, t[k], 0xFFFF);
            }
            else v.spriteram_w(wi + 1, 0, 0xFFFF);
        }
    }
}
