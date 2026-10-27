/***************************************************************************
    Crossover audio: the other games' sound boards, mixed with OutRun's.

    Super Hang-On's board (Z80 program + YM2151 + PCM, see soundboard.hpp) runs its own sound
    program. Each 60 Hz frame it receives the 8-byte packet Super Hang-On's main CPU sends
    (sub_018E0): slot 0 one queued command ($80 = none), slots 1-7 continuous data. When the bike
    is the vehicle, the bike supplies slots 1-3 (sho_sound.c) and its effect commands; slots 4-7
    (rival bikes' engines) are 0, since there are no rival bikes in OutRun's world.
***************************************************************************/
#pragma once

#include <cstdint>
#include <deque>
#include <array>
#include "crossover/soundboard.hpp"

class CrossoverAudio
{
public:
    SegaSoundBoard shangon;

    bool init(int rate, int fps);      // after sh_roms are loaded
    void reset_packets() { sh_packets.clear(); sh_cmds.clear(); }

    // Super Hang-On side: queue a command (one goes out per packet, as sub_019A4 does),
    // and add the packet of one Super Hang-On frame.
    void sh_command(uint8_t c) { if (sh_cmds.size() < 32) sh_cmds.push_back(c); }
    void sh_frame(const uint8_t slots17[7]);

    // Called once per audio tick: adds the boards' output to mix (interleaved stereo)
    void mix(int32_t* mix, int samples);

    bool sh_active = false;            // the Super Hang-On board is in use

private:
    int fps = 60;
    std::deque<std::array<uint8_t, 8>> sh_packets;
    std::deque<uint8_t> sh_cmds;
};

extern CrossoverAudio xaudio;
