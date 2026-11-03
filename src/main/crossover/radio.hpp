/***************************************************************************
    OUTRUNNERS RADIO: OutRunners' in-race radio (crossover/orradio, transcribed from its code) on
    OutRun's, Super Hang-On's and Hang-On's courses, driving OutRunners' sound board
    (crossover/ormusic) with the commands OutRunners' main CPU would send.

    - Started as an OutRunners race starts it: the race setup's $1F to the board ($12277), then the
      radio object; it plays the song OutRunners gives MAD POWER (Splash Wave, $1968D[0]): the
      other vehicles count as MAD POWER, as on OutRunners' courses (crossover/ormode). The race it
      introduces is a race of one car, MAD POWER, in first place.
    - Mid-race the d-pad is the radio's buttons (OutRunners' P2_A for player 1):
        UP    DJ/MUSIC (Mega Radio Station; any button there: back to the song)
        DOWN  >>
        LEFT  <<, RIGHT >> only when the steering is analogue (they steer otherwise)
      << and >> on the same tick (LEFT with RIGHT or DOWN): OutRunners' Jingle Bell. OutRunners'
      START (next song too, but not out of Mega Radio Station) is not mapped.
    - One tick of the radio every 30 Hz tick of the program (OutRunners' player tick), the
      commands one a tick to the board's $700700 (OutRunners' frame interrupt).
    - The title (the note, then "SPLASH WAVE") as OutRunners draws it (sliding in from the right, held 2 s,
      sliding out to the left, centred at the bottom), in OutRun's radio-screen font (its 8x16
      letters and 16x16 note, text palette 5), over the screen.
    - The route node, the stage, the branch's slow-down ($208F0F) and the countdown are
      OutRunners' map's: on the other games' courses the radio is at the start of OutRunners' map
      (node 0, stage 0): Mega Radio Station plays the pre-stage's station ($F3).
    - It stops with the music (CrossoverAudio::stop_music: OutRun's FM_RESET, the end of a race on
      Super Hang-On's and Hang-On's boards, a mode's end).

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include <cstdio>
#include "crossover/orradio.hpp"

class XRadio
{
public:
    void start();                           // OUTRUNNERS RADIO chosen (the music starts)
    void stop();                            // the music stops
    bool active() const { return on; }

    // Every 30 Hz tick of the program. buttons: the d-pad is the radio's (a race, the player in
    // control); show: the title is drawn (a race on screen)
    void tick(bool buttons, bool show);

    // The title over the frame (Video::prepare_frame): w x h frame-buffer values
    void draw(uint16_t* pixels, int w, int h);

    orradio::Radio radio;

private:
    bool on = false;
    bool shown = false;
    bool colours_set = false;
    int  ticks = 0;
    FILE* log = nullptr;                    // CB_RADIO_LOG=<file>: the commands sent, the titles
    void colours();
};

extern XRadio xradio;
