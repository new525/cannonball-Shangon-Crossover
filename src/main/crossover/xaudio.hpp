/***************************************************************************
    Crossover audio: the other games' sound boards, mixed with OutRun's.

    Each board runs its game's own Z80 sound program (see soundboard.hpp) and receives, every
    60 Hz frame, the 8-byte packet that game's main CPU sends: slot 0 one queued command ($80 =
    none), slots 1-7 continuous data.

    - Super Hang-On's board: when the bike is the vehicle, the bike supplies slots 1-3
      (sho_sound.c) and its effect commands; slots 4-7 (rival bikes' engines) are 0, since there
      are no rival bikes in OutRun's world. It also plays Super Hang-On's music when selected.
    - Hang-On's board: plays Hang-On's race music when selected (slots 1-7 are 0).
    A board's music is stopped the way its game stops everything: by resetting its Z80.
***************************************************************************/
#pragma once

#include <cstdint>
#include <deque>
#include <array>
#include <vector>
#include "crossover/soundboard.hpp"

class CrossoverAudio
{
public:
    enum Board { SHANGON = 0, HANGON = 1, BOARDS = 2 };

    bool init(int rate, int fps);              // after the ROM sets are loaded
    bool ready(Board b) const { return ch[b].board.ready; }

    void command(Board b, uint8_t c) { if (ch[b].cmds.size() < 32) ch[b].cmds.push_back(c); }
    // One frame of continuous data (slots 1-7) for a board; boards without a feeder send zeros
    void frame_slots(Board b, const uint8_t slots17[7]);

    void play_music(Board b, uint8_t cmd);     // stops music on the other boards first
    void stop_music();                         // all boards (OutRun's FM_RESET)
    void reset_packets();

    bool sh_vehicle = false;                   // the bike feeds the Super Hang-On board

    // The emulated Super Hang-On board (shmode) feeds its sound board directly: each 60 Hz
    // frame, the bytes its main CPU wrote to the sound latch
    void sh_raw_begin();
    void sh_raw_frame(const std::vector<uint8_t>& bytes);
    void sh_raw_end();

    // Called once per audio tick: adds the boards' output to mix (interleaved stereo)
    void mix(int32_t* mix, int samples);

private:
    struct Channel
    {
        SegaSoundBoard board;
        std::deque<std::array<uint8_t, 7>> slots;
        std::deque<uint8_t> cmds;
        bool active = false;                   // running (music playing or feeding a vehicle)
        bool music = false;
        bool was_active = false;
        int  quiet = 0;                        // frames without packets after a Z80 reset
    };
    Channel ch[BOARDS];
    bool sh_raw = false;
    std::deque<std::vector<uint8_t>> raw;
    int fps = 60;
    void stop(Board b);
};

extern CrossoverAudio xaudio;
