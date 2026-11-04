/***************************************************************************
    OutRunners' player car (MAME set orunnersj), its handling transcribed from the V70 program as
    C++: what the player-car object ($200400, routine $715C) does per tick that determines the
    car's motion and state, and the shift logic of the gear-box object ($200C00, $C3C4).

    Every variable keeps OutRunners' units and fixed-point arithmetic and is named after its
    address in player 1's work RAM (R20 = $200400 the car object, R23 = $209000 the car's
    variables, R25 = $208000 the player's base). Every step gives the address of the instruction
    it transcribes; the V70's operand sizes are kept (a 16-bit operation on a register changes
    its low 16 bits only, unsigned compares are unsigned, divisions truncate towards zero and a
    division by 0 leaves the dividend, as in MAME 0.264's V60 core).

    Covered (docs/OUTRUNNERS-RE.md section 13):
      $715C   the tick: the shaker byte, the inputs ($A304, the game's path), the state
              machine: 0 init ($719C: the grid position, $A278 parameters, $A2B6 torque
              table, $B764 surface zones, $7A10 world position, $7F5D crash parameters),
              1 start line ($72F4), 2 driving ($7325), 3 time up ($73A0), 4 goal run ($746C,
              $7602), 5 crash / bounce ($819D); the collision responses ($7F87: kinds 0-3,
              $8150-$8197), $7A4B
      $A39C   the handling: surface and off-road ($B79B, $B93D), slope and drag, accelerator /
              brake maps, torque, the speed integration and engine speed ($AAC9), steering
              angle, curve push, grip and slide, lateral velocity, the advance along the course
              ($12318 atan2; the catch-up $A620), segment crossing, lateral limits,
              world position ($104F6) and heading interpolation
      $7A7A   the sprite frame: base frame by the hill, steering frame, wheelspin and slide
              frames, the crash's animation ($F579, $F5D2; the goal run's stop frame)
      $F579   the car's animation step (it ends the crash: $81CB)
      $7B3F   the hill index of the frame
      $7B97-$7BE5  the camera's flag for the sprite object ($209145: the speed jumped)
      $1219C, $121E8, $1222F, $12235, $12124  the sounds: the engine's parameters, the tyre/skid
              sound's number, the commands queued (the crash's, the bounce's, the time-up's
              through $1966E); $209134-$209136
      $C3C4   the gear box: manual shifts, the over-rev smoke timer $209144 (and its tyre sound)
    Left to the board (not the car's motion): the camera ($A89E, the rest of $7B74, $79A0), the
    publishing of the car record ($77BD; orcarspr.hpp Record::publish has the sprite's fields),
    the road generator and projection ($FF8E, $102CE), race order ($76AF), the collision tests
    that report a contact ($124A4, $127DB and the roadside objects' tests: their result is an
    input), the demo driver ($A325) and the linked-cabinet mode ($20E623 set): see unsupported().
    The car's sprite object ($99B8) is in orcarspr.hpp.

    Inputs: the controls, the road (Road: the segment ring and the course node's surface data),
    and the world values the code reads (TickInput: the race's car records, the camera's pitch,
    the race flags, a contact reported by the collision tests). ROM tables (sine/cosine, arc
    tangent, the per-car parameters, gear tables, torque curves, accelerator and brake maps,
    collision weights and response tables, animation definitions) are read from the program
    ROM image at their addresses.

    Proven tick by tick against the emulated board (crossover/orboard): tools/orcartest.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include <cstddef>

namespace orcar
{

// One record of the segment ring ($20A000 + $20 x n), the fields the handling reads
struct Segment
{
    uint16_t heading = 0;       // +0   12-bit angle (4096 = a turn), grows in a right turn
    uint16_t pitch = 0;         // +2   12-bit angle
    int32_t  x = 0;             // +4   world x of the segment's start on the centre line (the car's world
                                //      x is this + its rotated lateral offset: the lateral position's units)
    int32_t  z = 0;             // +8   world z, the same units (a level segment is $80 long)
    int32_t  y = 0;             // +C   world y (height); the car's is interpolated between this and the next segment's
    uint16_t half_width = 0;    // +12  road half-width in the lateral position's units (440 for width code 5)
};

// The road as the handling reads it. OutRunners' own road: the ring at $20A000 and the ROM's
// surface zones; another game's road gives its segments here through an adapter.
class Road
{
public:
    virtual ~Road() {}
    // The ring record at offset `off` (0..$FE0, a multiple of $20; the car's is $209040)
    virtual Segment segment(uint16_t off) const = 0;
    // The course node whose script header was read last ($2080A3)
    virtual uint8_t node() const = 0;
    // Segments in each node ($208700 + 2 x node, set at power-on by $14022) and the extra
    // count added to each ($20873E)
    virtual uint16_t node_length(uint8_t node) const = 0;
    virtual uint16_t node_extra() const = 0;
    // Surface zone lists (8-byte entries: +0 segment from the node's start, +2/+4 lateral
    // bounds + $8000, +6/+7 wheel kinds; a negative +0 ends the list): the address of the
    // node's list for side 0 / 1 (the ROM's table at $B9F8: 8 bytes a node), and their contents
    virtual uint32_t zone_list(uint8_t node, int side) const = 0;
    virtual uint16_t zone16(uint32_t addr) const = 0;
    virtual uint8_t  zone8(uint32_t addr) const = 0;
};

// What the car's tick reads besides the road and its own state
struct TickInput
{
    // controls as the game holds them ($208100-$208102: steering $FF full left, $7F centre,
    // $00 full right; accelerator and brake 0-$FF) and the shift buttons' new presses ($208118:
    // bit 4 up, bit 5 down; read by the gear box)
    uint8_t steer = 0x7F, accel = 0, brake = 0;
    uint8_t shift_edges = 0;
    // the race
    uint8_t player = 0;         // $208000: 0 monitor A, 1 monitor B
    uint8_t carnum = 0;         // $208001: the car's index in the race's car array
    uint8_t rank = 0;           // $208320 + carnum: position (indexes $8292 and $97AC)
    uint8_t players_m1 = 0;     // 1F[$208308] - 1 (the start grid table, init only)
    uint8_t time_up = 0;        // $209180 ($CA0F)
    uint8_t stage = 0;          // $2080A4: the stage (the time-up's sound, $7448)
    uint8_t goal = 0;           // $209181 ($CB51)
    uint16_t finish_time = 0;   // $208410 + 2 x carnum (goal run, $74AE)
    // the race's car array as copied at the start of the frame ($E88C), the car's own entry
    // ($20FC00 + $80 x carnum) and the one $76AF puts at 68[$208000] (the car one place ahead in
    // the race order; the car's own when it leads or races alone)
    uint16_t own_rec_seg = 0;   // +42 ($209044 published)
    uint8_t  own_rec_goal = 0;  // +5E
    uint16_t own_rec_pitch = 0; // +54 (the road's pitch at the car, published)
    uint16_t ref_rec_seg = 0;   // +42 of the car at 68[$208000]
    // the car's own record ($20FB00 + $80 x player) when the tick starts: +60 the markers passed
    // (the road generator counts the checkpoints, $101F5; the goal run counts the goal, $7499)
    uint8_t  rec_markers = 0;
    // the gear-box object's state ($200C0C) when it runs (the HUD creates it in state 0)
    uint8_t  gearbox_state = 0;
    // the player's flags word $208094 when the tick starts (the time-up and goal-run counts set
    // bit 4 when they end; the game's ending clears it)
    uint16_t flags94 = 0;
    // the camera's pitch ($20902C): the previous tick's camera for the crash frame's hill index ($8175
    // bsr 7B3F, before this tick's camera), this tick's for the sprite frame's ($7A7A, after the camera $A89E)
    uint16_t camera_pitch = 0;
    uint16_t camera_pitch_now = 0;
    // a contact reported by the collision tests: bit 2 of $200400 when the car's tick starts
    // (the roadside objects' tests, run after the car in the previous frame) and at $7348
    // (after the car-to-car test $127DB), with the kind $20046E and argument $200468
    bool     hit_at_entry = false;
    bool     hit = false;           // at $7348 (state 2 only)
    uint8_t  hit_kind = 0;          // 0 scenery, 1 an object with its own position, 2 a car, 3 minor
    uint16_t hit_arg = 0;
    // kind 1: the object ($20xxxx at the argument): 20 lateral, 3F half-width factor (signed
    // byte, x the road's half-width), 2E lateral velocity, 2C speed (<< 8: the car's units), 6A weight
    int16_t  obj_lateral = 0; int8_t obj_width = 0; int16_t obj_latvel = 0; uint16_t obj_speed = 0; uint16_t obj_weight = 0;
    // kind 2: the other car's record in the car array: 44 lateral, 4E lateral velocity, 4C speed, 49 car
    int16_t  car_lateral = 0; int16_t car_latvel = 0; uint16_t car_speed = 0; uint8_t car_model = 0;
    // the camera's state ($2004E0, 1 in a race) and its flag $2004E1 (0 in a race): $7B74's part above
    uint8_t cam_state = 1, cam_e1 = 0;
    // must be 0 (see ORCar::unsupported)
    uint8_t demo = 0;           // bit 8 of $208090 (the attract demo's driver)
    uint8_t link = 0;           // $20E623
};

// The car's state: every variable the transcribed code writes (player 1's addresses)
struct State
{
    // the car object ($200400)
    uint16_t flags = 0;         // $200400: bit 0 active, 2 contact, 3 crashing
    uint8_t  state = 0, sub = 0, st_e = 0, st_f = 0;   // $20040C-$20040F (written as one 32-bit word)
    uint16_t timer = 0;         // $200410: start countdown, time-up and goal-run counts
    int32_t  wx = 0, wy = 0, wz = 0;    // $200420, $200424, $200428: world position
    uint16_t speed_copy = 0;    // $20042C
    int16_t  latvel = 0;        // $20042E: lateral velocity
    uint16_t anim_frame = 0;    // $200438: animation frame (owned in the crash)
    uint8_t  anim_step = 0, anim_rate = 0, anim_count = 0;  // $200440, $200441, $200442
    uint8_t  grace = 0;         // $20046F: ticks after a crash before flag bit 3 clears
    uint16_t pitch_init = 0;    // $20048C (init only)
    uint16_t heading = 0;       // $20048E: road heading at the car
    uint16_t heading_prev = 0;  // $200490
    int16_t  force = 0;         // $200494: force on the car this tick
    int16_t  steer_angle = 0;   // $200496
    uint16_t brake_val = 0;     // $2004B0: brake map value
    int32_t  wy_prev = 0;       // $2004B4
    int16_t  slide = 0;         // $2004B8: slide amount
    uint32_t speed32 = 0;       // $2004C0
    int32_t  dspeed = 0;        // $2004C4: collision speed change
    int32_t  jump = 0;          // $2004C8: crash jump height (added to world y)
    int16_t  jump_v = 0;        // $2004CC: its vertical speed
    int16_t  push = 0;          // $2004CE: collision lateral push
    uint8_t  in_accel = 0, in_brake = 0, in_steer = 0;   // $2004D0-$2004D2
    uint8_t  hill = 0;          // $2004D9 (written by $7B3F when a crash starts)
    uint16_t goal_seg = 0;      // $2004F0: goal run, segment where the script ends
    int16_t  goal_lat = 0;      // $2004F2: lateral target
    uint16_t goal_speed = 0;    // $2004F4: speed target
    uint8_t  goal_steer = 0, goal_accel = 0;    // $2004F6, $2004F7
    uint8_t  goal_flag = 0;     // $2004F8
    // the car's variables ($209000)
    uint16_t pos_hi = 0;        // $209032: $209034 >> 8
    uint32_t pos = 0;           // $209034: position in the segment
    int16_t  lateral = 0;       // $209038
    uint16_t seg_rest = 0;      // $20903A: segment length - position, >> 8
    uint16_t ring_off = 0;      // $209040: the car's segment in the ring (x $20)
    uint16_t seg_count = 0;     // $209042
    uint16_t seg_count2 = 0;    // $209044 (16 + grid position ahead of $209042)
    uint16_t zone_base = 0, zone_base_prev = 0;     // $20904C, $20904E
    uint32_t torque_ptr = 0, gears_ptr = 0, params = 0;     // $209050, $209054, $209058
    uint8_t  car = 0, trans = 0;    // $209060, $209061 (set by the car select)
    uint8_t  gear = 0, gears_m1 = 0, ratio = 0;     // $209062-$209064
    uint8_t  spin = 0;          // $209065: wheelspin / locked wheels
    uint8_t  moving = 0;        // $209066
    uint8_t  sliding = 0;       // $209067
    uint16_t gap = 0, catchup = 0;  // $209068, $20906A
    uint16_t rpm = 0;           // $209070
    uint16_t speed = 0;         // $209074
    uint8_t  offroad = 0;       // $20907C
    uint16_t braking = 0;       // $20907E: 2 while the brake map gives a value
    uint16_t ring_off2 = 0;     // $2090A4
    uint16_t crash_restitution = 0, crash_push_decay = 0, crash_gravity = 0, crash_speed_decay = 0; // $209126, $209128, $20912A, $20912C
    uint32_t crash_table = 0;   // $209130
    uint8_t  wheel[3] = {};     // $209140-$209142: wheel surface / smoke kinds
    uint8_t  shift_smoke = 0;   // $209144
    uint8_t  zone_node = 0;     // $209146
    uint8_t  minor = 0;         // $209147: minor-contact ticks
    uint32_t zone_ptr[2] = {};  // $209148, $20914C
    uint8_t  game_over = 0;     // $209182
    uint16_t torque[8][256] = {};   // $209300 + $200 x gear
    // elsewhere
    uint8_t  shaker = 0;        // $20E0D0 + player
    uint8_t  gearbox_state = 0; // $200C0C: the gear-box object's state
    uint8_t  gearbox_hud = 0;   // $200C40: the gear shown
    uint16_t done_flags = 0;    // $208094 (the car sets bit 4: game over / goal run over)
    // the car's record ($20FB00 + $80 x player): the fields the handling writes
    uint16_t rec_pitch = 0;     // +54
    uint8_t  rec_markers = 0;   // +60 (the goal run counts the goal)
    uint8_t  rec_goal = 0;      // +5E
    uint16_t rec_finish = 0;    // +16
    // the sprite frame ($7A7A; with the animation fields above): the steering frame the car's frame
    // moves to one step a tick, and the palette index of its body ($7270: $977C[car])
    uint8_t  steer_frame = 0;   // $2004DB
    uint16_t pal_index = 0;     // $20043A
    // the sounds: the car's parameters in the sound board's shared RAM (player 1's: $700710 +
    // $100 x player; $1219C at the start, $121E8 every tick), the crash's sounds ($8159, $81B0) and
    // the command queue ($12124: the frame interrupt sends one a frame to $700700)
    uint8_t  snd_engine = 0;    // $700710: the engine speed >> 8 ($121E8), 1 at the start
    uint8_t  snd_ctrl = 0;      // $700712: $A0 at the start, $20 running
    uint8_t  snd_car = 0;       // $700713: the car's engine ($12292[car])
    uint8_t  snd_box = 0;       // $700714: $10 automatic, $30 manual
    uint8_t  crash_snd = 0;     // $209134: the crash's second sound
    uint8_t  crash_snd_wait = 0;// $209135: ticks until it
    uint8_t  crash_dust = 0;    // $209136: $FF when a crash starts (the sprite object takes it)
    uint8_t  sq_wr = 0;         // $208801: the queue's write index
    uint8_t  sq[32] = {};       // $208802: the queue
    // the camera's flag for the car's sprite object ($7B74 at $7B97-$7BE5, the rest of the camera is
    // not transcribed): $FF when the speed grew by more than $A0 this tick (published as the record's +53)
    uint8_t  squat = 0;         // $209145
    int16_t  neg_speed = 0;     // $2004B2: minus the speed, last tick
};

// What a tick sent to the sound board besides the parameters in State: the tyre/skid sound ($12235:
// $700718 := the sound's number; the board's Z80 takes it) and the commands queued ($12124)
struct SoundOut
{
    int     tyre_writes = 0;    // writes to $700718 this tick
    uint8_t tyre = 0;           // the last one's value
    int     ncmd = 0;           // commands queued this tick
    uint8_t cmd[8] = {};
};

class ORCar
{
public:
    // rom: the main CPU's 2 MB region (program and data ROM, $000000-$1FFFFF, as the board holds it)
    explicit ORCar(const uint8_t* rom) : rom(rom) {}

    State s;
    SoundOut snd;               // cleared at the start of tick() and gearbox_tick()

    // One tick of the car object ($715C), player's tick (30 Hz)
    void tick(const TickInput& in, const Road& road);
    // The gear box's tick ($C379, state 1 $C3C4), run later in the same frame (object $200C00)
    void gearbox_tick(const TickInput& in);

    const uint8_t* rom_data() const { return rom; }

    // Set when the tick met a path that is not transcribed (the demo driver, the linked mode, a
    // state outside 0-5); the state is then not valid. 0 = none.
    uint32_t unsupported() const { return unsupported_pc; }

    // Crossover (BALANCE, curve push): the push out of the curve ($A50E) is passed through push_fn
    // when it is set (crossover/madpower); the last tick's push before and after it, and the heading
    // term it came from, for the measurement log
    int (*push_fn)(int push) = nullptr;
    int16_t curve_term = 0, curve_push_raw = 0, curve_push = 0;

private:
    const uint8_t* rom;
    uint32_t unsupported_pc = 0;

    uint8_t  r8_(uint32_t a) const  { return rom[a & 0x1FFFFF]; }
    uint16_t r16(uint32_t a) const { return (uint16_t)(r8_(a) | r8_(a + 1) << 8); }
    uint32_t r32(uint32_t a) const { return (uint32_t) r16(a) | (uint32_t) r16(a + 2) << 16; }
    int32_t  sin_(uint32_t i) const;    // movs.hw 100C00(Ri)
    int32_t  cos_(uint32_t i) const;    // movs.hw 101400(Ri)

    void state0(const TickInput& in, const Road& road);
    void handling(const TickInput& in, const Road& road);   // $7358: $A39C and the tick's tail
    void a39c(const TickInput& in, const Road& road);
    uint32_t b79b(const TickInput& in, const Road& road, uint32_t r0, int32_t r1, uint32_t r2, uint32_t r3);
    void b764(const Road& road);
    void b93d(const TickInput& in);
    void aac9(const TickInput& in, uint32_t r7, uint32_t r8, uint32_t r9, uint32_t r19);
    void a7cc(const Road& road, uint16_t r11, uint16_t r12, int32_t r13, int32_t r15, int32_t r17);
    void collision(const TickInput& in, const Road& road);  // $7F87
    bool crash_tick(const TickInput& in);                   // $819D; false: the crash is over ($8263)
    void clear_contact();                                   // $7A4B
    void goal_drive();                                      // $7602
    void anim_step_f579();
    void anim_set_f5d2(uint16_t frame);                     // $F5D2: start a frame's animation
    void frame_select(const TickInput& in);                 // $7A7A: the sprite frame
    void camera_flag(const TickInput& in);                  // $7B97-$7BE5: $209145
    uint8_t hill_index(uint16_t camera_pitch, const TickInput& in);     // $7B3F
    void sound_queue(uint8_t cmd);                          // $12124
    void sound_tyre(uint8_t num);                           // $12235
    void sound_tyre_if_on_road(uint8_t num);                // $1222F
};

} // namespace orcar
