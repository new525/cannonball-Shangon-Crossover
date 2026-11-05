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
      <frame> steerto <x> [abs]      steer towards lateral position x each frame, relative to the
                                     bike's road (abs: absolute); "steerto off" ends it
      <frame> steerto <x> road       the same relative to the road the car is on, for the Ferrari too
                                     (OutRun's rule: one of two roads apart when the width is over $FF),
                                     and for Super Hang-On's and Hang-On's own bikes on their boards
                                     (their X, $260022 / $C7E050) and OutRunners' own car (its lateral
                                     $209038 x -6/11 in OutRun's units)
      <frame> autopilot on|off       the bike steers itself (Super Hang-On's attract-mode rider)
      <frame> trace <file>           write engine variables per frame to the file
      <frame> exit                   quit
    <frame> counts engine ticks from 1. Commands run before that tick.
***************************************************************************/
#include "crossover/harness.hpp"

#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>

#include "main.hpp"
#include "sdl2/input.hpp"
#include "video.hpp"
#include "engine/outrun.hpp"
#include "engine/oroad.hpp"
#include "engine/oinitengine.hpp"
#include "engine/oferrari.hpp"
#include "engine/ocrash.hpp"
#include "crossover/bike.hpp"
#include "crossover/madpower.hpp"
#include "crossover/balance_pace.hpp"
#include "crossover/shboard.hpp"
#include "crossover/shferrari.hpp"
#include "crossover/hoboard.hpp"
#include "crossover/hovehicle.hpp"
#include "crossover/orboard.hpp"
#include "crossover/ormode.hpp"
#include "crossover/orvehicle.hpp"
#include "crossover/balance.hpp"
#include "crossover/shmode.hpp"
#include "crossover/homode.hpp"
#include "engine/osprites.hpp"

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
    else if (c.op == "steerto")          // steerto <x> [abs] | steerto off
    {
        steer_on = !c.args.empty() && c.args[0] != "off";
        if (steer_on) { steer_target = num(0); steer_abs = c.args.size() > 1 && c.args[1] == "abs";
                        steer_road = c.args.size() > 1 && c.args[1] == "road"; }
    }
    else if (c.op == "autopilot")
        bike.autopilot = !c.args.empty() && c.args[0] == "on";
    else if (c.op == "trace")
    {
        if (!c.args.empty()) trace = fopen(c.args[0].c_str(), "w");
        if (trace) fprintf(trace, "frame state road_pos road_width road_ctrl split car_x car_kmh wheels bike_kmh bike_x bike_off crash_ctr crash_state coll1 ingame_eng spin1\n");
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
    balance::contact_test_rails();      // testing: CB_RAILS
    // steering autopilot: + X is left, and wheel > 0x80 steers right (moves X down)
    if (steer_on)
    {
        int x = steer_abs ? oinitengine.car_x_pos : (bike.enabled ? bike.car_x_relative() :
                madpower.enabled ? madpower.car_x_relative() : oinitengine.car_x_pos);
        if (steer_road && !bike.enabled && !madpower.enabled)
        {
            int rw = (int16_t)(oroad.road_width >> 16), cx = oinitengine.car_x_pos;
            x = rw > 0xFF ? cx - (cx < 0 ? -rw : rw) : cx;
        }
        if (steer_road && cannonball::state == cannonball::STATE_SHANGON && !shferrari.enabled)
            x = (int16_t)(shboard.cpu1ram[0x22] << 8 | shboard.cpu1ram[0x23]);
        if (steer_road && cannonball::state == cannonball::STATE_HANGON && !hovehicle.enabled)
            x = (int16_t)(hoboard.subram[0x2050] << 8 | hoboard.subram[0x2051]);
        if (steer_road && cannonball::state == cannonball::STATE_OUTRUNNERS && !orvehicle.driving() && ormode.board_for_tests())
        {
            const ORBoard* b = ormode.board_for_tests();
            x = -(int16_t)(b->read8(0x209038) | b->read8(0x209039) << 8) * 6 / 11;
        }
        int w = 0x80 + (x - steer_target) / 3;
        // testing (CB_STEER_REL): steered relative to the centre of the vehicle's road, gently and damped (the
        // Ferrari's X from its road, the bikes at home from their boards' X $260022 / $C7E050); with CB_CHASE,
        // towards the nearest of OutRun's traffic ahead (its x relative to the car, OTraffic::check_collision)
        static const bool rel = getenv("CB_STEER_REL") != nullptr, chase = getenv("CB_CHASE") != nullptr;
        static int x_prev = 0;
        if (rel && !steer_abs)
        {
            int target = steer_target;
            if (shmode.active && !shferrari.enabled)
                x = (int16_t)(shboard.cpu1ram[0x22] << 8 | shboard.cpu1ram[0x23]);
            else if (homode.active && !hovehicle.enabled)
                x = (int16_t)(hoboard.subram[0x2050] << 8 | hoboard.subram[0x2051]);
            else if (!bike.enabled && !madpower.enabled)
            {
                int rw = (int16_t)(oroad.road_width >> 16);
                if (oroad.road_ctrl == ORoad::ROAD_BOTH_P0 || oroad.road_ctrl == ORoad::ROAD_BOTH_P1)
                    x -= rw > 0xFF ? (x < 0 ? -rw : rw) : 0;
            }
            // the boards' rivals: Super Hang-On's (task $74B6, depth +$14, hit from $A0) and Hang-On's (task
            // $A8D0, line +$C, hit from $D0) by their screen x +$A ($160 = the vehicle); OutRunners' cars
            // ($20FC00 + $80 n: lateral +$44, segment counter +$46) against MAD POWER's or the vehicle's own
            auto rw16 = [](const uint8_t* p) { return (int)(int16_t)(p[0] << 8 | p[1]); };
            if (chase && (shmode.active || homode.active))
            {
                const uint8_t* ram = shmode.active ? shboard.workram : hoboard.workram;
                uint32_t task = shmode.active ? 0x74B6 : 0xA8D0, from = shmode.active ? 0x3000 : 0x800;
                int dfield = shmode.active ? 0x14 : 0x0C, dlo = shmode.active ? 0x60 : 0x90, dhi = shmode.active ? 0xB0 : 0xE0;
                int best = -1;
                for (uint32_t a = from; a < 0x4000 + (shmode.active ? 0x4000 : 0); a += 0x40)
                {
                    const uint8_t* s = ram + a;
                    uint32_t t = (uint32_t) s[2] << 24 | s[3] << 16 | s[4] << 8 | s[5];
                    int d = rw16(s + dfield);
                    if ((s[0] & 0x80) && t == task && d >= dlo && d < dhi && d > best)
                    {
                        best = d;
                        target = x + (0x160 - rw16(s + 0x0A));
                    }
                }
            }
            if (chase && ormode.active && balance::test_or_board)
            {
                const ORBoard& b = *balance::test_or_board;
                auto r16 = [&](uint32_t a) { return (int)(int16_t)(b.read8(a) | b.read8(a + 1) << 8); };
                int own_lat = r16(0x209038), own_seg = r16(0x209042);
                if (!orvehicle.enabled) x = -own_lat * 6 / 11;
                int best = 99;
                for (int n = 1; n < 8; n++)
                {
                    uint32_t c = 0x20FC00 + 0x80 * n;
                    int ds = (int16_t)(r16(c + 0x46) - own_seg);
                    if (ds >= 0 && ds <= 3 && ds < best && r16(c + 0x4C) > 0)
                    {
                        best = ds;
                        target = x - (r16(c + 0x44) - own_lat) * 6 / 11;
                    }
                }
            }
            if (chase && !shmode.active && !homode.active && !ormode.active)
            {
                int best = -1;
                for (int i = OSprites::SPRITE_TRAFF1; i < OSprites::SPRITE_TRAFF1 + 8; i++)
                {
                    const oentry& t = osprites.jump_table[i];
                    int z = t.z >> 16;
                    if ((t.control & OSprites::ENABLE) && z >= 0x100 && z < 0x1E0 && z > best)
                    {
                        best = z;
                        target = x - t.x;
                    }
                }
            }
            static const int kp = getenv("CB_STEER_P") ? atoi(getenv("CB_STEER_P")) : 8;
            static const int kd = getenv("CB_STEER_D") ? atoi(getenv("CB_STEER_D")) : 2;
            w = 0x80 + (x - target) / kp + (x - x_prev) * kd;
            x_prev = x;
        }
        wheel = w < 0x48 ? 0x48 : (w > 0xB8 ? 0xB8 : w);
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
    balance::pace_trace_frame(frame);
    balance::contact_trace_frame();
    if (trace)
        fprintf(trace, "%d %d %u %d %d %d %d %d %d %d %d %d\n", frame, outrun.game_state, oroad.road_pos >> 16,
                (int16_t)(oroad.road_width >> 16), oroad.road_ctrl, oinitengine.rd_split_state, oinitengine.car_x_pos,
                oinitengine.car_increment >> 16, oferrari.wheel_state, bike.speed_kmh(), bike.bike_x(), bike.offroad());
    if (trace)
        fprintf(trace, "    crash %d %d %d %d %d\n", ocrash.crash_counter, ocrash.crash_state, ocrash.coll_count1,
                oinitengine.ingame_engine, ocrash.spin_control1);
    if (trace && getenv("CB_TRACE_ROADY"))
    {
        fprintf(trace, "    road_y");
        for (int k = 0; k < 0x200; k += 0x20) fprintf(trace, " %d", oroad.road_y[oroad.road_p0 + k] >> 4);
        fprintf(trace, " | %d %d\n", oroad.road_y[oroad.road_p0 + 0x1F0] >> 4, oroad.road_y[oroad.road_p0 + 0x1FD] >> 4);
    }
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
    if (trace) { fclose(trace); trace = nullptr; }
    if (!wav) return;
    uint8_t h[44];
    memcpy(h, "RIFF", 4); put32(h + 4, 36 + wav_samples * 2); memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16); put16(h + 20, 1); put16(h + 22, 2); put32(h + 24, wav_rate);   // 16-bit stereo
    put32(h + 28, wav_rate * 4); put16(h + 32, 4); put16(h + 34, 16);
    memcpy(h + 36, "data", 4); put32(h + 40, wav_samples * 2);
    fseek(wav, 0, SEEK_SET); fwrite(h, 1, 44, wav); fclose(wav); wav = nullptr;
}
