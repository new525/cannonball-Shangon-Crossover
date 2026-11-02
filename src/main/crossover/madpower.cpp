/***************************************************************************
    OutRunners' car MAD POWER as the vehicle on OutRun's courses. See madpower.hpp and
    docs/CROSSOVER-OUTRUNNERS.md section 9.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#include "crossover/madpower.hpp"
#include "crossover/orcar/orcar.hpp"
#include "crossover/orcar/orcarspr.hpp"
#include "crossover/orboard.hpp"
#include "crossover/orsound.hpp"
#include "crossover/resample.hpp"
#include "crossover/romset.hpp"
#include "crossover/harness.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <iostream>

#include "globals.hpp"
#include "trackloader.hpp"
#include "video.hpp"
#include "hwvideo/hwsprites.hpp"
#include "sdl2/input.hpp"
#include "frontend/config.hpp"
#include "engine/outrun.hpp"
#include "engine/oinitengine.hpp"
#include "engine/oroad.hpp"
#include "engine/oferrari.hpp"
#include "engine/oinputs.hpp"
#include "engine/osprites.hpp"
#include "engine/olevelobjs.hpp"
#include "engine/oentry.hpp"

CrossoverMadPower madpower;
static orcar::Record rec_prev;          // the car record the sprite object draws (the last tick's)

// ------------------------------------------------------------------------------------------------
// Conversions (docs/CROSSOVER-OUTRUNNERS.md 9.2)
// ------------------------------------------------------------------------------------------------
// Lateral: OutRun X (+ = left; the Ferrari's half-width 24) x -11/6 = OutRunners' lateral (+ = right;
// MAD POWER's half-width 44), the conversion of section 7 the other way
static const int LAT_OR = 11, LAT_OUT = 6;
// Speed: OutRunners' speed (its advance per 30 Hz tick, $8000 a segment) = km/h x 136; OutRun's
// car_increment is 16.16 km/h
static const int SPEED_PER_KMH = 136;
// Distance: one OutRunners segment = 303/272 OutRun path positions (OutRun 303 per km/h per tick in its
// 16.16 road position, OutRunners 136 per km/h per tick in $8000 a segment)
static const double POS_PER_SEG = 303.0 / 272.0;
// OutRun's single-road asphalt half-width around a road centre (OFerrari::check_wheels: one wheel off
// beyond w + $D4, both beyond w + $104; the edge at w + $EC), as for the bike
static const int16_t OR_ROAD_HALF = 0xEC;
// The ring the handling reads: the car's segment and those after it
static const int NSEG = 16;
// OutRunners' sprite list offset and the car's projected point on its start line (R16 = -12 below the
// list's jump to (160, 222): $9A0F-$9AB7 in a race at scale 1), its place on OutRun's screen
static const int ORIGIN_X = 160, ORIGIN_Y = 222, POINT_Y = -12;
// The camera's pitch for $7B3F's hill index h (0 up, 1 level, 2.. down), with the road level:
// (pitch - $B0) x 16 / $300 = h
static inline uint16_t pitch_for_hill(int h) { return (uint16_t)(0xB0 + h * 0x30 + 0x18); }
// Sound: as crossover/ormode (MAME's left speaker, the race level x 0.17)
static const float YM_ROUTE = 0.40f, PCM_ROUTE = 1.0f, GAIN_OR = 0.17f;
static const uint32_t SH_ENGINE = 0x0710, SH_CTRL = 0x0712, SH_CAR = 0x0713, SH_BOX = 0x0714, SH_TYRE = 0x0718, SH_CMD = 0x0700;
// The foreign entry in OutRun's sprite list (hwsprites): bank 7, address $FFFF
static const uint16_t FOREIGN_ADDR = 0xFFFF;

static bool dbg() { static int d = getenv("CB_MP_DEBUG") ? 1 : 0; return d; }

// ------------------------------------------------------------------------------------------------
// The road for OutRunners' car code
// ------------------------------------------------------------------------------------------------
namespace
{
struct MadRoad : orcar::Road
{
    const uint8_t* rom = nullptr;
    uint16_t base_off = 0;
    orcar::Segment seg[NSEG];
    orcar::Segment segment(uint16_t off) const override
    {
        int k = ((off - base_off) & 0xFFF) >> 5;
        return seg[std::min(k, NSEG - 1)];
    }
    // the pre-stage's surface (node 0): its lists end at once (kind 7 beyond the edges)
    uint8_t node() const override { return 0; }
    uint16_t node_length(uint8_t) const override { return 0; }
    uint16_t node_extra() const override { return 0; }
    uint32_t zone_list(uint8_t n, int side) const override
    {
        uint32_t a = 0xB9F8 + 8u * n + 4u * (uint32_t) side;
        return (uint32_t) zone16(a) | (uint32_t) zone16(a + 2) << 16;
    }
    uint16_t zone16(uint32_t a) const override { return (uint16_t)(rom[a & 0x1FFFFF] | rom[(a + 1) & 0x1FFFFF] << 8); }
    uint8_t zone8(uint32_t a) const override { return rom[a & 0x1FFFFF]; }
};
MadRoad road;

// OutRun's path: its direction (atan2 of the unit vector) at road position p (positions, fractional),
// in 12-bit units (4096 a turn), relative to `ref` (unwrapped around it)
double path_angle(double p)
{
    if (p < 0) p = 0;
    uint32_t i = (uint32_t) p;
    double t = p - i;
    auto ang = [](uint32_t k) {
        double x = trackloader.readPath(k << 2), y = trackloader.readPath((k << 2) + 2);
        return std::atan2(x, y) * 4096.0 / (2.0 * M_PI);
    };
    double a0 = ang(i), a1 = ang(i + 1);
    double d = a1 - a0;
    while (d > 2048) d -= 4096;
    while (d < -2048) d += 4096;
    return a0 + t * d;
}
double wrap12(double d) { while (d > 2048) d -= 4096; while (d < -2048) d += 4096; return d; }
}

bool CrossoverMadPower::available() const { return or_roms.loaded() && or_roms.full; }

int16_t CrossoverMadPower::speed_kmh() const { return car ? (int16_t)(car->s.speed / SPEED_PER_KMH) : 0; }
int16_t CrossoverMadPower::car_x_relative() const { return car ? (int16_t)(-(int) car->s.lateral * LAT_OUT / LAT_OR) : 0; }
int CrossoverMadPower::state() const { return car ? car->s.state : 0; }
uint8_t CrossoverMadPower::gear() const { return car ? car->s.gear : 0; }

bool CrossoverMadPower::load()
{
    if (loaded) return true;
    if (!available()) return false;
    ORBoard::Roms r;
    std::string err;
    if (!ORBoard::load_roms(config.crossover.outrunners_rompath, r, err))
    {
        std::cerr << "MAD POWER: " << err << std::endl;
        return false;
    }
    maincpu = std::move(r.maincpu);
    soundcpu = std::move(r.soundcpu);
    samples = std::move(r.sega);
    sprites = std::move(r.sprites);
    car.reset(new orcar::ORCar(maincpu.data()));
    spr.reset(new orcar::CarSprite(maincpu.data()));
    s32.reset(new orcar::S32Sprites(maincpu.data(), sprites.data(), sprites.size()));
    road.rom = maincpu.data();
    // the car's colours (palette entries $000-$FFF as the mixer shows sprite pixels) after OutRun's
    std::vector<uint32_t> rgb(XCAR_COLOURS, 0);
    for (uint32_t i = 0; i < XCAR_COLOURS; i++) { uint32_t c = 0; if (s32->colour((uint16_t)(0x8000 | i), c)) rgb[i] = c; }
    video.set_car_colours(rgb.data());
    board.reset(new ORSound());
    if (!board->init(soundcpu, samples)) { board.reset(); std::cerr << "MAD POWER: sound board not started" << std::endl; }
    ym.reset(new FrameResampler());
    pcm.reset(new FrameResampler());
    image.assign((size_t) IMG_W * IMG_H, 0);
    loaded = true;
    return true;
}

void CrossoverMadPower::reset()
{
    // OInitEngine::init: a race (GS_INIT_GAME) with MAD POWER chosen; the attract mode keeps the Ferrari
    enabled = want && loaded && outrun.game_state == GS_INIT_GAME;
    started = false;
    have_hit = false;
    frame_kind = -1;
    shift_prev = 0;
    rank_flag = 0;
    entries.clear();
    if (loaded) std::fill(image.begin(), image.end(), 0);
    video.sprite_layer->set_foreign(enabled && loaded ? &CrossoverMadPower::blit : nullptr, this);
    // the sound board: powered on, its program started; the race's commands and parameters follow
    writes.clear(); cmds.clear();
    snd_on = false;
    if (enabled && board)
    {
        board->power_on();
        board->release_cpu();
        next_time = 0; frame_frac = 0;
        ym->clear(); pcm->clear();
        board->run_until((int64_t)(0.05 * ORSound::CLOCK));   // the program's start-up (crossover/ormusic)
        board->ym0.clear(); board->ym1.clear(); board->pcm0.clear(); board->pcm1.clear();
        next_time = board->now();
        snd_on = true;
    }
}

// ------------------------------------------------------------------------------------------------
// The road frame: which OutRun road the car is on (as CrossoverBike::compute_road_frame)
// ------------------------------------------------------------------------------------------------
void CrossoverMadPower::compute_frame(int16_t car_x)
{
    int16_t rw = (int16_t)(oroad.road_width >> 16);
    switch (oroad.road_ctrl)
    {
        case ORoad::ROAD_R0_SPLIT:  road_center = +rw; road_half = OR_ROAD_HALF; break;
        case ORoad::ROAD_R1_SPLIT:  road_center = -rw; road_half = OR_ROAD_HALF; break;
        case ORoad::ROAD_R0:
        case ORoad::ROAD_R1:        road_center = -rw; road_half = OR_ROAD_HALF; break;
        case ORoad::ROAD_OFF:       road_center = 0;   road_half = 0x3FFF; break;
        default:
            if (rw > 0xFF) { road_center = car_x < 0 ? -rw : rw; road_half = OR_ROAD_HALF; }
            else           { road_center = 0; road_half = (int16_t)(rw + OR_ROAD_HALF); }
            break;
    }
}

// The car's segment and the ones after it: OutRunners' heading = OutRun's path direction at the segment's
// start (mirrored on the mirrored road of a fork), anchored at the car's own heading
void CrossoverMadPower::build_segments()
{
    orcar::State& s = car->s;
    road.base_off = s.ring_off;
    double p_car = oroad.road_pos / 65536.0;
    double a_car = path_angle(p_car);
    int hw = std::min(0x7FFF, (road_half * LAT_OR + LAT_OUT / 2) / LAT_OUT);
    for (int k = 0; k < NSEG; k++)
    {
        double p = p_car + (k - seg_frac) * POS_PER_SEG;
        double h = heading + mirror * wrap12(path_angle(p) - a_car);
        orcar::Segment g;
        g.heading = (uint16_t)((int32_t) std::lround(h) & 0xFFF);
        g.pitch = 0;
        g.x = g.z = g.y = 0;
        g.half_width = (uint16_t) hw;
        road.seg[k] = g;
    }
}

// ------------------------------------------------------------------------------------------------
// The tick
// ------------------------------------------------------------------------------------------------
void CrossoverMadPower::tick()
{
    if (!loaded) return;
    orcar::ORCar& c = *car;
    orcar::State& s = c.s;

    // OutRun hands control to the engine in OFerrari::tick_engine_disabled (inside move(), which MAD POWER
    // replaces), as for the bike
    if (!oinitengine.ingame_engine && (outrun.game_state == GS_ATTRACT || outrun.game_state == GS_INGAME))
        oinitengine.ingame_engine = true;

    // Controls as crossover/ormode puts OutRun's onto OutRunners' cabinet: the wheel over the ADC's travel
    // (the game holds $FF - value at $208100), the pedals 0-$FF
    orcar::TickInput in;
    int st = std::max(0x48, std::min((int) oinputs.input_steering, 0xB8));
    int adc = std::max(0, std::min(255, 0x80 + ((st - 0x80) * 0x7F) / 0x38));
    in.steer = (uint8_t)(0xFF - adc);
    in.accel = (uint8_t) std::max(0, std::min(255, (int) oinputs.acc_adjust));
    in.brake = (uint8_t) std::max(0, std::min(255, (int) oinputs.brake_adjust));
    // the manual box (the separate gear buttons setting, as on OutRunners' courses): GEAR2 up, GEAR1 down
    uint8_t shift = 0;
    if (config.controls.gear == config.controls.GEAR_SEPARATE)
        shift = (uint8_t)((input.is_pressed(Input::GEAR2) ? 0x10 : 0) | (input.is_pressed(Input::GEAR1) ? 0x20 : 0));
    in.shift_edges = (uint8_t)(shift & ~shift_prev);
    shift_prev = shift;
    // one player, alone: rank 0, the car ahead is itself (no catch-up)
    in.player = 0; in.carnum = 0; in.rank = 0; in.players_m1 = 0;
    in.own_rec_seg = in.ref_rec_seg = s.seg_count2;
    in.own_rec_goal = s.rec_goal;
    in.own_rec_pitch = 0;
    in.rec_markers = s.rec_markers;
    in.flags94 = s.done_flags;
    in.gearbox_state = s.gearbox_state;
    // the hill index of the car's frame from OutRun's own decision for its car (OFerrari::setup_ferrari_sprite:
    // road_y[$1E8] - road_y[$1F0]: $12 level, above it uphill, below it downhill)
    int16_t dy = (int16_t)(oroad.road_y[oroad.road_p0 + 0x1E8] - oroad.road_y[oroad.road_p0 + 0x1F0]);
    in.camera_pitch = in.camera_pitch_now = pitch_for_hill(dy >= 0x13 ? 0 : dy >= 0x12 ? 1 : 2);
    if (have_hit)
    {
        in.hit_at_entry = in.hit = true;
        in.hit_kind = hit_kind;
        in.car_lateral = hit_lateral; in.car_latvel = hit_latvel; in.car_speed = hit_speed; in.car_model = 0;
        have_hit = false;
    }

    // OutRun's road-width carry ($C1B2), as for the bike; the car's road
    int16_t x = oinitengine.car_x_pos;
    int16_t rw = (int16_t)(oroad.road_width >> 16);
    int16_t change = (int16_t)(rw - oferrari.road_width_old);
    oferrari.road_width_old = rw;
    x = (int16_t)(x + (x < 0 ? -change : change));
    compute_frame(x);
    int kind = oroad.road_ctrl == ORoad::ROAD_OFF ? 3 : road_center > 0 ? 1 : road_center < 0 ? 2 : 0;
    // on OutRun's mirrored road at a fork (the car_x < 0 side while split: OutRun mirrors its own curve push
    // there, OFerrari::set_curve_adjust) the path's turns are the other way; the car's heading continues
    mirror = (oinitengine.rd_split_state != 0 && x < 0) ? -1 : 1;

    if (!started)
    {
        // OutRunners' state 0 ($719C): the grid, the parameters, the torque table, the frame, the engine's start
        s = orcar::State();
        s.flags = 1;
        s.car = 0;
        s.trans = config.controls.gear == config.controls.GEAR_SEPARATE ? 1 : 0;
        heading = 0; seg_frac = 0; pos_acc = 0;
        build_segments();
        c.tick(in, road);
        spr->init(0);
        frame_kind = -1;
        started = true;
        snd_write(SH_CAR, s.snd_car); snd_write(SH_BOX, s.snd_box);
        snd_write(SH_ENGINE, s.snd_engine); snd_write(SH_CTRL, s.snd_ctrl);
        s.lateral = (int16_t) std::lround(-(double)(x - road_center) * LAT_OR / LAT_OUT);
        frame_kind = kind;
        rec_prev = orcar::Record::publish(s);
        oinitengine.car_increment = 0;
        return;                             // state 0 is a tick of its own, as on OutRunners' board
    }
    if (kind != frame_kind || (kind == 0 && change != 0))
    {
        s.lateral = (int16_t) std::lround(-(double)(x - road_center) * LAT_OR / LAT_OUT);
        frame_kind = kind;
    }

    // the start: OutRunners' start line (state 1) holds while OutRun counts down, and ends with it
    bool countdown = outrun.game_state >= GS_START1 && outrun.game_state <= GS_START3;
    if (s.state == 1) s.timer = countdown || !oinitengine.ingame_engine ? 2 : 1;

    // OutRun's road at the car, then OutRunners' tick
    build_segments();
    uint16_t seg_before = s.seg_count;
    uint8_t state_before = s.state;
    c.tick(in, road);
    c.gearbox_tick(in);
    if (c.unsupported() && dbg()) fprintf(stderr, "madpower: unsupported path $%X\n", c.unsupported());
    if (state_before == 1 && s.state != 1) rank_flag = 0xFF;                // $7311: $20831C := $FF

    // back into OutRun: the advance along the road, the heading, the lateral position, the speed
    int crossed = (uint16_t)(s.seg_count - seg_before);
    int ks = std::min(crossed, NSEG - 2);
    double dh = wrap12((double) road.seg[ks].heading - road.seg[ks + 1].heading) * 2.0 * M_PI / 4096.0;
    double len = 0x8000 + std::sin(dh) * 16384.0 * s.lateral / 64.0;
    double f = len > 0 ? std::min(0.999999, std::max(0.0, (double) s.pos / len)) : 0;
    double dseg = crossed + f - seg_frac;
    seg_frac = f;
    if (dseg < 0) dseg = 0;
    double p_old = oroad.road_pos / 65536.0;
    pos_acc += dseg * POS_PER_SEG * 65536.0;
    uint32_t adv = (uint32_t) pos_acc;
    pos_acc -= adv;
    oroad.road_pos += adv;
    oroad.road_pos_change = (int32_t) adv;
    double p_new = oroad.road_pos / 65536.0;
    heading += mirror * wrap12(path_angle(p_new) - path_angle(p_old));
    // (OutRun sets its road position back to 0 at a new lap or stage between ticks: the heading follows the
    // path's turns within a tick only, so it continues across)

    x = (int16_t) std::lround(road_center - (double) s.lateral * LAT_OUT / LAT_OR);
    oinitengine.car_x_pos = x;
    oferrari.set_ferrari_bounds();          // OutRun's limit (MAD POWER's own, its half-width + $60, is tighter)
    if (oinitengine.car_x_pos != x)
        s.lateral = (int16_t) std::lround(-(double)(oinitengine.car_x_pos - road_center) * LAT_OR / LAT_OUT);
    oinitengine.car_increment = (int32_t)(((uint32_t) s.speed << 16) / SPEED_PER_KMH);
    oferrari.car_inc_old = (uint16_t)(oinitengine.car_increment >> 16);
    // OutRun's wheel state stays on the road: MAD POWER's off-road dust and sounds are its own
    oferrari.wheel_state = OFerrari::WHEELS_ON;
    oferrari.wheel_traction = OFerrari::TRACTION_ON;

    // the sound board: the parameters as the car's tick wrote them, the tyre sound, the queued commands
    snd_write(SH_ENGINE, s.snd_engine);
    snd_write(SH_CTRL, s.snd_ctrl);
    if (c.snd.tyre_writes) snd_write(SH_TYRE, c.snd.tyre);
    for (int i = 0; i < c.snd.ncmd && i < 8; i++) cmds.push_back(c.snd.cmd[i]);

    // the sprite object (later in OutRunners' frame): the record of the last tick, then its $14374 objects
    orcar::SpriteInput si;
    si.x = 0;
    si.y = (int16_t)(POINT_Y - (s.jump >> 0));
    si.scale = 0x100;
    si.braking = s.braking;
    si.crash_dust = s.crash_dust;
    si.crash_snd = s.crash_snd;
    si.marker = rank_flag == 0 && s.timer >= 0x3C;
    entries.clear();
    spr->draw(rec_prev, si, entries);
    s.crash_dust = si.crash_dust;
    spr->after();
    rec_prev = orcar::Record::publish(s);
    render();

    static const char* trace = getenv("CB_MP_TRACE");
    if (trace)
    {
        static FILE* tf = fopen(trace, "w");
        if (tf)
        {
            fprintf(tf, "%d %d %d %.3f %d %d %d %u %d %d %d %d %d %d %d %u %d %d %d\n", harness.frame, outrun.game_state, s.state,
                    s.speed / (double) SPEED_PER_KMH, oinitengine.car_x_pos, s.lateral, (int) road.seg[0].half_width,
                    oroad.road_pos >> 16, oroad.road_ctrl, (int16_t)(oroad.road_width >> 16), oinitengine.rd_split_state, mirror,
                    s.heading, s.offroad, s.gear, s.rpm, s.anim_frame, s.anim_step, s.jump);
            fflush(tf);
        }
    }
}

// ------------------------------------------------------------------------------------------------
// Collisions: OutRun's tests decide, MAD POWER's responses follow (next tick, as OutRunners' roadside
// tests run after its car and flag the contact for its next tick)
// ------------------------------------------------------------------------------------------------
void CrossoverMadPower::scenery_hit()
{
    if (!started) return;
    have_hit = true;
    hit_kind = 0;
    if (dbg()) fprintf(stderr, "madpower: f%d scenery hit at X %d\n", harness.frame, oinitengine.car_x_pos);
}

void CrossoverMadPower::traffic_hit(const oentry* t)
{
    if (!started || (have_hit && hit_kind == 0)) return;
    // a car of OutRunners' race ($8074), of MAD POWER's weight: its lateral position from its screen x at
    // the car's depth (OTraffic::update_props: x = xw1 x z / 512 + the road's centre, z ~ 512 there: OutRun
    // X units), its lateral speed its lane change (4 a tick towards xw2), its speed its km/h
    have_hit = true;
    hit_kind = 2;
    hit_lateral = (int16_t)(car->s.lateral + t->x * LAT_OR / LAT_OUT);
    int lv = t->xw2 > t->xw1 ? 4 : t->xw2 < t->xw1 ? -4 : 0;
    hit_latvel = (int16_t)(lv * LAT_OR / LAT_OUT);
    hit_speed = (uint16_t) std::min(0xFFFF, (int) t->traffic_speed * SPEED_PER_KMH);
    if (dbg()) fprintf(stderr, "madpower: f%d traffic hit: screen x %d, lateral %d -> %d, %d km/h\n", harness.frame, t->x, car->s.lateral, hit_lateral, t->traffic_speed);
}

// ------------------------------------------------------------------------------------------------
// Drawing
// ------------------------------------------------------------------------------------------------
void CrossoverMadPower::setup_sprite(oentry* spr_ferrari)
{
    spr_ferrari->x = 0;
    spr_ferrari->y = 221;
    spr_ferrari->zoom = 0x7F;
    spr_ferrari->draw_props = oentry::BOTTOM;
    spr_ferrari->shadow = 3;
    spr_ferrari->priority = spr_ferrari->road_priority = 0x1FD;
    spr_ferrari->control &= ~OSprites::HFLIP;
}

bool CrossoverMadPower::owns(const oentry* e) const { return enabled && e == oferrari.spr_ferrari; }

bool CrossoverMadPower::draw(oentry* input, osprite* output)
{
    (void) input;
    // a hardware entry that only marks the car's place in the list: bank 7, address $FFFF, OutRun's sprite
    // priority 3 (hwsprites draws the car's image there)
    for (int i = 0; i < 7; i++) output->data[i] = 0;
    output->data[0] = (uint16_t)((7 << 9) | 0x100);
    output->data[1] = FOREIGN_ADDR;
    output->data[3] = (uint16_t)(3 << 12 | 0x200);
    output->data[4] = 0x200;
    return true;
}

void CrossoverMadPower::render()
{
    std::fill(image.begin(), image.end(), 0);
    s32->draw(entries.data(), (int)(entries.size() / 8), ORIGIN_X, ORIGIN_Y, image.data(), IMG_W, IMG_H);
}

// hwsprites, at the foreign entry: the image into OutRun's frame (widescreen offset, hi-res doubled)
void CrossoverMadPower::blit(void* ctx, uint16_t* target, int w, int h, int x_off, bool hires)
{
    CrossoverMadPower* m = (CrossoverMadPower*) ctx;
    if (!m->loaded || !m->enabled) return;
    const int sc = hires ? 2 : 1;
    for (int y = 0; y < IMG_H; y++)
        for (int x = 0; x < IMG_W; x++)
        {
            uint16_t p = m->image[(size_t) y * IMG_W + x];
            if (!p) continue;
            uint32_t rgb;
            bool shadow = !m->s32->colour(p, rgb);
            for (int dy = 0; dy < sc; dy++)
                for (int dx = 0; dx < sc; dx++)
                {
                    int tx = (x + x_off) * sc + dx, ty = y * sc + dy;
                    if (tx < 0 || tx >= w || ty < 0 || ty >= h) continue;
                    uint16_t& d = target[(size_t) ty * w + tx];
                    if (shadow) { if (d < S16_PALETTE_ENTRIES) d = (uint16_t)((d & 0xFFF) + S16_PALETTE_ENTRIES); }
                    else d = (uint16_t)(EXT_COLOURS + 1 + (p & 0x0FFF));
                }
        }
}

// ------------------------------------------------------------------------------------------------
// Sound: OutRunners' board, as its main CPU drives it for player 1 (monitor A)
// ------------------------------------------------------------------------------------------------
void CrossoverMadPower::snd_write(uint32_t off, uint8_t v)
{
    if (!snd_on) return;
    writes.push_back({ next_time, off, v });
}

void CrossoverMadPower::mix(int32_t* out, int samples_n, int fps)
{
    if (!snd_on || !board) return;
    if (!enabled || !(outrun.game_state >= GS_START1 && outrun.game_state <= GS_BONUS)) { writes.clear(); cmds.clear(); }
    if (fps <= 0) fps = 60;
    const int64_t frame60 = ORSound::CLOCK / 60;
    double cyc = (double) ORSound::CLOCK / fps + frame_frac;
    int64_t n = (int64_t) cyc;
    frame_frac = cyc - (double) n;
    int64_t start = next_time;
    next_time += n;
    while (!writes.empty())
    {
        board->main_write(std::max(writes.front().t, board->now()), writes.front().off, writes.front().data);
        writes.pop_front();
    }
    // the frame interrupt ($3F8DA): one queued command a 60 Hz frame to $700700
    for (int64_t t = start; t < next_time && !cmds.empty(); t += frame60)
    {
        board->main_write(std::max(t, board->now()), SH_CMD, cmds.front());
        cmds.pop_front();
    }
    board->run_until(next_time);
    if (dbg())
    {
        static int k = 0; double e = 0; for (float v : board->pcm1) e += v * v; for (float v : board->ym1) e += v * v;
        if (k++ % 60 == 0) fprintf(stderr, "madpower snd: t %lld state %d energy %.4f writes pending %zu cmds %zu\n", (long long) next_time, outrun.game_state, e, writes.size(), cmds.size());
    }
    for (float v : board->ym1) { float x = v * YM_ROUTE * GAIN_OR * 32768.0f; ym->push(x, x); }
    for (float v : board->pcm1) { float x = v * PCM_ROUTE * GAIN_OR * 32768.0f; pcm->push(x, x); }
    board->ym0.clear(); board->ym1.clear(); board->pcm0.clear(); board->pcm1.clear();
    static const bool mute = getenv("CB_MP_MUTE_BOARD") != nullptr;
    if (mute || !enabled) return;
    if (ym->available()) ym->pull((double) board->ym_rate() / config.sound.rate, out, samples_n);
    if (pcm->available()) pcm->pull((double) board->pcm_rate() / config.sound.rate, out, samples_n);
}
