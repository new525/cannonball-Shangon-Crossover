/***************************************************************************
    OutRunners' player car (MAME set orunnersj): its sprite, transcribed from the V70 program as
    C++, with a renderer for the System 32 sprite entries it makes.

    The car's sprite object ($201800 for player 1's car, routine $99B8) draws the car from its entry
    in the race's car array ($20FC00 + $80 x car, copied from the car record at the start of each
    frame: so it draws the record the car's tick published the tick before). Transcribed, for the
    player's own car ($9AE0-$9E7E, $9E8A-$9FBF):
      - the body: its frame and sub-frame from the record (+3C, +4A: the car's tick chooses them,
        orcar::ORCar::frame_select, $7A7A), its palette index (+3E) plus the braking value $20907E
        and a toggle that changes with the distance driven ($88-$8B: the record's speed added each
        tick, bit 16);
      - the two wheels ($9EDD), turning while the car moves, placed by a per-car table by the body's
        image; hidden for good once the body shows a frame without them ($A0B8[frame - first]);
      - the wheels' smoke ($9E8A), by the wheel kinds (+50-52: the surface, spin, slides);
      - the dust under the car when the speed jumps (+53, $209145: a five-step animation);
      - the crash's dust ($209136, set when a crash starts: thirty ticks, at the crash's line).
    Not transcribed: the player marker drawn over the car on the start line ($9CCC-$9D16, while
    $20831C = 0 and the car's countdown >= $3C: the entries are left out), the other players' and the
    drones' cars (BD[R20] != 0, $9CB3, $9D9D), the depth sorting ($9E48: the caller places the car),
    and the separate objects the init spawns ($14374, $14460).
    The init ($97FC-$99B3) is reduced to the fields $99B8 reads for the car. The objects it spawns with
    routine $14374 restore the wheels after a crash frame without them (after(), see below).

    Rendering (S32Sprites): the entries drawn as MAME 0.264's segas32_v.cpp draw_one_sprite draws
    them (BSD-3-Clause, Aaron Giles: 4/8 bpp, indirect palettes, flips, zoom, alignment, offsets,
    the end-of-line code), from the sprite ROMs as MAME loads them, into an image of 15-bit pixels;
    the mixer's colour for a sprite pixel (segas32_v.cpp mix_all_layers) with the registers the game
    sets in a race (measured: sprite groups by bits 12-13, palette base 0, no colour offsets; the
    sprite control's transparency mask $0FFF, indirect entries with bit 15); the palette RAM's
    entries $000-$9FF and $F00-$FFF as the game loads them at power-on from the ROM ($153904 and
    $3153C, $11881), the indirect tables as it copies them into sprite RAM ($107020 -> $401000,
    $10BE0).

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include <vector>
#include "crossover/orcar/orcar.hpp"

namespace orcar
{

// The car array entry's fields the sprite object reads (published by $77BD from the car's state)
struct Record
{
    uint16_t frame = 0;         // +3C  38[R20]
    uint16_t pal = 0;           // +3E  3A[R20]
    uint8_t  sub = 0;           // +4A  40[R20]
    uint16_t speed = 0;         // +4C  2C[R20]
    uint8_t  wheel[4] = {};     // +50-53: $209140, $209141, $209142, $209145
    static Record publish(const State& s);
};

// What $99B8 reads besides the record and its own fields
struct SpriteInput
{
    int16_t  x = 0, y = 0;      // R15, R16: the car's projected point (screen, relative to the list's offset)
    uint16_t scale = 0x100;     // R8 (8.8): $19A340[depth] >> 4
    uint16_t braking = 0;       // $20907E: 2 while the brake map gives a value
    uint8_t  crash_dust = 0;    // $209136 (taken: set to 0 when the dust starts)
    uint8_t  crash_snd = 0;     // $209134 (chooses the dust's frames)
    bool     marker = false;    // $20831C = 0 and the car's countdown 10[$200400] >= $3C
    uint8_t  hide = 0;          // E1[R20]: wheels masked
    uint16_t monitor = 0;       // 1A[R25]: or'ed into each entry's word 3 (0 for monitor A)
};

class CarSprite
{
public:
    explicit CarSprite(const uint8_t* rom) : rom(rom) {}
    void init(uint8_t car);                     // $97FC-$99B3: this car's fields (and its $14374 objects)
    // $99B8 for the player's car: the entries (8 words each, as written to sprite RAM) appended to out
    void draw(const Record& rec, SpriteInput& in, std::vector<uint16_t>& out);

    // the object's fields ($201800 +)
    uint8_t  car = 0;           // +0A
    uint8_t  wheels_off = 0;    // +2E
    uint16_t body_flags = 0;    // +84
    uint32_t acc = 0;           // +88 (dword): the distance toggle's accumulator; bit 16 (+8A bit 0) the toggle,
                                //     its top byte (+8B) the toggle's last value
    uint16_t img_base = 0, frame_base = 0;     // +8C, +8E
    struct Wheel { uint32_t table = 0; uint16_t frame = 0, pal = 0; uint8_t mask = 0, count = 0, limit = 0; };
    Wheel    wheel[2];          // +90, +A0: table, frame base, palette index, mask, count, limit
    uint16_t dust_y = 0;        // +B0
    uint8_t  dust_count = 0, dust_step = 0;    // +B2, +B3
    uint16_t dust_frame = 0, dust_pal = 0;     // +B4, +B6
    uint8_t  bf = 0;            // +BF: the dust under the car
    struct Smoke { uint32_t frames = 0, pos = 0; uint8_t which = 0, count = 0, mask = 0, kind = 0; };
    Smoke    smoke[2];          // +C0, +D0: frame table, positions, wheel kind (+51/+52), count, mask, kind
    uint8_t  row = 0;           // +E3
    uint8_t  flag137 = 0;       // $209137: $FF when the wheels went (the car's own)
    // The two objects the init spawns with routine $14374 (A128/A148[car]): when the body shows a frame
    // without its wheels (+2E set), each plays an animation (the wheels flying off, drawn by the generic
    // $ED50: not drawn here) and at its end sets +2E to its own +A: the first $FF, the second 0 (the
    // wheels are back). They run after the sprite object in the frame: after(). The two $14460 objects
    // switch themselves off for MAD POWER ($146BA[0] = 0) and are not transcribed.
    struct Fly { uint8_t state = 0, idx = 0, a = 0; uint16_t frame = 0; uint8_t step = 0, rate = 0, count = 0; };
    Fly      fly[2];
    void after();

private:
    const uint8_t* rom;
    uint8_t  r8_(uint32_t a) const  { return rom[a & 0x1FFFFF]; }
    uint16_t r16(uint32_t a) const { return (uint16_t)(r8_(a) | r8_(a + 1) << 8); }
    uint32_t r32(uint32_t a) const { return (uint32_t) r16(a) | (uint32_t) r16(a + 2) << 16; }
    // $9F0A / $9F22: one piece of a frame (the entry words); returns word 0
    uint32_t piece_ptr = 0, pal_ptr = 0;
    uint16_t piece_start(uint16_t frame, uint16_t pal, uint8_t sub, int16_t x, int16_t y, uint16_t scale, uint16_t mon, std::vector<uint16_t>& out);
    uint16_t piece_next(int16_t x, int16_t y, uint16_t scale, uint16_t mon, std::vector<uint16_t>& out);
    void wheel_piece(uint32_t table, uint16_t frame, uint16_t pal, uint8_t sub, int16_t bx, int16_t by, uint16_t rowoff,
                     const SpriteInput& in, std::vector<uint16_t>& out);         // $9EDD
    void smoke_piece(Smoke& sm, const Record& rec, int16_t bx, int16_t by, uint16_t rowoff,
                     const SpriteInput& in, std::vector<uint16_t>& out);         // $9E8A
};

// The System 32 sprite entries drawn as the board draws them (see the header)
class S32Sprites
{
public:
    // rom: the main CPU's 2 MB; sprites: the 16 MB sprite region as MAME holds it (uint32, big-endian values)
    S32Sprites(const uint8_t* rom, const uint32_t* sprites, size_t sprite_words);
    // Draws entries (8 words each; a jump's offsets xoffs/yoffs) into a w x h image of 15-bit
    // palette indices (bit 15 set; 0 = nothing drawn), clipped to it
    void draw(const uint16_t* entries, int n, int xoffs, int yoffs, uint16_t* img, int w, int h) const;
    // The mixer's colour of a pixel: 0x00RRGGBB; false for a shadow pixel (the layer below halved)
    bool colour(uint16_t pix, uint32_t& rgb) const;

private:
    const uint8_t* rom;
    const uint32_t* spr;
    size_t spr_words;
    uint16_t pal[0x1000];       // palette RAM $000-$FFF (xBBBBBGGGGGRRRRR), the entries the car uses loaded
    uint16_t ind[0x1200];       // sprite RAM $401000-$4033FF (words $800-$19FF): the indirect tables
    void draw_one(const uint16_t* d, int xoffs, int yoffs, uint16_t* img, int w, int h) const;
};

} // namespace orcar
