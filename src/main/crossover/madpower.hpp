/***************************************************************************
    OutRunners' car MAD POWER as the vehicle on OutRun's courses.

    The design of Super Hang-On's bike on OutRun's courses (crossover/bike), with OutRunners' car:
    OutRun's world (road, camera, traffic, scenery, collisions' detection, checkpoints and forks,
    timer, goal, Cannonball's OutRun code) keeps running; the vehicle in OutRun's car slot is
    OutRunners' own car code, transcribed and proven against its board (crossover/orcar: the
    handling, the sprite frame and the sound parameters, orcar::ORCar; the car's sprite object,
    orcar::CarSprite), at OutRunners' 30 Hz car rate (OutRun's logic rate).

    - The road (MadRoad): OutRunners' car reads a ring of segments ($8000 position units long on the
      centre line, a heading each) and the course's surface zones. OutRun's road gives them: one
      OutRunners segment is 303/272 of an OutRun path position (the same distance per km/h in both
      games: OutRun 303 per km/h per tick in its 16.16 road position, OutRunners 136 in $8000 a
      segment), each segment's heading OutRun's path's direction (atan2 of its unit vector, 4096 a
      turn, both growing in right curves) at that distance from the car, mirrored on OutRun's
      mirrored road at a fork; the half-width OutRun's asphalt edge (its road_width + $EC around the
      road the car is on, as for the bike) x 11/6; the pitch at the car from OutRun's height sections
      (outrun_pitch: its 2D hills read as a road under a camera that rides it, scaled so that a hill
      that looks as steep in both games has the same pitch); the pre-stage's surface lists (they end
      at once: kind 7 beyond the edges).
    - Units: lateral OutRun X x (-11/6) (the Ferrari's half-width 24, MAD POWER's 44: MAD POWER is
      the Ferrari's width, so OutRun's own collision tests apply unchanged); speed km/h x 136 (the
      speed shown by both games); the car's advance along the segments back to OutRun's road
      position (x 303/272 positions a segment).
    - Collisions: OutRun's tests decide; MAD POWER's own responses ($7F87): scenery its kind 0, a
      traffic car its kind 2 (a car of OutRunners' race, of MAD POWER's own weight).
    - Drawing: the car's sprite object's System 32 entries rendered with OutRunners' colours
      (orcar::S32Sprites) at the car's place in OutRun's sprite list (hwsprites' foreign entry), at
      OutRunners' scale 1 (MAD POWER on its start line: the Ferrari's size).
    - Sound: OutRunners' sound board (crossover/orsound) driven by the car's parameters and commands
      as its main CPU drives it ($700710-$700718, the queue to $700700), no music.
    docs/CROSSOVER-OUTRUNNERS.md section 9 has the addresses, the conversions and the checks.

    The same car drives Super Hang-On's and Hang-On's courses on their emulated boards (crossover/shferrari,
    crossover/hovehicle: begin_board), with those boards' worlds in OutRun's variables, their road paths
    (OutRun's format) in place of OutRun's and the image drawn into their frames (section 10).

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <deque>

struct oentry;
class osprite;
namespace orcar { class ORCar; class CarSprite; class S32Sprites; }
class ORSound;
struct FrameResampler;

class CrossoverMadPower
{
public:
    bool want = false;                  // chosen in the race setup (VEHICLE: OUTRUNNERS MAD POWER, an OutRun track)
    bool enabled = false;               // the vehicle in this race (reset(): a race started while wanted)
    bool available() const;             // every orunnersj file found (or_roms.full)
    bool load();                        // the ROMs (main program, sprites, sound) on the first race
    bool ready() const { return loaded; }

    void reset();                       // a new race (OInitEngine::init)
    void tick();                        // OInitEngine::update_engine's car block, once per 30 Hz tick
    void setup_sprite(oentry* spr);     // OFerrari::setup_ferrari_sprite
    bool owns(const oentry* e) const;
    bool draw(oentry* input, osprite* output);      // OSprites::do_sprite: the foreign entry
    void scenery_hit();                 // OutRun's scenery test hit the car
    void traffic_hit(const oentry* traffic);        // OutRun's traffic test hit the car
    void mix(int32_t* out, int samples, int fps);   // its sound board, once per audio frame

    // Super Hang-On's and Hang-On's courses (crossover/shferrari, crossover/hovehicle): the same car in OutRun's
    // car slot, with those boards' worlds faked into OutRun's variables there; the road path is theirs (OutRun's
    // format: 4 bytes a position, x then y), the image goes into their frames as an overlay
    typedef int16_t (*PathFn)(void* ctx, uint32_t off);
    void begin_board(PathFn path);      // the board's race is about to start
    void end_board();                   // the session ends (the engine off)
    bool on_board() const { return board_path != nullptr; }
    bool accel_cut = false;             // board: no accelerator (its timer at 0, as the Ferrari there)
    bool hold = false;                  // board: stopped past the goal line (speed 0 each tick, as the Ferrari there)
    int16_t board_pitch = 0;            // board: the road's pitch at the car (OutRunners' 12-bit, + = down), from its hill data
    void rival_hit(int16_t lateral, int16_t latvel, uint16_t speed);   // board: a rival bike (OutRunners' units)
    void engine_off();                  // board: $700712 = 0, as OutRunners does for a car not racing
    void to_overlay(uint16_t* ov, int w, int h, int x_off) const;      // board: the image (see the .cpp)

    // for the HUD, tests and traces
    int16_t speed_kmh() const;
    int16_t car_x_relative() const;     // OutRun X relative to the centre of the car's road
    int     state() const;
    uint8_t gear() const;

    static const int IMG_W = 320, IMG_H = 224;      // the car's image in OutRun's screen (before widescreen)

private:
    bool loaded = false, started = false;
    std::vector<uint8_t> maincpu, soundcpu, samples;
    std::vector<uint32_t> sprites;
    std::unique_ptr<orcar::ORCar> car;
    std::unique_ptr<orcar::CarSprite> spr;
    std::unique_ptr<orcar::S32Sprites> s32;

    // the road frame (OutRun X units): the centre of the car's road and its asphalt's half-width
    int16_t road_center = 0, road_half = 0;
    int     frame_kind = -1;            // which road the frame describes (a change recomputes the lateral)
    double  heading = 0;                // the heading at the car (12-bit units, continuous: OutRun's path's turns added)
    double  seg_frac = 0;               // the car's place in its segment (0-1)
    double  pos_acc = 0;                // OutRun road position (16.16) fraction carried
    // the slope (docs/CROSSOVER-OUTRUNNERS.md 9.10): OutRun's height sections as its camera takes them in
    int16_t pitch = 0;                  // the road's pitch at the car this tick (12-bit, + = down)
    double  dist = 0;                   // OutRun positions driven since the race began
    double  sl_k = 0;                   // the height values passed in this height section (OutRun's change-per-entry units)
    double  sl_close = 0, sl_close_at = 0, sl_close_len = 0;   // a section left on a slope, closed back to level
    double  sl_entry_at = 0, sl_entry_len = 8;                 // where the current entry began, the last entry's length
    bool    sl_have = false, sl_appr = false;
    uint16_t sl_lookup = 0; int16_t sl_index = 0, sl_v = 0;
    void    slope_reset();
    int16_t outrun_pitch();
    int     mirror = 1;
    bool    have_hit = false;
    uint8_t hit_kind = 0;
    int16_t hit_lateral = 0, hit_latvel = 0;
    uint16_t hit_speed = 0;
    uint8_t shift_prev = 0, rank_flag = 0;
    PathFn  board_path = nullptr;
    int16_t last_x = 0;                 // board: the X the last tick left (moved by the board: the lateral follows)
    std::vector<uint16_t> entries;      // the sprite object's entries this tick
    std::vector<uint16_t> image;        // the car's image (S32 15-bit pixels)
    void build_segments();
    void compute_frame(int16_t car_x);
    void render();
    static void blit(void* ctx, uint16_t* target, int w, int h, int x_off, bool hires);

    // sound
    std::unique_ptr<ORSound> board;
    std::unique_ptr<FrameResampler> ym, pcm;
    struct Write { int64_t t; uint32_t off; uint8_t data; };
    std::deque<Write> writes;
    std::deque<uint8_t> cmds;           // the queue $208800 (the frame interrupt sends one a frame)
    int64_t next_time = 0;
    double  frame_frac = 0;
    bool    snd_on = false;
    uint8_t snd_last[4] = {};
    void snd_write(uint32_t off, uint8_t v);
    void sound_reset();
};

extern CrossoverMadPower madpower;
