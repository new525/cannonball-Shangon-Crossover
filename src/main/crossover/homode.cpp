/***************************************************************************
    Hang-On's course on the emulated board. See homode.hpp.
***************************************************************************/
#include "crossover/homode.hpp"
#include "crossover/hoboard.hpp"
#include "crossover/hovideo.hpp"
#include "crossover/hovehicle.hpp"
#include "crossover/romset.hpp"
#include "crossover/xaudio.hpp"
#include "main.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include "video.hpp"
#include "frontend/config.hpp"
#include "sdl2/input.hpp"
#include "engine/oinputs.hpp"
#include "engine/omusic.hpp"
#include "engine/audio/osoundint.hpp"

HOMode homode;

static const uint8_t HO_RACE_MUSIC = 0x9D;
static const uint32_t W_INGAME = 0x408;     // $20C408: $FFFF while a game is played

// Work RAM areas kept between sessions (offsets from $20C000), see homode.hpp
static const struct { uint32_t off, len; } SCORE_AREAS[] = { { 0x1800, 0x4A4 }, { 0x003C, 4 }, { 0x1F90, 0x10 } };
static const uint32_t SCORE_BYTES = 0x4A4 + 4 + 0x10;

static std::string scores_file() { return config.data.save_path + "hiscores_hangon.bin"; }

void HOMode::load_scores()
{
    scores.clear();
    std::ifstream f(scores_file(), std::ios::binary);
    if (!f) return;
    std::vector<uint8_t> v((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (v.size() == SCORE_BYTES) scores = v;
}

void HOMode::save_scores()
{
    std::vector<uint8_t> v;
    for (const auto& a : SCORE_AREAS) v.insert(v.end(), hoboard.workram + a.off, hoboard.workram + a.off + a.len);
    std::ofstream f(scores_file(), std::ios::binary);
    if (f) f.write((const char*) v.data(), (std::streamsize) v.size());
}

bool HOMode::init()
{
    ok = ho_roms.loaded() && hoboard.init(ho_roms) && hovideo.init(ho_roms);
    hoboard.sound_command = [this](uint8_t b) { packet.push_back(b); };
    hoboard.sound_reset = nullptr;          // only used at power-on, which happens unheard
    return ok;
}

bool HOMode::ingame() const { return hoboard.workram[W_INGAME] || hoboard.workram[W_INGAME + 1]; }

// Player controls -> the board's ADC (OutRun's input ranges onto the same travel of Hang-On's
// controls, read from its input tasks: steering $83B6 clamps to $44-$BC around $80, accelerator
// $848E reaches full at $A2, brake $84F2 at $A3)
void HOMode::set_inputs()
{
    if (cannonball::tick_frame)
        oinputs.adjust_inputs();
    int st = std::max(0x48, std::min((int) oinputs.input_steering, 0xB8));
    hoboard.adc[0] = (uint8_t)(0x80 - ((st - 0x80) * 0x3C) / 0x38);     // $BC (left) .. $44 (right)
    int a = std::max(0, std::min((int) oinputs.acc_adjust, 0x100));
    int b = std::max(0, std::min((int) oinputs.brake_adjust, 0x100));
    hoboard.adc[1] = a ? (uint8_t)(1 + (a * 0xA1) / 0x100) : 0;
    hoboard.adc[2] = (uint8_t)((b * 0xA3) / 0x100);
    uint8_t buttons = 0xFF;                                              // active low
    if (input.is_pressed(Input::COIN))  buttons &= ~0x01;
    if (input.is_pressed(Input::START)) buttons &= ~0x10;
    hoboard.port_service = buttons;
}

void HOMode::start(int music_index)
{
    if (!ok) return;
    hovideo.set_width(config.video.widescreen ? HOVideo::MAXW : HOVideo::W);
    other_music = -1;
    if (music_index >= 0 && music_index < (int) config.sound.music.size())
    {
        const music_t& m = config.sound.music.at(music_index);
        if (!(m.type == music_t::IS_HANGON && m.cmd == HO_RACE_MUSIC)) other_music = music_index;
    }
    hoboard.reset();
    feeding = false;
    packet.clear();
    hoboard.adc[0] = 0x80; hoboard.adc[1] = 0; hoboard.adc[2] = 0;
    hoboard.port_service = 0xFF;
    // the run-up: a coin, then START (frame numbers from power-on)
    load_scores();
    for (int f = 1; f <= 707; f++)
    {
        if (f == 60 && !scores.empty())                                  // boot code done (rankings reset at frame 1)
        {
            uint32_t o = 0;
            for (const auto& a : SCORE_AREAS) { std::copy(&scores[o], &scores[o] + a.len, hoboard.workram + a.off); o += a.len; }
        }
        if (f == 601) hoboard.port_service = 0xFE;
        if (f == 607) hoboard.port_service = 0xFF;
        if (f == 701) hoboard.port_service = 0xEF;
        if (f == 707) hoboard.port_service = 0xFF;
        hoboard.run_frame();
        hovideo.render(hoboard);            // the sprite chip's scratch words, as on the board
        packet.clear();
    }
    hovehicle.begin();                      // when the Ferrari is the vehicle
    xaudio.raw_begin(CrossoverAudio::HANGON);
    feeding = true;
    acc = 0;
    was_ingame = ingame();
    finished = false;
    active = true;
    show_frame();
}

void HOMode::stop()
{
    if (!active) return;
    active = false;
    hovehicle.end();
    save_scores();
    xaudio.raw_end(CrossoverAudio::HANGON);
    xaudio.stop_music();
    osoundint.queue_sound(sound::FM_RESET);
    video.set_external(nullptr);
}

void HOMode::show_frame()
{
    static uint32_t rgb[EXT_COLOURS];
    hovideo.palette_rgb(hoboard, rgb);
    for (uint32_t i = 0x2000; i < EXT_COLOURS; i++) rgb[i] = 0;
    if (hovehicle.enabled) hovehicle.colours(rgb);
    video.set_external(hovideo.pixels, hovideo.width, HOVideo::H, rgb);
}

void HOMode::tick()
{
    if (!active) return;
    set_inputs();
    acc += 60;
    int frames = acc / config.fps;
    acc -= frames * config.fps;
    for (int i = 0; i < frames; i++)
    {
        packet.clear();
        hoboard.run_frame();
        hovehicle.after_frame();            // OutRun's sprites, drawn into the frame by hovideo
        hovideo.render(hoboard);
        if (other_music >= 0 && !packet.empty() && packet[0] == HO_RACE_MUSIC)
        {
            packet[0] = 0x80;
            omusic.play_music(other_music);
        }
        if (feeding) xaudio.raw_frame(CrossoverAudio::HANGON, packet);
        bool g = ingame();
        if (was_ingame && !g) finished = true;
        was_ingame = g;
    }
    show_frame();
}
