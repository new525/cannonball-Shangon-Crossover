/***************************************************************************
    Crossover audio. See xaudio.hpp.
***************************************************************************/
#include "crossover/xaudio.hpp"
#include "crossover/romset.hpp"
#include <cstdio>
#include <cstdlib>

CrossoverAudio xaudio;

// After a Z80 reset the sound program sets up its NMI handler chain (HL') in its first
// instructions; a command byte arriving before that would leave the 8-byte packet framing
// misaligned. The games' main CPUs wait before sending (Super Hang-On's second reset at frame
// 11 is followed by its first command at frame 13), so: no packets for this many frames.
static const int BOOT_FRAMES = 10;

bool CrossoverAudio::init(int rate, int fps_)
{
    fps = fps_;
    bool any = false;
    if (sh_roms.loaded())
        any |= ch[SHANGON].board.init(SegaSoundBoard::Kind::OUTRUN, sh_roms["soundcpu"].data, sh_roms["pcm"].data, rate, 60);
    if (ho_roms.loaded())
        any |= ch[HANGON].board.init(SegaSoundBoard::Kind::HANGON, ho_roms["soundcpu"].data, ho_roms["pcm"].data, rate, 60);
    if (cr_roms.loaded())
        any |= cr.init(rate, fps);
    return any;
}

void CrossoverAudio::reset_packets()
{
    for (Channel& c : ch) { c.slots.clear(); c.cmds.clear(); }
}

void CrossoverAudio::frame_slots(Board b, const uint8_t s[7])
{
    std::array<uint8_t, 7> a;
    for (int i = 0; i < 7; i++) a[i] = s[i];
    if (ch[b].slots.size() < 8) ch[b].slots.push_back(a);
}

void CrossoverAudio::stop(Board b)
{
    Channel& c = ch[b];
    if (!c.board.ready) return;
    c.board.cpu_reset();          // as the game's main CPU does through the sound reset line
    c.quiet = BOOT_FRAMES;
    c.cmds.clear();
    c.music = false;
}

void CrossoverAudio::stop_music()
{
    if (getenv("CB_XAUDIO_DEBUG")) fprintf(stderr, "xaudio: stop music\n");
    for (int b = 0; b < BOARDS; b++)
        if (ch[b].music) stop((Board) b);
    cr.stop();
}

void CrossoverAudio::play_coolriders(uint8_t song)
{
    if (getenv("CB_XAUDIO_DEBUG")) fprintf(stderr, "xaudio: Cool Riders song %02x (ready %d)\n", song, cr.ready);
    for (int b = 0; b < BOARDS; b++)
        if (ch[b].music) stop((Board) b);
    cr.play(song);
}

void CrossoverAudio::play_music(Board b, uint8_t cmd)
{
    if (getenv("CB_XAUDIO_DEBUG")) fprintf(stderr, "xaudio: play %02x on board %d (ready %d)\n", cmd, b, ch[b].board.ready);
    stop_music();
    if (!ch[b].board.ready) return;
    ch[b].music = true;
    ch[b].cmds.push_back(cmd);
}

void CrossoverAudio::raw_begin(Board b)
{
    stop_music();
    Channel& c = ch[b];
    c.raw.clear();
    c.cmds.clear(); c.slots.clear();
    c.music = false;
    c.raw_mode = true;
    c.was_active = false;                       // mix() starts it from a fresh Z80 reset
}

void CrossoverAudio::raw_frame(Board b, const std::vector<uint8_t>& bytes)
{
    Channel& c = ch[b];
    if (c.raw_mode && c.raw.size() < 8) c.raw.push_back(bytes);
}

void CrossoverAudio::raw_end(Board b)
{
    Channel& c = ch[b];
    if (!c.raw_mode) return;
    c.raw_mode = false;
    c.raw.clear();
    stop(b);
}

void CrossoverAudio::mix(int32_t* out, int samples)
{
    ch[SHANGON].active = ch[SHANGON].raw_mode || sh_vehicle || ch[SHANGON].music;
    ch[HANGON].active  = ch[HANGON].raw_mode || ch[HANGON].music;
    // the boards run in 60 Hz frames; at 30 fps two of them make one audio tick
    int sub = fps >= 60 ? 1 : 60 / fps;
    int per = samples / sub;
    for (int b = 0; b < BOARDS; b++)
    {
        Channel& c = ch[b];
        if (!c.board.ready || !c.active) { c.slots.clear(); c.was_active = false; continue; }
        if (!c.was_active)                      // (re)starting: from a fresh Z80 reset
        {
            c.board.cpu_reset();
            c.quiet = BOOT_FRAMES;
            c.was_active = true;
        }
        for (int s = 0; s < sub; s++)
        {
            if (c.quiet > 0)
            {
                c.quiet--;
                if (!c.slots.empty()) c.slots.pop_front();
                if (c.raw_mode && !c.raw.empty()) c.raw.pop_front();
                c.board.frame(out + 2 * per * s, per);
                continue;
            }
            if (c.raw_mode)
            {
                if (!c.raw.empty()) { for (uint8_t v : c.raw.front()) c.board.command(v); c.raw.pop_front(); }
                c.board.frame(out + 2 * per * s, per);
                continue;
            }
            std::array<uint8_t, 7> slots = {};
            if (!c.slots.empty()) { slots = c.slots.front(); c.slots.pop_front(); }
            uint8_t cmd = 0x80;
            if (!c.cmds.empty()) { cmd = c.cmds.front(); c.cmds.pop_front(); }
            c.board.command(cmd);
            for (uint8_t v : slots) c.board.command(v);
            c.board.frame(out + 2 * per * s, per);
        }
    }
    cr.mix(out, samples);

    // Debug: the boards' own output (env CB_XAUDIO_RAW=<file>: raw stereo int32)
    static FILE* raw = nullptr;
    static const char* rawname = getenv("CB_XAUDIO_RAW");
    if (rawname && !raw) raw = fopen(rawname, "wb");
    if (raw) fwrite(out, 4, (size_t) samples * 2, raw);
}
