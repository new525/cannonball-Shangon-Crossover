/***************************************************************************
    OutRun's Ferrari or Super Hang-On's bike as the vehicle on OutRunners' courses (the emulated
    System Multi 32 board, crossover/ormode).

    The design of the vehicles on Hang-On's and Super Hang-On's courses (crossover/hovehicle,
    crossover/shferrari) on OutRunners' program: the world is OutRunners running unmodified on the
    emulated board, with MAD POWER (automatic) chosen in its run-up; the vehicle is another game's own
    code, fed from that world and feeding back into it:
      - OutRun's Ferrari: Cannonball's OutRun code (OFerrari, OCrash, OSmoke, OSprites, the OutRun
        sound board);
      - Super Hang-On's bike: its reconstructed tasks (crossover/bike, crossover/shophys: speed,
        centrifugal push, steering, lean, off-road, knockback, crash, the sound task and its sound
        board) in OutRun's car slot, as on OutRun's and Hang-On's courses.
    OutRunners' road, camera, scenery, checkpoints and branch choice, timer and goal follow the
    vehicle's speed and lateral position, written where OutRunners' car code uses its own.

    Instruction hooks on the V70 (ORBoard::set_pc_hooks), player 1 only (R25 = $208000), in a race:
      $A39C   the car's handling (state 1 start line, 2 driving, 3 time up, 4 goal run): the vehicle's
              logic runs here, once per OutRunners tick (30 Hz: OutRun's car rate, two Super Hang-On
              frames for the bike)
      $A477   speed $209074 := the vehicle's km/h x 136 (OutRunners' units, its HUD shows / 136)
      $A5EE   the lateral velocity used for the advance along the course := 0 (the vehicle's speed
              is its speed along the road)
      $A66E   lateral position $209038 := the vehicle's X x 11/6 (OutRun's and Super Hang-On's lateral
              units against OutRunners': the cars' half-widths 24 -> 44), with its lateral velocity in
              $20042E (OutRunners' camera lags it as it lags its own cars) and no collision push ($2004CE)
      $7348   the collision flag (bit 2 of $200400, kind $20046E): taken, cleared, and answered with
              the vehicle's own rules instead of OutRunners' bounce ($7F87)
      $9E48   the car's sprite object ($99B8) after it has written its sprites: the body's entry
              becomes the overlay tag (crossover/s32video), the other parts are hidden
      $14971, $ED6F  the same for the car's game-over animation ($14900 splits the car into objects
              drawn by the generic $ED50): its body part is tagged, the others hidden
      $121E8  the car's engine parameters to the sound board ($700710/$700712): not written, the
              engine is off ($700712 = 0); the vehicle's own sound board plays it
      $1222F, $12235  the car's tyre/skid sound parameter ($700718; the second entry is called directly
              by the wheelspin, off-road and goal-run code): not written
    The bike only (Super Hang-On's widths in OutRunners' tests):
      $1254F, $12554, $1261B, $126F8  the roadside-object tests: the car's box ($208500 -> its entry
              in the list $124A4 builds) given the bike's half-width for scenery, 0 (its centre line)
      $127DB, $12896  the tests against cars and moving objects: the bike's reach, 10 x BIKE SIZE
      $A3EA   jsr $B79B, the off-road test: R3 (the car's half-width, parameter +$1A) := 0
    In the goal run (car state 4) OutRunners' own script moves the car (its speed and lateral
    position are not written); the vehicle follows it.

    The collision flag's kinds are answered with the vehicle's rules: scenery (0) its crash; a car or
    an object with its own position (2, 1: Kenya's elephants) OutRun's car contact for the Ferrari,
    Super Hang-On's knockback for the bike; a minor contact (3) the vehicle's off-road for the 15
    ticks OutRunners uses.

    OutRunners' addresses and the reasoning for every conversion are in
    docs/CROSSOVER-OUTRUNNERS.md, sections 7 (the Ferrari) and 8 (the bike).

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include <vector>

class ORBoard;

class ORVehicle
{
public:
    enum Vehicle { FERRARI, SH_BIKE };
    bool    enabled = false;                // another game's vehicle drives this session's course
    Vehicle vehicle = FERRARI;

    void begin(ORBoard* b);                 // after the board's run-up (ORMode::start)
    void end();                             // ORMode::stop
    bool driving() const;                   // in a race (car states 1-4): OutRunners' car inputs are neutral

    // OutRun's sprites drawn into the board's frame: one image per OutRun tick, kept for a few ticks
    // (the board shows the sprite list a frame or two after the game wrote it)
    static const int IMG_W = 320, IMG_H = 224, IMG_COUNT = 4;

private:
    ORBoard* board = nullptr;
    uint32_t tick_no = 0;
    uint32_t last_tick_frame = 0xFFFFFFFF;  // the board frame of the last OutRun tick
    int16_t  lat_prev = 0;                  // OutRunners lateral position written last tick
    int      minor_ticks = 0;               // a kind-3 contact: OutRun's off-road for 15 ticks
    uint16_t speed_prev = 0;                // OutRunners' speed in the goal run, last tick
    std::vector<uint16_t> img[IMG_COUNT];   // S32Video overlay images (see s32video.hpp)
    std::vector<uint16_t> layer;            // OutRun's sprite layer, rendered

    struct Hit { int kind; int side; int speed; uint32_t who; };
    Hit hits[8];
    int n_hits = 0;
    uint32_t contact_prev = 0;              // the bike: the car or object touched last tick (kind << 24 | arg)
    int16_t  reach = 0;                     // the bike's half-width against cars and moving objects (OutRunners' units)

    static void hook(void* ctx, uint32_t pc);
    void on_pc(uint32_t pc);
    bool racing() const;
    int  car_state() const;
    void collision();
    void tick();                            // the vehicle's logic, one 30 Hz tick
    void ferrari_tick(int cs);              // OutRun's car block and its collision responses
    void bike_tick(int cs);                 // Super Hang-On's bike and its collision responses
    void world_to_outrun();
    void build_road();                      // curvature, OutRun's road builder, road heights
    void check_wheels();                    // OutRun's wheel rule at OutRunners' road edge
    void set_box_width(int w);              // the bike: the car's collision box half-width
    void render_overlay();
    // the sprite entries [start, end) of object r20: hidden, the first made the overlay's tag if body
    void tag_sprites(uint32_t r20, uint32_t start, uint32_t end, bool body);
    uint32_t car_obj = 0;                   // the car's sprite object ($99B8), once seen
    uint32_t body_part = 0;                 // its game-over part drawing the body
    int16_t world_lateral() const;          // the vehicle's X in OutRunners' units, within its limits
};

extern ORVehicle orvehicle;
