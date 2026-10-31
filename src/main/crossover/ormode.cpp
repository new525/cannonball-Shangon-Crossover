/***************************************************************************
    OutRunners' courses on the emulated board. See ormode.hpp.
***************************************************************************/
#include "crossover/ormode.hpp"
#include "crossover/orboard.hpp"
#include "crossover/romset.hpp"
#include "main.hpp"

#include <algorithm>
#include <cstdio>
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

ORMode ormode;

// Work RAM addresses (player 1), docs/OUTRUNNERS-RE.md
static const uint32_t A_STATE    = 0x208080;    // game state: 2 credit, 6 game
static const uint32_t A_SUBSTATE = 0x208082;    // game substate: 3 car select, 5 race setup, 6 start
static const uint32_t A_CAR      = 0x201080;    // player 1's car (name-table index), set as the race starts
static const uint32_t A_CAR_SEL  = 0x209060;    // the car select's cursor (set 6 frames into substate 3)
static const uint32_t A_TRANS    = 0x209061;    // transmission: 0 automatic, 1 manual
static const uint32_t A_ROUTE    = 0x2080A0;    // route node / stage bytes $2080A0-$2080A6

// Sound: MAME's routing of the left speaker (YM3438 output 1 x 0.40, MultiPCM output 1 x 1.0) and
// the program's level for a race on OutRunners' board: x 0.17 brings a race (music and the car's
// sounds, RMS 4,500 at x 0.26 on Kenya, 16-bit) to OutRun's own races in the program (about 3,000).
// A design decision, as for the music player (crossover/ormusic.cpp).
static const float YM_ROUTE = 0.40f, PCM_ROUTE = 1.0f, GAIN_OR = 0.17f;

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

    // coin
    while (f < 600) frame();
    b.set_coin(0, true);
    for (int i = 0; i < 6; i++) frame();
    b.set_coin(0, false);
    while (rd(A_STATE) != 2 && f < RUNUP_LIMIT) frame();
    // START
    b.set_start(0, true);
    for (int i = 0; i < 60; i++) frame();
    b.set_start(0, false);
    while (rd(A_SUBSTATE) != 3 && f < RUNUP_LIMIT) frame();
    int t0 = f;
    while (f < t0 + 10) frame();
    // the car: one step left at a time (20 frames held, then centred) until the cursor is on it
    for (int i = 0; i < 8 && rd(A_CAR_SEL) != car && f < RUNUP_LIMIT; i++)
    {
        while (f < t0 + 133 + 150 * i) frame();
        b.set_steering(0, 0x00);
        for (int k = 0; k < 20; k++) frame();
        b.set_steering(0, 0x80);
        for (int k = 0; k < 40; k++) frame();
    }
    if (rd(A_CAR_SEL) != car) std::cerr << "OutRunners: car select is on car " << (int) rd(A_CAR_SEL) << std::endl;
    // the transmission: held right for a manual box (e09: frames 2200-2520, car select from 767)
    while (f < t0 + 1433 && rd(A_SUBSTATE) == 3) frame();
    if (manual) b.set_steering(0, 0xFF);
    while (f < t0 + 1733 && rd(A_SUBSTATE) == 3) frame();
    b.set_steering(0, 0x80);
    // the single stage: route node 30, stage 5 (Kenya, course 41), before the race setup
    if (course == KENYA)
    {
        if (rd(A_SUBSTATE) >= 5) std::cerr << "OutRunners: too late for the stage" << std::endl;
        for (uint32_t o : { 0u, 1u, 3u, 6u }) b.write8(A_ROUTE + o, 30);
        for (uint32_t o : { 2u, 4u, 5u })     b.write8(A_ROUTE + o, 5);
    }
    while (rd(A_SUBSTATE) != 6 && f < RUNUP_LIMIT) frame();
    if (f >= RUNUP_LIMIT) std::cerr << "OutRunners: run-up did not reach the race" << std::endl;
    if (rd(A_TRANS) != (manual ? 1 : 0)) std::cerr << "OutRunners: transmission is " << (int) rd(A_TRANS) << std::endl;
    if (rd(A_CAR) != car) std::cerr << "OutRunners: the race has car " << (int) rd(A_CAR) << std::endl;
    b.draw_mask = 1;                                    // the left screen (player 1)
}

void ORMode::start(int course, int car, bool manual)
{
    if (!ok || !load_board()) return;
    std::vector<uint8_t> e;
    load_eeprom(e);
    board->power_on(e.empty() ? nullptr : e.data());
    manual_box = manual;
    run_up(course, car, manual);
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
    // a manual box: GEAR1 shifts down, GEAR2 up (the program's separate-buttons gear setting)
    b.set_shift_up(0, manual_box && input.is_pressed(Input::GEAR2));
    b.set_shift_down(0, manual_box && input.is_pressed(Input::GEAR1));
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
        board->run_frame();
        take_sound();
        if (board->read8(A_STATE) != 6) finished = true;
    }
    show_frame();
}
