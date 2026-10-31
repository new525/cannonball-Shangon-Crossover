/***************************************************************************
    OutRunners' music on its own sound board (orsound), for the music selection.

    The board runs OutRunners' sound program (orsound.hpp). The main V60 is not emulated: its
    part is played by writing what its sound test writes to the shared RAM. To play song n on
    monitor A (player 1): $1F (stop) to $700700 and to $700800 (monitor B), then n to $700700,
    one frame apart each. To stop: $1F to both; the song fades out in about half a second, and
    the board is then paused until the next play. The program needs nothing else from the main
    CPU: commands are taken from 0.05 s after the Z80 leaves reset (measured with tools/ortest).

    Monitor A's sound is MAME's left speaker; it is played on both sides here.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include <memory>
#include <deque>
#include "crossover/orsound.hpp"
#include "crossover/resample.hpp"

class OutRunnersMusic
{
public:
    bool init(int out_rate);                // after or_roms is loaded
    bool ready = false;

    void play(uint8_t song);                // the sound test's numbers (Magical Sound Shower = 1)
    void stop();

    // One frame (1/fps s): adds the board's output to mix (interleaved stereo, 16-bit scale)
    void mix(int32_t* mix, int samples, int fps);

private:
    enum State { OFF, RUNNING, STOPPING, PAUSED };
    State state = OFF;
    std::unique_ptr<ORSound> board;
    int out_rate = 44100;
    int64_t next_time = 0;                  // the board's time at the end of the next frame (cycles)
    double frame_frac = 0;
    int64_t stop_until = 0;
    struct Cmd { int64_t t; uint32_t off; uint8_t data; };
    std::deque<Cmd> cmds;                   // commands waiting for their time (one frame apart)
    FrameResampler ym, pcm;

    void queue(uint32_t off, uint8_t data);
};
