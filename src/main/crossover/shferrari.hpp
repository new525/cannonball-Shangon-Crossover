/***************************************************************************
    OutRun's Ferrari as the vehicle on Super Hang-On's courses (the emulated board, shmode).

    The mirror of crossover/bike (the bike on OutRun's courses): the world is Super Hang-On's
    program on the emulated board; the vehicle is Cannonball's own OutRun code (OFerrari, OCrash,
    OSmoke, OSprites, the OutRun sound board), fed from that world and feeding back into it.

    The board runs Super Hang-On unmodified, except where its main CPU reaches the player's
    parts. There an instruction hook (SHBoard::set_pc_hooks) takes over:
      $1212   the task dispatcher's jsr: the player tasks are replaced -
              slot 8  speed $610A      -> the Ferrari's speed; distance from OFerrari::update_road_pos
                                          (OutRun's 303 per km/h per 30 Hz tick, its curve correction),
                                          then Super Hang-On's own tail (sho_track_advance: track
                                          events, course distance, road stripes, odometer)
              slot 9  centrifugal $63CE -> none: OFerrari::set_curve_adjust on OutRun's road builder
                                          run over Super Hang-On's path (as the bike uses Super
                                          Hang-On's builder on OutRun's path)
              slot 34 bike $7AE6       -> the Ferrari: OutRun's car logic at OutRun's 30 Hz rate
              slots 3-6 crash objects  -> none (OutRun's crash sequence instead)
              slot 1  sound $5B98      -> runs, minus the bike's engine and effects (see below)
      $2B66/$2B78 sub_02994 for the bike's object slot: its sprite entry is tagged, and shvideo
              draws OutRun's sprites (Ferrari, passengers, shadow, smoke, crash) at that point of
              Super Hang-On's depth-sorted sprite list, with OutRun's colours
      $750A/$7510/$7554 the rivals' contact test: OutRun's car rule and OutRun's response
      $811E   the bike's dust/smoke object is not drawn (the Ferrari has OutRun's own)
      $28B0   sound commands queued by the sound task: the bike's effects are dropped

    Units shared by the two games (see crossover/bike.hpp): track position, 16.16 km/h speed,
    lateral X (+ = left), curve type/radius.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include "crossover/shvideo.hpp"

extern "C" {
#include "crossover/shophys/shophys.h"
}

class SHFerrari
{
public:
    bool enabled = false;                   // the Ferrari drives this session's course

    void begin();                           // after the board's run-up (SHMode::start)
    void end();                             // SHMode::stop
    void after_frame();                     // after each board frame: OutRun's sprites -> overlay
    void colours(uint32_t* rgb);            // rgb[0x2000..0x2FFF]: OutRun's colours for the overlay

    uint16_t overlay[SHVideo::MAXW * SHVideo::H];

private:
    sho_mem mem;
    bool    racing = false;                 // Super Hang-On's mode 1 (start) or 2 (race) in a game
    int     sub_frame = 0;                  // board frames since the last OutRun tick (0 or 1)
    bool    in_sound_task = false;          // between the dispatch of slot 1 and the next one
    uint16_t crash_count = 0;               // $260084 at the last tick
    int     slot_depth = -1;                // the bike slot's own +$14 (draw-list depth)
    uint32_t owed = 0;                      // OutRun distance not yet given to the track
    int     owed_frames = 0;
    uint16_t road_x[512];                   // OutRun's road X table on Super Hang-On's path
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
    void outrun_to_world();
    void fill_road_heights();
    void build_road_x();
    void register_sprite(uint32_t a5);
    void rival_contact(uint32_t a5);
    int  rival_speed(uint32_t a5);
    void silence_packet();
};

extern SHFerrari shferrari;
