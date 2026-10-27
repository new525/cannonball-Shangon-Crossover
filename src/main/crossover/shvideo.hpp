/***************************************************************************
    Screen of the emulated Super Hang-On board (crossover/shboard).

    Transcribed from MAME 0.264 (BSD-3-Clause, Aaron Giles and the MAME team):
    segaorun_v.cpp screen_update_shangon (layer order and sprite mixing), segaic16.cpp (System 16B
    tilemaps in the TILEMAP_16B_ALT format, 16 pages of 64x32 tiles, row/column scroll and the
    alternate tilemaps; text layer; palette weights), segaic16_road.cpp (ROAD_OUTRUN with
    Super Hang-On's colour bases $7F6/$7C0/$7C0) and sega16sp.cpp (System 16B sprites).

    Output: 320x224 pixels (398x224 in widescreen) as palette indices 0-$FFF, plus $1000 for shadowed/highlighted
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
    static const int W = 320, H = 224, MAXW = 398;     // MAXW: Cannonball's widescreen width

    // Widescreen (as Cannonball widens OutRun): the game's 320-pixel screen stays in the middle;
    // road, tilemaps and sprites extend into the extra width, the text layer does not.
    void set_width(int w);
    int width = W, xoff = 0;

    // Another game's sprites drawn into the frame (crossover/shferrari): a width x H buffer from
    // OutRun's sprite renderer (0 = nothing, OutRun colour $800-$FFF, +$1000 = shadowed,
    // $1000 alone = shadow), put into the sprite bitmap in place of the one sprite entry the
    // game's list marks with the tag (hide bit + address TAG_ADDR), with that entry's priority.
    // Its pixels come out as $2000 + OutRun colour ($2800-$2FFF), shadows as this board's.
    static const uint16_t TAG_ADDR = 0xFE7A;
    const uint16_t* overlay = nullptr;
    bool overlay_placed = false;            // the tagged entry was in the last frame's list

    bool init(const RomSet& roms);
    void render(SHBoard& b);                // into pixels[]
    uint16_t pixels[MAXW * H];                 // palette index (+$1000 = shadow/highlight entry, $2000+ = overlay)

    // RGB of a palette index (MAME's resistor weights), from the board's palette RAM
    uint32_t rgb(const SHBoard& b, uint16_t index) const;
    void     palette_rgb(const SHBoard& b, uint32_t* out) const;  // 8192 entries, 0x00RRGGBB

private:
    std::vector<uint8_t>  tiles;            // decoded 8x8 tiles, one byte per pixel (pen 0-7)
    int                   num_tiles = 0;
    std::vector<uint8_t>  road;             // decoded road rows: (512 + 1) x 512
    std::vector<uint16_t> sprites;          // sprite ROM as 16-bit words
    uint8_t  pal_normal[32], pal_shadow[32], pal_hilight[32];
    uint8_t  pri[MAXW * H];                    // priority bitmap
    uint16_t spr[MAXW * H];                    // sprite bitmap ($FFFF = empty)
    bool     ok = false;

    void draw_road(SHBoard& b, bool foreground);
    void draw_tile_layer(SHBoard& b, int which, int category, uint8_t mark);
    void draw_virtual(SHBoard& b, int x0, int x1, int y0, int y1, uint16_t pages,
                      uint16_t xscroll, uint16_t yscroll, int category, uint8_t mark);
    void draw_text(SHBoard& b, int category, uint8_t mark);
    void draw_sprites(SHBoard& b);
};

extern SHVideo shvideo;
