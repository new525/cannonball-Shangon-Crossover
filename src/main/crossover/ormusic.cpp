/***************************************************************************
    OutRunners' music on its own sound board. See ormusic.hpp.
***************************************************************************/
#include "crossover/ormusic.hpp"
#include "crossover/romset.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>

static const uint32_t CMD_A = 0x0700, CMD_B = 0x0800;   // shared RAM: monitor A's and B's command
static const uint8_t  STOP = 0x1f;
static const double   BOOT_TIME = 0.05;                   // seconds after reset before commands are taken
static const double   FADE_TIME = 1.0;                    // the stop's fade is silent after about 0.5 s

// Level: MAME's mix (+-1.0 full scale) to 16-bit, times 0.26: the covers of Magical Sound
// Shower, Passing Breeze and Splash Wave then match OutRun's own three tracks as the program
// plays them, on average (RMS 2,494 against 4,800 measured at x 0.5, 16-bit scale, at the radio
// screen). A design decision, as for Cool Riders' track (crmusic.cpp).
static const float GAIN_OR = 0.26f;
// MAME's left speaker: the YM3438's output 1 x 0.40, the MultiPCM's output 1 x 1.0
static const float YM_ROUTE = 0.40f, PCM_ROUTE = 1.0f;

bool OutRunnersMusic::init(int rate)
{
    ready = false;
    if (!or_roms.loaded()) return false;
    out_rate = rate;
    board.reset(new ORSound());
    if (!board->init(or_roms["soundcpu"].data, or_roms["sega"].data)) { board.reset(); return false; }
    state = OFF;
    ready = true;
    return true;
}

void OutRunnersMusic::queue(uint32_t off, uint8_t data)
{
    // now, or one frame after the last queued command if that has not been written yet
    int64_t t = cmds.empty() ? board->now() : cmds.back().t + ORSound::CLOCK / 60;
    if (t < board->now()) t = board->now();
    cmds.push_back({ t, off, data });
}

void OutRunnersMusic::start()
{
    if (!ready) return;
    if (getenv("CB_XAUDIO_DEBUG")) fprintf(stderr, "ormusic: start (state %d)\n", (int) state);
    if (state == OFF)
    {
        board->power_on();
        board->release_cpu();
        next_time = 0; frame_frac = 0;
        ym.clear(); pcm.clear(); cmds.clear();
        board->run_until((int64_t)(BOOT_TIME * ORSound::CLOCK));   // the sound program's start-up
    }
    cmds.clear();                          // (a stop's commands not sent yet)
    queue(CMD_A, STOP);                    // the race setup's ($12277)
    state = RUNNING;
}

void OutRunnersMusic::command(uint8_t c)
{
    if (!ready || state != RUNNING) return;
    queue(CMD_A, c);
}

void OutRunnersMusic::shared(uint32_t off, uint8_t v)
{
    if (!ready || state != RUNNING) return;
    queue(off & 0x1FFF, v);
}

void OutRunnersMusic::stop()
{
    if (!ready || state != RUNNING) return;
    if (getenv("CB_XAUDIO_DEBUG")) fprintf(stderr, "ormusic: stop\n");
    cmds.clear();                          // a song not started yet is dropped
    queue(CMD_A, STOP);
    queue(CMD_B, STOP);
    stop_until = board->now() + (int64_t)(FADE_TIME * ORSound::CLOCK);
    state = STOPPING;
}

void OutRunnersMusic::mix(int32_t* mix, int samples, int fps)
{
    if (!ready) return;
    // Debug (env CB_ORMUSIC_RAW=<file>): this board's output alone, raw stereo int32, every frame
    static FILE* raw = nullptr;
    static const char* rawname = getenv("CB_ORMUSIC_RAW");
    static std::vector<int32_t> own;
    if (rawname) { own.assign((size_t) samples * 2, 0); if (!raw) raw = fopen(rawname, "wb"); }
    if (state == OFF)
    {
        if (raw) fwrite(own.data(), 4, (size_t) samples * 2, raw);
        return;
    }
    if (fps <= 0) fps = 60;
    if (state == RUNNING || state == STOPPING)
    {
        if (next_time < board->now()) next_time = board->now();
        double c = (double) ORSound::CLOCK / fps + frame_frac;
        int64_t n = (int64_t) c;
        frame_frac = c - (double) n;
        next_time += n;
        while (!cmds.empty() && cmds.front().t < next_time)
        {
            board->main_write(cmds.front().t, cmds.front().off, cmds.front().data);
            cmds.pop_front();
        }
        board->run_until(next_time);
        for (size_t i = 0; i < board->ym1.size(); i++)
        {
            float v = board->ym1[i] * YM_ROUTE * GAIN_OR * 32768.0f;
            ym.push(v, v);
        }
        for (size_t i = 0; i < board->pcm1.size(); i++)
        {
            float v = board->pcm1[i] * PCM_ROUTE * GAIN_OR * 32768.0f;
            pcm.push(v, v);
        }
        board->ym0.clear(); board->ym1.clear(); board->pcm0.clear(); board->pcm1.clear();
        if (state == STOPPING && board->now() >= stop_until) state = PAUSED;
    }
    int32_t* out = rawname ? own.data() : mix;
    if (ym.available()) ym.pull((double) board->ym_rate() / out_rate, out, samples);
    if (pcm.available()) pcm.pull((double) board->pcm_rate() / out_rate, out, samples);
    if (rawname)
    {
        for (int i = 0; i < samples * 2; i++) mix[i] += own[i];
        if (raw) fwrite(own.data(), 4, (size_t) samples * 2, raw);
    }
}
