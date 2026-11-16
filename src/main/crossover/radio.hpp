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
      << and >> on the same tick (LEFT with RIGHT or DOWN): OutRunners' Jingle Bell.
      START is OutRunners' START: the next song, as >>, but nothing in Mega Radio Station
      ($192F7). In a race START has no other use on these courses: OutRun reads it at a stage
      change only (its hidden text, engine/opalette.cpp), Super Hang-On's and Hang-On's programs
      read it only on their selection, ranking and test screens (their boards still see it).
    - One tick of the radio every 30 Hz tick of the program (OutRunners' player tick), the
      commands one a tick to the board's $700700 (OutRunners' frame interrupt).
    - The title (the note, then "Splash Wave") as OutRunners draws it: sliding in from the right,
      held 2 s, sliding out to the left, centred on row 200, in OutRunners' own 8 x 8 font and its
      eight palettes, one every 2 ticks ($197CD), both read from OutRunners' ROMs
      (crossover/orradiogfx). Without the whole OutRunners set (the three sound ROMs are enough
      for the radio): OutRun's radio-screen font (8x16 letters, 16x16 note, text palette 5), no
      colour cycle.
    - The radio panel: while the cabinet's LCD scrolls a new title (the DJ's face, its mouth
      moving with a voice, and the title: orradio's lcd), the LCD is shown as a small panel under
      the title; nothing otherwise. It needs the whole OutRunners set.
    - The route node, the stage, the branch's slow-down ($208F0F) and the countdown are
      OutRunners' map's: on the other games' courses the radio is at the start of OutRunners' map
      (node 0, stage 0): Mega Radio Station plays the pre-stage's station ($F3).
    - It stops with the music (CrossoverAudio::stop_music: OutRun's FM_RESET, the end of a race on
      Super Hang-On's and Hang-On's boards, a mode's end).

    On OutRunners' own courses (crossover/ormode) the radio is the board's own; only the panel
    is drawn here, from the board's display (board_panel).

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include <cstdio>
#include "crossover/orradio.hpp"
#include "crossover/orradiogfx.hpp"

struct OrLcd;

class XRadio
{
public:
    void start();                           // OUTRUNNERS RADIO chosen (the music starts)
    void stop();                            // the music stops
    bool active() const { return on; }

    // Every 30 Hz tick of the program. buttons: the d-pad and START are the radio's (a race, the
    // player in control); show: the title and the panel are drawn (a race on screen)
    void tick(bool buttons, bool show);

    // The title and the panel over the frame (Video::prepare_frame): w x h frame-buffer values
    void draw(uint16_t* pixels, int w, int h);

    // OutRunners' own courses: the board's radio panel display to draw as the panel, or null for
    // none (crossover/ormode says when: a title of the board's radio scrolling on it)
    void board_panel(const OrLcd* lcd);

    // The panel on the 320 x 224 screen (a design decision, see docs/CROSSOVER-OUTRUNNERS.md 11.5):
    // 16 character cells of 7 x 8 pixels, a margin of 2 and a border of 1; centred, under the title's row
    static const int CELL_W = 7, CELL_H = 8, MARGIN = 3;
    static const int PANEL_W = 16 * CELL_W - 1 + 2 * MARGIN, PANEL_H = CELL_H + 2 * MARGIN;
    static const int PANEL_X = (320 - PANEL_W) / 2, PANEL_Y = 209;

    orradio::Radio radio;
    const orradio::Gfx& graphics() const { return gfx; }

private:
    bool on = false;
    bool shown = false;
    bool colours_set = false;
    int  ticks = 0;
    FILE* log = nullptr;                    // CB_RADIO_LOG=<file>: the commands sent, the buttons, the LCD
    orradio::Gfx gfx;                       // OutRunners' font, palettes and LCD characters, if the whole set is there
    bool gfx_tried = false;
    const OrLcd* board_lcd = nullptr;
    uint8_t logged_lcd[orradio::LCD_CHARS] = {};
    void load_gfx();
    void colours();
};

extern XRadio xradio;
