/***************************************************************************
    Video of the emulated Sega System Multi 32 board (OutRunners, MAME set orunnersj).

    Transcribed from MAME 0.264 (license:BSD-3-Clause, copyright-holders:Aaron Giles):
    src/mame/sega/segas32_v.cpp (all of the video: tilemap cache, NBG0-3, text, bitmap and
    background layers, sprite list renderer, mixer, screen update), the video parts of
    src/mame/sega/segas32.cpp and segas32.h (palette and mixer handlers, the multi32_map memory
    map, the sega_multi32_state machine config: screens, gfx decode, I/O chip video outputs,
    the VBLANK interrupt and sprite timers), and the subset of MAME's core that System 32 uses
    (src/emu/tilemap.cpp: the cached tilemap pixmaps; src/emu/drawgfx.cpp gfx_element::decode;
    src/emu/romload.cpp: ROM_LOAD16_BYTE / ROM_LOAD64_WORD / ROM_REGION32_BE; src/emu/screen.cpp
    and video.cpp: when the screens are updated and what a frame shows). See s32video.LICENSE.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.

    Lines marked "PORT:" in s32video.cpp are the only places where the code differs from MAME.
    Function and member names are MAME's, so the two can be read side by side.

    ---------------------------------------------------------------------------------------
    Memory map (MAME multi32_map). The V70 bus is 32 bits wide and little-endian. MAME calls
    a 16-bit handler once per 16-bit half of a 32-bit access whose mem_mask half is nonzero:
    the half at the lower address (bits 0-15 of the dword) with offset 2n, the upper half
    (bits 16-31) with offset 2n+1, each with its 16-bit part of the mem_mask. A byte access
    calls it with mem_mask 0x00ff (even byte address) or 0xff00 (odd byte address).

      $300000-$31FFFF  mirror $0E0000  videoram_r/videoram_w        offset = (addr & $1FFFF) >> 1
      $400000-$41FFFF  mirror $0E0000  spriteram_r/spriteram_w      offset = (addr & $1FFFF) >> 1
      $500000-$50000F  mirror $0FFFF0  sprite_control_r/_w, 8 bits, umask32($00FF00FF):
                       byte lane 0 (bits 0-7) of dword n is offset 2n, lane 2 (bits 16-23)
                       is offset 2n+1, i.e. offset = (addr & $F) >> 1 for addr & 1 == 0.
                       Lanes 1 and 3 are not connected (MAME's map is unmap_value_high: a
                       read_u32 returned $FF there in the reference dumps).
      $600000-$60FFFF  mirror $060000  paletteram_r/_w(0, ...)      offset = (addr & $FFFF) >> 1
      $610000-$61007F  mirror $06FF80  mixer_r/_w(0, ...)           offset = (addr & $7F) >> 1
      $680000-$68FFFF  mirror $060000  paletteram_r/_w(1, ...)      offset = (addr & $FFFF) >> 1
      $690000-$69007F  mirror $06FF80  mixer_r/_w(1, ...)           offset = (addr & $7F) >> 1

    I/O chip outputs (two Sega 315-5296, MAME sega_multi32_state::device_add_mconfig):
      io_chip_0 ($C00000) port H output   -> tilebank_external_w(data)
      io_chip_0 CNT1 output              -> display_enable_w(0, state)   (left screen)
      io_chip_1 ($C80000) CNT1 output    -> display_enable_w(1, state)   (right screen)
    (315-5296: a write to port H calls out_ph only while port H is an output (direction
    register bit 7); a direction change calls it with the latch, or 0 for input. A write to
    the CNT register (reg $E) calls out_cnt1 with bit 1.)

    ---------------------------------------------------------------------------------------
    Timing (MAME: two raster screens, 60 Hz, 416 x 262 total, visible lines 0-223; VBLANK
    is lines 224-261, i.e. 38 of the 262 lines of a 1/60 s frame). For each frame:

      1. VBLANK start (line 224), primary screen's vblank_begin:
           screen_update(0)   - left screen ("screen")
           screen_update(1)   - right screen ("screen2")
         (video_manager::frame_update -> finish_screen_updates, in device order), then
         MAME calls start_of_vblank_int: the V60/V70 VBLANK-start IRQ (the board's) and
           system32_set_vblank(1)      - no effect on the video
         and arms the end-of-VBLANK timer for screen time_until_pos(0) (the start of line 0
         of the next frame, 38 lines later).
      2. VBLANK end (line 0), end_of_vblank_int: the VBLANK-end IRQ (the board's), then
           system32_set_vblank(0)      - arms MAME's update_sprites timer: 50 us later
      3. 50 us after step 2:
           update_sprites()            - automatic/manual sprite buffer erase, swap, render
         (MAME timer adjust(): if step 2 happens again before this fires, the earlier call
         is dropped.)
    No CPU execution happens between the screen updates and step 1's IRQ. The V70 must be run
    up to each of these instants before the call (as MAME's scheduler does).

    What a frame shows: the tilemaps, text, bitmap, background, palette and mixer as they are
    at step 1; the sprites from the front sprite buffer, i.e. the list that the previous-but-one
    update_sprites() rendered (update_sprites swaps first, then renders the new list into the
    back buffer). With OutRunners' automatic 60 Hz mode that is spriteram as it was 50 us
    after the VBLANK end that began the previous frame.

    Output: screen_pixels(which) after screen_update(which): visible_width(which) x 224
    pixels, 0x00RRGGBB, each channel the mixer's 5 bits << 3 (as MAME's bitmap_rgb32).
    The width is 320 or 416 ($31FF00 bit 15); as in MAME, a change takes effect at the end
    of the screen update that sees it (that frame is drawn with the old cliprect).
***************************************************************************/
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class S32Video
{
public:
    // MAME's rectangle (src/lib/util/bitmap.h), inclusive bounds
    struct rectangle
    {
        int32_t min_x = 0, max_x = 0, min_y = 0, max_y = 0;
        rectangle() = default;
        rectangle(int32_t minx, int32_t maxx, int32_t miny, int32_t maxy) : min_x(minx), max_x(maxx), min_y(miny), max_y(maxy) { }
        rectangle& operator&=(const rectangle& src)
        {
            if (src.min_x > min_x) min_x = src.min_x;
            if (src.max_x < max_x) max_x = src.max_x;
            if (src.min_y > min_y) min_y = src.min_y;
            if (src.max_y < max_y) max_y = src.max_y;
            return *this;
        }
    };

    static const int SCREEN_W_MAX = 52 * 8;     // 416
    static const int SCREEN_H     = 28 * 8;     // 224
    static const int SCREEN_TOTAL_H = 262;

    // ---- ROM regions -------------------------------------------------------------------
    // Builds MAME's regions for orunnersj (segas32.cpp ROM_START( orunnersj )) from the ROM
    // files in `dir` (MAME file names):
    //   gfx1:    0x400000 bytes, ROM_LOAD16_BYTE mpr-15548.ic3 (even bytes), mpr-15549.ic11 (odd)
    //   sprites: 0x1000000 bytes, ROM_REGION32_BE, ROM_LOAD64_WORD mpr-15540/15542/15544/15546
    //            at 0x000000/2/4/6 and mpr-15541/15543/15545/15547 at 0x800000/2/4/6,
    //            returned as MAME holds it on a little-endian host: 32-bit words, each the
    //            big-endian value of 4 region bytes (romload byte-swaps BE regions).
    static bool load_orunnersj_regions(const std::string& dir, std::vector<uint8_t>& gfx1,
                                       std::vector<uint32_t>& sprites, std::string& error);

    // ---- device_start ------------------------------------------------------------------
    // gfx1 is decoded (copied); `sprites` is referenced, not copied, and must outlive this
    // object (MAME's required_region_ptr<uint32_t> m_sprite_region; words = length()).
    bool init(const uint8_t* gfx1, uint32_t gfx1_bytes, const uint32_t* sprites, uint32_t sprite_words);

    // ---- memory handlers (MAME signatures; offsets as in the memory map above) --------
    uint16_t videoram_r(uint32_t offset);
    void     videoram_w(uint32_t offset, uint16_t data, uint16_t mem_mask = 0xffff);
    uint8_t  sprite_control_r(uint32_t offset);
    void     sprite_control_w(uint32_t offset, uint8_t data);
    uint16_t spriteram_r(uint32_t offset);
    void     spriteram_w(uint32_t offset, uint16_t data, uint16_t mem_mask = 0xffff);
    uint16_t paletteram_r(int which, uint32_t offset);                                   // paletteram_r<Which>
    void     paletteram_w(int which, uint32_t offset, uint16_t data, uint16_t mem_mask = 0xffff);
    uint16_t mixer_r(int which, uint32_t offset);                                        // mixer_r<Which>
    void     mixer_w(int which, uint32_t offset, uint16_t data, uint16_t mem_mask = 0xffff);

    // ---- I/O chip outputs ----------------------------------------------------------------
    void tilebank_external_w(uint8_t data);
    void display_enable_w(int which, int state);        // display_enable_w<Which>

    // ---- timing (see the header comment) ---------------------------------------------------
    // Returns true when MAME arms the update_sprites timer (state == 0): the board must then
    // call update_sprites() 50 us of emulated time later.
    bool system32_set_vblank(int state);
    void update_sprites();                                // TIMER_CALLBACK update_sprites
    uint32_t screen_update(int which);                    // screen_update_multi32_left/right

    // ---- output ---------------------------------------------------------------------------
    int visible_width(int which) const { return m_visarea[which].max_x + 1; }
    const uint32_t* screen_pixels(int which) const;      // last drawn frame, row stride SCREEN_W_MAX
    void get_frame(int which, uint32_t* dst) const;      // visible_width x 224, 0x00RRGGBB

    // ---- the separate drawing steps (screen_update = update_tilemaps + mix_all_layers) -----
    // Layer indices (MAME's MIXER_LAYER_*)
    enum
    {
        MIXER_LAYER_TEXT = 0, MIXER_LAYER_NBG0, MIXER_LAYER_NBG1, MIXER_LAYER_NBG2, MIXER_LAYER_NBG3,
        MIXER_LAYER_BITMAP, MIXER_LAYER_SPRITES, MIXER_LAYER_BACKGROUND,
        MIXER_LAYER_SPRITES_2,      // semi-kludge to have a frame buffer for sprite backlayer
        MIXER_LAYER_MULTISPR, MIXER_LAYER_MULTISPR_2,
        NUM_LAYERS
    };
    struct layer_info
    {
        std::vector<uint16_t> bitmap;           // 416 x 224 (bitmap_ind16), row stride 416
        std::vector<uint8_t>  transparent;      // 256 entries, per scanline
        int num = 0;
        uint16_t* pix(int y, int x = 0) { return &bitmap[(size_t) y * SCREEN_W_MAX + x]; }
    };
    layer_info m_layer_data[NUM_LAYERS];

    // `screen` selects the visible area used for flipping (MAME's screen_device argument)
    uint8_t update_tilemaps(int screen, const rectangle& cliprect);
    void    sprite_erase_buffer();
    void    sprite_swap_buffers();
    void    sprite_render_list();
    void    mix_all_layers(int which, int xoffs, uint32_t* bitmap, int rowpixels, const rectangle& cliprect, uint8_t enablemask);

    // ---- raw state (MAME's shared memories and members; for tests and save states) ------
    // Writing these directly skips the handlers' side effects; call state_loaded() after.
    uint16_t m_videoram[0x20000 / 2];
    uint16_t m_spriteram[0x20000 / 2];
    uint16_t m_paletteram[2][0x4000];         // PORT: MAME allocates 0x10000 bytes, uses 0x4000 words
    uint16_t m_mixer_control[2][0x40];
    uint8_t  m_sprite_control_latched[8];
    uint8_t  m_sprite_control[8];
    uint8_t  m_sprite_render_count = 0;
    uint16_t m_system32_displayenable[2];
    uint16_t m_system32_tilebank_external = 0;
    void state_loaded();                       // rebuild spriteram_32bit, mark all tilemaps dirty

    // ---- crossover: another game's sprites in monitor A's sprite layer (PORT: not in MAME) -----
    // An entry of the sprite list with word 6 = OVL_TAG and word 1 = 0 (no source data, so MAME
    // draws nothing for it) marks the place in the list where an image from overlay_source (asked
    // for with the entry's word 2) is drawn into monitor A's sprite layer: in list order, clipped
    // as a sprite there would be, with the entry's palette word (word 7: its priority group). The
    // entry's X/Y (words 5/4, with word 0's X/Y offset flags, as for a sprite) is where the image's
    // reference point goes. Word 2: bits 0-3 the image asked for, bits 4-15 its scale x 1024 (the
    // image is zoomed about its reference point, nearest pixel). Image pixels: 0 transparent;
    // OVL_SHADOW a shadow; otherwise $8000 | the colour as palette RAM holds it (xBBBBBGGGGGRRRRR),
    // which then goes through the mixer as the sprite pixel's palette colour would (layer
    // priority, colour offsets, blending, shadows).
    // A later sprite drawn over an image pixel replaces it, as it would replace a sprite pixel.
    // While overlay_source is empty nothing of this runs and the video is MAME's.
    struct OverlayImage { const uint16_t* px = nullptr; int w = 0, h = 0, ref_x = 0, ref_y = 0; };
    std::function<bool(uint16_t id, OverlayImage& img)> overlay_source;
    static const uint16_t OVL_TAG = 0x0C5E, OVL_SHADOW = 0x0001;
    void set_overlay(std::function<bool(uint16_t, OverlayImage&)> src);   // sets or (empty) clears it

private:
    struct extents_list
    {
        uint8_t  scan_extent[256]{};
        uint16_t extent[32][16]{};
    };

    // PORT: the subset of MAME's tilemap_t that System 32 uses (TILEMAP_SCAN_ROWS, 16x16
    // tiles, 32x16, pixmap() only)
    struct tilemap
    {
        std::vector<uint16_t> pixmap;           // 512 x 256
        uint8_t  dirty[32 * 16];
        bool     all_tiles_dirty = true;
        bool     all_tiles_clean = false;
        void*    user_data = nullptr;
        void mark_tile_dirty(uint32_t memindex) { if (memindex < 32 * 16) { dirty[memindex] = 1; all_tiles_clean = false; } }
        void mark_all_dirty() { all_tiles_dirty = true; all_tiles_clean = false; }
    };
    struct cache_entry
    {
        cache_entry* next = nullptr;
        tilemap*     tmap = nullptr;
        uint8_t      page = 0;
        uint8_t      bank = 0;
    };
    static constexpr int TILEMAP_CACHE_SIZE = 32;

    // gfx (MAME gfx_segas32: bgcharlayout, 16x16 4bpp, colorbase 0, 0x3ff colors)
    std::vector<uint8_t> m_gfxdata;            // decoded, 256 bytes per tile
    uint32_t m_gfx_elements = 0;
    const uint32_t* m_sprite_region = nullptr;
    uint32_t m_sprite_region_length = 0;       // in uint32 units

    std::vector<uint32_t> m_spriteram_32bit;
    tilemap m_tilemaps[TILEMAP_CACHE_SIZE];
    cache_entry m_tmap_cache[TILEMAP_CACHE_SIZE];
    cache_entry* m_cache_head = nullptr;
    uint16_t m_solid_0000[512];
    uint16_t m_solid_ffff[512];
    int32_t  m_prev_bgstartx[512];
    int32_t  m_prev_bgendx[512];
    int32_t  m_bgcolor_line[512];

    // PORT: the parts of MAME's two screen_devices that the update uses
    rectangle m_visarea[2];
    std::vector<uint32_t> m_screen_bitmap[2][2]; // screen_device m_bitmap[2], 416 x 262
    int m_curbitmap[2] = { 0, 0 };
    int m_curtexture[2] = { 0, 0 };

    bool m_ok = false;

    // crossover overlay (see above): per pixel of monitor A's two sprite buffers, 0 or the image
    // pixel drawn there; front = MIXER_LAYER_SPRITES (shown), back = MIXER_LAYER_SPRITES_2 (drawn)
    std::vector<uint16_t> m_ovl_front, m_ovl_back;
    bool m_ovl_on = false;
    void draw_overlay(uint16_t const* data, int xoffs, int yoffs, const rectangle& clipin, const rectangle& clipout);

    void pixmap_update(tilemap& tm);
    const uint16_t* tilemap_pixmap(tilemap* tm) { pixmap_update(*tm); return tm->pixmap.data(); }
    void get_tile_info(tilemap& tm, uint32_t tile_index, uint32_t& code, uint32_t& color, uint8_t& flags);
    tilemap* find_cache_entry(int page, int bank);
    void get_tilemaps(int bgnum, tilemap** tilemaps);
    int  compute_clipping_extents(int screen, int enable, int clipout, int clipmask, const rectangle& cliprect, extents_list* list);
    void compute_tilemap_flips(int bgnum, int& flipx, int& flipy);
    void update_tilemap_zoom(int screen, layer_info& layer, const rectangle& cliprect, int bgnum);
    void update_tilemap_rowscroll(int screen, layer_info& layer, const rectangle& cliprect, int bgnum);
    void update_tilemap_text(int screen, layer_info& layer, const rectangle& cliprect);
    void update_bitmap(int screen, layer_info& layer, const rectangle& cliprect);
    void update_background(layer_info& layer, const rectangle& cliprect);
    int  draw_one_sprite(uint16_t const* data, int xoffs, int yoffs, const rectangle& clipin, const rectangle& clipout);
    uint8_t  compute_color_offsets(int which, int layerbit, int layerflag);
    uint16_t compute_sprite_blend(uint8_t encoding);
    uint16_t* get_layer_scanline(int layer, int scanline);
    uint32_t multi32_update(int which, const rectangle& cliprect);
};
