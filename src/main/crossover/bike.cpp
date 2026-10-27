/***************************************************************************
    Super Hang-On bike as a vehicle in the OutRun world. See bike.hpp.
***************************************************************************/
#include "crossover/bike.hpp"
#include "crossover/romset.hpp"
#include "crossover/harness.hpp"
#include "crossover/xaudio.hpp"

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <iostream>

#include "main.hpp"
#include "trackloader.hpp"
#include "sdl2/input.hpp"
#include "hwvideo/hwsprites.hpp"
#include "video.hpp"
#include "engine/outrun.hpp"
#include "frontend/config.hpp"
#include "engine/oinitengine.hpp"
#include "engine/oroad.hpp"
#include "engine/oferrari.hpp"
#include "engine/oinputs.hpp"
#include "engine/ostats.hpp"
#include "engine/ocrash.hpp"
#include "engine/osprites.hpp"
#include "engine/olevelobjs.hpp"
#include "engine/oentry.hpp"

extern "C" {
#include "crossover/shophys/sho_road.h"
#include "crossover/shophys/sho_crash.h"
#include "crossover/shophys/sho_sound.h"
}

#include "crossover/bike_init.inc"

CrossoverBike bike;

// Super Hang-On addresses used here (see shophys sources / FINDINGS)
static const uint32_t V_MODE = 0x260000, V_EVENTS = 0x260002, V_EVENT_OFF = 0x260006, V_STAGE_DONE = 0x260010,
    V_COURSE = 0x260012, V_X = 0x260022, V_POS = 0x260030, V_STEER = 0x260068, V_SPEED = 0x260078, V_KNOCK = 0x260088,
    V_CRASH = 0x260094, V_OFFROAD = 0x260098, V_CURVE_NEXT = 0x2600A4, V_CURVE_NEXT_R = 0x2600A8, V_CURVE = 0x2600AC,
    V_CURVE_R = 0x2600B0, V_ROADBUF = 0x2607FE;
static const uint32_t W_SC_BUTTON = 0x060051, W_RNG = 0x060402, W_INGAME = 0x060408, W_TIMER = 0x060500;
static const uint32_t ROM_EVENT_END = 0x16308;   // a 32767 terminator (end of Africa stage 1 list at $162F0)
static const uint32_t ROM_BIKE_PALS = 0xD54E;    // 6 x 16 colours, sprite palettes 0-5 of the bike
static const uint32_t ROM_SPR_TABLES[3][2] = { {0xCA4E, 10}, {0xCA76, 8}, {0xCA96, 8} };  // lean, knockback, skid
static const uint32_t ROM_KNOCK_STEP = 0x85B8, ROM_KNOCK_MAX = 0x85BA;   // traffic knockback: 14, limit 40
static const uint32_t V_CLIP_SEL = 0x2607FC, V_CLIP = 0x260448;          // sprite renderer's hill clip
static const uint32_t CRASH_SLOTS[4] = { SHO_A5_CRASH, SHO_A5_CRASH_SHADOW, SHO_A5_RIDER, SHO_A5_RIDER_SHADOW };
static const uint32_t CRASH_TAG = 0xC0000000;    // oentry::addr of a crash object: tag | object number

// Sprite banks used for the converted bike frames (OutRun's own data occupies banks 0-3), and for
// the crash objects, which are rasterised on demand (banks 5-7, reused in a ring)
static const int BIKE_BANK = 4;
static const int CRASH_BANK0 = 5, CRASH_BANKS = 3;

// OutRun's single-road asphalt half-width around a road centre, in car_x_pos units: OFerrari::check_wheels
// puts one wheel off at |x| > 0xD4 and all wheels off at |x| > 0x104; with a symmetric track that gives
// the edge at the midpoint 0xEC (and a half-track of 0x18).
static const int16_t OR_ROAD_HALF = 0xEC;

static inline uint8_t* loc(uint8_t* work, uint8_t* shared, uint32_t a)
{
    if (a >= 0x260000) return shared + (a - 0x260000);
    return work + (a - 0x060000);
}

#define WW(a, v) do { uint8_t* p_ = loc(work, shared, a); uint16_t v_ = (uint16_t)(v); p_[0] = v_ >> 8; p_[1] = (uint8_t)v_; } while (0)
#define WL(a, v) do { uint32_t l_ = (uint32_t)(v); WW(a, l_ >> 16); WW((a) + 2, l_); } while (0)
#define WB(a, v) (*loc(work, shared, a) = (uint8_t)(v))
#define RW(a) ((uint16_t)(loc(work, shared, a)[0] << 8 | loc(work, shared, a)[1]))
#define RL(a) ((uint32_t)RW(a) << 16 | RW((a) + 2))
#define RB(a) (*loc(work, shared, a))

// ------------------------------------------------------------------------------------------------
// Graphics: Super Hang-On sprite definitions -> OutRun sprite banks
// ------------------------------------------------------------------------------------------------
//
// Definition (Super Hang-On renderer sub_02994, full size: zoom table entry 0 adds 0):
//   +0 word  rows + 1         +2 word  width in 16-bit words (4 pixels each)
//   +6 long  low word = offset of the first row in sprite bank 0
// Checked against the hardware sprite list captured in MAME for all four lean frames (rows,
// width, address and the flipped address). A row ends after a word whose last pixel is 15
// (MAME sega_sys16b_sprite_device::draw); pixels 0 and 15 are transparent.
//
// OutRun format (Cannonball hwsprites): 32-bit words of 8 pixels, first pixel in bits 28-31, and a line
// ends after a word whose 7th pixel (bits 4-7) is 15. Each row gets a 0x000000F0 terminator word.

void CrossoverBike::convert_graphics()
{
    const RomRegion& rom = sh_roms["maincpu_data"];
    const RomRegion& spr = sh_roms["sprites"];

    def_ptrs.clear();
    for (auto& t : ROM_SPR_TABLES)
        for (uint32_t i = 0; i < t[1]; i++)
        {
            uint32_t p = rom.r32(t[0] + 4 * i);
            if (std::find(def_ptrs.begin(), def_ptrs.end(), p) == def_ptrs.end()) def_ptrs.push_back(p);
        }

    uint32_t* bank = video.sprite_layer->bank_ptr(BIKE_BANK);
    uint32_t pos = 0;
    frames.clear();
    for (uint32_t p : def_ptrs)
    {
        Frame fr;
        uint16_t rows  = rom.r16(p) - 1;
        uint16_t words = rom.r16(p + 2);
        uint16_t off   = rom.r16(p + 8);
        fr.rows  = (uint8_t) rows;
        fr.width = words * 4;
        uint16_t out_words = (uint16_t)((fr.width + 7) / 8 + 1);
        fr.pitch = (uint8_t) out_words;

        for (int flip = 0; flip < 2; flip++)
        {
            fr.offset[flip] = (uint16_t) pos;
            for (uint16_t r = 0; r < rows; r++)
            {
                std::vector<uint8_t> px(fr.width, 0);
                // read the row as the hardware does: words from off + r*words until a pixel 15
                uint32_t a = off + (uint32_t) r * words;
                bool end = false;
                for (uint16_t w = 0; w < words && !end; w++)
                {
                    uint16_t d = spr.r16(((a + w) & 0xFFFF) * 2);       // bank 0, 16-bit words
                    for (int n = 0; n < 4; n++)
                    {
                        uint8_t v = (d >> (12 - 4 * n)) & 0xF;
                        px[w * 4 + n] = v == 15 ? 0 : v;                // 0 and 15 are both transparent
                    }
                    if ((d & 0xF) == 0xF) end = true;                   // line ends after a word ending in 15
                }
                if (flip) std::reverse(px.begin(), px.end());
                for (uint16_t w = 0; w < out_words; w++)
                {
                    uint32_t v = 0;
                    if (w + 1 == out_words) v = 0x000000F0;               // end-of-line marker
                    else
                        for (int n = 0; n < 8; n++)
                        {
                            uint32_t i = w * 8 + n;
                            v |= (uint32_t)(i < fr.width ? px[i] : 0) << (28 - 4 * n);
                        }
                    bank[pos++] = v;
                }
            }
        }
        frames.push_back(fr);
    }
}

bool CrossoverBike::init()
{
    ready = false;
    if (!sh_roms.loaded()) return false;
    const RomRegion& rom = sh_roms["maincpu_data"];

    std::memset(&mem, 0, sizeof mem);
    mem.rom = rom.data.data();
    mem.rom_len = rom.size();
    mem.work = work;
    mem.shared = shared;
    mem.shared_len = sizeof shared;
    mem.offroad_fn = offroad_cb;
    mem.offroad_ctx = this;
    mem.sound_fn = sound_cb;
    mem.sound_ctx = this;

    // Bike palettes from Super Hang-On's ROM, added to OutRun's sprite palette expansion table
    uint32_t pals[NUM_PALS * 8];
    for (int i = 0; i < NUM_PALS * 8; i++)
        pals[i] = rom.r32(ROM_BIKE_PALS + 4 * i);
    pal_base = osprites.add_palettes(pals, NUM_PALS);

    convert_graphics();
    reset();
    ready = true;
    return true;
}

void CrossoverBike::reset()
{
    std::memset(work, 0, sizeof work);
    std::memset(shared, 0, sizeof shared);
    std::memset(road_native, 0, sizeof road_native);
    mirrored = false;
    std::memcpy(loc(work, shared, SHO_A5_SPEED), SLOT_SPEED, 64);
    std::memcpy(loc(work, shared, SHO_A5_CENTRIFUGAL), SLOT_CENTRIFUGAL, 64);
    std::memcpy(loc(work, shared, SHO_A5_BIKE), SLOT_BIKE, 64);
    std::memcpy(loc(work, shared, SHO_A5_STEER), SLOT_STEER, 64);
    std::memcpy(loc(work, shared, SHO_A5_THROTTLE), SLOT_THROTTLE, 64);
    std::memcpy(loc(work, shared, SHO_A5_BRAKE), SLOT_BRAKE, 64);
    std::memcpy(loc(work, shared, SHO_A5_CRASH), SLOT_CRASH, 256);
    // The renderer's hill clip ($260448 indexed by $2607FC) is Super Hang-On's own road; OutRun's
    // sprite/road priorities do that job here, so make its "beyond the crest" test never true.
    WB(V_CLIP_SEL, 0);
    WW(V_CLIP, 0x8000);
    WL(W_RNG, RNG_SEED);
    WW(V_MODE, 2);
    WL(V_EVENTS, ROM_EVENT_END);
    WW(V_EVENT_OFF, 0);
    WW(V_COURSE, 0);
    WB(W_TIMER, 0x99);
    road_center = 0;
    prev_pal = 0;
    last_frame = -1;
    sh_frame_acc = 0;
    knock_pending = 0;
    for (bool& v : crash_vis) v = false;
    xaudio.reset_packets();
}

// Super Hang-On passes effect commands to the sound board in a game only (sub_01924 mutes them in
// attract mode unless the demo-sound switch is on); OutRun's game state stands in for $60408.
void CrossoverBike::sh_sound(uint8_t code)
{
    if (outrun.game_state == GS_INGAME) xaudio.command(CrossoverAudio::SHANGON, code);
}

void CrossoverBike::sound_cb(void* ctx, uint8_t code)
{
    ((CrossoverBike*) ctx)->sh_sound(code);
}

bool CrossoverBike::crashing() const { return shared[0x94] || shared[0x95]; }

// One Super Hang-On frame's packet: slots 1-3 from the sound task; 4-7 are rival bikes (none here).
// sub_0196C zeroes slots 1-7 outside a game.
void CrossoverBike::send_sound_packet()
{
    uint8_t s[7] = {0, 0, 0, 0, 0, 0, 0};
    if (outrun.game_state == GS_INGAME)
        for (int i = 0; i < 3; i++) s[i] = RB(SHO_PKT + 1 + i);
    xaudio.frame_slots(CrossoverAudio::SHANGON, s);
}

int16_t CrossoverBike::speed_kmh() const { return (int16_t)((shared[0x78] << 8) | shared[0x79]); }
int16_t CrossoverBike::bike_x() const    { return (int16_t)((shared[0x22] << 8) | shared[0x23]); }
bool    CrossoverBike::offroad() const   { return shared[0x98] || shared[0x99]; }

// ------------------------------------------------------------------------------------------------
// Road geometry: which road is the bike on, and where are its edges (OutRun car_x_pos units)
// ------------------------------------------------------------------------------------------------
void CrossoverBike::compute_road_frame(int16_t car_x)
{
    int16_t rw = (int16_t)(oroad.road_width >> 16);
    switch (oroad.road_ctrl)
    {
        case ORoad::ROAD_R0_SPLIT:  road_center = +rw; road_half = OR_ROAD_HALF; break;   // check_wheels: x -= rw
        case ORoad::ROAD_R1_SPLIT:  road_center = -rw; road_half = OR_ROAD_HALF; break;
        case ORoad::ROAD_R0:
        case ORoad::ROAD_R1:        road_center = -rw; road_half = OR_ROAD_HALF; break;   // check_wheels: x += rw
        case ORoad::ROAD_OFF:       road_center = 0;   road_half = 0x7FFF; break;
        default:                                                                          // both roads drawn
            if (rw > 0xFF)        { road_center = car_x < 0 ? -rw : rw; road_half = OR_ROAD_HALF; }
            else                  { road_center = 0; road_half = (int16_t)(rw + OR_ROAD_HALF); } // one wide road
            break;
    }
}

int CrossoverBike::offroad_cb(void* ctx, int16_t x)
{
    CrossoverBike* b = (CrossoverBike*) ctx;
    int v = x < 0 ? -x : x;          // x is relative to the bike's road centre
    return v > b->road_half;
}

// Super Hang-On's road table for the centrifugal push, built from OutRun's path with the verified
// Super Hang-On builder (sub_01236) into the buffer the next speed task will flip to.
//
// Forks: both OutRun roads share one path, and the road on the car_x < 0 side is drawn as its mirror
// image ("invert road 1"). OutRun mirrors its own curve push to match (OFerrari::set_curve_adjust,
// $BF6E: x_diff negated when rd_split_state != 0 and car_x < 0), so the bike's table is mirrored
// under the same condition. The builder keeps its own unmirrored copy per buffer, because it only
// rewrites the slices it reaches.
static int16_t outrun_path_word(void*, uint32_t off) { return trackloader.readPath(off); }

void CrossoverBike::build_road_table()
{
    const RomRegion& sub = sh_roms["subcpu"];
    int16_t depth[33];
    for (int k = 0; k < 33; k++) depth[k] = (int16_t) sub.r16(0x19A6 + 2 * k);
    int b = ((RW(V_ROADBUF) ^ 0x400) & 0x400) ? 1 : 0;
    uint16_t buf = 0x800 + (b << 10);
    uint16_t* table = road_native[b];
    sho_path_reader pr = { outrun_path_word, nullptr };
    sho_road_build_sh(&pr, (oroad.road_pos >> 16) << 2, depth, table);
    for (int k = 0; k < 0x200 && buf + 2 * k + 1 < (int) sizeof shared; k++)
    {
        uint16_t v = mirrored ? (uint16_t)(-(int16_t) table[k]) : table[k];
        shared[buf + 2 * k] = v >> 8;
        shared[buf + 2 * k + 1] = (uint8_t) v;
    }
}

// Curve type and radius from OutRun's curve list. On the mirrored road of a fork (see
// build_road_table) the curve type describes the other road, so OutRun's ROAD_RIGHT (2) and
// ROAD_LEFT (3) swap there.
void CrossoverBike::set_curve_inputs(int16_t car_x)
{
    mirrored = oinitengine.rd_split_state != 0 && car_x < 0;
    auto dir = [&](int16_t t) -> uint16_t { return mirrored && t >= 2 ? (uint16_t)(5 - t) : (uint16_t) t; };
    WW(V_CURVE,   dir(oinitengine.road_type));
    WW(V_CURVE_R, oinitengine.road_curve);
    WW(V_CURVE_NEXT,   dir(oinitengine.road_type_next));
    WW(V_CURVE_NEXT_R, oinitengine.road_curve_next);
}

// ------------------------------------------------------------------------------------------------
// Per OutRun logic tick: as many Super Hang-On frames (60 Hz) as the tick spans - two at
// OutRun's original 30 Hz logic rate, one in Cannonball's 60 fps "smooth" mode
// ------------------------------------------------------------------------------------------------
void CrossoverBike::tick()
{
    if (!ready) return;
    static FILE* lat_trace = getenv("CB_LAT_TRACE") ? fopen(getenv("CB_LAT_TRACE"), "w") : nullptr;

    // Inputs: OutRun raw-domain values -> Super Hang-On ADC bytes (same physical travel)
    int st = oinputs.input_steering;                                     // 0x48 (left) .. 0xB8 (right)
    if (st < 0x48) st = 0x48; else if (st > 0xB8) st = 0xB8;
    uint8_t raw_steer = (uint8_t)(0x80 - ((st - 0x80) * 0x48) / 0x38);  // 0xC8 (left) .. 0x38 (right)
    int acc = oinputs.acc_adjust, brk = oinputs.brake_adjust;            // 0 .. 0x100
    acc = std::max(0, std::min(acc, 0x100)); brk = std::max(0, std::min(brk, 0x100));
    uint8_t raw_thr = (uint8_t)(8 + (acc * 0x8B) / 0x100);
    uint8_t raw_brk = (uint8_t)((brk * 0x9D) / 0x100);
    bool sc = input.is_pressed(Input::GEAR1) || input.is_pressed(Input::GEAR2);   // supercharger button

    bool ingame = outrun.game_state == GS_INGAME;
    bool countdown = outrun.game_state >= GS_START1 && outrun.game_state <= GS_START3;
    WW(W_INGAME, (ingame && !autopilot) ? 1 : 0);   // 0 = Super Hang-On's demo driver steers
    WB(W_SC_BUTTON, sc ? 0x20 : 0x00);
    WB(W_TIMER, (ostats.time_counter & 0xFF) ? 0x99 : 0x00);             // time over cuts the throttle
    WW(V_STAGE_DONE, ostats.cur_stage ? 1 : 0);

    // OutRun hands control to the engine in OFerrari::tick_engine_disabled (inside move(), which the
    // bike replaces): immediately when in game or attract, or after a crash's ingame_counter runs out.
    if (!oinitengine.ingame_engine)
    {
        if (ocrash.coll_count1)
        {
            olevelobjs.spray_counter = 0;
            if (--oinitengine.ingame_counter == 0) oinitengine.ingame_engine = true;
        }
        else if (outrun.game_state == GS_ATTRACT || outrun.game_state == GS_INGAME)
            oinitengine.ingame_engine = true;
    }

    int16_t car_x = oinitengine.car_x_pos;
    uint32_t pos_start = oroad.road_pos;

    // OutRun's road-width carry (OFerrari::set_ferrari_x, $C1B2): the vehicle moves with its road as
    // the width changes, outward for car_x >= 0. This is also what puts it on a road at the start.
    {
        int16_t rw = (int16_t)(oroad.road_width >> 16);
        int16_t change = (int16_t)(rw - oferrari.road_width_old);
        oferrari.road_width_old = rw;
        car_x = (int16_t)(car_x + (car_x < 0 ? -change : change));
    }

    // Super Hang-On frames owed: 60 per second of video frames since the last call (Cannonball
    // calls this at OutRun's 30 Hz logic rate in its 30 and 60 fps modes; measured, not assumed)
    const int per_tick = config.fps >= 60 ? 2 : 1;          // video frames in one 30 Hz logic tick
    int elapsed = cannonball::frame - last_frame;
    if (last_frame < 0 || elapsed <= 0 || elapsed > 4 * per_tick) elapsed = per_tick;   // first call / resumed
    last_frame = cannonball::frame;
    sh_frame_acc += elapsed * 60;
    int steps = sh_frame_acc / config.fps;
    sh_frame_acc -= steps * config.fps;
    if (getenv("CB_BIKE_STEPS")) { static int calls = 0, total = 0; calls++; total += steps;
        if (calls % 60 == 0) fprintf(stderr, "f%d bike.tick calls %d steps %d tick_fps %d fps %d\n", harness.frame, calls, total, config.tick_fps, config.fps); }
    for (int step = 0; step < steps; step++)
    {
        WL(V_POS, oroad.road_pos);
        compute_road_frame(car_x);
        set_curve_inputs(car_x);

        // slot 1: the sound task reads the previous frame's state (speed, X, knock, off-road)
        WW(V_X, car_x - road_center);
        sho_sound_task(&mem, SHO_A5_SOUND);
        send_sound_packet();

        sho_steering_task(&mem, SHO_A5_STEER, raw_steer);
        // crash objects (slots 3-6) run next, relative to the bike's road like the bike task
        WW(V_X, car_x - road_center);
        fill_road_heights();
        crash_frame();
        car_x = (int16_t)(road_center + (int16_t) RW(V_X));
        if (countdown || !oinitengine.ingame_engine)
        {
            WL(V_SPEED, 0);
            continue;
        }
        // speed task: distance correction uses the absolute offset from the path centre
        WW(V_X, car_x);
        build_road_table();
        uint32_t dbg_pos = RL(V_POS), dbg_spd = RL(V_SPEED);
        static uint8_t dbg_work[sizeof work], dbg_shared[sizeof shared];
        std::memcpy(dbg_work, work, sizeof work); std::memcpy(dbg_shared, shared, sizeof shared);
        sho_speed_task(&mem, SHO_A5_SPEED);
        if (getenv("CB_BIKE_DEBUG") && ((int32_t)(RL(V_POS) - dbg_pos) > 0x20000 || (int32_t)RL(V_SPEED) < (int32_t)dbg_spd - 0x50000))
            fprintf(stderr, "bike step: pos %08x -> %08x  speed %08x -> %08x  X(abs) %d  curve %d radius %d  throttle %d brake %d\n",
                    dbg_pos, RL(V_POS), dbg_spd, RL(V_SPEED), car_x, (int16_t) RW(V_CURVE), (int16_t) RW(V_CURVE_R),
                    RW(0x26006C), RW(0x260070));
        if (getenv("CB_BIKE_DEBUG") && (int32_t)RL(V_SPEED) < 0)
        {
            FILE* f = fopen("bike_state_before.bin", "wb");
            if (f) { fwrite(dbg_work, 1, sizeof dbg_work, f); fwrite(dbg_shared, 1, sizeof dbg_shared, f); fclose(f); }
        }
        oroad.road_pos = RL(V_POS);
        // centrifugal + bike: relative to the bike's own road
        compute_road_frame(car_x);
        WW(V_X, car_x - road_center);
        int16_t lat_x0 = (int16_t) RW(V_X);
        if (RB(SHO_A5_CENTRIFUGAL) & 0x80)                   // the crash switches this task off
            sho_centrifugal_task(&mem, SHO_A5_CENTRIFUGAL);
        int16_t lat_x1 = (int16_t) RW(V_X);
        sho_throttle_task(&mem, raw_thr);
        sho_brake_task(&mem, raw_brk);
        // Traffic knockback ($7510): +/-14 per frame of contact, kept only within +/-40. OutRun's
        // traffic check runs once per 30 Hz tick, i.e. for both Super Hang-On frames of the tick.
        if (knock_pending)
        {
            const RomRegion& rom = sh_roms["maincpu_data"];
            int16_t step = (int16_t) rom.r16(ROM_KNOCK_STEP), lim = (int16_t) rom.r16(ROM_KNOCK_MAX);
            int16_t d0 = (int16_t)((knock_pending > 0 ? step : -step) + (int16_t) RW(V_KNOCK));
            if (d0 <= lim && d0 >= -lim) WW(V_KNOCK, d0);
        }
        // The bike task registers itself in a per-priority draw list (sub_07A72: $61C00 + (prio-$80)*32,
        // count word first). Super Hang-On's sprite system empties these lists every frame; here
        // nothing else consumes them, so clear the bike's list first (else it grows through RAM).
        WW(0x61C00 + (((RW(SHO_A5_BIKE + 0x14) - 0x80) << 5) & 0xFFFF), 0);
        if (RB(SHO_A5_BIKE) & 0x80)                          // the crash switches this task off
            sho_bike_task(&mem, SHO_A5_BIKE);
        int or_push = 0;
        if (lat_trace)
        {   // OutRun's own curve push for comparison (OFerrari::set_curve_adjust, per 30 Hz tick)
            int16_t xd = oroad.road_x[170] - oroad.road_x[511];
            if (oinitengine.rd_split_state && car_x < 0) xd = -xd;
            xd >>= 6;
            if (xd) or_push = ((xd * (int16_t)(RW(V_SPEED))) / 0xDC) << 1;
        }
        if (lat_trace)
            fprintf(lat_trace, "%d %d %u %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n", harness.frame, step, RL(V_POS) >> 16,
                    oroad.road_ctrl, oinitengine.rd_split_state, (int16_t)(oroad.road_width >> 16), road_center,
                    lat_x0, lat_x1 - lat_x0, (int16_t) RW(V_X), (int16_t)(RW(V_SPEED)), RB(0x060412), offroad(),
                    (int16_t) RW(SHO_A5_BIKE + 0x34), or_push, oinitengine.road_type, oinitengine.road_curve);
        car_x = (int16_t)(road_center + (int16_t) RW(V_X));
    }

    knock_pending = 0;

    // Results back into OutRun's state
    oinitengine.car_x_pos = car_x;
    oinitengine.car_increment = RL(V_SPEED);
    oroad.road_pos_change = (int32_t)(oroad.road_pos - pos_start);
    oroad.car_x_bak = car_x;
    oroad.road_width_bak = oroad.road_width >> 16;
    oferrari.wheel_state = offroad() ? OFerrari::WHEELS_OFF : OFerrari::WHEELS_ON;
    oferrari.wheel_traction = offroad() ? 2 : OFerrari::TRACTION_ON;
}

// ------------------------------------------------------------------------------------------------
// Sprite
// ------------------------------------------------------------------------------------------------
void CrossoverBike::setup_sprite(oentry* spr)
{
    crash_oentry[0] = spr;
    if (crashing())                                     // the crashing bike replaces the riding one
    {
        setup_crash_entry(spr, 0);
        return;
    }
    // Bike slot: +0 bit0 = leaning left (mirror), +$A/+$C screen x/y (wobble), +$16 palette, +$1A definition
    uint32_t a5 = SHO_A5_BIKE;
    int16_t x = (int16_t) RW(a5 + 0xA) - 0x160;
    int16_t y = (int16_t) RW(a5 + 0xC) - 0xDF;
    spr->x = x;
    spr->y = 222 + y;
    spr->zoom = 0x7F;
    spr->draw_props = oentry::BOTTOM;
    spr->shadow = 3;
    spr->priority = spr->road_priority = 0x1FD;
    spr->pal_src = pal_base + prev_pal;                 // shown one frame late, as in Super Hang-On
    prev_pal = RB(a5 + 0x16) % NUM_PALS;
    if (RB(a5) & 1) spr->control |= OSprites::HFLIP; else spr->control &= ~OSprites::HFLIP;
    spr->addr = RL(a5 + 0x1A);                          // Super Hang-On definition address
}

bool CrossoverBike::draw(oentry* input, osprite* output)
{
    if ((input->addr & CRASH_TAG) == CRASH_TAG)
        return draw_crash(input, output, (int)(input->addr & 3));
    auto it = std::find(def_ptrs.begin(), def_ptrs.end(), input->addr);
    if (it == def_ptrs.end())
    {
        if (getenv("CB_BIKE_DEBUG")) fprintf(stderr, "bike draw: unknown def %06x\n", input->addr);
        return false;
    }
    const Frame& fr = frames[it - def_ptrs.begin()];
    int flip = (input->control & OSprites::HFLIP) ? 1 : 0;

    // Size (the race setup's BIKE SIZE, a crossover option): OutRun's sprite hardware advances
    // zoom/0x200 source pixels per screen pixel, so zoom 0x200 is full size and 0x400 half.
    uint16_t zoom = scaled_zoom();
    int rows = scaled_lines(fr.rows, zoom), width = fr.width * 0x200 / zoom;
    output->set_vzoom(zoom);
    output->set_hzoom(zoom);
    input->width = width;
    osprites.set_sprite_xy(input, output, width, rows);  // anchored at the bottom centre
    int16_t y2 = (int16_t)(output->get_y() + rows);     // before set_bank, which ORs into the same word
    output->set_pal(input->pal_dst);
    output->set_offset(fr.offset[flip]);
    output->set_bank(BIKE_BANK << 1);
    output->set_height((uint8_t)(rows - 1));
    if (y2 > 0x1DF) output->sub_height((uint8_t)(y2 - 0x1DF));
    output->set_render(0xE0);                           // forwards, left to right, top to bottom
    output->set_pitch((uint8_t)(fr.pitch << 1));
    output->set_priority(input->shadow << 4);
    if (getenv("CB_BIKE_DEBUG"))
    {
        static int n = 0;
        if (n++ % 60 == 0)
            fprintf(stderr, "bike draw: def %06x flip %d rows %d width %d pitch %d off %04x pal_dst %d | hw %04x %04x %04x %04x %04x %04x x %04x\n",
                    input->addr, flip, fr.rows, fr.width, fr.pitch, fr.offset[flip], input->pal_dst,
                    output->data[0], output->data[1], output->data[2], output->data[3], output->data[4], output->data[5], output->data[6]);
    }
    return true;
}

// ------------------------------------------------------------------------------------------------
// Collisions and the crash
// ------------------------------------------------------------------------------------------------

// Super Hang-On's scenery test ($6EF8) adds 1 to the crash counter for every frame of contact while
// not already crashing; the crash task starts when the counter differs from its own copy.
void CrossoverBike::scenery_hit()
{
    if (!ready || crashing()) return;
    WW(SHO_V_CRASH_COUNT, RW(SHO_V_CRASH_COUNT) + 1);
}

// Super Hang-On ($7510): the knock is +14 for a car at or right of the bike's centre ($A >= $160),
// -14 for one to the left (+X = left, so the bike is pushed away from the car).
void CrossoverBike::traffic_hit(int16_t traffic_x)
{
    if (!ready) return;
    knock_pending = traffic_x >= 0 ? 1 : -1;
    if (getenv("CB_CRASH_DEBUG")) fprintf(stderr, "f%d traffic hit x %d knock %d\n", harness.frame, traffic_x, (int16_t) RW(SHO_V_KNOCK));
}

// Depth (Super Hang-On's crash objects: $14 - $80, 0 = horizon, $BE = at the bike) -> OutRun's road
// index (0 = horizon, $1FD = the player's vehicle, as OFerrari uses). Both are linear in the scale
// of an object on a flat road; this maps the bike's own depth onto the Ferrari's index. The heights
// then come from OutRun's road, so the objects follow its hills.
uint16_t CrossoverBike::depth_to_index(uint16_t depth)
{
    return (uint16_t)(((uint32_t) depth * 0x1FD) / 0xBE);
}

// The depth -> road height table the crash tasks read at $261200 (Super Hang-On's sub CPU builds it
// from its road; same format as OutRun's road_y: height above the bottom line, << 4).
void CrossoverBike::fill_road_heights()
{
    const int16_t* ry = oroad.road_y + oroad.road_p0;
    int16_t last = ry[0x1FF], slope = (int16_t)(ry[0x1EF] - ry[0x1FF]);    // per 16 entries
    for (uint32_t i = 0; i < 256; i++)
    {
        uint32_t k = depth_to_index((uint16_t) i);
        int32_t h = k <= 0x1FF ? ry[k] : last - (int32_t) slope * (int32_t)(k - 0x1FF) / 16;
        shared[0x1200 + 2 * i]     = (uint8_t)((uint16_t) h >> 8);
        shared[0x1200 + 2 * i + 1] = (uint8_t) h;
    }
}

void CrossoverBike::crash_frame()
{
    uint8_t drawn[4];
    sho_crash_frame(&mem, drawn);
    for (int i = 0; i < 4; i++)
        crash_vis[i] = drawn[i] && sho_sprite_entry(&mem, CRASH_SLOTS[i], crash_entry[i]);
    if (getenv("CB_CRASH_DEBUG") && crashing())
        for (int i = 0; i < 4; i++)
        {
            uint32_t a5 = CRASH_SLOTS[i];
            fprintf(stderr, "f%d crash obj %d vis %d depth %02x x %03x y %03x z %02x h %04x tbl %d | e:", harness.frame, i, crash_vis[i],
                    RW(a5 + 0x14) - 0x80, RW(a5 + 0xA), RW(a5 + 0xC), RW(a5 + 0xE), RW(a5 + 0x24),
                    (int16_t) RW(0x261200 + 2 * ((RW(a5 + 0x14) - 0x80) & 0xFF)) >> 4);
            for (int b = 0; b < 12; b++) fprintf(stderr, " %02x", crash_entry[i][b]);
            fprintf(stderr, "\n");
        }
}

void CrossoverBike::setup_crash_entry(oentry* spr, int obj)
{
    uint32_t a5 = CRASH_SLOTS[obj];
    uint16_t k = depth_to_index((uint16_t)(RW(a5 + 0x14) - 0x80));
    if (k > 0x1FF) k = 0x1FF;
    spr->x = 0;
    spr->y = 0;
    spr->zoom = 0x7F;                                   // non-zero: drawn (size comes from the entry)
    spr->draw_props = 0;
    spr->priority = spr->road_priority = k;
    spr->shadow = (uint8_t)(crash_entry[obj][9] >> 6);  // sprite/tilemap priority bits
    spr->pal_src = (uint16_t)(pal_base + (crash_entry[obj][9] & 0x3F) % NUM_PALS);
    spr->control = (uint8_t)((spr->control & ~(OSprites::HFLIP | OSprites::SHADOW)) | OSprites::ENABLE);
    spr->addr = CRASH_TAG | (uint32_t) obj;
}

void CrossoverBike::draw_crash_objects(oentry* rider, oentry* rider_shadow, oentry* bike_shadow)
{
    crash_oentry[1] = bike_shadow;
    crash_oentry[2] = rider;
    crash_oentry[3] = rider_shadow;
    if (!ready || !crashing()) return;
    for (int obj = 1; obj < 4; obj++)
    {
        if (!crash_vis[obj]) continue;
        oentry* e = crash_oentry[obj];
        setup_crash_entry(e, obj);
        if (oroad.get_view_mode() != ORoad::VIEW_INCAR)
        {
            osprites.map_palette(e);
            osprites.do_spr_order_shadows(e);
        }
    }
}

// BIKE SIZE as an OutRun sprite zoom value (0x200 = full size), and the screen lines that show
// a sprite of 'rows' source rows at that zoom without reading past its last row
uint16_t CrossoverBike::scaled_zoom() const
{
    int pct = config.crossover.bike_scale;
    if (pct < 50 || pct > 100) pct = 100;
    return (uint16_t)((0x200 * 100 + pct / 2) / pct);
}

int CrossoverBike::scaled_lines(int rows, uint16_t zoom)
{
    int n = rows * 0x200 / zoom;
    return n < 1 ? 1 : n;
}

bool CrossoverBike::owns(const oentry* e) const
{
    if (e == oferrari.spr_ferrari) return true;
    return (e->addr & CRASH_TAG) == CRASH_TAG && (e == crash_oentry[1] || e == crash_oentry[2] || e == crash_oentry[3]);
}

// The entry is drawn as Super Hang-On's hardware (System 16B sprites, MAME sega_sys16b_sprite_device)
// would draw it, into an image used at 1:1 by OutRun's hardware. Horizontal position: the same
// mapping as the riding bike (Super Hang-On's x = OutRun's x, so $160 = OutRun's centre).
bool CrossoverBike::draw_crash(oentry* input, osprite* output, int obj)
{
    CrashImg img;
    const uint8_t* e = crash_entry[obj];
    if (!crash_vis[obj] || !crash_image(e, img))
    {
        output->hide();
        return true;
    }
    // Scaled like the riding bike (BIKE SIZE) about each object's own horizontal centre. Shadows
    // keep their bottom edge on the road; the bike and rider (objects 0 and 2) keep their place
    // over their shadows (1 and 3), with the height above the shadow scaled too.
    uint16_t zoom = scaled_zoom();
    int src_rows = e[0] - e[1];
    int rows = scaled_lines(src_rows, zoom);
    int x = ((e[2] << 8) | e[3]) & 0xFFF;
    x += (img.width - img.width * 0x200 / zoom) / 2;
    int bottom = e[0];
    if ((obj == 0 || obj == 2) && crash_vis[obj + 1])
    {
        int ground = crash_entry[obj + 1][0];
        if (ground > bottom) bottom = ground - (ground - bottom) * 0x200 / zoom;
    }
    output->set_vzoom(zoom);
    output->set_hzoom(zoom);
    output->set_y((uint16_t)(bottom - rows + 256));
    output->set_x((uint16_t)(x & 0xFFF));
    output->set_pal(input->pal_dst);
    output->set_offset(img.offset);
    output->set_bank((uint8_t)(img.bank << 1));
    output->set_height((uint8_t)(rows - 1));
    output->set_render(0xE0);
    output->set_pitch((uint8_t)(img.pitch << 1));
    output->set_priority((uint8_t)(input->shadow << 4));
    if (getenv("CB_CRASH_DEBUG"))
        fprintf(stderr, "f%d crash draw %d rows %d bank %d off %04x pitch %d | hw %04x %04x %04x %04x %04x %04x %04x\n", harness.frame, obj, rows,
                img.bank, img.offset, img.pitch, output->data[0], output->data[1], output->data[2], output->data[3],
                output->data[4], output->data[5], output->data[6]);
    return true;
}

bool CrossoverBike::crash_image(const uint8_t* e, CrashImg& img)
{
    int bottom = e[0], top = e[1];
    if (top >= bottom) return false;
    int rows = bottom - top;
    uint16_t addr  = (uint16_t)(e[6] << 8 | e[7]);
    int      pitch = (int8_t) e[5];
    bool     flip  = e[4] & 1;
    uint16_t zw    = (uint16_t)(e[10] << 8 | e[11]);
    int vzoom = (zw >> 5) & 0x1F, hzoom = zw & 0x1F;
    const RomRegion& spr = sh_roms["sprites"];
    uint32_t numbanks = (uint32_t)(spr.size() / 0x20000);
    uint32_t bank = e[8] & 0xF;
    if (numbanks) bank %= numbanks;

    uint64_t key = (uint64_t) bank << 43 | (uint64_t) flip << 42 | (uint64_t)(uint8_t) pitch << 34 |
                   (uint64_t) addr << 18 | (uint64_t) rows << 10 | (zw & 0x3FF);
    auto it = crash_cache.find(key);
    if (it != crash_cache.end()) { img = it->second; return true; }

    // MAME sega_sys16b_sprite_device::draw for one entry, into a local bitmap
    static const int MAXW = 520;
    std::vector<uint8_t> px((size_t) rows * MAXW, 0);
    auto word = [&](uint16_t a) { return spr.r16((bank * 0x10000 + a) * 2); };
    uint16_t acc = zw & 0x03FF;
    int width = 0;
    for (int r = 0; r < rows; r++)
    {
        addr = (uint16_t)(addr + pitch);
        acc = (uint16_t)(acc + (vzoom << 10));
        if (acc & 0x8000) { addr = (uint16_t)(addr + pitch); acc &= ~0x8000; }
        int xacc = 4 * hzoom, x = 0;
        uint16_t w = (uint16_t)(flip ? addr + 1 : addr - 1);
        while (x < 512)
        {
            w = (uint16_t)(flip ? w - 1 : w + 1);
            uint16_t pixels = word(w);
            int pix = 0;
            for (int n = 0; n < 4; n++)
            {
                pix = (pixels >> (flip ? 4 * n : 12 - 4 * n)) & 0xF;
                xacc = (xacc & 0x3F) + hzoom;
                if (xacc < 0x40)
                {
                    if (pix != 0 && pix != 15 && x < MAXW) px[(size_t) r * MAXW + x] = (uint8_t) pix;
                    x++;
                }
            }
            if (pix == 15) break;
        }
        width = std::max(width, std::min(x, MAXW));
    }

    // OutRun format (as convert_graphics): 8 pixels per word, end-of-line word 0x000000F0
    uint16_t out_words = (uint16_t)((width + 7) / 8 + 1);
    uint32_t size = (uint32_t) rows * out_words;
    if (out_words > 127 || size > 0x10000) return false;
    uint32_t off = crash_cache_pos & 0xFFFF;
    if (off + size > 0x10000) crash_cache_pos = (crash_cache_pos | 0xFFFF) + 1;
    if (crash_cache_pos + size > (uint32_t) CRASH_BANKS * 0x10000)
    {
        crash_cache_pos = 0;
        crash_cache.clear();
    }
    img.bank   = (uint8_t)(CRASH_BANK0 + (crash_cache_pos >> 16));
    img.offset = (uint16_t)(crash_cache_pos & 0xFFFF);
    img.pitch  = (uint8_t) out_words;
    img.width  = (uint16_t) width;
    uint32_t* dst = video.sprite_layer->bank_ptr(img.bank) + img.offset;
    for (int r = 0; r < rows; r++)
        for (uint16_t wi = 0; wi < out_words; wi++)
        {
            uint32_t v = 0;
            if (wi + 1 == out_words) v = 0x000000F0;
            else
                for (int n = 0; n < 8; n++)
                {
                    int x = wi * 8 + n;
                    v |= (uint32_t)(x < width ? px[(size_t) r * MAXW + x] : 0) << (28 - 4 * n);
                }
            *dst++ = v;
        }
    crash_cache_pos += size;
    crash_cache[key] = img;
    return true;
}
