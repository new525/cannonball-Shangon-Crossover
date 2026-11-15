/***************************************************************************
    Start-up and race-start measurement (testing). With the environment variable CB_STARTUP_LOG
    set, each stage of the start-up (main.cpp) and of a race's start (frontend/menu.cpp
    Menu::xover_start and what it calls) writes one line to stderr:

        startup: <ms since the process started> <ms since the last line> rss <MB> peak <MB>  <stage>

    rss is the resident memory now, peak its highest value so far (Linux: VmRSS and VmHWM of
    /proc/self/status; Windows: the working set and its peak; elsewhere 0). The time starts at
    this module's static initialisation, i.e. before main() but after the program is loaded.
    Without the variable every call returns at once and nothing is written.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

namespace startlog
{
    bool on();                          // CB_STARTUP_LOG is set
    void mark(const char* stage);       // one line: the stage that has just ended

    // A race's first frame: race_begin() when START RACE is chosen, then race_frame() after each
    // frame drawn with a race on screen writes one line for the first of them
    void race_begin();
    void race_frame();
}
