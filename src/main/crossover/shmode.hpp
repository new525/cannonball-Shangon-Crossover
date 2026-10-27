/***************************************************************************
    Super Hang-On's courses: the emulated Super Hang-On board (shboard/shvideo) run as a mode of
    the program.

    Starting a course: the board is reset and run without display or sound through its own start
    sequence: a coin, the class (= course) chosen on the game's own class screen by brake
    presses, START, then the music on the game's own music screen the same way. Then the player
    takes over at the start line. With MUSIC set to "choose on the radio", the player takes over
    on the music screen instead and chooses there.

    Measured on the emulated board (and the same in MAME 0.264): coin accepted at frame 601;
    brake presses 20 frames apart step the class Beginner (Africa), Junior (Asia), Senior
    (America), Expert (Europe) and the music Outride a Crisis ($9D), Sprinter ($82),
    Winning Run ($96), Hard Road ($81). The game returns to its attract mode ($060408 = 0)
    after its ranking screen; the mode ends there and the program returns to the menu.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include <vector>

class SHMode
{
public:
    bool init();                            // after the Super Hang-On ROMs are loaded
    bool available() const { return ok; }

    // course 0-3 (Africa, Asia, America, Europe); music: an index into the program's music list
    // (config.sound.music), or -1 = the player chooses on the game's own music screen. Super
    // Hang-On's own tracks are chosen on its music screen; any other track replaces Super
    // Hang-On's race music when the game starts it.
    void start(int course, int music);
    void tick();                            // once per program frame
    void stop();
    bool active = false;
    bool finished = false;                  // the game has gone back to its attract mode

private:
    bool ok = false;
    int  acc = 0;                           // 60 Hz frames owed (30 fps mode runs two per tick)
    bool was_ingame = false;
    std::vector<uint8_t> packet;            // sound latch bytes of the current board frame
    bool feeding = false;                   // packets go to the sound board (not during the run-up)
    int  other_music = -1;                  // music list index replacing Super Hang-On's race music
    void set_inputs();
    void show_frame();

    // High scores. Super Hang-On resets its rankings at every power-on (sub_088FA, called from the
    // boot code at $1038: the four course tables at $61800-$619BF, $6003C and $61BE0-$61BFF), so
    // its scores never outlive a session. Like Cannonball's hiscores.xml for OutRun, the program
    // keeps exactly those areas in a file and puts them back once the boot code has run.
    void load_scores();
    void save_scores();
    std::vector<uint8_t> scores;            // the saved areas, in the order above (empty = none)
};

extern SHMode shmode;
