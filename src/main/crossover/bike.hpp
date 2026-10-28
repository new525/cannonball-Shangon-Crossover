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

    Collisions keep OutRun's detection (its own scenery and traffic geometry) and answer with
    Super Hang-On's responses: a scenery hit starts Super Hang-On's crash sequence (crash, shadow and
    rider tasks, verified against MAME), a traffic hit Super Hang-On's knockback ($7510).
***************************************************************************/
#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>

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

    // Collisions (called by OutRun's collision code when the bike is the vehicle)
    void scenery_hit();                             // OFerrari::setup_ferrari_sprite
    void traffic_hit(int16_t traffic_x);            // OTraffic: screen x of the car hit
    bool crashing() const;                          // Super Hang-On's crash flag $260094
    // Crash objects other than the bike itself: rider and the two shadows, drawn through
    // the Ferrari's passenger and shadow entries. Called every frame from OFerrari.
    void draw_crash_objects(oentry* rider, oentry* rider_shadow, oentry* bike_shadow);
    bool owns(const oentry* e) const;               // drawn by this class (OSprites::do_sprite)
    // Collision widths, Super Hang-On's rules on OutRun's objects (see bike.cpp)
    bool    scenery_overlap(const oentry* obj, int16_t x1, int16_t x2) const;
    int16_t traffic_half_width(uint16_t car_width) const;
    // A Super Hang-On sound command (effects); goes to Super Hang-On's sound board in a game
    void sh_sound(uint8_t code);

    // For the HUD / sound / tests
    int16_t  speed_kmh() const;
    int16_t  bike_x() const;
    int16_t  car_x_relative() const { return bike_x(); }   // X relative to the bike's road centre
    bool     offroad() const;

    // Racing another game's course (crossover/hovehicle: Hang-On's): drawn full size, and the road path the
    // centrifugal table is built from read through path (nullptr: OutRun's course, the default)
    using PathFn = int16_t (*)(void* ctx, uint32_t offset);
    void set_course(bool full_size, PathFn path);
    // The bike's Super Hang-On state ($260000 + off, word), for a host world
    uint16_t sh_word(uint32_t off) const { return (uint16_t)(shared[off] << 8 | shared[off + 1]); }

    // Palette expansion entries added for the bike (index base set at init)
    static const int NUM_PALS = 6;
    uint16_t pal_base = 0;

private:
    uint8_t work[0x8000];
    uint8_t shared[0x2000];     // $260000-$261FFF (incl. the depth->height table at $261200)
    sho_mem mem;

    int16_t road_center = 0;    // centre of the bike's current road (OutRun car_x_pos units)
    int16_t road_half = 0;      // asphalt half-width around that centre
    bool    sh_course = false;  // racing a Super Hang-On course: drawn full size whatever BIKE SIZE says
    bool    full_size = false;  // another game's course (set_course): the same
    PathFn  path_fn = nullptr;
                                // (set by the course loader once Super Hang-On's courses are in)
    bool    mirrored = false;   // on OutRun's mirrored road at a fork (see build_road_table)
    uint16_t road_native[2][512];   // the builder's own tables, per buffer, before mirroring
    int     prev_pal = 0;       // palette shown this frame (Super Hang-On displays the previous frame's)

    struct Frame { uint16_t offset[2]; uint8_t pitch; uint8_t rows; uint16_t width; };
    std::vector<uint32_t> def_ptrs;     // Super Hang-On sprite definition addresses
    std::vector<Frame>    frames;       // parallel to def_ptrs

    // crash
    int     sh_frame_acc = 0;     // 60 Hz Super Hang-On frames owed to OutRun's logic ticks
    int     last_frame = -1;      // cannonball::frame at the previous tick()
    int     knock_pending = 0;  // sign of a traffic hit reported this tick (applied per Super Hang-On frame)
    uint8_t crash_entry[4][12]; // sprite entries of the crash objects (sub_02994), last frame
    bool    crash_vis[4] = {};
    oentry* crash_oentry[4] = {};
    struct CrashImg { uint8_t bank; uint16_t offset; uint8_t pitch; uint16_t width; };
    std::unordered_map<uint64_t, CrashImg> crash_cache;
    uint32_t crash_cache_pos = 0;
    void crash_frame();
    void send_sound_packet();
    void fill_road_heights();
    void setup_crash_entry(oentry* spr, int obj);
    bool draw_crash(oentry* input, osprite* output, int obj);
    bool crash_image(const uint8_t* e, CrashImg& img);
    static uint16_t depth_to_index(uint16_t depth);
    static void sound_cb(void* ctx, uint8_t code);

    void convert_graphics();
    void build_road_table();
    uint16_t scaled_zoom() const;
    int size_percent() const;
    static int scaled_lines(int rows, uint16_t zoom);
    void set_curve_inputs(int16_t car_x);
    void compute_road_frame(int16_t car_x);
    static int offroad_cb(void* ctx, int16_t x);
};

extern CrossoverBike bike;
