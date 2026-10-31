/***************************************************************************
    OutRunners' courses: the emulated System Multi 32 board (crossover/orboard) run as a mode of
    the program, the way HOMode runs Hang-On's course.

    The player is player 1 (monitor A, the left screen and the left speaker). Starting: the board
    is powered on and run without display or sound through a coin, START, the car select and the
    transmission screen, then the player takes over as the race starts. The run-up follows the
    game's own state (docs/OUTRUNNERS-RE.md), not fixed frame numbers:
      - coin at frame 600; START once the game shows a credit ($208080 = 2);
      - at the car select ($208082 = 3) the wheel is turned left, one step at a time, until the
        cursor ($209060) is on the chosen car;
      - a manual box: the wheel held right while the transmission screen is up, as in
        tools/ortrace e09 (frames 2200-2520 of a run with the car select from 767);
      - the single stage: the route node and stage bytes $2080A0-$2080A6 are written before the
        race setup ($208082 = 5), as in tools/ortrace e04, so the race starts on that stage;
      - the player takes over at substate 6 ($208082), the start line.
    The race ends when the game leaves its game state ($208080 no longer 6).

    Courses: the full course (the game's own map: West/East and each branch chosen by the car's
    side of the road at the checkpoint) or the Kenya stage (course 41, route node 30, stage 5).
    Vehicles: OutRunners' own cars (MAD POWER for now).

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "crossover/resample.hpp"

class ORBoard;

class ORMode
{
public:
    enum Course { FULL_COURSE = 0, KENYA = 1 };
    static const int CAR_MAD_POWER = 0;     // OutRunners' car index (name table at $1F38)

    bool init();                            // after or_roms is loaded: needs the full orunnersj set
    bool available() const { return ok; }

    void start(int course, int car, bool manual);
    void tick();
    void stop();
    bool active = false;
    bool finished = false;

    // The board's sound (player 1's speaker), added to the program's output
    void mix(int32_t* out, int samples, int out_rate);

private:
    bool ok = false;
    std::unique_ptr<ORBoard> board;
    bool loaded = false;
    bool load_board();

    int  acc = 0;
    bool manual_box = false;
    FrameResampler ym, pcm;
    bool feeding = false;

    void run_up(int course, int car, bool manual);
    void set_inputs();
    void take_sound();
    void show_frame();

    std::vector<uint16_t> pixels;           // the left screen as indices into colours[]
    std::vector<uint32_t> colours;

    std::string eeprom_file() const;
    void load_eeprom(std::vector<uint8_t>& e) const;
    void save_eeprom() const;
};

extern ORMode ormode;
