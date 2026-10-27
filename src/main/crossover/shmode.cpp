/***************************************************************************
    Super Hang-On's courses on the emulated board. See shmode.hpp.
***************************************************************************/
#include "crossover/shmode.hpp"
#include "crossover/shboard.hpp"
#include "crossover/shvideo.hpp"
#include "crossover/romset.hpp"
#include "crossover/xaudio.hpp"
#include "crossover/shferrari.hpp"
#include "main.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <iterator>
#include "video.hpp"
#include "frontend/config.hpp"
#include "sdl2/input.hpp"
#include "engine/oinputs.hpp"
#include "engine/omusic.hpp"
#include "engine/audio/osoundint.hpp"

SHMode shmode;

bool SHMode::init()
{
    ok = sh_roms.loaded() && shboard.init(sh_roms) && shvideo.init(sh_roms);
    shboard.sound_command = [this](uint8_t b) { packet.push_back(b); };
    shboard.sound_reset = nullptr;          // only used at power-on, which happens unheard
    return ok;
}

// Player controls -> the board's ports (same conversions as the bike on OutRun's courses:
// OutRun's input ranges mapped onto the same physical travel of Super Hang-On's controls)
void SHMode::set_inputs()
{
    if (cannonball::tick_frame)
        oinputs.adjust_inputs();            // pedal scaling, at OutRun's 30 Hz like its game loop
    int st = oinputs.input_steering;
    st = std::max(0x48, std::min(st, 0xB8));
    shboard.adc[0] = (uint8_t)(0x80 - ((st - 0x80) * 0x48) / 0x38);    // 0xC8 (left) .. 0x38 (right)
    int a = std::max(0, std::min((int) oinputs.acc_adjust, 0x100));
    int b = std::max(0, std::min((int) oinputs.brake_adjust, 0x100));
    shboard.adc[1] = (uint8_t)(8 + (a * 0x8B) / 0x100);
    shboard.adc[2] = (uint8_t)((b * 0x9D) / 0x100);
    uint8_t buttons = 0xFF;                                              // active low
    if (input.is_pressed(Input::COIN))  buttons &= ~0x01;
    if (input.is_pressed(Input::START)) buttons &= ~0x10;
    if (input.is_pressed(Input::GEAR1) || input.is_pressed(Input::GEAR2)) buttons &= ~0x20;
    shboard.port_buttons = buttons;
}

// Work RAM areas Super Hang-On's power-on reset gives default rankings (offsets in work RAM)
static const struct { uint32_t off, len; } SCORE_AREAS[] = { { 0x1800, 0x1C0 }, { 0x003C, 4 }, { 0x1BE0, 0x20 } };
static const uint32_t SCORE_BYTES = 0x1C0 + 4 + 0x20;

static std::string scores_file() { return config.data.save_path + "hiscores_superhangon.bin"; }

void SHMode::load_scores()
{
    scores.clear();
    std::ifstream f(scores_file(), std::ios::binary);
    if (!f) return;
    std::vector<uint8_t> v((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (v.size() == SCORE_BYTES) scores = v;
}

void SHMode::save_scores()
{
    std::vector<uint8_t> v;
    for (const auto& a : SCORE_AREAS) v.insert(v.end(), shboard.workram + a.off, shboard.workram + a.off + a.len);
    std::ofstream f(scores_file(), std::ios::binary);
    if (f) f.write((const char*) v.data(), (std::streamsize) v.size());
}

// Super Hang-On's race music commands, in the order of its music screen
static const uint8_t SH_MUSIC[4] = { 0x9D, 0x82, 0x96, 0x81 };

void SHMode::start(int course, int music_index)
{
    if (!ok) return;
    // Widescreen as Cannonball does it for OutRun (config video.widescreen, set by GAME MODES > ENHANCED)
    shvideo.set_width(config.video.widescreen ? SHVideo::MAXW : SHVideo::W);
    shboard.set_widescreen(shvideo.xoff);
    int music = -1;                                                      // on the game's screen
    other_music = -1;
    if (music_index >= 0 && music_index < (int) config.sound.music.size())
    {
        const music_t& m = config.sound.music.at(music_index);
        music = 0;
        if (m.type == music_t::IS_SHANGON)
        {
            for (int i = 0; i < 4; i++) if (m.cmd == SH_MUSIC[i]) music = i;
        }
        else other_music = music_index;
    }
    shboard.reset();
    feeding = false;
    packet.clear();
    course = std::max(0, std::min(course, 3));

    // The run-up, as a player would do it (frame numbers from power-on)
    struct Step { int frame; int what; int value; };
    std::vector<Step> s;
    s.push_back({ 601, 0, 0xFE });                                       // coin
    s.push_back({ 607, 0, 0xFF });
    for (int i = 0; i < course; i++) { s.push_back({ 620 + 20 * i, 2, 0xFF }); s.push_back({ 626 + 20 * i, 2, 0 }); }
    s.push_back({ 700, 0, 0xEF });                                       // START: class chosen
    s.push_back({ 706, 0, 0xFF });
    int last = 706;
    if (music >= 0)
    {
        for (int i = 0; i < std::min(music, 3); i++) { s.push_back({ 720 + 20 * i, 2, 0xFF }); s.push_back({ 726 + 20 * i, 2, 0 }); }
        s.push_back({ 800, 0, 0xEF });                                   // START: music chosen
        s.push_back({ 806, 0, 0xFF });
        last = 806;
    }
    shboard.adc[0] = 0x80; shboard.adc[1] = 0; shboard.adc[2] = 0; shboard.port_buttons = 0xFF;
    load_scores();
    size_t k = 0;
    for (int f = 1; f <= last; f++)
    {
        if (f == 60 && !scores.empty())                                  // boot code done (rankings reset at frame 1)
        {
            uint32_t o = 0;
            for (const auto& a : SCORE_AREAS) { std::copy(&scores[o], &scores[o] + a.len, shboard.workram + a.off); o += a.len; }
        }
        while (k < s.size() && s[k].frame == f)
        {
            if (s[k].what == 0) shboard.port_buttons = (uint8_t) s[k].value;
            else                shboard.adc[s[k].what] = (uint8_t) s[k].value;
            k++;
        }
        shboard.run_frame();
        packet.clear();
    }

    shferrari.begin();                      // when the Ferrari is the vehicle
    xaudio.sh_raw_begin();
    feeding = true;
    acc = 0;
    was_ingame = false;
    finished = false;
    active = true;
    show_frame();
}

void SHMode::stop()
{
    if (!active) return;
    active = false;
    shferrari.end();
    save_scores();
    xaudio.sh_raw_end();
    xaudio.stop_music();
    osoundint.queue_sound(sound::FM_RESET);
    video.set_external(nullptr);
}

void SHMode::show_frame()
{
    shferrari.after_frame();                // OutRun's sprites, drawn into the frame by shvideo
    shvideo.render(shboard);
    static uint32_t rgb[EXT_COLOURS];
    shvideo.palette_rgb(shboard, rgb);
    if (shferrari.enabled) shferrari.colours(rgb);
    video.set_external(shvideo.pixels, shvideo.width, SHVideo::H, rgb);
}

void SHMode::tick()
{
    if (!active) return;
    set_inputs();
    acc += 60;
    int frames = acc / config.fps;
    acc -= frames * config.fps;
    for (int i = 0; i < frames; i++)
    {
        packet.clear();
        shboard.run_frame();
        if (other_music >= 0 && !packet.empty())
            for (uint8_t m : SH_MUSIC)
                if (packet[0] == m)
                {
                    if (getenv("CB_SHMODE_DEBUG")) fprintf(stderr, "shmode: race music %02x -> list entry %d\n", m, other_music);
                    packet[0] = 0x80; omusic.play_music(other_music); break;
                }
        if (feeding) xaudio.sh_raw_frame(packet);
        bool ingame = shboard.workram[0x408] || shboard.workram[0x409];
        if (was_ingame && !ingame) finished = true;
        was_ingame = ingame;
    }
    show_frame();
}
