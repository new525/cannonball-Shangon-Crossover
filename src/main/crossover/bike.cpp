/***************************************************************************
    Super Hang-On bike as a vehicle in the OutRun world. See bike.hpp.
***************************************************************************/
#include "crossover/bike.hpp"
#include "crossover/romset.hpp"

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <iostream>

#include "trackloader.hpp"
#include "sdl2/input.hpp"
#include "hwvideo/hwsprites.hpp"
#include "video.hpp"
#include "engine/outrun.hpp"
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

// Sprite banks used for the converted bike frames (OutRun's own data occupies banks 0-3)
static const int BIKE_BANK = 4;

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
    std::memcpy(loc(work, shared, SHO_A5_SPEED), SLOT_SPEED, 64);
    std::memcpy(loc(work, shared, SHO_A5_CENTRIFUGAL), SLOT_CENTRIFUGAL, 64);
    std::memcpy(loc(work, shared, SHO_A5_BIKE), SLOT_BIKE, 64);
    std::memcpy(loc(work, shared, SHO_A5_STEER), SLOT_STEER, 64);
    std::memcpy(loc(work, shared, SHO_A5_THROTTLE), SLOT_THROTTLE, 64);
    std::memcpy(loc(work, shared, SHO_A5_BRAKE), SLOT_BRAKE, 64);
    WL(W_RNG, RNG_SEED);
    WW(V_MODE, 2);
    WL(V_EVENTS, ROM_EVENT_END);
    WW(V_EVENT_OFF, 0);
    WW(V_COURSE, 0);
    WB(W_TIMER, 0x99);
    road_center = 0;
    prev_pal = 0;
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
static int16_t outrun_path_word(void*, uint32_t off) { return trackloader.readPath(off); }

void CrossoverBike::build_road_table()
{
    const RomRegion& sub = sh_roms["subcpu"];
    int16_t depth[33];
    for (int k = 0; k < 33; k++) depth[k] = (int16_t) sub.r16(0x19A6 + 2 * k);
    uint16_t buf = 0x800 + ((RW(V_ROADBUF) ^ 0x400) & 0x400);
    uint16_t table[512];
    for (int k = 0; k < 512; k++) table[k] = (uint16_t)(shared[buf + 2 * k] << 8 | shared[buf + 2 * k + 1]);
    sho_path_reader pr = { outrun_path_word, nullptr };
    sho_road_build_sh(&pr, (oroad.road_pos >> 16) << 2, depth, table);
    for (int k = 0; k < 0x200 && buf + 2 * k + 1 < (int) sizeof shared; k++)
    {
        shared[buf + 2 * k] = table[k] >> 8;
        shared[buf + 2 * k + 1] = (uint8_t) table[k];
    }
}

// ------------------------------------------------------------------------------------------------
// Per OutRun logic tick (30 Hz): two Super Hang-On frames
// ------------------------------------------------------------------------------------------------
void CrossoverBike::tick()
{
    if (!ready) return;

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
    WW(V_CRASH, (ocrash.crash_counter && ocrash.spin_control1 <= 0) ? 1 : 0);
    WW(V_STAGE_DONE, ostats.cur_stage ? 1 : 0);
    WW(V_CURVE,   oinitengine.road_type);
    WW(V_CURVE_R, oinitengine.road_curve);
    WW(V_CURVE_NEXT,   oinitengine.road_type_next);
    WW(V_CURVE_NEXT_R, oinitengine.road_curve_next);

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

    for (int step = 0; step < 2; step++)
    {
        WL(V_POS, oroad.road_pos);
        compute_road_frame(car_x);

        sho_steering_task(&mem, SHO_A5_STEER, raw_steer);
        if (countdown || !oinitengine.ingame_engine)
        {
            WL(V_SPEED, 0);
            continue;
        }
        // speed task: distance correction uses the absolute offset from the path centre
        WW(V_X, car_x);
        build_road_table();
        uint32_t dbg_pos = RL(V_POS), dbg_spd = RL(V_SPEED);
        static uint8_t dbg_work[0x8000], dbg_shared[0x1000];
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
        sho_centrifugal_task(&mem, SHO_A5_CENTRIFUGAL);
        sho_throttle_task(&mem, raw_thr);
        sho_brake_task(&mem, raw_brk);
        // The bike task registers itself in a per-priority draw list (sub_07A72: $61C00 + (prio-$80)*32,
        // count word first). Super Hang-On's sprite system empties these lists every frame; here
        // nothing else consumes them, so clear the bike's list first (else it grows through RAM).
        WW(0x61C00 + (((RW(SHO_A5_BIKE + 0x14) - 0x80) << 5) & 0xFFFF), 0);
        sho_bike_task(&mem, SHO_A5_BIKE);
        car_x = (int16_t)(road_center + (int16_t) RW(V_X));
    }

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
    auto it = std::find(def_ptrs.begin(), def_ptrs.end(), input->addr);
    if (it == def_ptrs.end())
    {
        if (getenv("CB_BIKE_DEBUG")) fprintf(stderr, "bike draw: unknown def %06x\n", input->addr);
        return false;
    }
    const Frame& fr = frames[it - def_ptrs.begin()];
    int flip = (input->control & OSprites::HFLIP) ? 1 : 0;

    output->set_vzoom(0x200);                           // full size
    output->set_hzoom(0x200);
    input->width = fr.width;
    osprites.set_sprite_xy(input, output, fr.width, fr.rows);
    int16_t y2 = (int16_t)(output->get_y() + fr.rows);  // before set_bank, which ORs into the same word
    output->set_pal(input->pal_dst);
    output->set_offset(fr.offset[flip]);
    output->set_bank(BIKE_BANK << 1);
    output->set_height((uint8_t)(fr.rows - 1));
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
