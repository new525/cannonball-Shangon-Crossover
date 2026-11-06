/***************************************************************************
    OutRunners' courses on the emulated board. See ormode.hpp.
***************************************************************************/
#include "crossover/ormode.hpp"
#include "crossover/orboard.hpp"
#include "crossover/romset.hpp"
#include "main.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <unordered_map>
#include "video.hpp"
#include "globals.hpp"
#include "frontend/config.hpp"
#include "sdl2/input.hpp"
#include "engine/oinputs.hpp"
#include "engine/audio/osoundint.hpp"
#include "crossover/xaudio.hpp"
#include "crossover/orvehicle.hpp"
#include "crossover/balance_push.hpp"
#include "crossover/balance.hpp"
#include "ffb/ffb.hpp"

ORMode ormode;

// Work RAM addresses (player 1), docs/OUTRUNNERS-RE.md
static const uint32_t A_STATE    = 0x208080;    // game state: 2 credit, 6 game
static const uint32_t A_SUBSTATE = 0x208082;    // game substate: 3 car select, 5 race setup, 6 start
static const uint32_t A_CAR      = 0x201080;    // player 1's car (name-table index), set as the race starts
static const uint32_t A_CAR_SEL  = 0x209060;    // the car select's cursor (set 6 frames into substate 3)
static const uint32_t A_TRANS    = 0x209061;    // transmission: 0 automatic, 1 manual
static const uint32_t A_ROUTE    = 0x2080A0;    // route node / stage bytes $2080A0-$2080A6
static const uint32_t A_ENTRY_P1 = 0x20FB1A, A_ENTRY_P2 = 0x20FC1A;  // entry countdowns (16-bit, 1/30 s)
static const uint32_t A_GOAL     = 0x209181;    // $FF: the car has reached the goal ($CB51)

// Sound: MAME's routing of the left speaker (YM3438 output 1 x 0.40, MultiPCM output 1 x 1.0) and
// the program's level for a race on OutRunners' board: x 0.17 brings a race (music and the car's
// sounds, RMS 4,500 at x 0.26 on Kenya, 16-bit) to OutRun's own races in the program (about 3,000).
// A design decision, as for the music player (crossover/ormusic.cpp).
static const float YM_ROUTE = 0.40f, PCM_ROUTE = 1.0f, GAIN_OR = 0.17f;

// Kenya alone: the checkpoint bookkeeping of normal play on reaching Kenya by the West and the lower
// branches (tools/ortrace, e15 at frame 16420 against the start), written when the car reaches the
// goal ($209181 set, the frame before the goal run starts and counts the goal as the sixth marker).
// Without it the goal is counted as the first marker, its finish time is never written and the goal
// run waits for it for ever ($74AE); written at the start instead, it shows EXTEND TIME. The
// checkpoint times stay 0. $208330 the player's markers passed, $208338-$20833C the cars past each
// marker, $208342 the markers processed, $208344-$208348 the route's nodes, $208349 the decisions,
// $20834A-$20834E the choices (1 = left), $20835A-$20835E markers per car, $208480 the player's
// markers; in the player's record $20FB60 markers passed, $20FB61/$20FB63 processed, $20FB65 the last
// marker's index, $20FB67 its choice (docs/CROSSOVER-OUTRUNNERS.md section 6)
static const std::pair<uint32_t, uint8_t> KENYA_BOOKKEEPING[] = {
    { 0x208330, 5 }, { 0x208338, 1 }, { 0x208339, 1 }, { 0x20833A, 1 }, { 0x20833B, 1 }, { 0x20833C, 1 },
    { 0x208342, 5 }, { 0x208344, 2 }, { 0x208345, 6 }, { 0x208346, 12 }, { 0x208347, 20 }, { 0x208348, 30 },
    { 0x208349, 5 }, { 0x20834A, 1 }, { 0x20834B, 1 }, { 0x20834C, 1 }, { 0x20834D, 1 }, { 0x20834E, 1 },
    { 0x20835A, 1 }, { 0x20835B, 1 }, { 0x20835C, 1 }, { 0x20835D, 1 }, { 0x20835E, 1 }, { 0x208480, 5 },
    { 0x20FB60, 5 }, { 0x20FB61, 5 }, { 0x20FB63, 5 }, { 0x20FB65, 4 }, { 0x20FB67, 1 },
};

// A run-up that has not reached the race by this frame has gone wrong
static const int RUNUP_LIMIT = 6000;

std::string ORMode::eeprom_file() const { return config.data.save_path + "orunners_eeprom.bin"; }

void ORMode::load_eeprom(std::vector<uint8_t>& e) const
{
    e.clear();
    std::ifstream f(eeprom_file(), std::ios::binary);
    if (!f) return;
    std::vector<uint8_t> v((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (v.size() == 128) e = v;
}

void ORMode::save_eeprom() const
{
    uint8_t e[128];
    board->eeprom_save(e);
    std::ofstream f(eeprom_file(), std::ios::binary);
    if (f) f.write((const char*) e, sizeof e);
}

bool ORMode::init()
{
    ok = or_roms.loaded() && or_roms.full;
    return ok;
}

// The board's ROMs (26 MB) are loaded on the first race only
bool ORMode::load_board()
{
    if (loaded) return true;
    ORBoard::Roms roms;
    std::string err;
    if (!ORBoard::load_roms(config.crossover.outrunners_rompath, roms, err))
    {
        std::cerr << "OutRunners board: " << err << std::endl;
        ok = false;
        return false;
    }
    board.reset(new ORBoard());
    if (!board->init(std::move(roms))) { ok = false; return false; }
    loaded = true;
    return true;
}

void ORMode::run_up(int course, int car, bool manual)
{
    ORBoard& b = *board;
    b.draw_mask = 0;                                    // nothing is shown during the run-up
    int f = 0;
    auto frame = [&]() { b.run_frame(); f++; b.sound().ym0.clear(); b.sound().ym1.clear();
                         b.sound().pcm0.clear(); b.sound().pcm1.clear(); };
    auto rd = [&](uint32_t a) { return b.read8(a); };

    auto frames = [&](int n) { for (int k = 0; k < n && f < RUNUP_LIMIT; k++) frame(); };

    // A coin during the boot (taken at once), START as soon as the boot is over ($208080 0 -> 2
    // at frame 679, coin or not; START at 680 starts the game at 683)
    frames(300);
    b.set_coin(0, true);
    frames(6);
    b.set_coin(0, false);
    while (rd(A_STATE) != 2 && f < RUNUP_LIMIT) frame();
    b.set_start(0, true);
    frames(20);
    b.set_start(0, false);
    while (rd(A_SUBSTATE) != 3 && f < RUNUP_LIMIT) frame();
    frames(6);                                          // the cursor is set 6 frames in
    // The car: the wheel left until the cursor moves (the screen takes no input for its first ~105
    // frames, then a step about 20 frames after the wheel turns), centred for 20 frames, again
    // until the cursor is on the chosen car
    for (int i = 0; i < 8 && rd(A_CAR_SEL) != car && f < RUNUP_LIMIT; i++)
    {
        uint8_t c = rd(A_CAR_SEL);
        b.set_steering(0, 0x00);
        for (int k = 0; k < 300 && rd(A_CAR_SEL) == c && f < RUNUP_LIMIT; k++) frame();
        b.set_steering(0, 0x80);
        frames(20);
    }
    if (rd(A_CAR_SEL) != car) std::cerr << "OutRunners: car select is on car " << (int) rd(A_CAR_SEL) << std::endl;
    // Confirmed with the accelerator ("choose with the wheel, decide with the accelerator"), then the
    // transmission screen: wheel right for the manual box, held through its confirmation
    frames(40);
    b.set_accelerator(0, 0xFF);
    frames(40);
    b.set_accelerator(0, 0x00);
    frames(40);
    if (manual) b.set_steering(0, 0xFF);
    frames(50);
    b.set_accelerator(0, 0xFF);
    frames(40);
    b.set_accelerator(0, 0x00);
    frames(10);
    b.set_steering(0, 0x80);
    // the single stage: route node 30, stage 5 (Kenya, course 41), before the race setup
    if (course == KENYA)
    {
        if (rd(A_SUBSTATE) >= 5) std::cerr << "OutRunners: too late for the stage" << std::endl;
        for (uint32_t o : { 0u, 1u, 3u, 6u }) b.write8(A_ROUTE + o, 30);
        for (uint32_t o : { 2u, 4u, 5u })     b.write8(A_ROUTE + o, 5);
    }
    // The game then waits for other players to enter until its entry countdown ($20FB1A, $20FC1A:
    // 1/30 s) runs out; it is shortened to 2 (the race starts about 200 frames later instead of
    // about 760)
    frames(50);
    if (rd(A_SUBSTATE) == 3)
    {
        b.write8(A_ENTRY_P1, 2); b.write8(A_ENTRY_P1 + 1, 0);
        b.write8(A_ENTRY_P2, 2); b.write8(A_ENTRY_P2 + 1, 0);
    }
    while (rd(A_SUBSTATE) != 6 && f < RUNUP_LIMIT) frame();
    if (f >= RUNUP_LIMIT) std::cerr << "OutRunners: run-up did not reach the race" << std::endl;
    if (getenv("CB_OR_DEBUG")) fprintf(stderr, "OutRunners: race from frame %d\n", f);
    if (rd(A_TRANS) != (manual ? 1 : 0)) std::cerr << "OutRunners: transmission is " << (int) rd(A_TRANS) << std::endl;
    if (rd(A_CAR) != car) std::cerr << "OutRunners: the race has car " << (int) rd(A_CAR) << std::endl;
    b.draw_mask = 1;                                    // the left screen (player 1)
}

void ORMode::start(int course, Vehicle v, bool manual)
{
    if (!ok || !load_board()) return;
    std::vector<uint8_t> e;
    load_eeprom(e);
    board->power_on(e.empty() ? nullptr : e.data());
    vehicle = v;
    course_ = course;
    kenya_goal = false;
    // OutRun's Ferrari or Super Hang-On's bike: the run-up chooses MAD POWER with its automatic box (its
    // collision box and off-road width are the Ferrari's at the conversion's scale; the bike's are set in
    // OutRunners' tests, docs/CROSSOVER-OUTRUNNERS.md 7 and 8)
    manual_box = v == MAD_POWER && manual;
    run_up(course, CAR_MAD_POWER, manual_box);
    orvehicle.enabled = v != MAD_POWER;
    orvehicle.vehicle = v == SH_BIKE ? ORVehicle::SH_BIKE : ORVehicle::FERRARI;
    orvehicle.begin(board.get());
    ym.clear(); pcm.clear();
    feeding = true;
    acc = 0;
    finished = false;
    active = true;
    board->run_frame();
    take_sound();
    show_frame();
}

void ORMode::stop()
{
    if (!active) return;
    active = false;
    feeding = false;
    orvehicle.end();
    save_eeprom();
    ym.clear(); pcm.clear();
    osoundint.queue_sound(sound::FM_RESET);
    video.set_external(nullptr);
}

// Player controls -> the board's ADC and shift buttons. The wheel: OutRun's adjusted steering
// ($48 left .. $B8 right around $80) over the full travel of ANALOG1 (0 left, $80 centre, $FF
// right: the game holds $FF - value at $208100, e11). Pedals: OutRun's 0-$100 over 0-$FF.
void ORMode::set_inputs()
{
    ORBoard& b = *board;
    if (cannonball::tick_frame)
        oinputs.adjust_inputs();
    int st = std::max(0x48, std::min((int) oinputs.input_steering, 0xB8));
    b.set_steering(0, (uint8_t) std::max(0, std::min(255, 0x80 + ((st - 0x80) * 0x7F) / 0x38)));
    int a = std::max(0, std::min((int) oinputs.acc_adjust, 0x100));
    int k = std::max(0, std::min((int) oinputs.brake_adjust, 0x100));
    b.set_accelerator(0, (uint8_t) std::min(255, a));
    b.set_brake(0, (uint8_t) std::min(255, k));
    // The Ferrari or the bike drives itself (crossover/orvehicle): in a race OutRunners' car sees the wheel
    // centred and no accelerator; the brake is passed on, so that its camera dips as the car brakes ($7B74)
    if (orvehicle.driving())
    {
        b.set_steering(0, 0x80);
        b.set_accelerator(0, 0);
    }
    // a manual box: GEAR1 shifts down, GEAR2 up (the program's separate-buttons gear setting)
    b.set_shift_up(0, manual_box && input.is_pressed(Input::GEAR2));
    b.set_shift_down(0, manual_box && input.is_pressed(Input::GEAR1));
    // The radio's buttons (P2_A, player 1), as OUTRUNNERS RADIO maps them on the other courses
    // (crossover/radio): UP DJ/MUSIC, DOWN >>; LEFT <<, RIGHT >> when the steering is analogue
    bool dj = input.is_pressed(Input::UP), next = input.is_pressed(Input::DOWN), prev = false;
    if (input.analog)
    {
        prev = input.is_pressed(Input::LEFT);
        next = next || input.is_pressed(Input::RIGHT);
    }
    b.in.p2_a = (uint8_t)(0xFF & ~((dj ? 0x01 : 0) | (prev ? 0x02 : 0) | (next ? 0x04 : 0)));
}

void ORMode::take_sound()
{
    ORSound& s = board->sound();
    s.flush_streams();
    if (feeding)
    {
        for (float v : s.ym1)  { float x = v * YM_ROUTE * GAIN_OR * 32768.0f;  ym.push(x, x); }
        for (float v : s.pcm1) { float x = v * PCM_ROUTE * GAIN_OR * 32768.0f; pcm.push(x, x); }
    }
    s.ym0.clear(); s.ym1.clear(); s.pcm0.clear(); s.pcm1.clear();
}

void ORMode::mix(int32_t* out, int samples, int out_rate)
{
    if (!active || !feeding || out_rate <= 0) return;
    static const bool mute = getenv("CB_OR_MUTE_BOARD") != nullptr;    // testing: the other sounds alone
    if (mute) return;
    if (ym.available())  ym.pull((double) board->sound().ym_rate() / out_rate, out, samples);
    if (pcm.available()) pcm.pull((double) board->sound().pcm_rate() / out_rate, out, samples);
}

// The left screen's RGB pixels, as indices into a palette of the colours it uses (447 at most in
// 235 screens measured; beyond EXT_COLOURS - 1 colours the rest would share the last entry)
void ORMode::show_frame()
{
    const int w = board->screen_width(0), h = 224;
    const uint32_t* px = board->screen_pixels(0);
    pixels.resize((size_t) w * h);
    colours.assign(EXT_COLOURS, 0);
    std::unordered_map<uint32_t, uint16_t> idx;
    idx.reserve(1024);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            uint32_t c = px[(size_t) y * S32Video::SCREEN_W_MAX + x] & 0xFFFFFF;
            auto it = idx.find(c);
            uint16_t i;
            if (it != idx.end()) i = it->second;
            else
            {
                i = (uint16_t) std::min<size_t>(idx.size(), EXT_COLOURS - 1);
                if (idx.size() < EXT_COLOURS - 1) { idx[c] = i; colours[i] = c; }
            }
            pixels[(size_t) y * w + x] = i;
        }
    video.set_external(pixels.data(), w, h, colours.data());
}

void ORMode::tick()
{
    if (!active) return;
    set_inputs();
    acc += 60;
    int frames = acc / config.fps;
    acc -= frames * config.fps;
    for (int i = 0; i < frames; i++)
    {
        // only the left screen, and only the frame that will be shown (at 30 fps, the second of
        // two); drawing changes nothing the game sees (tools/orbtest: RAM and I/O identical)
        board->draw_mask = i == frames - 1 ? 1 : 0;
        board->run_frame();
        balance::push_frame_or(board.get());    // measurement (CB_PUSH_LOG)
        take_sound();
        balance::contact_trace_or(*board, vehicle == MAD_POWER);   // testing: CB_CONTACT_LOG, CB_CHASE
        if (board->read8(A_STATE) != 6) finished = true;
        // testing: the wheel motor output (port D bit 4) against the shaker byte $20E0D0, per board frame
        static FILE* motor_log = getenv("CB_OR_MOTOR_LOG") ? fopen(getenv("CB_OR_MOTOR_LOG"), "w") : nullptr;
        if (motor_log)
            fprintf(motor_log, "%llu %02x %02x %d %d %d\n", (unsigned long long) board->frame(), board->lamps_d[0], board->read8(0x20E0D0),
                    board->read8(0x20040C), board->read8(0x20907C), board->read8(0x209140));
        // testing: the cars' sounds alone (the music stopped, $1F, at this board frame)
        static const int stop_music = getenv("CB_OR_STOP_MUSIC") ? atoi(getenv("CB_OR_STOP_MUSIC")) : 0;
        if (stop_music && board->frame() == (uint64_t) stop_music) board->write8(0x700700, 0x1F);
        // testing: the radio's song ($209014) and state (C[$201000]) as they change
        static const bool radio_debug = getenv("CB_OR_DEBUG") != nullptr;
        if (radio_debug)
        {
            static int song = -1, c = -1;
            int s = board->read8(0x209014), rc = board->read8(0x20100C);
            if (s != song || rc != c) fprintf(stderr, "OutRunners: frame %llu radio song %d state %d\n", (unsigned long long) board->frame(), s, rc);
            song = s; c = rc;
        }
        if (course_ == KENYA && !kenya_goal && board->read8(A_GOAL) == 0xFF)
        {
            for (const auto& w : KENYA_BOOKKEEPING) board->write8(w.first, w.second);
            kenya_goal = true;
        }
    }
    if (config.controls.haptic && cannonball::tick_frame && vehicle == MAD_POWER) ffb_tick();
    show_frame();
}

// Force feedback (ffb/ffb): OutRunners' own car on its board, from its RAM once per 30 Hz tick (player 1's
// addresses, docs/OUTRUNNERS-RE.md; crossover/madpower.cpp reads the same state from its transcription).
// The arcade base is the cabinet's wheel vibration motor as the game drives it: bit 4 of the I/O chip's
// port D (MAME's MA_Steering_Wheel_motor), the board's lamps_d[0]. The car: state $20040C (1 start line,
// 2 driving, 3 time up, 4 goal run, 5 a contact's bounce), speed $209074 (km/h x 136), off-road $20907C,
// the wheel kinds $209140 (bit 0 the right side in a surface zone, bit 1 the left: the side off the road,
// or a loose surface on it), sliding $209067, wheelspin $209065 and the shift's $209144, gear $209062,
// the contact's push $2004CE (+ right) and kind $20046E (0 scenery, 2 a car).
void ORMode::ffb_tick()
{
    if (!ffb::active() || !board) return;
    const ORBoard& b = *board;
    auto r8  = [&](uint32_t a) { return (int) b.read8(a); };
    auto r16 = [&](uint32_t a) { return (int)(int16_t)(b.read8(a) | b.read8(a + 1) << 8); };
    const int cs = r8(0x20040C);
    ffb::State s;
    s.speed = (int)(uint16_t) r16(0x209074) / 136;
    s.motor_bit = (b.lamps_d[0] >> 4) & 1;
    if (r8(A_STATE) == 6 && cs >= 2 && cs <= 5)
    {
        s.phase    = ffb::RACE;
        const int sides = r8(0x209140) & 3, shift_smoke = r8(0x209144);
        s.wheels   = !r8(0x20907C) ? ffb::ON : sides == 2 ? ffb::LEFT_OFF : sides == 1 ? ffb::RIGHT_OFF : ffb::OFF;
        s.side     = r16(0x209038) <= 0 ? 1 : -1;      // OutRunners' lateral is + right
        s.slipping = (r8(0x209067) || r8(0x209065) || shift_smoke) && s.wheels == ffb::ON;
        s.rough    = s.wheels == ffb::ON && sides && !shift_smoke;
        if (cs == 5)
        {
            const int push = r16(0x2004CE);
            if (ffb_crash_ticks == 0)
            {
                ffb_crash_dir = push > 0 ? -1 : (push < 0 ? 1 : s.side);
                ffb_hit_kind = (uint8_t) r8(0x20046E);
            }
            ffb_crash_ticks++;
            if (ffb_hit_kind == 2)
                s.skid = ffb_crash_dir;
            else
            {
                s.crash = ffb::CRASH_TUMBLE; s.crash_state = 1; s.crash_counter = ffb_crash_ticks; s.crash_dir = ffb_crash_dir;
            }
        }
        else
            ffb_crash_ticks = 0;
        const uint8_t gear = (uint8_t) r8(0x209062);
        if (gear != ffb_gear && manual_box) s.gear_shift = gear > ffb_gear ? 1 : -1;
        ffb_gear = gear;
    }
    else if (r8(A_STATE) == 6 && cs == 1)
    {
        s.phase = ffb::START;
        s.start_pedal = std::max(0, std::min(255, (int) oinputs.acc_adjust));
        ffb_crash_ticks = 0;
        ffb_gear = (uint8_t) r8(0x209062);
    }
    else
        ffb_crash_ticks = 0;
    ffb::tick(s);
}
