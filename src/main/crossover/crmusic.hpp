/***************************************************************************
    Cool Riders' music on its own sound board (crsound), for the music selection.

    The board is the original program on emulated hardware (crsound.hpp). Cool Riders' main SH-2
    is not emulated: its part is played by replaying what it writes to the shared RAM at power-on
    (recorded in MAME 0.264, the first 50 frames) and then writing sound commands into the
    command ring as it does:
    - play: the stop sequence its sound test sends before every song (be1a2800, ae720000,
      ae700000, be507d00), then ae10<song>00 (byte 3 = 00: the left / player 1 side);
    - stop: the stop sequence alone. The board fades the song out over about a second; it is
      then paused (its time stops) until the next play.

    The board boots in about 1.2 s of its own time (commands written before about 1.1 s are not
    acted on); the first play boots it at 3x speed with the output muted, then sends the command
    at 1.25 s.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include <memory>
#include "crossover/resample.hpp"

#include "crossover/crsound.hpp"

class CoolRidersMusic
{
public:
    CoolRidersMusic();
    ~CoolRidersMusic();

    bool init(int out_rate);                // after cr_roms is loaded
    bool ready = false;

    void play(uint8_t song);                // song number (the sound test's; HANG ON = $10)
    void stop();

    // One frame (1/fps s): adds the board's output to mix (interleaved stereo, 16-bit scale)
    void mix(int32_t* mix, int samples, int fps);

private:
    enum State { OFF, BOOTING, RUNNING, STOPPING, PAUSED };
    State state = OFF;
    std::unique_ptr<CRSound> board;
    int out_rate = 44100;
    CrTime next_time;                       // the board's time at the end of the next frame
    CrTime stop_until;                      // STOPPING: when the fade is over
    int  pending_song = -1;                 // asked for during the boot
    uint8_t ring = 8;                       // next command ring entry (8 after the boot writes)
    FrameResampler out;                     // board output at its rate

    void send(const uint32_t* cmds, int n); // ring entries, written at the board's current time
    void send_play(uint8_t song);
    void send_stop();
    void run_frame(int fps, bool output);
};
