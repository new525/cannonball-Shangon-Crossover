/***************************************************************************
    Hang-On's course: the emulated Hang-On board (hoboard/hovideo) run as a mode of the program,
    the way SHMode runs Super Hang-On's courses.

    Starting: the board is reset and run without display or sound through a coin and START (coin
    accepted at frame 601, START at 701, as measured on the emulated board and in MAME 0.264); the
    player takes over at the start line. Hang-On has one course and no music screen: its race
    music (command $9D) plays unless another track is chosen, which then replaces it. The game
    returns to its attract mode after its ranking screen; the mode ends there.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include <vector>

class HOMode
{
public:
    bool init();                            // after the Hang-On ROMs are loaded
    bool available() const { return ok; }

    void start(int music);                  // music: index into config.sound.music, or -1
    void tick();
    void stop();
    bool active = false;
    bool finished = false;

private:
    bool ok = false;
    int  acc = 0;
    bool was_ingame = false;
    std::vector<uint8_t> packet;
    bool feeding = false;
    int  other_music = -1;
    void set_inputs();
    void show_frame();
    bool ingame() const;
};

extern HOMode homode;
