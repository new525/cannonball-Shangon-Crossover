/***************************************************************************
    Screen of the emulated Super Hang-On board (crossover/shboard).

    Transcribed from MAME 0.264 (BSD-3-Clause, Aaron Giles and the MAME team):
    segaorun_v.cpp screen_update_shangon (layer order and sprite mixing), segaic16.cpp (System 16B
    tilemaps in the TILEMAP_16B_ALT format, 16 pages of 64x32 tiles, row/column scroll and the
    alternate tilemaps; text layer; palette weights), segaic16_road.cpp (ROAD_OUTRUN with
    Super Hang-On's colour bases $7F6/$7C0/$7C0) and sega16sp.cpp (System 16B sprites).

    Output: 320x224 pixels as palette indices 0-$FFF, plus $1000 for shadowed/highlighted
    pixels (MAME's second palette half), and the matching RGB values.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include <vector>

class SHBoard;
class RomSet;

class SHVideo
{
public:
    static const int W = 320, H = 224;

    bool init(const RomSet& roms);
    void render(SHBoard& b);                // into pixels[]
    uint16_t pixels[W * H];                 // palette index (+$1000 = shadow/highlight entry)

    // RGB of a palette index (MAME's resistor weights), from the board's palette RAM
    uint32_t rgb(const SHBoard& b, uint16_t index) const;
    void     palette_rgb(const SHBoard& b, uint32_t* out) const;  // 8192 entries, 0x00RRGGBB

private:
    std::vector<uint8_t>  tiles;            // decoded 8x8 tiles, one byte per pixel (pen 0-7)
    int                   num_tiles = 0;
    std::vector<uint8_t>  road;             // decoded road rows: (512 + 1) x 512
    std::vector<uint16_t> sprites;          // sprite ROM as 16-bit words
    uint8_t  pal_normal[32], pal_shadow[32], pal_hilight[32];
    uint8_t  pri[W * H];                    // priority bitmap
    uint16_t spr[W * H];                    // sprite bitmap ($FFFF = empty)
    bool     ok = false;

    void draw_road(SHBoard& b, bool foreground);
    void draw_tile_layer(SHBoard& b, int which, int category, uint8_t mark);
    void draw_virtual(SHBoard& b, int x0, int x1, int y0, int y1, uint16_t pages,
                      uint16_t xscroll, uint16_t yscroll, int category, uint8_t mark);
    void draw_text(SHBoard& b, int category, uint8_t mark);
    void draw_sprites(SHBoard& b);
};

extern SHVideo shvideo;
