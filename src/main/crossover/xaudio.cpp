/***************************************************************************
    Crossover audio. See xaudio.hpp.
***************************************************************************/
#include "crossover/xaudio.hpp"
#include "crossover/romset.hpp"
#include <cstdio>
#include <cstdlib>

CrossoverAudio xaudio;

bool CrossoverAudio::init(int rate, int fps_)
{
    fps = fps_;
    if (!sh_roms.loaded()) return false;
    const RomRegion& z = sh_roms["soundcpu"];
    const RomRegion& p = sh_roms["pcm"];
    return shangon.init(z.data, p.data, rate, 60);
}

void CrossoverAudio::sh_frame(const uint8_t slots17[7])
{
    std::array<uint8_t, 8> pkt;
    pkt[0] = 0x80;
    if (!sh_cmds.empty()) { pkt[0] = sh_cmds.front(); sh_cmds.pop_front(); }
    for (int i = 0; i < 7; i++) pkt[1 + i] = slots17[i];
    if (sh_packets.size() < 8) sh_packets.push_back(pkt);
}

void CrossoverAudio::mix(int32_t* out, int samples)
{
    if (!sh_active || !shangon.ready) return;
    // the board runs in 60 Hz frames; at 30 fps two of them make one audio tick
    int sub = fps >= 60 ? 1 : 60 / fps;
    int per = samples / sub;
    for (int s = 0; s < sub; s++)
    {
        if (!sh_packets.empty())
        {
            for (uint8_t b : sh_packets.front()) shangon.command(b);
            sh_packets.pop_front();
        }
        shangon.frame(out + 2 * per * s, per);
    }
    // Debug: the board's own output (env CB_XAUDIO_RAW=<file>: raw stereo int32)
    static FILE* raw = nullptr;
    static const char* rawname = getenv("CB_XAUDIO_RAW");
    if (rawname && !raw) raw = fopen(rawname, "wb");
    if (raw) fwrite(out, 4, (size_t) samples * 2, raw);
}
