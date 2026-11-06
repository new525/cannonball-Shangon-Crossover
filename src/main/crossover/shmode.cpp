/***************************************************************************
    Super Hang-On's courses on the emulated board. See shmode.hpp.
***************************************************************************/
#include "crossover/shmode.hpp"
#include "crossover/shboard.hpp"
#include "crossover/shvideo.hpp"
#include "crossover/romset.hpp"
#include "crossover/xaudio.hpp"
#include "crossover/shferrari.hpp"
#include "crossover/balance_push.hpp"
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
#include "ffb/ffb.hpp"

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
    // 8..$93 is the throttle task's travel ($6014: 8 and below = closed); released, the pedal reads 0,
    // which the name entry needs: it steps the letter right while (accelerator & $FC) != 0 ($8D24)
    shboard.adc[1] = a ? (uint8_t)(8 + (a * 0x8B) / 0x100) : 0;
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

// The track chosen in the race setup, whichever board or player plays it (as stop() does)
void SHMode::stop_other_music()
{
    xaudio.stop_music();
    osoundint.queue_sound(sound::FM_RESET);
    cannonball::audio.clear_wav();
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
        balance::push_frame_sh();           // measurement (CB_PUSH_LOG)
        // The game stops its music (sound command $00, FM_RESET in the OutRun family's sound
        // programs: at the end of a game, before its ranking music $83): the track playing in place of
        // its race music stops too
        if (other_music >= 0 && !packet.empty() && packet[0] == 0x00) stop_other_music();
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
    if (config.controls.haptic && cannonball::tick_frame && !shferrari.enabled) ffb_tick();
    show_frame();
}

// Force feedback (ffb/ffb): Super Hang-On's own bike from its RAM ($260000 + offset, the second CPU's RAM:
// the addresses in crossover/bike.cpp), once per 30 Hz tick: mode $260000 (1 start line, 2 race), speed
// $260078 (16.16 km/h), X $260022 (+ left), the traffic knockback $260088 (+ pushed left), the crash flag
// $260094, off-road $260098, the throttle $26006C (0-127). Super Hang-On's cabinet has no motor (no
// arcade base). Not read: the bike's lean and rear-wheel skid.
void SHMode::ffb_tick()
{
    if (!ffb::active()) return;
    const uint8_t* r = shboard.cpu1ram;
    auto w = [&](uint32_t off) { return (int16_t)(r[off] << 8 | r[off + 1]); };
    const bool ingame = shboard.workram[0x408] || shboard.workram[0x409];
    const int mode = w(0x00);
    ffb::State s;
    s.speed = w(0x78);
    if (ingame && mode == 2)
    {
        s.phase  = ffb::RACE;
        s.wheels = (r[0x98] || r[0x99]) ? ffb::OFF : ffb::ON;
        s.side   = w(0x22) >= 0 ? 1 : -1;
        s.skid   = w(0x88) > 0 ? 1 : (w(0x88) < 0 ? -1 : 0);
        if (r[0x94] || r[0x95])
        {
            if (ffb_crash_ticks == 0) ffb_crash_dir = s.side;
            ffb_crash_ticks++;
            s.crash = ffb::CRASH_TUMBLE; s.crash_state = 1; s.crash_counter = ffb_crash_ticks; s.crash_dir = ffb_crash_dir;
        }
        else
            ffb_crash_ticks = 0;
    }
    else if (ingame && mode == 1)
    {
        s.phase = ffb::START;
        s.start_pedal = std::min(255, 2 * (int) w(0x6C));
        ffb_crash_ticks = 0;
    }
    else
        ffb_crash_ticks = 0;
    ffb::tick(s);
}
