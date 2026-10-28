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
#include <string>

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

    // High scores. Hang-On resets its rankings ($20D800, 99 entries of 12 bytes, sub_05A7C), the value
    // at $20C03C and its lap-time records ($20DF90, sub_07E8A) at every power-on, and the board is powered
    // on for every race here; as SHMode does, the program keeps them in a file and puts them back once the
    // boot code has run.
    void load_scores();
    void save_scores();
    std::vector<uint8_t> scores;            // the saved areas, in the order of SCORE_AREAS (empty = none)
};

extern HOMode homode;
