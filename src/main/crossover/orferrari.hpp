/***************************************************************************
    OutRun's Ferrari as the vehicle on OutRunners' courses (the emulated System Multi 32 board,
    crossover/ormode).

    The design of the Ferrari on Hang-On's and Super Hang-On's courses (crossover/hovehicle,
    crossover/shferrari) on OutRunners' program: the world is OutRunners running unmodified on the
    emulated board, with MAD POWER (automatic) chosen in its run-up; the vehicle is Cannonball's own
    OutRun code (OFerrari, OCrash, OSmoke, OSprites, the OutRun sound board), fed from that world and
    feeding back into it. OutRunners' road, camera, scenery, checkpoints and branch choice, timer and
    goal follow the Ferrari's speed and lateral position, written where OutRunners' car code uses its
    own.

    Instruction hooks on the V70 (ORBoard::set_pc_hooks), player 1 only (R25 = $208000), in a race:
      $A39C   the car's handling (state 1 start line, 2 driving, 3 time up, 4 goal run): OutRun's
              car logic runs here, once per OutRunners tick (30 Hz, OutRun's car rate)
      $A477   speed $209074 := the Ferrari's km/h x 136 (OutRunners' units, its HUD shows / 136)
      $A5EE   the lateral velocity used for the advance along the course := 0 (the Ferrari's speed
              is its speed along the road)
      $A66E   lateral position $209038 := the Ferrari's X x 11/6 (car half-widths 24 -> 44), with its
              lateral velocity in $20042E (OutRunners' camera lags it as it lags its own cars) and
              no collision push ($2004CE)
      $7348   the collision flag (bit 2 of $200400, kind $20046E): taken, cleared, and answered with
              OutRun's rules instead of OutRunners' bounce ($7F87)
      $9E48   the car's sprite object ($99B8) after it has written its sprites: the body's entry
              becomes the overlay tag (crossover/s32video), the other parts are hidden
      $14971, $ED6F  the same for the car's game-over animation ($14900 splits the car into objects
              drawn by the generic $ED50): its body part is tagged, the others hidden
      $121E8  the car's engine parameters to the sound board ($700710/$700712): not written, the
              engine is off ($700712 = 0); OutRun's sound board plays the Ferrari
      $1222F  the car's tyre/skid sound parameter ($700718): not written
    In the goal run (car state 4) OutRunners' own script moves the car (its speed and lateral
    position are not written); the Ferrari follows it.

    The collision flag's kinds are answered with OutRun's rules: scenery (0) OutRun's crash, a car or
    an object with its own position (2, 1: Kenya's elephants) OutRun's car contact, a minor contact (3)
    OutRun's off-road for the 15 ticks OutRunners uses.

    OutRunners' addresses and the reasoning for every conversion are in
    docs/CROSSOVER-OUTRUNNERS.md, section 7.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include <vector>

class ORBoard;

class ORFerrari
{
public:
    bool enabled = false;                   // the Ferrari drives this session's course

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

    struct Hit { int kind; int side; int speed; };
    Hit hits[8];
    int n_hits = 0;

    static void hook(void* ctx, uint32_t pc);
    void on_pc(uint32_t pc);
    bool racing() const;
    int  car_state() const;
    void collision();
    void tick();                            // OutRun's car logic, one 30 Hz tick
    void world_to_outrun();
    void build_road();                      // curvature, OutRun's road builder, road heights
    void check_wheels();                    // OutRun's wheel rule at OutRunners' road edge
    void render_overlay();
    // the sprite entries [start, end) of object r20: hidden, the first made the overlay's tag if body
    void tag_sprites(uint32_t r20, uint32_t start, uint32_t end, bool body);
    uint32_t car_obj = 0;                   // the car's sprite object ($99B8), once seen
    uint32_t body_part = 0;                 // its game-over part drawing the body
    int16_t world_lateral() const;          // the Ferrari's X in OutRunners' units, within its limits
};

extern ORFerrari orferrari;
