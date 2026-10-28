/***************************************************************************
    OutRun's Ferrari or Super Hang-On's bike as the vehicle on Hang-On's course (the emulated Hang-On
    board, homode).

    The same design as crossover/shferrari (the Ferrari on Super Hang-On's courses), on Hang-On's
    program, which Super Hang-On was built from: the world is Hang-On running unmodified on the
    emulated board; the vehicle is Cannonball's own OutRun code (OFerrari, OCrash, OSmoke,
    OSprites, the OutRun sound board), fed from that world and feeding back into it.

    Instruction hooks on the main CPU (HOBoard::set_pc_hooks):
      $119A   the task dispatcher's jsr (a5 = slot $20C800 + 64n, a0 = task):
              slot 9  speed $8566       -> the Ferrari's speed; distance from OutRun's car
                                           (OFerrari::update_road_pos), then Hang-On's own tail
                                           ($86B2-$8730: track position, odometer for the stripes)
              slot 10 centrifugal $879A -> none: OFerrari::set_curve_adjust on OutRun's road builder
                                           run over Hang-On's path
              slot 34 bike $AF10        -> the Ferrari, OutRun's car logic at OutRun's 30 Hz rate;
                                           start-line blink and goal exit as Hang-On's bike task
              slots 4-7 crash $89F2/$8AD0 -> none (OutRun's crash sequence instead)
              slot 2  sound $8140       -> runs, minus the bike's engine and effects
      $4026/$4038 sub_03F84 after the bike's sprite entry is written: tagged, and hovideo draws
              OutRun's sprites at that point of Hang-On's depth-sorted sprite list
      $A922/$A928/$A978 the rivals' contact test: OutRun's car rule and OutRun's response
      $B76A   the bike's dust object is not drawn
      $3F2E   sound commands queued by the sound task: the bike's effects are dropped

    Super Hang-On's bike runs in OutRun's car slot here, as it does on OutRun's courses (crossover/bike:
    Super Hang-On's own speed, centrifugal and bike tasks, its crash and sound), with Hang-On's world in
    place of OutRun's: its centrifugal table built from Hang-On's path, Hang-On's road edge (260), a scenery
    hit or a rival's contact answered as Super Hang-On answers them, its dust drawn by Hang-On's dust task
    (the counterpart of Super Hang-On's own).

    Hang-On's addresses are listed (with the code they were read from) in docs/CROSSOVER-HANGON.md.
    Units are Super Hang-On's (the same as OutRun's: track position, 16.16 km/h, lateral X).

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include "crossover/hovideo.hpp"

class HOVehicle
{
public:
    // The vehicle on Hang-On's course when it is not Hang-On's own bike: OutRun's Ferrari, or Super Hang-On's
    // bike (crossover/bike: its reconstructed tasks and sprites, in OutRun's car slot as on OutRun's courses)
    enum Vehicle { FERRARI, SH_BIKE };
    bool    enabled = false;                // another game's vehicle drives this session's course
    Vehicle vehicle = FERRARI;

    void begin();                           // after the board's run-up (HOMode::start)
    void end();                             // HOMode::stop
    void after_frame();                     // after each board frame: OutRun's sprites -> overlay
    void colours(uint32_t* rgb);            // rgb[0x2000..0x2FFF]: OutRun's colours for the overlay

    uint16_t overlay[HOVideo::MAXW * HOVideo::H];

private:
    bool    racing = false;                 // Hang-On's mode 1 (start), 2 (race) or 3 (goal) in a game
    int     sub_frame = 0;                  // board frames since the last OutRun tick (0 or 1)
    bool    in_sound_task = false;          // between the dispatch of slot 2 and the next one
    uint16_t crash_count = 0;               // $C7E084 at the last tick
    int     slot_depth = -1;                // the bike slot's own +$14 (draw-list depth)
    uint32_t owed = 0;                      // OutRun distance not yet given to the track
    int     owed_frames = 0;
    uint16_t road_x[512];                   // OutRun's road X table on Hang-On's path
    uint32_t road_x_pos = 0xFFFFFFFF;       // path position it was built for

    struct RivalHit { uint32_t slot; int side; int speed; };
    RivalHit hits[8];
    int      n_hits = 0;

    static void hook(void* ctx, uint32_t pc);
    void on_pc(uint32_t pc);
    bool on_task(uint32_t a5, uint32_t entry);  // true = skip the task
    void speed_task(uint32_t a5);
    void bike_task(uint32_t a5);
    void tick();                            // OutRun's car logic, one 30 Hz tick
    void world_to_outrun();
    void fill_road_heights();
    void build_road_x();
    void register_sprite(uint32_t a5);
    void publish(uint32_t a5);
    void tag_sprite(bool hidden);
    void rival_contact(uint32_t a5);
    int  rival_speed(uint32_t a5);
    void silence_packet();
    int  car_depth() const;                 // Hang-On depth (= screen line) of the car's sprites
};

extern HOVehicle hovehicle;
