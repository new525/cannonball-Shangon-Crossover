/***************************************************************************
    Crossover test harness.

    Headless, deterministic runs for verification:
      -headless          run as fast as possible, no audio device, no frame pacing
      -script <file>     per-frame scripted inputs and actions (see harness.cpp)
      -snapdir <dir>     where "snap" writes PPM screenshots (default ".")
      -wav <file>        write the mixed audio output to a WAV file

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
    See docs/license.txt (Cannonball licence) for terms.
***************************************************************************/
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

class Harness
{
public:
    bool headless = false;
    bool active   = false;          // a script was given
    int  frame    = 0;              // engine ticks completed

    bool parse_arg(int& i, int argc, char* argv[]);
    bool load();                    // after config is loaded; returns false on error
    void before_tick();             // apply this frame's script lines (inputs, pokes)
    void after_render(const uint16_t* pixels, int w, int h);
    void audio_out(const int16_t* samples, int count, int rate);
    void close();

private:
    struct Cmd { int frame; std::string op; std::vector<std::string> args; };
    std::vector<Cmd> cmds;
    size_t next = 0;
    std::string script_file, snapdir = ".", wav_file;
    bool snap_pending = false;
    std::string snap_name;
    FILE* wav = nullptr;
    FILE* trace = nullptr;      // per-frame engine variables ("trace" command)
    uint32_t wav_samples = 0;
    int wav_rate = 0;

    // held analog values (-1 = not overridden)
    int wheel = -1, accel = -1, brake = -1;

    void apply(const Cmd& c);
    void write_ppm(const std::string& path, const uint16_t* pixels, int w, int h);
};

extern Harness harness;
