// orcartest: proves the reconstruction of OutRunners' player car (crossover/orcar) against the original
// running on the emulated System Multi 32 board (crossover/orboard), tick by tick.
//
// The board runs a race with a driver (inputs only, from what the board shows: the car's lateral
// position and speed). At every tick of a player's car object ($715C ... return to the object list
// at $11F41) the tool takes the inputs the car's code reads, where it reads them (instruction hooks:
// $715C the controls and the race values, $71F0 / $A39C the road (the segment ring), $7348 a contact,
// $11F3E / $11F41 the gear-box object), runs the model's tick and compares every variable the model
// owns with the board's RAM:
//   mode a (re-synced): the model's state is loaded from the board before each tick (per-tick errors);
//   mode b (free-running): the model's state is loaded once, before the car's first tick of the
//          race (state 0); from then on only the inputs and the road come from the board.
// Both models run on the same board run. Coverage: every instruction of the transcribed routines is
// counted when the board executes it.
//
// usage: orcartest <orunnersj rom dir> <scenario> [-car n] [-manual] [-abuse] [-left] [-nohold] [-force]
//                  [-frames n] [-v] [-diffs n]
//   scenarios: full     the full course from the pre-stage to the goal (26,000 frames), countdown held
//              kenya    Kenya alone (9,000 frames): off-road, scenery, Kenya's elephants, the goal;
//                       the countdown is released for the last 2,400 frames
//              two      two players (player 2: car 2, automatic), their cars pushed together (car
//                       contacts), both players' cars compared
//              brief    a short race (3,000 frames; with -car n), steering, braking, off-road
//   -left (full): always left of centre, the left branch at every checkpoint (the route to Kenya)
//   -manual: the manual box (shifts by the engine speed; -abuse: late shifts up, shifts down at speed);
//   -nohold: the countdown runs (time up); -force: rare paths forced by RAM writes at the tick's entry
//   (the collision speed change, the wheelspin flag, the speed for a crash's response; mode a only)
#include "crossover/orboard.hpp"
#include "crossover/orcar/orcar.hpp"
#include "crossover/v60/v60d.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <map>
#include <string>
#include <vector>

using orcar::ORCar;
using orcar::State;
using orcar::TickInput;
using orcar::Segment;

namespace
{

ORBoard* B = nullptr;
std::vector<uint8_t> ROM;           // the main CPU's 2 MB, for the model and the road's zones

uint8_t  rb(uint32_t a) { return B->read8(a); }
uint16_t rw(uint32_t a) { return (uint16_t)(rb(a) | rb(a + 1) << 8); }
uint32_t rl(uint32_t a) { return (uint32_t) rw(a) | (uint32_t) rw(a + 2) << 16; }
void wb(uint32_t a, uint8_t v) { B->write8(a, v); }
void ww(uint32_t a, uint16_t v) { wb(a, (uint8_t) v); wb(a + 1, (uint8_t)(v >> 8)); }

// ------------------------------------------------------------------------------------------------
// The road from the board: the ring as the handling reads it, the node table, the ROM's zones
// ------------------------------------------------------------------------------------------------
struct BoardRoad : orcar::Road
{
    uint8_t ring[0x1000];
    uint8_t node_ = 0;
    uint16_t lengths[64];
    uint16_t extra = 0;
    void capture(uint32_t ring_base, uint32_t r25)
    {
        for (uint32_t i = 0; i < 0x1000; i++) ring[i] = rb(ring_base + i);
        node_ = rb(r25 + 0xA3);
        for (int i = 0; i < 64; i++) lengths[i] = rw(r25 + 0x700 + 2 * i);
        extra = rw(r25 + 0x73E);
    }
    static uint16_t g16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
    static uint32_t g32(const uint8_t* p) { return (uint32_t) g16(p) | (uint32_t) g16(p + 2) << 16; }
    Segment segment(uint16_t off) const override
    {
        const uint8_t* p = ring + (off & 0xFE0);
        Segment s;
        s.heading = g16(p); s.pitch = g16(p + 2);
        s.x = (int32_t) g32(p + 4); s.z = (int32_t) g32(p + 8); s.y = (int32_t) g32(p + 0xC);
        s.half_width = g16(p + 0x12);
        return s;
    }
    uint8_t node() const override { return node_; }
    uint16_t node_length(uint8_t n) const override { return n < 64 ? lengths[n] : rw(0x208700 + 2 * n); }
    uint16_t node_extra() const override { return extra; }
    uint32_t zone_list(uint8_t n, int side) const override
    {
        uint32_t a = 0xB9F8 + 4 * (uint32_t)(uint8_t)(n + n) + 4 * side;
        return g32(&ROM[a]);
    }
    uint16_t zone16(uint32_t a) const override { return (uint16_t)(ROM[a & 0x1FFFFF] | ROM[(a + 1) & 0x1FFFFF] << 8); }
    uint8_t zone8(uint32_t a) const override { return ROM[a & 0x1FFFFF]; }
};

// ------------------------------------------------------------------------------------------------
// The model's variables in the board's RAM (player 1's addresses; player 2: + $10000 except the
// shaker and the record)
// ------------------------------------------------------------------------------------------------
enum Area { OBJ, VAR, BASE, ABS, REC };
enum When { ALWAYS, CRASH, CRASH_START, MASK10, GOALRUN, GEARBOX };
struct Field { const char* name; Area area; uint32_t off; int size; size_t member; When when; };
#define F(n, area, off, when) { #n, area, off, (int) sizeof(State::n), offsetof(State, n), when }
#define FA(n, i, area, off, when) { #n "[" #i "]", area, off, (int) sizeof(State::n[0]), offsetof(State, n) + i * sizeof(State::n[0]), when }
const Field FIELDS[] = {
    F(flags, OBJ, 0x00, ALWAYS), F(state, OBJ, 0x0C, ALWAYS), F(sub, OBJ, 0x0D, ALWAYS), F(st_e, OBJ, 0x0E, ALWAYS),
    F(st_f, OBJ, 0x0F, ALWAYS), F(timer, OBJ, 0x10, ALWAYS), F(wx, OBJ, 0x20, ALWAYS), F(wy, OBJ, 0x24, ALWAYS),
    F(wz, OBJ, 0x28, ALWAYS), F(speed_copy, OBJ, 0x2C, ALWAYS), F(latvel, OBJ, 0x2E, ALWAYS),
    F(anim_frame, OBJ, 0x38, CRASH), F(anim_step, OBJ, 0x40, CRASH), F(anim_rate, OBJ, 0x41, CRASH),
    F(anim_count, OBJ, 0x42, CRASH), F(grace, OBJ, 0x6F, ALWAYS), F(pitch_init, OBJ, 0x8C, ALWAYS),
    F(heading, OBJ, 0x8E, ALWAYS), F(heading_prev, OBJ, 0x90, ALWAYS), F(force, OBJ, 0x94, ALWAYS),
    F(steer_angle, OBJ, 0x96, ALWAYS), F(brake_val, OBJ, 0xB0, ALWAYS), F(wy_prev, OBJ, 0xB4, ALWAYS),
    F(slide, OBJ, 0xB8, ALWAYS), F(speed32, OBJ, 0xC0, ALWAYS), F(dspeed, OBJ, 0xC4, ALWAYS), F(jump, OBJ, 0xC8, ALWAYS),
    F(jump_v, OBJ, 0xCC, ALWAYS), F(push, OBJ, 0xCE, ALWAYS), F(in_accel, OBJ, 0xD0, ALWAYS),
    F(in_brake, OBJ, 0xD1, ALWAYS), F(in_steer, OBJ, 0xD2, ALWAYS), F(hill, OBJ, 0xD9, CRASH_START),
    F(goal_seg, OBJ, 0xF0, ALWAYS), F(goal_lat, OBJ, 0xF2, ALWAYS), F(goal_speed, OBJ, 0xF4, ALWAYS),
    F(goal_steer, OBJ, 0xF6, ALWAYS), F(goal_accel, OBJ, 0xF7, ALWAYS), F(goal_flag, OBJ, 0xF8, ALWAYS),
    F(pos_hi, VAR, 0x32, ALWAYS), F(pos, VAR, 0x34, ALWAYS), F(lateral, VAR, 0x38, ALWAYS), F(seg_rest, VAR, 0x3A, ALWAYS),
    F(ring_off, VAR, 0x40, ALWAYS), F(seg_count, VAR, 0x42, ALWAYS), F(seg_count2, VAR, 0x44, ALWAYS),
    F(zone_base, VAR, 0x4C, ALWAYS), F(zone_base_prev, VAR, 0x4E, ALWAYS), F(torque_ptr, VAR, 0x50, ALWAYS),
    F(gears_ptr, VAR, 0x54, ALWAYS), F(params, VAR, 0x58, ALWAYS), F(car, VAR, 0x60, ALWAYS), F(trans, VAR, 0x61, ALWAYS),
    F(gear, VAR, 0x62, ALWAYS), F(gears_m1, VAR, 0x63, ALWAYS), F(ratio, VAR, 0x64, ALWAYS), F(spin, VAR, 0x65, ALWAYS),
    F(moving, VAR, 0x66, ALWAYS), F(sliding, VAR, 0x67, ALWAYS), F(gap, VAR, 0x68, ALWAYS), F(catchup, VAR, 0x6A, ALWAYS),
    F(rpm, VAR, 0x70, ALWAYS), F(speed, VAR, 0x74, ALWAYS), F(offroad, VAR, 0x7C, ALWAYS), F(braking, VAR, 0x7E, ALWAYS),
    F(ring_off2, VAR, 0xA4, ALWAYS), F(crash_restitution, VAR, 0x126, ALWAYS), F(crash_push_decay, VAR, 0x128, ALWAYS),
    F(crash_gravity, VAR, 0x12A, ALWAYS), F(crash_speed_decay, VAR, 0x12C, ALWAYS), F(crash_table, VAR, 0x130, ALWAYS),
    FA(wheel, 0, VAR, 0x140, ALWAYS), FA(wheel, 1, VAR, 0x141, ALWAYS), FA(wheel, 2, VAR, 0x142, ALWAYS),
    F(shift_smoke, VAR, 0x144, ALWAYS), F(zone_node, VAR, 0x146, ALWAYS), F(minor, VAR, 0x147, ALWAYS),
    FA(zone_ptr, 0, VAR, 0x148, ALWAYS), FA(zone_ptr, 1, VAR, 0x14C, ALWAYS), F(game_over, VAR, 0x182, ALWAYS),
    F(shaker, ABS, 0x20E0D0, ALWAYS), F(gearbox_state, OBJ, 0x80C, GEARBOX), F(gearbox_hud, OBJ, 0x840, GEARBOX),
    F(done_flags, BASE, 0x94, MASK10),
    F(rec_pitch, REC, 0x54, ALWAYS), F(rec_markers, REC, 0x60, GOALRUN), F(rec_goal, REC, 0x5E, ALWAYS),
    F(rec_finish, REC, 0x16, ALWAYS),
};
const int NFIELDS = (int)(sizeof FIELDS / sizeof FIELDS[0]);

struct Player
{
    int p = 0;
    uint32_t r20 = 0x200400, r23 = 0x209000, r25 = 0x208000, ring = 0x20A000;
    uint32_t addr(const Field& f) const
    {
        switch (f.area)
        {
        case OBJ: return r20 + f.off;
        case VAR: return r23 + f.off;
        case BASE: return r25 + f.off;
        case ABS: return f.off + (uint32_t) p;
        case REC: return rl(r25 + 0x24) + f.off;
        }
        return 0;
    }
    uint32_t board_val(const Field& f) const
    {
        uint32_t a = addr(f), v = 0;
        for (int i = 0; i < f.size; i++) v |= (uint32_t) rb(a + (uint32_t) i) << (8 * i);
        return v;
    }
};

uint32_t model_val(const State& s, const Field& f)
{
    uint32_t v = 0;
    memcpy(&v, (const uint8_t*) &s + f.member, (size_t) f.size);
    return v;
}
void set_model_val(State& s, const Field& f, uint32_t v) { memcpy((uint8_t*) &s + f.member, &v, (size_t) f.size); }

void load_state(State& s, const Player& pl)
{
    for (const Field& f : FIELDS) set_model_val(s, f, pl.board_val(f));
    // the torque table: one $200-byte table per gear ($209300.., up to 6 gears: $209EFF)
    for (int g = 0; g < 8; g++)
        for (int i = 0; i < 256; i++) s.torque[g][i] = g <= s.gears_m1 ? rw(pl.r23 + 0x300 + 0x200 * g + 2 * i) : 0;
}

// ------------------------------------------------------------------------------------------------
// The test
// ------------------------------------------------------------------------------------------------
struct Diff { uint64_t tick; uint64_t frame; std::string what; };

struct Checker
{
    const char* name;
    ORCar model;
    bool resync;                // mode a
    bool started = false;       // mode b: loaded at the race's first tick
    bool dead = false;          // mode b: the model met an unsupported path
    uint64_t ticks = 0, ticks_bad = 0, field_diffs = 0, gear_ticks = 0, gear_bad = 0, unsupported = 0;
    std::vector<Diff> first;
    explicit Checker(const char* n, bool a) : name(n), model(ROM.data()), resync(a) {}
};

struct PlayerTest
{
    Player pl;
    BoardRoad road;
    TickInput in;
    bool in_tick = false;
    bool road_ok = false;
    uint8_t state_before = 0;
    Checker a{ "a (re-synced)", true }, b{ "b (free-running)", false };
    uint64_t tick_no = 0;
    uint64_t demo_ticks = 0;    // the attract demo's ticks after the race (not checked: its driver is not transcribed)
    bool demo = false;
    std::string route;          // the checkpoints' nodes and choices, when the goal is reached
    // coverage of situations (the board's state after each tick)
    std::map<std::string, uint64_t> events;
};

PlayerTest PT[2];
int max_diffs = 10;
bool force = false;             // -force: rare paths forced by writes at the tick's entry (mode a only)
bool verbose = false;
uint64_t frame_no = 0;
std::map<uint32_t, uint64_t> cover;          // instruction -> count (the transcribed routines)
std::vector<uint32_t> cover_pcs;

int player_of(uint32_t r25) { return r25 == 0x208000 ? 0 : r25 == 0x218000 ? 1 : -1; }

void capture_entry(PlayerTest& t)
{
    const Player& pl = t.pl;
    TickInput& in = t.in;
    in = TickInput();
    in.steer = rb(pl.r25 + 0x100); in.accel = rb(pl.r25 + 0x101); in.brake = rb(pl.r25 + 0x102);
    in.player = rb(pl.r25 + 0); in.carnum = rb(pl.r25 + 1);
    in.rank = rb(pl.r25 + 0x320 + in.carnum);
    in.players_m1 = (uint8_t)(rb(rl(pl.r25 + 0x308) + 0x1F) - 1);
    in.time_up = rb(pl.r23 + 0x180); in.goal = rb(pl.r23 + 0x181);
    in.finish_time = rw(pl.r25 + 0x410 + 2u * in.carnum);
    uint32_t own = rl(pl.r25 + 0x20);
    in.own_rec_seg = rw(own + 0x42); in.own_rec_goal = rb(own + 0x5E); in.own_rec_pitch = rw(own + 0x54);
    in.camera_pitch = rw(pl.r23 + 0x2C);
    in.rec_markers = rb(rl(pl.r25 + 0x24) + 0x60);
    in.flags94 = rw(pl.r25 + 0x94);
    in.hit_at_entry = (rw(pl.r20) & 4) != 0;
    in.demo = rb(pl.r25 + 0x91) & 1;
    in.link = rb(0x20E623);
}

void capture_road(PlayerTest& t) { t.road.capture(t.pl.ring, t.pl.r25); t.road_ok = true; }

void report(Checker& c, PlayerTest& t, const char* what)
{
    if ((int) c.first.size() < max_diffs) c.first.push_back({ t.tick_no, frame_no, what });
}

void compare(Checker& c, PlayerTest& t, bool gear_only)
{
    const State& s = c.model.s;
    int nd = 0;
    std::string first;
    for (const Field& f : FIELDS)
    {
        bool gearf = !strcmp(f.name, "gear") || !strcmp(f.name, "ratio") || !strcmp(f.name, "shift_smoke") ||
                     !strcmp(f.name, "gearbox_state") || !strcmp(f.name, "gearbox_hud");
        if (gear_only && !gearf) continue;
        uint32_t mv = model_val(s, f), bv = t.pl.board_val(f);
        if (f.when == CRASH && s.state != 5) continue;
        if (f.when == CRASH_START && !(s.state == 5 && t.state_before != 5)) continue;
        if (f.when == MASK10) { mv &= 0x10; bv &= 0x10; }
        // the record's marker count is also the road generator's (in the car's tick, $FF8E): compared in
        // the goal run; the gear-box object's own fields at its ticks
        if (f.when == GOALRUN && s.state != 4) continue;
        if (f.when == GEARBOX && !gear_only) continue;
        if (mv != bv)
        {
            nd++;
            if (first.size() < 400)
            {
                char buf[160];
                snprintf(buf, sizeof buf, "%s%s ($%06X) model %X board %X", first.empty() ? "" : "; ", f.name, t.pl.addr(f), mv, bv);
                first += buf;
            }
        }
    }
    if (!gear_only)
        for (int g = 0; g <= std::min<int>(s.gears_m1, 7); g++)
            for (int i = 0; i < 256; i++)
                if (s.torque[g][i] != rw(t.pl.r23 + 0x300 + 0x200 * g + 2 * i))
                {
                    nd++;
                    if (first.size() < 400) { char buf[80]; snprintf(buf, sizeof buf, "; torque[%d][%d]", g, i); first += buf; }
                }
    if (gear_only) { c.gear_ticks++; if (nd) c.gear_bad++; }
    else { c.ticks++; if (nd) c.ticks_bad++; }
    c.field_diffs += (uint64_t) nd;
    if (nd)
    {
        char buf[600];
        snprintf(buf, sizeof buf, "P%d tick %llu (%s, state %d.%d): %s", t.pl.p + 1, (unsigned long long) t.tick_no,
                 gear_only ? "gear box" : "car", t.state_before, rb(t.pl.r20 + 0xD), first.c_str());
        report(c, t, buf);
        if (verbose) printf("[%s] f%llu %s\n", c.name, (unsigned long long) frame_no, buf);
    }
}

void run_model(Checker& c, PlayerTest& t)
{
    if (!c.resync && (!c.started || c.dead)) return;
    c.model.tick(t.in, t.road);
    if (c.model.unsupported())
    {
        c.unsupported++;
        char buf[120];
        snprintf(buf, sizeof buf, "P%d tick %llu: unsupported path $%X", t.pl.p + 1, (unsigned long long) t.tick_no, c.model.unsupported());
        report(c, t, buf);
        if (!c.resync) c.dead = true;
        return;
    }
    compare(c, t, false);
}

void events(PlayerTest& t)
{
    const Player& pl = t.pl;
    char k[64];
    uint8_t st = rb(pl.r20 + 0xC);
    snprintf(k, sizeof k, "state %d.%d", st, rb(pl.r20 + 0xD)); t.events[k]++;
    if (st == 5 && t.state_before != 5) { snprintf(k, sizeof k, "crash, kind %d", t.in.hit_kind); t.events[k]++; }
    if (t.in.hit && t.in.hit_kind == 3) t.events["minor contact (kind 3)"]++;
    if (rb(pl.r23 + 0x7C)) t.events["off-road"]++;
    if (rb(pl.r23 + 0x67)) t.events["sliding"]++;
    if (rb(pl.r23 + 0x65)) t.events["wheelspin / locked"]++;
    if (rb(pl.r23 + 0x144)) t.events["shift smoke"]++;
    if (rb(pl.r23 + 0x140) & 3) t.events["surface zone under a wheel"]++;
}

void hook(void*, uint32_t pc)
{
    auto it = cover.find(pc);
    if (it != cover.end()) it->second++;
    V60Cpu& cpu = B->maincpu();
    int p = player_of(cpu.reg(25));
    if (p < 0) return;
    PlayerTest& t = PT[p];
    uint32_t r20 = cpu.reg(20);
    switch (pc)
    {
    case 0x715C:
        if (r20 != t.pl.r20) return;
        t.demo = rb(t.pl.r25 + 0x91) & 1;
        if (t.demo) { t.demo_ticks++; return; }
        t.in_tick = true;
        t.road_ok = false;
        if (force && rb(t.pl.r20 + 0xC) == 2)
        {
            // the collision speed change $2004C4 large (the speed above $FFFF: $ACA0), or
            // negative with the wheelspin flag (the speed below 0 while spinning: $AB8B); the speed
            // $2004C0 above $FFFF for a scenery crash's response ($7FC6)
            // (each for one tick: $2004C4 is otherwise cleared only when a crash ends)
            uint64_t k = t.tick_no;
            static bool undo = false;
            if (undo) { ww(t.pl.r20 + 0xC4, 0); ww(t.pl.r20 + 0xC6, 0); undo = false; }
            if (k % 97 == 0 && rw(t.pl.r23 + 0x74) > 0x9000)
            {
                ww(t.pl.r20 + 0xC4, 0x9000); ww(t.pl.r20 + 0xC6, 0); undo = true;
                // every other time with the wheels locked: the wheelspin flag and the brake (the
                // controls' byte $208102) for this tick: the locked wheels' path above $FFFF ($ABC6)
                if (k % 194 == 0) { wb(t.pl.r23 + 0x65, 0xFF); wb(t.pl.r25 + 0x102, 0xFF); }
            }
            if (k % 401 == 50) { wb(t.pl.r23 + 0x65, 0xFF); ww(t.pl.r20 + 0xC4, 0); ww(t.pl.r20 + 0xC6, 0xFFFE); undo = true; }
            ww(t.pl.r20 + 0xC0, 0x2345); ww(t.pl.r20 + 0xC2, 1);
            t.b.started = false;    // mode b does not apply
        }
        capture_entry(t);
        t.state_before = rb(t.pl.r20 + 0xC);
        // mode b: loaded once, before the car's first tick of the race (state 0)
        if (t.state_before == 0) { load_state(t.b.model.s, t.pl); t.b.started = true; t.b.dead = false; }
        load_state(t.a.model.s, t.pl);
        break;
    case 0x71F0:                    // state 0, after $FF28: the course's first segments
    case 0xA39C:                    // the handling
        if (!t.in_tick || r20 != t.pl.r20) return;
        if (pc == 0xA39C) t.in.ref_rec_seg = rw(rl(t.pl.r25 + 0x68) + 0x42);
        if (!t.road_ok) capture_road(t);
        break;
    case 0x7348:                    // the contact flag after $127DB
        if (!t.in_tick || r20 != t.pl.r20) return;
        t.in.hit = (rw(t.pl.r20) & 4) != 0;
        t.in.hit_kind = rb(t.pl.r20 + 0x6E);
        t.in.hit_arg = rw(t.pl.r20 + 0x68);
        if (t.in.hit && t.in.hit_kind == 1)
        {
            uint32_t o = (t.pl.r25 & 0xFFFF0000u) | t.in.hit_arg;
            t.in.obj_lateral = (int16_t) rw(o + 0x20); t.in.obj_width = (int8_t) rb(o + 0x3F);
            t.in.obj_latvel = (int16_t) rw(o + 0x2E); t.in.obj_speed = rw(o + 0x2C); t.in.obj_weight = rw(o + 0x6A);
        }
        if (t.in.hit && t.in.hit_kind == 2)
        {
            uint32_t r = 0x20FC00 + ((uint32_t)(uint16_t)(t.in.hit_arg << 7));
            t.in.car_lateral = (int16_t) rw(r + 0x44); t.in.car_latvel = (int16_t) rw(r + 0x4E);
            t.in.car_speed = rw(r + 0x4C); t.in.car_model = rb(r + 0x49);
        }
        break;
    case 0x11F3E:                   // an object is called: the gear box's entry
        if (rl(r20 + 4) != 0xC379 || r20 != t.pl.r20 + 0x800) return;
        load_state(t.a.model.s, t.pl);
        t.in.shift_edges = rb(t.pl.r25 + 0x118);
        t.in.gearbox_state = rb(r20 + 0xC);
        t.in.goal = rb(t.pl.r23 + 0x181);
        break;
    case 0x11F41:                   // an object returned
        if (r20 == t.pl.r20 && t.in_tick)
        {
            t.in_tick = false;
            t.tick_no++;
            if (!t.road_ok && t.state_before != 0) { /* every state calls $A39C; state 0 $71F0 */ }
            run_model(t.a, t);
            run_model(t.b, t);
            events(t);
            if (t.route.empty() && rb(t.pl.r20 + 0xC) == 4)
            {
                char buf[16];
                t.route = "start node " + std::to_string(rb(0x2080A0)) + ", nodes";
                for (int i = 0; i < rb(0x208349) && i < 5; i++)
                { snprintf(buf, sizeof buf, " %d%s", rb(0x208344 + i), rb(0x20834A + i) ? "L" : "R"); t.route += buf; }
                t.route += ", the goal's node " + std::to_string(rb(0x2080A3));
            }
        }
        else if (r20 == t.pl.r20 + 0x800 && rl(r20 + 4) == 0xC379)
        {
            for (Checker* c : { &t.a, &t.b })
            {
                if (!c->resync && (!c->started || c->dead)) continue;
                c->model.gearbox_tick(t.in);
                compare(*c, t, true);
            }
        }
        break;
    }
}

// The transcribed routines' instructions (code only: the jump tables between them are left out)
const uint32_t RANGES[][2] = {
    { 0x715C, 0x7183 }, { 0x719C, 0x72F4 }, { 0x72F4, 0x739B }, { 0x739B, 0x73CE }, { 0x73E0, 0x7467 },
    { 0x7467, 0x7477 }, { 0x7490, 0x7602 }, { 0x7602, 0x76AF }, { 0x7A10, 0x7A4B }, { 0x7A4B, 0x7A7A },
    { 0x7B3F, 0x7B74 }, { 0x7F5D, 0x7F87 }, { 0x7F87, 0x7F92 }, { 0x7FA4, 0x8292 },
    { 0xA278, 0xA2B6 }, { 0xA2B6, 0xA304 }, { 0xA304, 0xA325 }, { 0xA39C, 0xA85E }, { 0xA870, 0xA89E },
    { 0xAAC9, 0xAD28 }, { 0xB764, 0xB968 }, { 0xC3C4, 0xC43E }, { 0xF579, 0xF5B7 }, { 0xF5C4, 0xF602 },
    { 0x104F6, 0x10575 }, { 0x12318, 0x12375 },
};

void build_cover()
{
    V60Disassembler dis;
    for (auto& r : RANGES)
        for (uint32_t pc = r[0]; pc < r[1];)
        {
            uint32_t res = 0;
            dis.disassemble(pc, [](uint32_t a) { return ROM[a & 0x1FFFFF]; }, &res);
            uint32_t len = res & V60Disassembler::LENGTHMASK;
            cover[pc] = 0;
            pc += len ? len : 1;
        }
}

// ------------------------------------------------------------------------------------------------
// The run-up and the driver
// ------------------------------------------------------------------------------------------------
struct Opts
{
    std::string scenario = "brief";
    int car = 0;
    bool manual = false;
    int frames = 0;
    bool nohold = false;        // the countdown is not held: time up, GAME OVER
    bool abuse = false;         // manual box: shift up at the top of the rev range, down at speed
    bool left = false;          // full: always left of centre (the left branch at every checkpoint: Kenya)
};

void frame() { B->run_frame(); frame_no++; }
void frames(int n) { for (int i = 0; i < n; i++) frame(); }

// As crossover/ormode's run-up: coin, START, the car (wheel left until the cursor is on it), the
// accelerator to confirm, the transmission (wheel right through its confirmation: manual), the entry
// countdowns shortened. Kenya: the route bytes of docs/OUTRUNNERS-RE.md 3.4.
bool run_up(const Opts& o, bool kenya, bool two)
{
    ORBoard& b = *B;
    frames(300);
    b.set_coin(0, true); if (two) b.set_coin(1, true);
    frames(6);
    b.set_coin(0, false); b.set_coin(1, false);
    while (rb(0x208080) != 2 && frame_no < 6000) frame();
    b.set_start(0, true); if (two) b.set_start(1, true);
    frames(20);
    b.set_start(0, false); b.set_start(1, false);
    while (rb(0x208082) != 3 && frame_no < 6000) frame();
    // the car select: the checks start here (the car's set-up, its state 0, comes with the race)
    B->set_pc_hooks(cover_pcs.data(), (int) cover_pcs.size(), hook, nullptr);
    frames(6);
    // the screen takes no input for about its first 105 frames (docs/OUTRUNNERS-RE.md): when the
    // cursor is already on the car, wait for it before confirming
    if (rb(0x209060) == o.car) frames(120);
    for (int i = 0; i < 9 && rb(0x209060) != o.car && frame_no < 6000; i++)
    {
        uint8_t c = rb(0x209060);
        b.set_steering(0, 0x00);
        for (int k = 0; k < 300 && rb(0x209060) == c; k++) frame();
        b.set_steering(0, 0x80);
        frames(20);
    }
    frames(40); b.set_accelerator(0, 0xFF); if (two) b.set_accelerator(1, 0xFF);
    frames(40); b.set_accelerator(0, 0); b.set_accelerator(1, 0);
    frames(40);
    if (o.manual) b.set_steering(0, 0xFF);
    frames(50); b.set_accelerator(0, 0xFF); if (two) b.set_accelerator(1, 0xFF);
    frames(40); b.set_accelerator(0, 0); b.set_accelerator(1, 0);
    frames(10);
    b.set_steering(0, 0x80);
    if (kenya)
    {
        for (uint32_t off : { 0u, 1u, 3u, 6u }) wb(0x2080A0 + off, 30);
        for (uint32_t off : { 2u, 4u, 5u }) wb(0x2080A0 + off, 5);
    }
    frames(50);
    if (rb(0x208082) == 3) { ww(0x20FB1A, 2); ww(0x20FC1A, 2); }
    while (rb(0x208082) != 6 && frame_no < 7000) frame();
    printf("race from frame %llu: P1 car %d, transmission %d%s\n", (unsigned long long) frame_no, rb(0x209060), rb(0x209061),
           two ? "; two players" : "");
    if (two) printf("P2 car %d, transmission %d\n", rb(0x219060), rb(0x219061));
    return rb(0x209060) == o.car && rb(0x209061) == (o.manual ? 1 : 0);
}

// A driver from what a player sees: steer towards a target lateral position (PD on the lateral
// position and velocity); the target and the pedals follow a script by time.
struct Driver
{
    int p;
    bool manual;
    bool abuse = false;
    int t = 0;
    void drive(double target, int accel, int brake)
    {
        const Player& pl = PT[p].pl;
        double lat = (int16_t) rw(pl.r23 + 0x38), vel = (int16_t) rw(pl.r20 + 0x2E);
        double st = 128 - 0.35 * (lat - target) - 3.0 * vel;
        int s = (int) std::lround(std::max(0.0, std::min(255.0, st)));
        B->set_steering(p, (uint8_t) s);
        B->set_accelerator(p, (uint8_t) accel);
        B->set_brake(p, (uint8_t) brake);
        if (manual)
        {
            uint16_t rpm = rw(pl.r23 + 0x70);
            uint8_t gear = rb(pl.r23 + 0x62), top = rb(pl.r23 + 0x63);
            static int hold[2];
            bool up = false, down = false;
            t++;
            if (hold[p] > 0) hold[p]--;
            else if (abuse)
            {
                // late shifts up (the over-rev smoke, $C3F9); every 400 frames two shifts down at
                // speed (over the lower gear's top speed: $AC73; the engine's limit $AD1E, $C428)
                if (t % 400 < 40 && gear > 1) { down = true; hold[p] = 12; }
                else if (rpm >= 0xF800 && gear < top && t % 400 > 200) { up = true; hold[p] = 8; }
            }
            else if (rpm > 0xE800 && gear < top) { up = true; hold[p] = 8; }
            else if (rpm < 0x7000 && gear > 0) { down = true; hold[p] = 8; }
            B->set_shift_up(p, up);
            B->set_shift_down(p, down);
        }
    }
};

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: orcartest <romdir> full|kenya|two|brief [-car n] [-manual] [-abuse] [-left] [-nohold] [-force] [-frames n] [-v] [-diffs n]\n");
        return 2;
    }
    Opts o;
    o.scenario = argv[2];
    for (int i = 3; i < argc; i++)
    {
        std::string a = argv[i];
        if (a == "-car" && i + 1 < argc) o.car = atoi(argv[++i]);
        else if (a == "-manual") o.manual = true;
        else if (a == "-nohold") o.nohold = true;
        else if (a == "-abuse") o.abuse = true;
        else if (a == "-force") force = true;
        else if (a == "-left") o.left = true;
        else if (a == "-frames" && i + 1 < argc) o.frames = atoi(argv[++i]);
        else if (a == "-v") verbose = true;
        else if (a == "-diffs" && i + 1 < argc) max_diffs = atoi(argv[++i]);
    }
    ORBoard::Roms roms; std::string err;
    if (!ORBoard::load_roms(argv[1], roms, err)) { fprintf(stderr, "ROMs: %s\n", err.c_str()); return 1; }
    ROM = roms.maincpu;
    static ORBoard board;
    B = &board;
    if (!board.init(std::move(roms))) { fprintf(stderr, "board init failed\n"); return 1; }
    board.power_on(nullptr);
    board.draw_mask = 0;
    for (int p = 0; p < 2; p++)
    {
        Player& pl = PT[p].pl;
        pl.p = p;
        uint32_t off = p ? 0x10000 : 0;
        pl.r20 = 0x200400 + off; pl.r23 = 0x209000 + off; pl.r25 = 0x208000 + off; pl.ring = 0x20A000 + off;
    }
    for (PlayerTest& t : PT) { t.a.model = ORCar(ROM.data()); t.b.model = ORCar(ROM.data()); }
    setvbuf(stdout, nullptr, _IOLBF, 0);
    build_cover();
    for (auto& e : cover) cover_pcs.push_back(e.first);
    for (uint32_t pc : { 0x715Cu, 0x71F0u, 0xA39Cu, 0x7348u, 0x11F3Eu, 0x11F41u }) cover_pcs.push_back(pc);
    std::sort(cover_pcs.begin(), cover_pcs.end());
    cover_pcs.erase(std::unique(cover_pcs.begin(), cover_pcs.end()), cover_pcs.end());

    bool kenya = o.scenario == "kenya", two = o.scenario == "two";
    if (two) { if (!run_up(o, false, true)) printf("run-up: not the car / transmission asked for\n"); }
    else if (!run_up(o, kenya, false)) printf("run-up: not the car / transmission asked for\n");

    int total = o.frames ? o.frames : o.scenario == "full" ? 26000 : o.scenario == "kenya" ? 9000 : o.scenario == "two" ? 6000 : 3000;
    Driver d1{ 0, o.manual }, d2{ 1, false };
    d1.abuse = o.abuse;
    uint64_t f0 = frame_no;
    for (int k = 0; k < total; k++)
    {
        int t = k;
        // the countdown held (40 s) except for Kenya's last 2,400 frames (time up, GAME OVER)
        bool hold = !o.nohold && !(kenya && t > total - 2400);
        if (hold) { ww(0x20FB6A, 1200); if (two) ww(0x20FBEA, 1200); }
        double target;
        int accel = 0xFF, brake = 0;
        if (o.scenario == "full")
        {
            // left of centre (the checkpoints' left branches: Kenya), waves, off the road now and then
            target = -120 + 160 * sin(t / 170.0);
            if (t % 3000 > 2700) target = (t / 3000) % 2 ? 700 : -700;
            if (o.left) { target = -160 + 90 * sin(t / 170.0); if (t % 3000 > 2700) target = -700; }
            if (t % 1100 > 1040) brake = 0xFF;
            if (t % 2300 > 2200) accel = 0x60;
        }
        else if (kenya)
        {
            target = 250 * sin(t / 120.0) + 120 * sin(t / 37.0);
            if (t % 1500 > 1350) target = (t / 1500) % 2 ? 650 : -650;
            if (t % 900 > 850) brake = 0xC0;
            if (t % 700 > 640) accel = 0x80;
        }
        else if (two)
        {
            target = (t % 600 < 300) ? 0 : -200;
            if (t % 2000 > 1800) target = 600;
        }
        else
        {
            target = 200 * sin(t / 90.0);
            if (t % 1000 > 900) target = (t / 1000) % 2 ? 650 : -650;
            if (t % 700 > 660) brake = 0xFF;
        }
        d1.drive(target, accel, brake);
        if (two)
        {
            double t2 = (t % 600 < 300) ? 0 : 200;
            if (t % 2000 > 1800) t2 = -600;
            d2.drive(t2, 0xFF, 0);
        }
        frame();
    }
    printf("%llu frames from frame %llu\n", (unsigned long long)(frame_no - f0), (unsigned long long) f0);
    // the route: the nodes of the checkpoints passed ($208344..) and the choices (1 = left, $20834A..)
    if (!PT[0].route.empty()) printf("goal reached: %s\n", PT[0].route.c_str());

    int fail = 0;
    for (int p = 0; p < (two ? 2 : 1); p++)
    {
        PlayerTest& t = PT[p];
        printf("\nPlayer %d: %llu car ticks; state at the end %d.%d; %llu ticks of the attract demo afterwards (not checked)\n",
               p + 1, (unsigned long long) t.tick_no, rb(t.pl.r20 + 0xC), rb(t.pl.r20 + 0xD), (unsigned long long) t.demo_ticks);
        for (Checker* c : { &t.a, &t.b })
        {
            printf("  mode %s: %llu car ticks compared, %llu with differences; %llu gear-box ticks, %llu with differences; "
                   "%llu field differences; %llu unsupported\n", c->name, (unsigned long long) c->ticks,
                   (unsigned long long) c->ticks_bad, (unsigned long long) c->gear_ticks, (unsigned long long) c->gear_bad,
                   (unsigned long long) c->field_diffs, (unsigned long long) c->unsupported);
            for (const Diff& d : c->first) printf("    frame %llu: %s\n", (unsigned long long) d.frame, d.what.c_str());
            if (c->ticks_bad || c->gear_bad || c->unsupported || (c->ticks == 0 && !(force && !c->resync))) fail = 1;
        }
        printf("  situations (ticks):");
        for (auto& e : t.events) printf(" [%s] %llu", e.first.c_str(), (unsigned long long) e.second);
        printf("\n");
    }
    // coverage of the transcribed routines
    size_t hit = 0;
    for (auto& e : cover) if (e.second) hit++;
    printf("\ncoverage: %zu of %zu instructions of the transcribed routines executed\n", hit, cover.size());
    if (verbose || getenv("ORCARTEST_COVER"))
    {
        printf("not executed:");
        for (auto& e : cover) if (!e.second) printf(" %X", e.first);
        printf("\n");
    }
    if (const char* cf = getenv("ORCARTEST_COVER_FILE"))
        if (FILE* f = fopen(cf, "w")) { for (auto& e : cover) fprintf(f, "%X %llu\n", e.first, (unsigned long long) e.second); fclose(f); }
    return fail;
}
