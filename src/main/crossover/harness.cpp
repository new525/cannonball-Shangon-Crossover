/***************************************************************************
    Crossover test harness. See harness.hpp.

    Script format, one command per line, '#' starts a comment:
      <frame> wheel <0-255>          hold the steering value (0x80 = centre)
      <frame> accel <0-255>          hold the accelerator
      <frame> brake <0-255>          hold the brake
      <frame> release                stop overriding analog inputs
      <frame> press <key> [frames]   hold a digital input (coin, start, gear1, gear2, left,
                                     right, up, down, accel, brake, viewpoint) for N frames (default 2)
      <frame> snap [name]            write a PPM screenshot after this frame is rendered
      <frame> exit                   quit
    <frame> counts engine ticks from 1. Commands run before that tick.
***************************************************************************/
#include "crossover/harness.hpp"

#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>

#include "main.hpp"
#include "sdl2/input.hpp"
#include "video.hpp"

Harness harness;

bool Harness::parse_arg(int& i, int argc, char* argv[])
{
    std::string a = argv[i];
    if (a == "-headless")                 { headless = true; return true; }
    if (a == "-script"  && i + 1 < argc)  { script_file = argv[++i]; active = true; return true; }
    if (a == "-snapdir" && i + 1 < argc)  { snapdir = argv[++i]; return true; }
    if (a == "-wav"     && i + 1 < argc)  { wav_file = argv[++i]; return true; }
    return false;
}

bool Harness::load()
{
    if (!script_file.empty())
    {
        std::ifstream f(script_file);
        if (!f) { std::cerr << "harness: cannot open " << script_file << std::endl; return false; }
        std::string line;
        int seq = 0;
        std::vector<std::pair<int, Cmd>> tmp;
        while (std::getline(f, line))
        {
            size_t h = line.find('#');
            if (h != std::string::npos) line.resize(h);
            std::istringstream ss(line);
            Cmd c;
            if (!(ss >> c.frame >> c.op)) continue;
            std::string w;
            while (ss >> w) c.args.push_back(w);
            tmp.push_back({seq++, c});
        }
        // stable by frame, file order within a frame
        std::stable_sort(tmp.begin(), tmp.end(), [](const std::pair<int, Cmd>& x, const std::pair<int, Cmd>& y)
                         { return x.second.frame < y.second.frame; });
        for (auto& p : tmp) cmds.push_back(p.second);
    }
    if (!wav_file.empty())
    {
        wav = fopen(wav_file.c_str(), "wb");
        if (!wav) { std::cerr << "harness: cannot write " << wav_file << std::endl; return false; }
        uint8_t hdr[44] = {0};
        fwrite(hdr, 1, 44, wav);        // patched in close()
    }
    return true;
}

static int key_index(const std::string& k)
{
    static const char* names[] = { "left", "right", "up", "down", "accel", "brake", "gear1", "gear2",
                                   "start", "coin", "viewpoint" };
    for (int i = 0; i < 11; i++) if (k == names[i]) return i;
    return -1;
}

struct HeldKey { int key; int until; };
static std::vector<HeldKey> held_keys;

void Harness::apply(const Cmd& c)
{
    auto num = [&](size_t n) { return n < c.args.size() ? (int)strtol(c.args[n].c_str(), nullptr, 0) : 0; };
    if      (c.op == "wheel")   wheel = num(0);
    else if (c.op == "accel")   accel = num(0);
    else if (c.op == "brake")   brake = num(0);
    else if (c.op == "release") wheel = accel = brake = -1;
    else if (c.op == "press")
    {
        int k = c.args.empty() ? -1 : key_index(c.args[0]);
        if (k < 0) { std::cerr << "harness: bad key in frame " << c.frame << std::endl; return; }
        int len = c.args.size() > 1 ? num(1) : 2;
        held_keys.push_back({k, frame + len});
    }
    else if (c.op == "snap")
    {
        snap_pending = true;
        snap_name = c.args.empty() ? "" : c.args[0];
    }
    else if (c.op == "exit")    cannonball::state = cannonball::STATE_QUIT;
    else std::cerr << "harness: unknown command '" << c.op << "' at frame " << c.frame << std::endl;
}

void Harness::before_tick()
{
    frame++;
    while (next < cmds.size() && cmds[next].frame <= frame)
        apply(cmds[next++]);

    // digital keys
    for (int k = 0; k < 11; k++) input.keys[k] = false;
    for (auto it = held_keys.begin(); it != held_keys.end();)
    {
        if (frame >= it->until) { it = held_keys.erase(it); continue; }
        input.keys[it->key] = true;
        ++it;
    }
    // analog overrides
    if (wheel >= 0 || accel >= 0 || brake >= 0)
    {
        input.analog  = 1;
        input.gamepad = true;
        if (wheel >= 0) input.a_wheel = wheel;
        if (accel >= 0) input.a_accel = accel;
        if (brake >= 0) input.a_brake = brake;
    }
}

void Harness::after_render(const uint16_t* pixels, int w, int h)
{
    if (!snap_pending) return;
    snap_pending = false;
    char name[64];
    if (snap_name.empty()) snprintf(name, sizeof name, "f%06d.ppm", frame);
    else snprintf(name, sizeof name, "%s.ppm", snap_name.c_str());
    write_ppm(snapdir + "/" + name, pixels, w, h);
}

void Harness::write_ppm(const std::string& path, const uint16_t* pixels, int w, int h)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { std::cerr << "harness: cannot write " << path << std::endl; return; }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++)
    {
        uint32_t c = video.rgb_of(pixels[i]);
        uint8_t px[3] = { (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

void Harness::audio_out(const int16_t* samples, int count, int rate)
{
    if (!wav) return;
    wav_rate = rate;
    fwrite(samples, sizeof(int16_t), count, wav);
    wav_samples += count;
}

static void put32(uint8_t* p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void put16(uint8_t* p, uint16_t v) { p[0] = v; p[1] = v >> 8; }

void Harness::close()
{
    if (!wav) return;
    uint8_t h[44];
    memcpy(h, "RIFF", 4); put32(h + 4, 36 + wav_samples * 2); memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16); put16(h + 20, 1); put16(h + 22, 2); put32(h + 24, wav_rate);   // 16-bit stereo
    put32(h + 28, wav_rate * 4); put16(h + 32, 4); put16(h + 34, 16);
    memcpy(h + 36, "data", 4); put32(h + 40, wav_samples * 2);
    fseek(wav, 0, SEEK_SET); fwrite(h, 1, 44, wav); fclose(wav); wav = nullptr;
}
