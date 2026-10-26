/***************************************************************************
    Super Hang-On bike as a vehicle in the OutRun world.

    The bike's physics are the verified C reconstructions of Super Hang-On's own tasks
    (crossover/shophys: speed $610A, centrifugal $63CE, bike $7AE6, inputs $5F4C/$6014/$6086),
    run on a memory image laid out like Super Hang-On's RAM, twice per OutRun logic tick
    (Super Hang-On runs them at 60 Hz, OutRun's logic runs at 30 Hz).

    Unit mapping between the two games (from code, see docs/CROSSOVER.md):
      track position   both .l, high word = path index; same path step length (4096)
      speed            both 16.16 km/h (bike $260078, OutRun car_increment)
      lateral X        same units and sign: bike $260022 vs OutRun car_x_pos (+ = left)
      curve info       OutRun road_type/road_curve = bike $2600AC/$2600B0 (same list format)

    Forks and wide roads: the bike's X is kept relative to the road it is on (centres from
    OutRun's road_ctrl/road_width, as OFerrari::check_wheels does), so Super Hang-On's clamp,
    centrifugal push and steering act on the bike's own road, and the bike is carried with
    its road as a split widens. Off-road follows OutRun's road edges.
***************************************************************************/
#pragma once

#include <cstdint>
#include <vector>

extern "C" {
#include "crossover/shophys/shophys.h"
}

struct oentry;
class osprite;

class CrossoverBike
{
public:
    bool enabled = false;       // vehicle selected (config); requires Super Hang-On ROMs
    bool ready   = false;       // ROMs loaded and graphics converted
    bool autopilot = false;     // test harness: Super Hang-On's demo driver rides in a game too

    bool init();                // after sh_roms are loaded
    void reset();               // new race / attract start
    void tick();                // replaces OFerrari::move ... set_ferrari_bounds (OInitEngine::update_engine)
    void setup_sprite(oentry* spr);                 // replaces the Ferrari frame setup
    bool draw(oentry* input, osprite* output);      // OSprites::do_sprite override; true if handled

    // For the HUD / sound / tests
    int16_t  speed_kmh() const;
    int16_t  bike_x() const;
    int16_t  car_x_relative() const { return bike_x(); }   // X relative to the bike's road centre
    bool     offroad() const;

    // Palette expansion entries added for the bike (index base set at init)
    static const int NUM_PALS = 6;
    uint16_t pal_base = 0;

private:
    uint8_t work[0x8000];
    uint8_t shared[0x1000];
    sho_mem mem;

    int16_t road_center = 0;    // centre of the bike's current road (OutRun car_x_pos units)
    int16_t road_half = 0;      // asphalt half-width around that centre
    int     prev_pal = 0;       // palette shown this frame (Super Hang-On displays the previous frame's)

    struct Frame { uint16_t offset[2]; uint8_t pitch; uint8_t rows; uint16_t width; };
    std::vector<uint32_t> def_ptrs;     // Super Hang-On sprite definition addresses
    std::vector<Frame>    frames;       // parallel to def_ptrs

    void convert_graphics();
    void build_road_table();
    void compute_road_frame(int16_t car_x);
    static int offroad_cb(void* ctx, int16_t x);
};

extern CrossoverBike bike;
