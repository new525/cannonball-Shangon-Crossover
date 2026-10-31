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
    - Cool Riders' board (crmusic.hpp): its cover of Hang-On's theme, when selected.
***************************************************************************/
#pragma once

#include <cstdint>
#include <deque>
#include <array>
#include <vector>
#include "crossover/soundboard.hpp"
#include "crossover/crmusic.hpp"

class CrossoverAudio
{
public:
    enum Board { SHANGON = 0, HANGON = 1, BOARDS = 2 };

    bool init(int rate);                       // after the ROM sets are loaded
    bool ready(Board b) const { return ch[b].board.ready; }

    void command(Board b, uint8_t c) { if (ch[b].cmds.size() < 32) ch[b].cmds.push_back(c); }
    // One frame of continuous data (slots 1-7) for a board; boards without a feeder send zeros
    void frame_slots(Board b, const uint8_t slots17[7]);

    void play_music(Board b, uint8_t cmd);     // stops music on the other boards first
    // Cool Riders' music (its sound test's song numbers); stops the other boards' music first
    void play_coolriders(uint8_t song);
    bool coolriders_ready() const { return cr.ready; }
    void stop_music();                         // all boards (OutRun's FM_RESET)
    void reset_packets();

    bool sh_vehicle = false;                   // the bike feeds the Super Hang-On board

    // The emulated Super Hang-On board (shmode) feeds its sound board directly: each 60 Hz
    // frame, the bytes its main CPU wrote to the sound latch
    // (and the emulated Hang-On board, homode, its own)
    void raw_begin(Board b);
    void raw_frame(Board b, const std::vector<uint8_t>& bytes);
    void raw_end(Board b);
    void sh_raw_begin() { raw_begin(SHANGON); }
    void sh_raw_frame(const std::vector<uint8_t>& bytes) { raw_frame(SHANGON, bytes); }
    void sh_raw_end() { raw_end(SHANGON); }

    // Called once per audio tick (a frame at fps): adds the boards' output to mix (interleaved stereo)
    void mix(int32_t* mix, int samples, int fps);

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
        bool raw_mode = false;                 // fed by an emulated board's main CPU
        std::deque<std::vector<uint8_t>> raw;
    };
    Channel ch[BOARDS];
    CoolRidersMusic cr;
    void stop(Board b);
};

extern CrossoverAudio xaudio;
