/***************************************************************************
    Screen of the emulated Hang-On board (crossover/hoboard).

    Transcribed from MAME 0.264 (BSD-3-Clause, Aaron Giles and the MAME team):
    segahang_v.cpp screen_update (layer order and Hang-On sprite mixing), segaic16.cpp
    (TILEMAP_HANGON: 16A-style tilemaps of 4 pages, row/column scroll from the PPI, text layer;
    hangon_paletteram_w with separate shadow and highlight banks), segaic16_road.cpp
    (ROAD_HANGON, colour bases $038/$7C0/$7C0) and sega16sp.cpp (Hang-On sprites with the zoom
    ROM, origin 189, -1).

    Output: 320x224 pixels (398x224 in widescreen) as indices into 6144 colours: palette entries
    $000-$7FF, shadowed $800-$FFF, highlighted $1000-$17FF.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include <vector>

class HOBoard;
class RomSet;

class HOVideo
{
public:
    static const int W = 320, H = 224, MAXW = 398;

    void set_width(int w);
    int width = W, xoff = 0;

    bool init(const RomSet& roms);
    void render(HOBoard& b);
    uint16_t pixels[MAXW * H];

    uint32_t rgb(const HOBoard& b, uint16_t index) const;
    void     palette_rgb(const HOBoard& b, uint32_t* out) const;   // 0x2000 entries (0x1800-0x1FFF black)

    // Another game's sprites drawn into the frame (as SHVideo::overlay)
    // An entry whose sprite data address is TAG_ADDR (while tag_active) is that point of the list: the
    // overlay is drawn there, with the entry's priority; with no overlay the entry is not drawn
    static const uint16_t TAG_ADDR = 0xFE7A;
    bool tag_active = false;
    const uint16_t* overlay = nullptr;
    bool overlay_placed = false;

private:
    std::vector<uint8_t>  tiles;
    int                   num_tiles = 0;
    std::vector<uint8_t>  road;             // 256 rows x 512
    std::vector<uint16_t> sprites;
    std::vector<uint8_t>  zoom;
    uint8_t  pal_normal[32], pal_shadow[32], pal_hilight[32];
    uint8_t  pri[MAXW * H];
    uint16_t spr[MAXW * H];
    bool     ok = false;

    void draw_road(HOBoard& b, bool foreground);
    void draw_tile_layer(HOBoard& b, int which, int category, uint8_t mark);
    void draw_virtual(HOBoard& b, int x0, int x1, int y0, int y1, uint16_t pages,
                      uint16_t xscroll, uint16_t yscroll, int category, uint8_t mark);
    void draw_text(HOBoard& b, int category, uint8_t mark);
    void draw_sprites(HOBoard& b);
};

extern HOVideo hovideo;
