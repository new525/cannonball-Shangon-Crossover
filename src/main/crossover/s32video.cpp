/***************************************************************************
    Video of the emulated Sega System Multi 32 board. See s32video.hpp.

    license:BSD-3-Clause
    copyright-holders:Aaron Giles
    (MAME 0.264 src/mame/sega/segas32_v.cpp and the video parts of segas32.cpp;
    see s32video.LICENSE)

    The code below follows MAME's segas32_v.cpp function by function, in the same order,
    with MAME's comments. Deviations are marked "PORT:".
***************************************************************************/
#include "crossover/s32video.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

// definitions of the class constants (used by reference in unoptimised builds)
const int S32Video::SCREEN_W_MAX, S32Video::SCREEN_H, S32Video::SCREEN_TOTAL_H;
constexpr int S32Video::TILEMAP_CACHE_SIZE;

// util::sext (src/lib/util/coretmpl.h) for the uint16_t values System 32 sign-extends
static inline int16_t sext16(uint16_t value, int width)
{
    return int16_t(uint16_t(value << (16 - width))) >> (16 - width);
}

// COMBINE_DATA
static inline void combine_data(uint16_t* varptr, uint16_t data, uint16_t mem_mask)
{
    *varptr = (*varptr & ~mem_mask) | (data & mem_mask);
}


/*************************************
 *
 *  ROM regions (segas32.cpp ROM_START( orunnersj ), romload.cpp)
 *
 *************************************/

static uint32_t crc32_of(const std::vector<uint8_t>& d)
{
    uint32_t crc = 0xffffffff;
    for (uint8_t b : d)
    {
        crc ^= b;
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xedb88320 & (0 - (crc & 1)));
    }
    return ~crc;
}

static bool read_rom(const std::string& dir, const char* name, uint32_t length, uint32_t crc,
                     std::vector<uint8_t>& out, std::string& error)
{
    std::string path = dir + "/" + name;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { error = "missing " + path; return false; }
    out.assign(length, 0);
    size_t got = fread(out.data(), 1, length, f);
    int extra = fgetc(f);
    fclose(f);
    if (got != length || extra != EOF) { error = "wrong size: " + path; return false; }
    if (crc32_of(out) != crc)
    {
        char b[64];
        snprintf(b, sizeof b, " (CRC %08x, expected %08x)", crc32_of(out), crc);
        error = "wrong CRC: " + path + b;
        return false;
    }
    return true;
}

bool S32Video::load_orunnersj_regions(const std::string& dir, std::vector<uint8_t>& gfx1,
                                      std::vector<uint32_t>& sprites, std::string& error)
{
    std::vector<uint8_t> rom;

    // ROM_REGION( 0x400000, "mainpcb:gfx1", 0 ) - 8-bit wide, no byte swap
    gfx1.assign(0x400000, 0);
    static const struct { const char* name; uint32_t offset, length, crc; } gfx[] =
    {
        { "mpr-15548.ic3",  0x000000, 0x200000, 0xb6470a66 },   // ROM_LOAD16_BYTE
        { "mpr-15549.ic11", 0x000001, 0x200000, 0x81d12520 },   // ROM_LOAD16_BYTE
    };
    for (const auto& r : gfx)
    {
        if (!read_rom(dir, r.name, r.length, r.crc, rom, error)) return false;
        // ROM_LOAD16_BYTE = ROMX_LOAD(..., ROM_SKIP(1)): groupsize 1, skip 1
        for (uint32_t i = 0; i < r.length; i++)
            gfx1[r.offset + i * 2] = rom[i];
    }

    // ROM_REGION32_BE( 0x1000000, "mainpcb:sprites", 0 )
    std::vector<uint8_t> region(0x1000000, 0);
    static const struct { const char* name; uint32_t offset, length, crc; } spr[] =
    {
        { "mpr-15540.ic14", 0x000000, 0x200000, 0xa10d72b4 },   // ROM_LOAD64_WORD
        { "mpr-15542.ic15", 0x000002, 0x200000, 0x40952374 },
        { "mpr-15544.ic10", 0x000004, 0x200000, 0x39e3df45 },
        { "mpr-15546.ic38", 0x000006, 0x200000, 0xe3fcc12c },
        { "mpr-15541.ic22", 0x800000, 0x200000, 0xa2003c2d },
        { "mpr-15543.ic23", 0x800002, 0x200000, 0x933e8e7b },
        { "mpr-15545.ic18", 0x800004, 0x200000, 0x53dd0235 },
        { "mpr-15547.ic41", 0x800006, 0x200000, 0xedcb2a43 },
    };
    for (const auto& r : spr)
    {
        if (!read_rom(dir, r.name, r.length, r.crc, rom, error)) return false;
        // ROM_LOAD64_WORD = ROMX_LOAD(..., ROM_GROUPWORD | ROM_SKIP(6)): groups of 2 bytes
        // copied in file order (not reversed), then 6 bytes skipped
        for (uint32_t g = 0; g < r.length / 2; g++)
        {
            region[r.offset + g * 8 + 0] = rom[g * 2 + 0];
            region[r.offset + g * 8 + 1] = rom[g * 2 + 1];
        }
    }
    // romload region_post_process: a 32-bit big-endian region is byte-swapped on a
    // little-endian host, so MAME's uint32_t reads give the big-endian values
    sprites.assign(region.size() / 4, 0);
    for (size_t i = 0; i < sprites.size(); i++)
        sprites[i] = uint32_t(region[i * 4]) << 24 | uint32_t(region[i * 4 + 1]) << 16 |
                     uint32_t(region[i * 4 + 2]) << 8 | region[i * 4 + 3];
    return true;
}


/*************************************
 *
 *  Video start
 *
 *************************************/

bool S32Video::init(const uint8_t* gfx1, uint32_t gfx1_bytes, const uint32_t* sprites, uint32_t sprite_words)
{
    m_ok = false;

    // PORT: MAME's gfxdecode device decodes gfx1 with gfx_segas32 / bgcharlayout lazily
    // (gfx_element::decode on first use); here every element is decoded up front.
    {
        static const uint32_t planeoffset[4] = { 0, 1, 2, 3 };                       // STEP4(0,1)
        static const uint32_t xoffset[16] = { 0, 4, 16, 20, 8, 12, 24, 28, 32, 36, 48, 52, 40, 44, 56, 60 };
        static const int planes = 4, width = 16, height = 16;
        static const uint32_t charincrement = 16 * 64;
        uint32_t yoffset[16];
        for (int i = 0; i < 16; i++) yoffset[i] = i * 4 * 16;                           // STEP16(0,4*16)

        // RGN_FRAC(1,1): total = region_length (bits) / charincrement
        m_gfx_elements = (uint32_t) ((uint64_t) gfx1_bytes * 8 / charincrement);
        if (m_gfx_elements == 0) return false;
        m_gfxdata.assign((size_t) m_gfx_elements * width * height, 0);
        for (uint32_t code = 0; code < m_gfx_elements; code++)
        {
            uint8_t* decode_base = &m_gfxdata[(size_t) code * width * height];
            int plane, planebit;
            for (plane = 0, planebit = 1 << (planes - 1); plane < planes; plane++, planebit >>= 1)
            {
                uint32_t planeoffs = code * charincrement + planeoffset[plane];
                for (int y = 0; y < height; y++)
                {
                    uint32_t yoffs = planeoffs + yoffset[y];
                    uint8_t* dp = decode_base + y * width;
                    for (int x = 0; x < width; x++)
                    {
                        uint32_t bitnum = yoffs + xoffset[x];               // m_layout_xormask = 0
                        if (gfx1[bitnum / 8] & (0x80 >> (bitnum % 8)))     // readbit
                            dp[x] |= planebit;
                    }
                }
            }
        }
    }

    m_sprite_region = sprites;
    m_sprite_region_length = sprite_words;

    /* allocate a copy of spriteram in 32-bit format */
    m_spriteram_32bit.assign(0x20000 / 4, 0);

    /* allocate the tilemap cache */
    m_cache_head = nullptr;
    for (int tmap = 0; tmap < TILEMAP_CACHE_SIZE; tmap++)
    {
        cache_entry* entry = &m_tmap_cache[tmap];

        // tilemap create(..., TILEMAP_SCAN_ROWS, 16,16, 32,16): all tiles dirty
        tilemap& tm = m_tilemaps[tmap];
        tm.pixmap.assign(512 * 256, 0);
        memset(tm.dirty, 0, sizeof(tm.dirty));
        tm.all_tiles_dirty = true;
        tm.all_tiles_clean = false;

        entry->tmap = &tm;
        entry->page = 0xff;
        entry->bank = 0;
        entry->next = m_cache_head;
        entry->tmap->user_data = entry;

        m_cache_head = entry;
    }

    /* allocate the bitmaps (a few extra for multi32) */
    for (int bmap = 0; bmap < 9 + 2; bmap++)
    {
        m_layer_data[bmap].bitmap.assign(416 * 224, 0);        // bitmap_t::allocate clears to 0
        m_layer_data[bmap].transparent.assign(256, 0);
        m_layer_data[bmap].num = bmap;
    }

    /* allocate pre-rendered solid lines of 0's and ffff's */
    std::fill_n(m_solid_0000, 512, 0);
    std::fill_n(m_solid_ffff, 512, ~uint16_t(0));

    /* allocate background color per line*/
    std::fill_n(&m_prev_bgstartx[0], 512, -1);
    std::fill_n(&m_prev_bgendx[0], 512, -1);
    std::fill_n(&m_bgcolor_line[0], 512, -1);

    /* initialize videoram */
    memset(m_videoram, 0x00, 0x20000);
    m_videoram[0x1ff00 / 2] = 0x8000;

    memset(m_mixer_control, 0xff, sizeof(m_mixer_control[0][0]) * 0x80);

    // shared memories are zero-filled by memory_manager::allocate_memory; members as in
    // the segas32_state constructor
    memset(m_spriteram, 0, sizeof(m_spriteram));
    memset(m_paletteram, 0, sizeof(m_paletteram));
    memset(m_sprite_control_latched, 0, sizeof(m_sprite_control_latched));
    memset(m_sprite_control, 0, sizeof(m_sprite_control));
    m_sprite_render_count = 0;
    m_system32_displayenable[0] = m_system32_displayenable[1] = 0;
    m_system32_tilebank_external = 0;

    // PORT: the two screen_devices: set_visarea(0*8, 52*8-1, 0*8, 28*8-1), bitmaps of
    // set_size(52*8, 262) (allocated cleared), double-buffered (m_bitmap[2])
    for (int s = 0; s < 2; s++)
    {
        m_visarea[s] = rectangle(0, 52 * 8 - 1, 0, 28 * 8 - 1);
        for (int b = 0; b < 2; b++)
            m_screen_bitmap[s][b].assign((size_t) SCREEN_W_MAX * SCREEN_TOTAL_H, 0);
        m_curbitmap[s] = 0;
        m_curtexture[s] = 0;
    }

    m_ok = true;
    return true;
}

void S32Video::state_loaded()
{
    for (uint32_t offset = 0; offset < 0x20000 / 2; offset += 2)
        m_spriteram_32bit[offset >> 1] =
            ((m_spriteram[offset |  1] >> 8 ) & 0x000000ff) |
            ((m_spriteram[offset |  1] << 8 ) & 0x0000ff00) |
            ((m_spriteram[offset & ~1u] << 8 ) & 0x00ff0000) |
            ((uint32_t(m_spriteram[offset & ~1u]) << 24) & 0xff000000);
    for (auto& tm : m_tilemaps)
        tm.mark_all_dirty();
}


/*************************************
 *
 *  Sprite management
 *
 *************************************/

void S32Video::update_sprites()
{
    /* if automatic mode is selected, do it every frame (0) or every other frame (1) */
    if (!(m_sprite_control[3] & 2))
    {
        /* if we count down to the start, process the automatic swapping, but only after a short delay */
        if (m_sprite_render_count-- == 0)
        {
            m_sprite_control[0] = 3;
            m_sprite_render_count = m_sprite_control[3] & 1;
        }
    }

    /* look for pending commands */
    if (m_sprite_control[0] & 2)
        sprite_erase_buffer();
    if (m_sprite_control[0] & 1)
    {
        sprite_swap_buffers();
        sprite_render_list();
    }
    m_sprite_control[0] = 0;
}


bool S32Video::system32_set_vblank(int state)
{
    /* at the end of VBLANK is when automatic sprite rendering happens */
    // PORT: MAME does m_update_sprites_timer->adjust(attotime::from_usec(50), 1) here; the
    // board owns the timer and calls update_sprites() 50 us later when this returns true
    return !state;
}


/*************************************
 *
 *  Video RAM access
 *
 *************************************/

uint16_t S32Video::videoram_r(uint32_t offset)
{
    return m_videoram[offset];
}


void S32Video::videoram_w(uint32_t offset, uint16_t data, uint16_t mem_mask)
{
    combine_data(&m_videoram[offset], data, mem_mask);

    /* if we are not in the control area, just update any affected tilemaps */
    if (offset < 0x1ff00 / 2)
    {
        cache_entry* entry;
        int page = offset >> 9;
        offset &= 0x1ff;

        /* scan the cache for a matching pages */
        for (entry = m_cache_head; entry != nullptr; entry = entry->next)
            if (entry->page == page)
                entry->tmap->mark_tile_dirty(offset);
    }
}


/*************************************
 *
 *  Sprite control registers
 *
 *************************************/

uint8_t S32Video::sprite_control_r(uint32_t offset)
{
    switch (offset)
    {
        case 0:
            /*  D1 : Seems to be '1' only during an erase in progress, this
                     occurs very briefly though.
                D0 : Selected frame buffer (0= A, 1= B) */
            return 0xfc | (int)(m_layer_data[MIXER_LAYER_SPRITES].num < m_layer_data[MIXER_LAYER_SPRITES_2].num);

        case 1:
            /*  D1 : ?
                D0 : ?

                Values seem to be:

                0 = Unknown (relates to *approaching* out of time condition)
                1 = Normal status
                2 = Overdraw (rendering time is over but end-of-list command not read yet)
                3 = Never occurs

                Condition 2 can occur during rendering or list processing. */
            return 0xfc | 1;

        case 2:
            /*  D1 : 1= Vertical flip, 0= Normal orientation
                D0 : 1= Horizontal flip, 0= Normal orientation */
            return 0xfc | m_sprite_control_latched[2];

        case 3:
            /*  D1 : 1= Manual mode, 0= Automatic mode
                D0 : 1= 30 Hz update, 0= 60 Hz update (automatic mode only) */
            return 0xfc | m_sprite_control_latched[3];

        case 4:
            /*  D1 : ?
                D0 : ? */
            return 0xfc | m_sprite_control_latched[4];

        case 5:
            /*  D1 : ?
                D0 : ? */
            return 0xfc | m_sprite_control_latched[5];

        case 6:
            /*  D0 : 1= 416 pixels
                     0= 320 pixels */
            return 0xfc | (m_sprite_control_latched[6] & 1);

        case 7:
            /*  D1 : ?
                D0 : ? */
            return 0xfc;
    }
    return 0xff;
}


void S32Video::sprite_control_w(uint32_t offset, uint8_t data)
{
    m_sprite_control[offset & 7] = data;
}


/*************************************
 *
 *  Sprite RAM access
 *
 *************************************/

uint16_t S32Video::spriteram_r(uint32_t offset)
{
    return m_spriteram[offset];
}


void S32Video::spriteram_w(uint32_t offset, uint16_t data, uint16_t mem_mask)
{
    combine_data(&m_spriteram[offset], data, mem_mask);
    m_spriteram_32bit[offset >> 1] =
        ((m_spriteram[offset |  1] >> 8 ) & 0x000000ff) |
        ((m_spriteram[offset |  1] << 8 ) & 0x0000ff00) |
        ((m_spriteram[offset & ~1u] << 8 ) & 0x00ff0000) |
        ((uint32_t(m_spriteram[offset & ~1u]) << 24) & 0xff000000);
}


/*************************************
 *
 *  Palette RAM access (segas32.cpp)
 *
 *************************************/

static inline uint16_t xBBBBBGGGGGRRRRR_to_xBGRBBBBGGGGRRRR(uint16_t value)
{
    int r = (value >> 0) & 0x1f;
    int g = (value >> 5) & 0x1f;
    int b = (value >> 10) & 0x1f;
    value = (value & 0x8000) | ((b & 0x01) << 14) | ((g & 0x01) << 13) | ((r & 0x01) << 12);
    value |= ((b & 0x1e) << 7) | ((g & 0x1e) << 3) | ((r & 0x1e) >> 1);
    return value;
}


static inline uint16_t xBGRBBBBGGGGRRRR_to_xBBBBBGGGGGRRRRR(uint16_t value)
{
    int r = ((value >> 12) & 0x01) | ((value << 1) & 0x1e);
    int g = ((value >> 13) & 0x01) | ((value >> 3) & 0x1e);
    int b = ((value >> 14) & 0x01) | ((value >> 7) & 0x1e);
    return (value & 0x8000) | (b << 10) | (g << 5) | (r << 0);
}


uint16_t S32Video::paletteram_r(int which, uint32_t offset)
{
    int convert;

    /* the lower half of palette RAM is formatted xBBBBBGGGGGRRRRR */
    /* the upper half of palette RAM is formatted xBGRBBBBGGGGRRRR */
    /* we store everything if the first format, and convert accesses to the other format */
    /* on the fly */
    convert = (offset & 0x4000);
    offset &= 0x3fff;

    if (!convert)
        return m_paletteram[which][offset];
    else
        return xBBBBBGGGGGRRRRR_to_xBGRBBBBGGGGRRRR(m_paletteram[which][offset]);
}


void S32Video::paletteram_w(int which, uint32_t offset, uint16_t data, uint16_t mem_mask)
{
    uint16_t value;
    int convert;

    /* the lower half of palette RAM is formatted xBBBBBGGGGGRRRRR */
    /* the upper half of palette RAM is formatted xBGRBBBBGGGGRRRR */
    /* we store everything if the first format, and convert accesses to the other format */
    /* on the fly */
    convert = (offset & 0x4000);
    offset &= 0x3fff;

    /* read, modify, and write the new value, updating the palette */
    value = m_paletteram[which][offset];
    if (convert) value = xBBBBBGGGGGRRRRR_to_xBGRBBBBGGGGRRRR(value);
    combine_data(&value, data, mem_mask);
    if (convert) value = xBGRBBBBGGGGRRRR_to_xBBBBBGGGGGRRRRR(value);
    m_paletteram[which][offset] = value;
    // PORT: update_color() (m_palette->set_pen_color) omitted; it only feeds MAME's palette
    // viewer, the mixer reads m_paletteram directly

    /* if blending is enabled, writes go to both halves of palette RAM */
    if (m_mixer_control[which][0x4e / 2] & 0x0880)
    {
        offset ^= 0x2000;

        /* read, modify, and write the new value, updating the palette */
        value = m_paletteram[which][offset];
        if (convert) value = xBBBBBGGGGGRRRRR_to_xBGRBBBBGGGGRRRR(value);
        combine_data(&value, data, mem_mask);
        if (convert) value = xBGRBBBBGGGGRRRR_to_xBBBBBGGGGGRRRRR(value);
        m_paletteram[which][offset] = value;
    }
}


/*************************************
 *
 *  Mixer control registers (segas32.cpp)
 *
 *************************************/

uint16_t S32Video::mixer_r(int which, uint32_t offset)
{
    return m_mixer_control[which][offset];
}


void S32Video::mixer_w(int which, uint32_t offset, uint16_t data, uint16_t mem_mask)
{
    combine_data(&m_mixer_control[which][offset], data, mem_mask);
}


/*************************************
 *
 *  I/O chip outputs (segas32.cpp)
 *
 *************************************/

void S32Video::tilebank_external_w(uint8_t data)
{
    m_system32_tilebank_external = data;
}


void S32Video::display_enable_w(int which, int state)
{
    m_system32_displayenable[which] = state;
}


/*************************************
 *
 *  Tilemap cache
 *
 *************************************/

S32Video::tilemap* S32Video::find_cache_entry(int page, int bank)
{
    cache_entry *entry, *prev;

    /* scan the list for a matching entry */
    prev = nullptr;
    entry = m_cache_head;
    while (1)
    {
        if (entry->page == page && entry->bank == bank)
        {
            /* move us to the head before returning */
            if (prev)
            {
                prev->next = entry->next;
                entry->next = m_cache_head;
                m_cache_head = entry;
            }
            return entry->tmap;
        }

        /* stop on the last entry */
        if (entry->next == nullptr)
            break;
        prev = entry;
        entry = entry->next;
    }

    /* okay, we didn't find one; take over this last entry */
    entry->page = page;
    entry->bank = bank;
    entry->tmap->mark_all_dirty();

    /* move it to the head */
    prev->next = entry->next;
    entry->next = m_cache_head;
    m_cache_head = entry;

    return entry->tmap;
}


/*************************************
 *
 *  Tilemap callback
 *
 *************************************/

void S32Video::get_tile_info(tilemap& tm, uint32_t tile_index, uint32_t& code, uint32_t& color, uint8_t& flags)
{
    cache_entry* entry = (cache_entry*) tm.user_data;
    uint16_t data = m_videoram[((entry->page & 0x7f) << 9) | tile_index];
    // tileinfo.set(0, (entry->bank << 13) | (data & 0x1fff), (data >> 4) & 0x1ff, (data >> 14) & 3);
    code = (entry->bank << 13) | (data & 0x1fff);
    color = (data >> 4) & 0x1ff;
    flags = (data >> 14) & 3;
}


// PORT: tilemap_t::pixmap_update / tile_update / tile_draw (src/emu/tilemap.cpp), the parts
// that produce the pixmap (no flagsmap, categories or global flip: System 32 uses none)
void S32Video::pixmap_update(tilemap& tm)
{
    // if everything is clean, do nothing
    if (tm.all_tiles_clean)
        return;

    // flush the dirty state to all tiles as appropriate (realize_all_dirty_tiles)
    if (tm.all_tiles_dirty)
    {
        memset(tm.dirty, 1, sizeof(tm.dirty));
        tm.all_tiles_dirty = false;
    }

    // iterate over rows and columns (TILEMAP_SCAN_ROWS: memory index = logical index)
    uint32_t logindex = 0;
    for (uint32_t row = 0; row < 16; row++)
        for (uint32_t col = 0; col < 32; col++, logindex++)
            if (tm.dirty[logindex])
            {
                // tile_update
                uint32_t rawcode, rawcolor;
                uint8_t flags;
                get_tile_info(tm, logindex, rawcode, rawcolor, flags);

                // tile_data::set: code % elements, colorbase 0 + granularity 16 * (color % 0x3ff)
                uint32_t code = rawcode % m_gfx_elements;
                const uint8_t* pendata = &m_gfxdata[(size_t) code * 256];
                uint32_t palette_base = 0 + 16 * (rawcolor % 0x3ff);

                // tile_draw
                uint32_t x0 = 16 * col;
                uint32_t y0 = 16 * row;
                int dy0 = 1;
                if (flags & 0x02)       // TILE_FLIPY
                {
                    y0 += 16 - 1;
                    dy0 = -1;
                }
                int dx0 = 1;
                if (flags & 0x01)       // TILE_FLIPX
                {
                    x0 += 16 - 1;
                    dx0 = -1;
                }
                for (uint16_t ty = 0; ty < 16; ty++)
                {
                    uint16_t* pixptr = &tm.pixmap[(size_t) y0 * 512 + x0];
                    y0 += dy0;
                    int xoffs = 0;
                    for (uint16_t tx = 0; tx < 16; tx++)
                    {
                        uint8_t pen = (*pendata++) & 0xff;     // pen_mask
                        pixptr[xoffs] = palette_base + pen;
                        xoffs += dx0;
                    }
                }
                tm.dirty[logindex] = 0;
            }

    // mark it all clean
    tm.all_tiles_clean = true;
}


/*************************************
 *
 *  Clipping extents computation
 *
 *************************************/

int S32Video::compute_clipping_extents(int screen, int enable, int clipout, int clipmask, const rectangle& cliprect, extents_list* list)
{
    int flip = (m_videoram[0x1ff00 / 2] >> 9) & 1;
    rectangle tempclip;
    rectangle clips[5];
    int sorted[5];
    int i, j, y;

    /* expand our cliprect to exclude the bottom-right */
    tempclip = cliprect;
    tempclip.max_x++;
    tempclip.max_y++;

    /* create the 0th entry */
    list->extent[0][0] = tempclip.min_x;
    list->extent[0][1] = tempclip.max_x;

    /* simple case if not enabled */
    if (!enable)
    {
        memset(&list->scan_extent[tempclip.min_y], 0, sizeof(list->scan_extent[0]) * (tempclip.max_y - tempclip.min_y));
        return 1;
    }

    /* extract the from videoram into locals, and apply the cliprect */
    for (i = 0; i < 5; i++)
    {
        if (!flip)
        {
            clips[i].min_x = m_videoram[0x1ff60 / 2 + i * 4] & 0x1ff;
            clips[i].min_y = m_videoram[0x1ff62 / 2 + i * 4] & 0x0ff;
            clips[i].max_x = (m_videoram[0x1ff64 / 2 + i * 4] & 0x1ff) + 1;
            clips[i].max_y = (m_videoram[0x1ff66 / 2 + i * 4] & 0x0ff) + 1;
        }
        else
        {
            const rectangle& visarea = m_visarea[screen];

            clips[i].max_x = (visarea.max_x + 1) - (m_videoram[0x1ff60 / 2 + i * 4] & 0x1ff);
            clips[i].max_y = (visarea.max_y + 1) - (m_videoram[0x1ff62 / 2 + i * 4] & 0x0ff);
            clips[i].min_x = (visarea.max_x + 1) - ((m_videoram[0x1ff64 / 2 + i * 4] & 0x1ff) + 1);
            clips[i].min_y = (visarea.max_y + 1) - ((m_videoram[0x1ff66 / 2 + i * 4] & 0x0ff) + 1);
        }
        clips[i] &= tempclip;
        sorted[i] = i;
    }

    /* bubble sort them by min_x */
    for (i = 0; i < 5; i++)
        for (j = i + 1; j < 5; j++)
            if (clips[sorted[i]].min_x > clips[sorted[j]].min_x) { int temp = sorted[i]; sorted[i] = sorted[j]; sorted[j] = temp; }

    /* create all valid extent combinations */
    for (i = 1; i < 32; i++)
        if (i & clipmask)
        {
            uint16_t* extent = &list->extent[i][0];

            /* start off with an entry at tempclip.min_x */
            *extent++ = tempclip.min_x;

            /* loop in sorted order over extents */
            for (j = 0; j < 5; j++)
                if (i & (1 << sorted[j]))
                {
                    const rectangle& cur = clips[sorted[j]];

                    /* see if this intersects our last extent */
                    if (extent != &list->extent[i][1] && cur.min_x <= extent[-1])
                    {
                        if (cur.max_x > extent[-1])
                            extent[-1] = cur.max_x;
                    }

                    /* otherwise, just append to the list */
                    else
                    {
                        *extent++ = cur.min_x;
                        *extent++ = cur.max_x;
                    }
                }

            /* append an ending entry */
            *extent++ = tempclip.max_x;
        }

    /* loop over scanlines and build extents */
    for (y = tempclip.min_y; y < tempclip.max_y; y++)
    {
        int sect = 0;

        /* figure out all the clips that intersect this scanline */
        for (i = 0; i < 5; i++)
            if ((clipmask & (1 << i)) && y >= clips[i].min_y && y < clips[i].max_y)
                sect |= 1 << i;
        list->scan_extent[y] = sect;
    }

    return clipout;
}


void S32Video::compute_tilemap_flips(int bgnum, int& flipx, int& flipy)
{
    /* determine if we're flipped */
    int global_flip = (m_videoram[0x1ff00 / 2] >> 9) & 1;

    flipx = global_flip;
    flipy = global_flip;

    int layer_flip = (m_videoram[0x1ff00 / 2] >> bgnum) & 1;

    flipy ^= layer_flip;
    flipx ^= layer_flip;

    // this bit is set on Air Rescue (screen 2) title screen, during the Air Rescue introduction demo, and in f1en when you win a single player race
    // it seems to prohibit (at least) the per-tilemap y flipping (maybe global y can override it)
    if ((m_videoram[0x1ff00 / 2] >> 8) & 1) flipy = 0;
}


/*************************************
 *
 *  Zooming tilemaps (NBG0/1)
 *
 *************************************/

void S32Video::get_tilemaps(int bgnum, tilemap** tilemaps)
{
    int tilebank, page;

    /* determine the current tilebank */
    // m_is_multi32
    tilebank = (m_system32_tilebank_external >> (2 * bgnum)) & 3;

    /* find the cache entries */
    page = (m_videoram[0x1ff40 / 2 + 2 * bgnum + 0] >> 0) & 0x7f;
    tilemaps[0] = find_cache_entry(page, tilebank);
    page = (m_videoram[0x1ff40 / 2 + 2 * bgnum + 0] >> 8) & 0x7f;
    tilemaps[1] = find_cache_entry(page, tilebank);
    page = (m_videoram[0x1ff40 / 2 + 2 * bgnum + 1] >> 0) & 0x7f;
    tilemaps[2] = find_cache_entry(page, tilebank);
    page = (m_videoram[0x1ff40 / 2 + 2 * bgnum + 1] >> 8) & 0x7f;
    tilemaps[3] = find_cache_entry(page, tilebank);
}


void S32Video::update_tilemap_zoom(int screen, layer_info& layer, const rectangle& cliprect, int bgnum)
{
    /* get the tilemaps */
    tilemap* tilemaps[4];
    get_tilemaps(bgnum, tilemaps);

    /* configure the layer */
    int opaque = 0;
    int flipx, flipy;

    // todo determine flipping
    compute_tilemap_flips(bgnum, flipx, flipy);

    /* determine the clipping */
    int clipenable = (m_videoram[0x1ff02 / 2] >> (11 + bgnum)) & 1;
    int clipout = (m_videoram[0x1ff02 / 2] >> (6 + bgnum)) & 1;
    int clips = (m_videoram[0x1ff06 / 2] >> (4 * bgnum)) & 0x0f;
    extents_list clip_extents;
    int clipdraw_start = compute_clipping_extents(screen, clipenable, clipout, clips, cliprect, &clip_extents);

    /* extract the X/Y step values (these are in destination space!) */
    int dstxstep = m_videoram[0x1ff50 / 2 + 2 * bgnum] & 0xfff;
    int dstystep;
    if (m_videoram[0x1ff00 / 2] & 0x4000)
        dstystep = m_videoram[0x1ff52 / 2 + 2 * bgnum] & 0xfff;
    else
        dstystep = dstxstep;

    /* clamp the zoom factors */
    if (dstxstep < 0x80)
        dstxstep = 0x80;
    if (dstystep < 0x80)
        dstystep = 0x80;

    /* compute high-precision reciprocals (in 12.20 format) */
    uint32_t srcxstep = (0x200 << 20) / dstxstep;
    uint32_t srcystep = (0x200 << 20) / dstystep;

    /* start with the fractional scroll offsets, in source coordinates */
    uint32_t srcx_start = (m_videoram[0x1ff12 / 2 + 4 * bgnum] & 0x3ff) << 20;
    srcx_start += (m_videoram[0x1ff10 / 2 + 4 * bgnum] & 0xff00) << 4;
    uint32_t srcy = (m_videoram[0x1ff16 / 2 + 4 * bgnum] & 0x1ff) << 20;
    srcy += (m_videoram[0x1ff14 / 2 + 4 * bgnum] & 0xfe00) << 4;

    /* then account for the destination center coordinates */
    srcx_start -= sext16(m_videoram[0x1ff30 / 2 + 2 * bgnum], 10) * srcxstep;
    srcy -= sext16(m_videoram[0x1ff32 / 2 + 2 * bgnum], 9) * srcystep;

    /* finally, account for destination top,left coordinates */
    srcx_start += cliprect.min_x * srcxstep;
    srcy += cliprect.min_y * srcystep;

    /* if we're flipped, simply adjust the start/step parameters */
    if (flipy)
    {
        const rectangle& visarea = m_visarea[screen];

        srcy += (visarea.max_y - 2 * cliprect.min_y) * srcystep;
        srcystep = -srcystep;
    }

    if (flipx)
    {
        const rectangle& visarea = m_visarea[screen];

        srcx_start += (visarea.max_x - 2 * cliprect.min_x) * srcxstep;
        srcxstep = -srcxstep;
    }

    /* loop over the target rows */
    for (int y = cliprect.min_y; y <= cliprect.max_y; y++)
    {
        uint16_t const* extents = &clip_extents.extent[clip_extents.scan_extent[y]][0];
        uint16_t* const dst = layer.pix(y);
        int clipdraw = clipdraw_start;

        /* optimize for the case where we are clipped out */
        if (clipdraw || extents[1] <= cliprect.max_x)
        {
            int transparent = 0;

            /* look up the pages and get their source pixmaps */
            const uint16_t* tm0 = tilemap_pixmap(tilemaps[((srcy >> 27) & 2) + 0]);
            const uint16_t* tm1 = tilemap_pixmap(tilemaps[((srcy >> 27) & 2) + 1]);
            uint16_t const* const src[2] = { &tm0[((srcy >> 20) & 0xff) * 512], &tm1[((srcy >> 20) & 0xff) * 512] };

            /* loop over extents */
            uint32_t srcx = srcx_start;
            while (1)
            {
                /* if we're drawing on this extent, draw it */
                if (clipdraw)
                {
                    for (int x = extents[0]; x < extents[1]; x++)
                    {
                        uint16_t pix = src[(srcx >> 29) & 1][(srcx >> 20) & 0x1ff];
                        srcx += srcxstep;
                        if ((pix & 0x0f) == 0 && !opaque)
                            pix = 0, transparent++;
                        dst[x] = pix;
                    }
                }

                /* otherwise, clear to zero */
                else
                {
                    int pixels = extents[1] - extents[0];
                    std::fill_n(&dst[extents[0]], pixels, 0);
                    srcx += srcxstep * pixels;
                    transparent += pixels;
                }

                /* stop at the end */
                if (extents[1] > cliprect.max_x)
                    break;

                /* swap states and advance to the next extent */
                clipdraw = !clipdraw;
                extents++;
            }

            layer.transparent[y] = (transparent == cliprect.max_x - cliprect.min_x + 1);
        }
        else
            layer.transparent[y] = 1;

        /* advance in Y */
        srcy += srcystep;
    }
}


/*************************************
 *
 *  Rowscroll/select tilemaps (NBG2/3)
 *
 *************************************/

void S32Video::update_tilemap_rowscroll(int screen, layer_info& layer, const rectangle& cliprect, int bgnum)
{
    /* get the tilemaps */
    tilemap* tilemaps[4];
    get_tilemaps(bgnum, tilemaps);

    /* configure the layer */
    int opaque = 0;

    int flipx, flipy;

    // todo determine flipping
    compute_tilemap_flips(bgnum, flipx, flipy);


    /* determine the clipping */
    int clipenable = (m_videoram[0x1ff02 / 2] >> (11 + bgnum)) & 1;
    int clipout = (m_videoram[0x1ff02 / 2] >> (6 + bgnum)) & 1;
    int clips = (m_videoram[0x1ff06 / 2] >> (4 * bgnum)) & 0x0f;
    extents_list clip_extents;
    int clipdraw_start = compute_clipping_extents(screen, clipenable, clipout, clips, cliprect, &clip_extents);

    /* determine if row scroll and/or row select is enabled */
    int rowscroll = (m_videoram[0x1ff04 / 2] >> (bgnum - 2)) & 1;
    int rowselect = (m_videoram[0x1ff04 / 2] >> bgnum) & 1;
    if ((m_videoram[0x1ff04 / 2] >> (bgnum + 2)) & 1)
        rowscroll = rowselect = 0;

    /* get a pointer to the table */
    uint16_t const* const table = &m_videoram[(m_videoram[0x1ff04 / 2] >> 10) * 0x400];

    /* start with screen-wide X and Y scrolls */
    int xscroll = (m_videoram[0x1ff12 / 2 + 4 * bgnum] & 0x3ff) - (m_videoram[0x1ff30 / 2 + 2 * bgnum] & 0x1ff);
    int yscroll = (m_videoram[0x1ff16 / 2 + 4 * bgnum] & 0x1ff);

    /* render the tilemap into its bitmap */
    for (int y = cliprect.min_y; y <= cliprect.max_y; y++)
    {
        uint16_t const* extents = &clip_extents.extent[clip_extents.scan_extent[y]][0];
        uint16_t* const dst = layer.pix(y);
        int clipdraw = clipdraw_start;

        /* optimize for the case where we are clipped out */
        if (clipdraw || extents[1] <= cliprect.max_x)
        {
            int transparent = 0;
            int srcxstep;

            /* if we're not flipped, things are straightforward */
            int srcx;
            if (!flipx)
            {
                srcx = cliprect.min_x + xscroll;
                srcxstep = 1;
            }
            else
            {
                srcx = cliprect.max_x + xscroll;
                srcxstep = -1;
            }

            int srcy;
            if (!flipy)
            {
                srcy = yscroll + y;
            }
            else
            {
                const rectangle& visarea = m_visarea[screen];
                srcy = yscroll + visarea.max_y - y;
            }

            /* apply row scroll/select */
            if (rowscroll)
                srcx += table[0x000 + 0x100 * (bgnum - 2) + y] & 0x3ff;
            if (rowselect)
                srcy = (yscroll + table[0x200 + 0x100 * (bgnum - 2) + y]) & 0x1ff;


            /* look up the pages and get their source pixmaps */
            const uint16_t* tm0 = tilemap_pixmap(tilemaps[((srcy >> 7) & 2) + 0]);
            const uint16_t* tm1 = tilemap_pixmap(tilemaps[((srcy >> 7) & 2) + 1]);
            uint16_t const* const src[2] = { &tm0[(srcy & 0xff) * 512], &tm1[(srcy & 0xff) * 512] };

            /* loop over extents */
            while (1)
            {
                /* if we're drawing on this extent, draw it */
                if (clipdraw)
                {
                    for (int x = extents[0]; x < extents[1]; x++, srcx += srcxstep)
                    {
                        uint16_t pix = src[(srcx >> 9) & 1][srcx & 0x1ff];
                        if ((pix & 0x0f) == 0 && !opaque)
                            pix = 0, transparent++;
                        dst[x] = pix;
                    }
                }

                /* otherwise, clear to zero */
                else
                {
                    int pixels = extents[1] - extents[0];
                    std::fill_n(&dst[extents[0]], pixels, 0);
                    srcx += srcxstep * pixels;
                    transparent += pixels;
                }

                /* stop at the end */
                if (extents[1] > cliprect.max_x)
                    break;

                /* swap states and advance to the next extent */
                clipdraw = !clipdraw;
                extents++;
            }

            layer.transparent[y] = (transparent == cliprect.max_x - cliprect.min_x + 1);
        }
        else
            layer.transparent[y] = 1;
    }
}


/*************************************
 *
 *  Text layer
 *
 *************************************/

void S32Video::update_tilemap_text(int screen, layer_info& layer, const rectangle& cliprect)
{
    const int rowpixels = SCREEN_W_MAX;

    /* determine if we're flipped */
    int flip = (m_videoram[0x1ff00 / 2] >> 9) & 1;

    /* determine the base of the tilemap and graphics data */
    uint16_t const* const tilebase = &m_videoram[((m_videoram[0x1ff5c / 2] >> 4) & 0x1f) * 0x800];
    uint16_t const* const gfxbase = &m_videoram[(m_videoram[0x1ff5c / 2] & 7) * 0x2000];

    /* compute start/end tile numbers */
    int startx = cliprect.min_x / 8;
    int starty = cliprect.min_y / 8;
    int endx = cliprect.max_x / 8;
    int endy = cliprect.max_y / 8;

    /* loop over tiles */
    for (int y = starty; y <= endy; y++)
        for (int x = startx; x <= endx; x++)
        {
            int tile = tilebase[y * 64 + x];
            uint16_t const* src = &gfxbase[(tile & 0x1ff) * 16];
            int color = (tile & 0xfe00) >> 5;

            /* non-flipped case */
            if (!flip)
            {
                uint16_t* dst = layer.pix(y * 8, x * 8);

                /* loop over rows */
                for (int iy = 0; iy < 8; iy++)
                {
                    int pixels = *src++;
                    int pix;

                    pix = (pixels >> 4) & 0x0f;
                    if (pix)
                        pix |= color;
                    dst[0] = pix;

                    pix = (pixels >> 0) & 0x0f;
                    if (pix)
                        pix |= color;
                    dst[1] = pix;

                    pix = (pixels >> 12) & 0x0f;
                    if (pix)
                        pix |= color;
                    dst[2] = pix;

                    pix = (pixels >> 8) & 0x0f;
                    if (pix)
                        pix |= color;
                    dst[3] = pix;

                    pixels = *src++;

                    pix = (pixels >> 4) & 0x0f;
                    if (pix)
                        pix |= color;
                    dst[4] = pix;

                    pix = (pixels >> 0) & 0x0f;
                    if (pix)
                        pix |= color;
                    dst[5] = pix;

                    pix = (pixels >> 12) & 0x0f;
                    if (pix)
                        pix |= color;
                    dst[6] = pix;

                    pix = (pixels >> 8) & 0x0f;
                    if (pix)
                        pix |= color;
                    dst[7] = pix;

                    dst += rowpixels;
                }
            }

            /* flipped case */
            else
            {
                const rectangle& visarea = m_visarea[screen];

                int effdstx = visarea.max_x - x * 8;
                int effdsty = visarea.max_y - y * 8;
                uint16_t* dst = layer.pix(effdsty, effdstx);

                /* loop over rows */
                for (int iy = 0; iy < 8; iy++)
                {
                    int pixels = *src++;
                    int pix;

                    pix = (pixels >> 4) & 0x0f;
                    if (pix)
                        pix |= color;
                    dst[0] = pix;

                    pix = (pixels >> 0) & 0x0f;
                    if (pix)
                        pix |= color;
                    dst[-1] = pix;

                    pix = (pixels >> 12) & 0x0f;
                    if (pix)
                        pix |= color;
                    dst[-2] = pix;

                    pix = (pixels >> 8) & 0x0f;
                    if (pix)
                        pix |= color;
                    dst[-3] = pix;

                    // (sic: MAME reads the second word into pix and keeps using `pixels`)
                    pix = *src++;

                    pix = (pixels >> 4) & 0x0f;
                    if (pix)
                        pix |= color;
                    dst[-4] = pix;

                    pix = (pixels >> 0) & 0x0f;
                    if (pix)
                        pix |= color;
                    dst[-5] = pix;

                    pix = (pixels >> 12) & 0x0f;
                    if (pix)
                        pix |= color;
                    dst[-6] = pix;

                    pix = (pixels >> 8) & 0x0f;
                    if (pix)
                        pix |= color;
                    dst[-7] = pix;

                    dst -= rowpixels;
                }
            }
        }
}


/*************************************
 *
 *  Bitmap layer
 *
 *************************************/

void S32Video::update_bitmap(int screen, layer_info& layer, const rectangle& cliprect)
{
    /* configure the layer */
    int bpp = (m_videoram[0x1ff00 / 2] & 0x0800) ? 8 : 4;

    /* determine the clipping */
    int clipenable = (m_videoram[0x1ff02 / 2] >> 15) & 1;
    int clipout = (m_videoram[0x1ff02 / 2] >> 10) & 1;
    int clips = 0x10;
    extents_list clip_extents;
    int clipdraw_start = compute_clipping_extents(screen, clipenable, clipout, clips, cliprect, &clip_extents);

    /* determine x/y scroll */
    int xscroll = m_videoram[0x1ff88 / 2] & 0x1ff;
    int yscroll = m_videoram[0x1ff8a / 2] & 0x1ff;
    int color = (m_videoram[0x1ff8c / 2] << 4) & 0x1fff0 & ~((1 << bpp) - 1);

    /* loop over target rows */
    for (int y = cliprect.min_y; y <= cliprect.max_y; y++)
    {
        uint16_t const* extents = &clip_extents.extent[clip_extents.scan_extent[y]][0];
        uint16_t* const dst = layer.pix(y);
        int clipdraw = clipdraw_start;

        /* optimize for the case where we are clipped out */
        if (clipdraw || extents[1] <= cliprect.max_x)
        {
            int transparent = 0;

            /* loop over extents */
            while (1)
            {
                /* if we're drawing on this extent, draw it */
                if (clipdraw)
                {
                    /* 8bpp mode case */
                    if (bpp == 8)
                    {
                        // util::little_endian_cast<uint8_t const>: byte n of the row is the
                        // low byte of word n/2 for even n, the high byte for odd n
                        uint16_t const* const srcw = &m_videoram[512 / 2 * ((y + yscroll) & 0xff)];
                        for (int x = extents[0]; x < extents[1]; x++)
                        {
                            int effx = (x + xscroll) & 0x1ff;
                            int pix = ((srcw[effx >> 1] >> (8 * (effx & 1))) & 0xff) + color;
                            if ((pix & 0xff) == 0)
                                pix = 0, transparent++;
                            dst[x] = pix;
                        }
                    }

                    /* 4bpp mode case */
                    else
                    {
                        uint16_t const* src = &m_videoram[512 / 4 * ((y + yscroll) & 0x1ff)];
                        for (int x = extents[0]; x < extents[1]; x++)
                        {
                            int effx = (x + xscroll) & 0x1ff;
                            int pix = ((src[effx / 4] >> (4 * (effx & 3))) & 0x0f) + color;
                            if ((pix & 0x0f) == 0)
                                pix = 0, transparent++;
                            dst[x] = pix;
                        }
                    }
                }

                /* otherwise, clear to zero */
                else
                {
                    int pixels = extents[1] - extents[0];
                    std::fill_n(&dst[extents[0]], pixels, 0);
                    transparent += pixels;
                }

                /* stop at the end */
                if (extents[1] > cliprect.max_x)
                    break;

                /* swap states and advance to the next extent */
                clipdraw = !clipdraw;
                extents++;
            }

            layer.transparent[y] = (transparent == cliprect.max_x - cliprect.min_x + 1);
        }
        else
            layer.transparent[y] = 1;
    }
}


/*************************************
 *
 *  Master tilemap chip updater
 *
 *************************************/

void S32Video::update_background(layer_info& layer, const rectangle& cliprect)
{
    for (int y = cliprect.min_y; y <= cliprect.max_y; y++)
    {
        uint16_t* const dst = layer.pix(y);
        int color;

        /* determine the color */
        if (m_videoram[0x1ff5e / 2] & 0x8000)
        {
            // line color select (bank wraps at 511, confirmed by arabfgt and kokoroj2)
            int yoffset = (m_videoram[0x1ff5e / 2] + y) & 0x1ff;
            color = (m_videoram[0x1ff5e / 2] & 0x1e00) + yoffset;
        }
        else
            color = m_videoram[0x1ff5e / 2] & 0x1e00;

        /* if the color doesn't match, fill */
        if ((m_bgcolor_line[y & 0x1ff] != color) || (m_prev_bgstartx[y & 0x1ff] != cliprect.min_x) || (m_prev_bgendx[y & 0x1ff] != cliprect.max_x))
        {
            for (int x = cliprect.min_x; x <= cliprect.max_x; x++)
                dst[x] = color;

            m_prev_bgstartx[y & 0x1ff] = cliprect.min_x;
            m_prev_bgendx[y & 0x1ff] = cliprect.max_x;
            m_bgcolor_line[y & 0x1ff] = color;
        }
    }
}


uint8_t S32Video::update_tilemaps(int screen, const rectangle& cliprect)
{
    int enable0 = !(m_videoram[0x1ff02 / 2] & 0x0001) && !(m_videoram[0x1ff8e / 2] & 0x0002);
    int enable1 = !(m_videoram[0x1ff02 / 2] & 0x0002) && !(m_videoram[0x1ff8e / 2] & 0x0004);
    int enable2 = !(m_videoram[0x1ff02 / 2] & 0x0004) && !(m_videoram[0x1ff8e / 2] & 0x0008) && !(m_videoram[0x1ff00 / 2] & 0x1000);
    int enable3 = !(m_videoram[0x1ff02 / 2] & 0x0008) && !(m_videoram[0x1ff8e / 2] & 0x0010) && !(m_videoram[0x1ff00 / 2] & 0x2000);
    int enablet = !(m_videoram[0x1ff02 / 2] & 0x0010) && !(m_videoram[0x1ff8e / 2] & 0x0001);
    int enableb = !(m_videoram[0x1ff02 / 2] & 0x0020) && !(m_videoram[0x1ff8e / 2] & 0x0020);

    /* update any tilemaps */
    if (enable0)
        update_tilemap_zoom(screen, m_layer_data[MIXER_LAYER_NBG0], cliprect, 0);
    if (enable1)
        update_tilemap_zoom(screen, m_layer_data[MIXER_LAYER_NBG1], cliprect, 1);
    if (enable2)
        update_tilemap_rowscroll(screen, m_layer_data[MIXER_LAYER_NBG2], cliprect, 2);
    if (enable3)
        update_tilemap_rowscroll(screen, m_layer_data[MIXER_LAYER_NBG3], cliprect, 3);
    if (enablet)
        update_tilemap_text(screen, m_layer_data[MIXER_LAYER_TEXT], cliprect);
    if (enableb)
        update_bitmap(screen, m_layer_data[MIXER_LAYER_BITMAP], cliprect);
    update_background(m_layer_data[MIXER_LAYER_BACKGROUND], cliprect);

    return (enablet << 0) | (enable0 << 1) | (enable1 << 2) | (enable2 << 3) | (enable3 << 4) | (enableb << 5);
}


/*************************************
 *
 *  Sprite buffer management
 *
 *************************************/

void S32Video::sprite_erase_buffer()
{
    /* erase the visible sprite buffer and clear the checksums */
    std::fill(m_layer_data[MIXER_LAYER_SPRITES].bitmap.begin(), m_layer_data[MIXER_LAYER_SPRITES].bitmap.end(), 0xffff);

    /* for multi32, erase the other buffer as well */
    std::fill(m_layer_data[MIXER_LAYER_MULTISPR].bitmap.begin(), m_layer_data[MIXER_LAYER_MULTISPR].bitmap.end(), 0xffff);
}


void S32Video::sprite_swap_buffers()
{
    /* swap between the two sprite buffers */
    std::swap(m_layer_data[MIXER_LAYER_SPRITES].bitmap, m_layer_data[MIXER_LAYER_SPRITES_2].bitmap);
    std::swap(m_layer_data[MIXER_LAYER_SPRITES].transparent, m_layer_data[MIXER_LAYER_SPRITES_2].transparent);
    std::swap(m_layer_data[MIXER_LAYER_SPRITES].num, m_layer_data[MIXER_LAYER_SPRITES_2].num);

    /* for multi32, swap the other buffer as well */
    std::swap(m_layer_data[MIXER_LAYER_MULTISPR].bitmap, m_layer_data[MIXER_LAYER_MULTISPR_2].bitmap);
    std::swap(m_layer_data[MIXER_LAYER_MULTISPR].transparent, m_layer_data[MIXER_LAYER_MULTISPR_2].transparent);
    std::swap(m_layer_data[MIXER_LAYER_MULTISPR].num, m_layer_data[MIXER_LAYER_MULTISPR_2].num);

    /* latch any pending info */
    memcpy(m_sprite_control_latched, m_sprite_control, sizeof(m_sprite_control_latched));
}


/*************************************
 *
 *  Sprite render
 *
 *************************************/

/*******************************************************************************************
 *
 *  System 32-style sprites
 *
 *      Offs  Bits               Usage
 *       +0   cc------ --------  Command (00=sprite, 01=clip, 02=jump, 03=end)
 *       +0   --i----- --------  Indirect palette enable
 *       +0   ---l---- --------  Indirect palette is inline in spriteram
 *       +0   ----s--- --------  Shadow sprite
 *       +0   -----r-- --------  Graphics from spriteram
 *       +0   ------8- --------  8bpp sprite
 *       +0   -------o --------  Opaque (no transparency)
 *       +0   -------- y-------  Flip Y
 *       +0   -------- -x------  Flip X
 *       +0   -------- --Y-----  Apply Y offset from last jump
 *       +0   -------- ---X----  Apply X offset from last jump
 *       +0   -------- ----aa--  Y alignment (00=center, 10=start, 01=end)
 *       +0   -------- ------AA  X alignment (00=center, 10=start, 01=end)
 *       +2   hhhhhhhh --------  Source data height
 *       +2   -------- wwwwwwww  Source data width
 *       +4   rrrr---- --------  Low bits of ROM bank
 *       +4   -----hhh hhhhhhhh  Onscreen height
 *       +6   -5--4--- --------  Bits 5 + 4 of ROM bank
 *       +6   -----www wwwwwwww  Onscreen width
 *       +8   ----yyyy yyyyyyyy  Y position
 *       +A   ----xxxx xxxxxxxx  X position
 *       +C   oooooooo oooooooo  Offset within selected sprite bank
 *       +E   -----ppp pppp----  Palette
 *       +E   -------- ----rrrr  Priority?
 *
 *******************************************************************************************/

#define sprite_draw_pixel_16(trans)                                         \
    /* only draw if onscreen, not 0 or 15 */                                \
    if (x >= clipin.min_x && x <= clipin.max_x &&                           \
        (!do_clipout || x < clipout.min_x || x > clipout.max_x) &&          \
        pix != trans)                                                       \
    {                                                                       \
        if (!indirect)                                                      \
        {                                                                   \
            if (pix != 0)                                                   \
            {                                                               \
                if (!shadow)                                                \
                    dest[x] = color | pix;                                  \
                else                                                        \
                    dest[x] &= 0x7fff;                                      \
            }                                                               \
        }                                                                   \
        else                                                                \
        {                                                                   \
            int indpix = indtable[pix];                                     \
            if ((indpix & transmask) != transmask)                          \
            {                                                               \
                if (!shadow)                                                \
                    dest[x] = indpix;                                       \
                else                                                        \
                    dest[x] &= 0x7fff;                                      \
            }                                                               \
        }                                                                   \
    }

#define sprite_draw_pixel_256(trans)                                        \
    /* only draw if onscreen, not 0 or 15 */                                \
    if (x >= clipin.min_x && x <= clipin.max_x &&                           \
        (!do_clipout || x < clipout.min_x || x > clipout.max_x) &&          \
        pix != trans)                                                       \
    {                                                                       \
        if (!indirect)                                                      \
        {                                                                   \
            if (pix != 0)                                                   \
            {                                                               \
                if (!shadow)                                                \
                    dest[x] = color | pix;                                  \
                else                                                        \
                    dest[x] &= 0x7fff;                                      \
            }                                                               \
        }                                                                   \
        else                                                                \
        {                                                                   \
            int indpix = (indtable[pix >> 4]) | (pix & 0x0f);               \
            if ((indpix & transmask) != transmask)                          \
            {                                                               \
                if (!shadow)                                                \
                    dest[x] = indpix;                                       \
                else                                                        \
                    dest[x] &= 0x7fff;                                      \
            }                                                               \
        }                                                                   \
    }

int S32Video::draw_one_sprite(uint16_t const* data, int xoffs, int yoffs, const rectangle& clipin, const rectangle& clipout)
{
    static const int transparency_masks[4][4] =
    {
        { 0x7fff, 0x3fff, 0x1fff, 0x0fff },
        { 0x3fff, 0x1fff, 0x0fff, 0x07ff },
        { 0x3fff, 0x1fff, 0x0fff, 0x07ff },
        { 0x1fff, 0x0fff, 0x07ff, 0x03ff }
    };

    // m_is_multi32
    layer_info& bitmap = m_layer_data[!(data[3] & 0x0800) ? MIXER_LAYER_SPRITES_2 : MIXER_LAYER_MULTISPR_2];
    uint8_t numbanks = m_sprite_region_length >> 20;

    int indirect = data[0] & 0x2000;
    int indlocal = data[0] & 0x1000;
    int shadow   = (data[0] & 0x0800) && (m_sprite_control_latched[0x0a / 2] & 1);
    int fromram  = data[0] & 0x0400;
    int bpp8     = data[0] & 0x0200;
    int transp   = (data[0] & 0x0100) ? 0 : (bpp8 ? 0xff : 0x0f);
    int flipy    = data[0] & 0x0080;
    int flipx    = data[0] & 0x0040;
    int offsety  = data[0] & 0x0020;
    int offsetx  = data[0] & 0x0010;
    int adjusty  = (data[0] >> 2) & 3;
    int adjustx  = (data[0] >> 0) & 3;
    int srch     = (data[1] >> 8);
    int srcw     = bpp8 ? (data[1] & 0x3f) : ((data[1] >> 1) & 0x3f);
    int bank     = ((data[3] & 0x2000) >> 13) | ((data[3] & 0x8000) >> 14);   // m_is_multi32
    int dsth     = data[2] & 0x3ff;
    int dstw     = data[3] & 0x3ff;
    int ypos     = sext16(data[4], 12);
    int xpos     = sext16(data[5], 12);
    uint32_t addr  = data[6] | ((data[2] & 0xf000) << 4);
    int color    = 0x8000 | (data[7] & (bpp8 ? 0x7f00 : 0x7ff0));
    int hzoom, vzoom;
    int xdelta = 1, ydelta = 1;
    int xtarget, ytarget, yacc = 0, pix, transmask;
    const uint32_t* spritedata;
    uint32_t addrmask;
    uint16_t indtable[16];

    /* if hidden, or top greater than/equal to bottom, or invalid bank, punt */
    if (srcw == 0 || srch == 0 || dstw == 0 || dsth == 0)
        goto bail;

    /* determine the transparency mask for pixels */
    transmask = transparency_masks[m_sprite_control_latched[0x08 / 2] & 3][m_sprite_control_latched[0x0a / 2] & 3];
    if (bpp8)
        transmask &= 0xfff0;

    /* create the local palette for the indirect case */
    if (indirect)
    {
        uint16_t const* src = indlocal ? &data[8] : &m_spriteram[8 * (data[7] & 0x1fff)];
        for (int x = 0; x < 16; x++)
            indtable[x] = (src[x] & (bpp8 ? 0xfff0 : 0xffff)) | ((m_sprite_control_latched[0x0a / 2] & 1) ? 0x8000 : 0x0000);
    }

    /* clamp to within the memory region size */
    if (fromram)
    {
        spritedata = m_spriteram_32bit.data();
        addrmask = (0x20000 / 4) - 1;
    }
    else
    {
        if (numbanks)
            bank %= numbanks;
        spritedata = &m_sprite_region[bank << 20];
        addrmask = 0xfffff;
    }

    /* compute X/Y deltas */
    hzoom = (((bpp8 ? 4 : 8) * srcw) << 16) / dstw;
    vzoom = (srch << 16) / dsth;

    /* adjust the starting X position */
    if (offsetx)
        xpos += xoffs;
    switch (adjustx)
    {
        case 0:
        case 3: xpos -= (dstw - 1) / 2;     break;
        case 1: xpos -= dstw - 1;           break;
        case 2:                             break;
    }

    /* adjust the starting Y position */
    if (offsety)
        ypos += yoffs;
    switch (adjusty)
    {
        case 0:
        case 3: ypos -= (dsth - 1) / 2;     break;
        case 1: ypos -= dsth - 1;           break;
        case 2:                             break;
    }

    /* adjust for flipping */
    if (flipx)
    {
        xpos += dstw - 1;
        xdelta = -1;
    }
    if (flipy)
    {
        ypos += dsth - 1;
        ydelta = -1;
    }

    /* compute target X,Y positions for loops */
    xtarget = xpos + xdelta * dstw;
    ytarget = ypos + ydelta * dsth;

    /* adjust target x for clipping */
    if (xdelta > 0 && xtarget > clipin.max_x)
    {
        xtarget = clipin.max_x + 1;
        if (xpos >= xtarget)
            goto bail;
    }
    if (xdelta < 0 && xtarget < clipin.min_x)
    {
        xtarget = clipin.min_x - 1;
        if (xpos <= xtarget)
            goto bail;
    }

    /* loop from top to bottom */
    for (int y = ypos; y != ytarget; y += ydelta)
    {
        /* skip drawing if not within the inclusive cliprect */
        if (y >= clipin.min_y && y <= clipin.max_y)
        {
            int do_clipout = (y >= clipout.min_y && y <= clipout.max_y);
            uint16_t* const dest = bitmap.pix(y);
            int xacc = 0;

            /* 4bpp case */
            if (!bpp8)
            {
                /* start at the word before because we preincrement below */
                uint32_t curaddr = addr - 1;
                for (int x = xpos; x != xtarget; )
                {
                    uint32_t pixels = spritedata[++curaddr & addrmask];

                    /* draw four pixels */
                    pix = (pixels >> 28) & 0xf; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_16(transp)  x += xdelta; xacc += hzoom; } xacc -= 0x10000;
                    pix = (pixels >> 24) & 0xf; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_16(0)       x += xdelta; xacc += hzoom; } xacc -= 0x10000;
                    pix = (pixels >> 20) & 0xf; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_16(0)       x += xdelta; xacc += hzoom; } xacc -= 0x10000;
                    pix = (pixels >> 16) & 0xf; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_16(0)       x += xdelta; xacc += hzoom; } xacc -= 0x10000;
                    pix = (pixels >> 12) & 0xf; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_16(0)       x += xdelta; xacc += hzoom; } xacc -= 0x10000;
                    pix = (pixels >>  8) & 0xf; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_16(0)       x += xdelta; xacc += hzoom; } xacc -= 0x10000;
                    pix = (pixels >>  4) & 0xf; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_16(0)       x += xdelta; xacc += hzoom; } xacc -= 0x10000;
                    pix = (pixels >>  0) & 0xf; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_16(transp)  x += xdelta; xacc += hzoom; } xacc -= 0x10000;

                    /* check for end code */
                    if (transp != 0 && pix == 0x0f)
                        break;
                }
            }

            /* 8bpp case */
            else
            {
                /* start at the word before because we preincrement below */
                uint32_t curaddr = addr - 1;
                for (int x = xpos; x != xtarget; )
                {
                    uint32_t pixels = spritedata[++curaddr & addrmask];

                    /* draw four pixels */
                    pix = (pixels >> 24) & 0xff; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_256(transp) x += xdelta; xacc += hzoom; } xacc -= 0x10000;
                    pix = (pixels >> 16) & 0xff; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_256(0)      x += xdelta; xacc += hzoom; } xacc -= 0x10000;
                    pix = (pixels >>  8) & 0xff; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_256(0)      x += xdelta; xacc += hzoom; } xacc -= 0x10000;
                    pix = (pixels >>  0) & 0xff; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_256(transp) x += xdelta; xacc += hzoom; } xacc -= 0x10000;

                    /* check for end code */
                    if (transp != 0 && pix == 0xff)
                        break;
                }
            }
        }

        /* accumulate zoom factors; if we carry into the high bit, skip an extra row */
        yacc += vzoom;
        addr += srcw * (yacc >> 16);
        yacc &= 0xffff;
    }

    /* if we had an enabled inline indirect palette, we skip two entries */
bail:
    return (indirect && indlocal) ? 2 : 0;
}

#undef sprite_draw_pixel_16
#undef sprite_draw_pixel_256


void S32Video::sprite_render_list()
{
    rectangle outerclip, clipin, clipout;
    int xoffs = 0, yoffs = 0;
    int numentries = 0;
    int spritenum = 0;

    /* compute the outer clip */
    outerclip.min_x = outerclip.min_y = 0;
    outerclip.max_x = (m_sprite_control_latched[0x0c / 2] & 1) ? 415 : 319;
    outerclip.max_y = 223;

    /* initialize the cliprects */
    clipin = outerclip;
    clipout.min_x = clipout.min_y = 0;
    clipout.max_x = clipout.max_y = -1;

    /* now draw */
    while (numentries++ < 0x20000 / 16)
    {
        /* top two bits are a command */
        uint16_t const* const sprite = &m_spriteram[8 * (spritenum & 0x1fff)];
        switch (sprite[0] >> 14)
        {
            /* command 0 = draw sprite */
            case 0:
                spritenum += 1 + draw_one_sprite(sprite, xoffs, yoffs, clipin, clipout);
                break;

            /* command 1 = set clipping */
            case 1:

                /* set the inclusive cliprect */
                if (sprite[0] & 0x1000)
                {
                    clipin.min_y = sext16(sprite[0], 12);
                    clipin.max_y = sext16(sprite[1], 12);
                    clipin.min_x = sext16(sprite[2], 12);
                    clipin.max_x = sext16(sprite[3], 12);
                    clipin &= outerclip;
                }

                /* set the exclusive cliprect */
                if (sprite[0] & 0x2000)
                {
                    clipout.min_y = sext16(sprite[4], 12);
                    clipout.max_y = sext16(sprite[5], 12);
                    clipout.min_x = sext16(sprite[6], 12);
                    clipout.max_x = sext16(sprite[7], 12);
                }

                /* advance to the next entry */
                spritenum++;
                break;

            /* command 2 = jump to position, and set X offset */
            case 2:

                /* set the global offset */
                if (sprite[0] & 0x2000)
                {
                    yoffs = sext16(sprite[1], 12);
                    xoffs = sext16(sprite[2], 12);
                }
                spritenum = sprite[0] & 0x1fff;
                break;

            /* command 3 = done */
            case 3:
                numentries = 0x20000 / 16;
                break;
        }
    }
}


/*************************************
 *
 *  Mixer layer render
 *
 *************************************/

uint8_t S32Video::compute_color_offsets(int which, int layerbit, int layerflag)
{
    int mode = ((m_mixer_control[which][0x3e / 2] & 0x8000) >> 14) | (layerbit & 1);

    switch (mode)
    {
        case 0:
        case 3:
        default:
            return !layerflag;

        case 1:
            /* fix me -- these are grayscale modes */
            return 2;

        case 2:
            return (!layerflag) ? 2 : 0;
    }
}

uint16_t S32Video::compute_sprite_blend(uint8_t encoding)
{
    int value = encoding & 0xf;

    switch ((encoding >> 4) & 3)
    {
        /* blend if priority == value */
        case 0:     return 1 << value;

        /* blend if priority <= value */
        case 1:     return (1 << value) | ((1 << value) - 1);

        /* blend if priority >= value */
        case 2:     return ~((1 << value) - 1) & 0xffff;

        /* blend always */
        default:
        case 3:     return 0xffff;
    }
}

uint16_t* S32Video::get_layer_scanline(int layer, int scanline)
{
    if (m_layer_data[layer].transparent[scanline])
        return (layer == MIXER_LAYER_SPRITES) ? m_solid_ffff : m_solid_0000;
    return m_layer_data[layer].pix(scanline);
}

void S32Video::mix_all_layers(int which, int xoffs, uint32_t* bitmap, int rowpixels, const rectangle& cliprect, uint8_t enablemask)
{
    int blendenable = m_mixer_control[which][0x4e / 2] & 0x0800;
    int blendfactor = (m_mixer_control[which][0x4e / 2] >> 8) & 7;
    struct mixer_layer_info
    {
        uint16_t      palbase;            /* palette base from control reg */
        uint16_t      sprblendmask;       /* mask of sprite priorities this layer blends with */
        uint8_t       blendmask;          /* mask of layers this layer blends with */
        uint8_t       index;              /* index of this layer (MIXER_LAYER_XXX) */
        uint8_t       effpri;             /* effective priority = (priority << 3) | layer_priority */
        uint8_t       mixshift;           /* shift from control reg */
        uint8_t       coloroffs;          /* color offset index */
    } layerorder[16][8], layersort[8];

    /* if we are the second monitor on multi32, swap in the proper sprite bank */
    if (which == 1)
    {
        std::swap(m_layer_data[MIXER_LAYER_SPRITES].bitmap, m_layer_data[MIXER_LAYER_MULTISPR].bitmap);
        std::swap(m_layer_data[MIXER_LAYER_SPRITES].transparent, m_layer_data[MIXER_LAYER_MULTISPR].transparent);
        std::swap(m_layer_data[MIXER_LAYER_SPRITES].num, m_layer_data[MIXER_LAYER_MULTISPR].num);
    }

    /* extract the RGB offsets */
    int rgboffs[3][3];
    rgboffs[0][0] = sext16(m_mixer_control[which][0x40 / 2], 6);
    rgboffs[0][1] = sext16(m_mixer_control[which][0x42 / 2], 6);
    rgboffs[0][2] = sext16(m_mixer_control[which][0x44 / 2], 6);
    rgboffs[1][0] = sext16(m_mixer_control[which][0x46 / 2], 6);
    rgboffs[1][1] = sext16(m_mixer_control[which][0x48 / 2], 6);
    rgboffs[1][2] = sext16(m_mixer_control[which][0x4a / 2], 6);
    rgboffs[2][0] = 0;
    rgboffs[2][1] = 0;
    rgboffs[2][2] = 0;

    /* determine the sprite grouping parameters first */
    uint8_t sprgroup_shift, sprgroup_mask, sprgroup_or;
    switch (m_mixer_control[which][0x4c / 2] & 0x0f)
    {
        default:
        case 0x0:   sprgroup_shift = 14;    sprgroup_mask = 0x00;   sprgroup_or = 0x01; break;
        case 0x1:   sprgroup_shift = 14;    sprgroup_mask = 0x01;   sprgroup_or = 0x02; break;
        case 0x2:   sprgroup_shift = 13;    sprgroup_mask = 0x03;   sprgroup_or = 0x04; break;
        case 0x3:   sprgroup_shift = 12;    sprgroup_mask = 0x07;   sprgroup_or = 0x08; break;

        case 0x4:   sprgroup_shift = 14;    sprgroup_mask = 0x01;   sprgroup_or = 0x00; break;
        case 0x5:   sprgroup_shift = 13;    sprgroup_mask = 0x03;   sprgroup_or = 0x00; break;
        case 0x6:   sprgroup_shift = 12;    sprgroup_mask = 0x07;   sprgroup_or = 0x00; break;
        case 0x7:   sprgroup_shift = 11;    sprgroup_mask = 0x0f;   sprgroup_or = 0x00; break;

        case 0x8:   sprgroup_shift = 14;    sprgroup_mask = 0x01;   sprgroup_or = 0x00; break;
        case 0x9:   sprgroup_shift = 13;    sprgroup_mask = 0x03;   sprgroup_or = 0x00; break;
        case 0xa:   sprgroup_shift = 12;    sprgroup_mask = 0x07;   sprgroup_or = 0x00; break;
        case 0xb:   sprgroup_shift = 11;    sprgroup_mask = 0x0f;   sprgroup_or = 0x00; break;

        case 0xc:   sprgroup_shift = 13;    sprgroup_mask = 0x01;   sprgroup_or = 0x00; break;
        case 0xd:   sprgroup_shift = 12;    sprgroup_mask = 0x03;   sprgroup_or = 0x00; break;
        case 0xe:   sprgroup_shift = 11;    sprgroup_mask = 0x07;   sprgroup_or = 0x00; break;
        case 0xf:   sprgroup_shift = 10;    sprgroup_mask = 0x0f;   sprgroup_or = 0x00; break;
    }
    int sprshadowmask = (m_mixer_control[which][0x4c / 2] & 0x04) ? 0x8000 : 0x0000;
    int sprpixmask = ((1 << sprgroup_shift) - 1) & 0x3fff;
    int sprshadow = 0x7ffe & sprpixmask;

    /* extract info about TEXT, NBG0-3, and BITMAP layers, which all follow the same pattern */
    int numlayers = 0;
    for (int laynum = MIXER_LAYER_TEXT; laynum <= MIXER_LAYER_BITMAP; laynum++)
    {
        int priority = m_mixer_control[which][0x20 / 2 + laynum] & 0x0f;
        if ((enablemask & (1 << laynum)) && priority != 0)
        {
            layersort[numlayers].index = laynum;
            layersort[numlayers].effpri = (priority << 3) | (6 - laynum);
            layersort[numlayers].palbase = (m_mixer_control[which][0x20 / 2 + laynum] & 0x00f0) << 6;
            layersort[numlayers].mixshift = (m_mixer_control[which][0x20 / 2 + laynum] >> 8) & 3;
            layersort[numlayers].blendmask = blendenable ? ((m_mixer_control[which][0x30 / 2 + laynum] >> 6) & 0xff) : 0;
            layersort[numlayers].sprblendmask = compute_sprite_blend(m_mixer_control[which][0x30 / 2 + laynum] & 0x3f);
            layersort[numlayers].coloroffs = compute_color_offsets(which, (m_mixer_control[which][0x3e / 2] >> laynum) & 1, (m_mixer_control[which][0x30 / 2 + laynum] >> 14) & 1);
            numlayers++;
        }
    }

    /* extract info about the BACKGROUND layer */
    layersort[numlayers].index = MIXER_LAYER_BACKGROUND;
    layersort[numlayers].effpri = (1 << 3) | 0;
    layersort[numlayers].palbase = (m_mixer_control[which][0x2c / 2] & 0x00f0) << 6;
    layersort[numlayers].mixshift = (m_mixer_control[which][0x2c / 2] >> 8) & 3;
    layersort[numlayers].blendmask = 0;
    layersort[numlayers].sprblendmask = 0;
    layersort[numlayers].coloroffs = compute_color_offsets(which, (m_mixer_control[which][0x3e / 2] >> 8) & 1, (m_mixer_control[which][0x3e / 2] >> 14) & 1);
    numlayers++;

    /* now bubble sort the list by effective priority */
    for (int laynum = 0; laynum < numlayers; laynum++)
        for (int i = laynum + 1; i < numlayers; i++)
            if (layersort[i].effpri > layersort[laynum].effpri)
            {
                mixer_layer_info temp = layersort[i];
                layersort[i] = layersort[laynum];
                layersort[laynum] = temp;
            }

    /* for each possible sprite group, insert the sprites into the list at the appropriate point */
    for (int groupnum = 0; groupnum <= sprgroup_mask; groupnum++)
    {
        int effgroup = sprgroup_or | groupnum;
        int priority = m_mixer_control[which][0x00 / 2 + effgroup] & 0x0f;
        int effpri = (priority << 3) | 7;
        int sprindex = numlayers;
        int dstnum = 0;

        /* make a copy of the sorted list, finding a location for the sprite entry */
        for (int laynum = 0; laynum < numlayers; laynum++)
        {
            if (effpri > layersort[laynum].effpri && sprindex == numlayers)
                sprindex = dstnum++;
            layerorder[groupnum][dstnum++] = layersort[laynum];
        }

        /* build the sprite entry */
        layerorder[groupnum][sprindex].index = MIXER_LAYER_SPRITES;
        layerorder[groupnum][sprindex].effpri = effpri;
        if ((m_mixer_control[which][0x4c / 2] & 3) != 3)
            layerorder[groupnum][sprindex].palbase = (m_mixer_control[which][0x00 / 2 + effgroup] & 0x00f0) << 6;
        else
            layerorder[groupnum][sprindex].palbase = (m_mixer_control[which][0x4c / 2] & 0x00f0) << 6;
        layerorder[groupnum][sprindex].mixshift = (m_mixer_control[which][0x00 / 2 + effgroup] >> 8) & 3;
        layerorder[groupnum][sprindex].blendmask = 0;
        layerorder[groupnum][sprindex].sprblendmask = 0;
        layerorder[groupnum][sprindex].coloroffs = compute_color_offsets(which, (m_mixer_control[which][0x3e / 2] >> 6) & 1, (m_mixer_control[which][0x4c / 2] >> 15) & 1);
    }

    /* based on the sprite controller flip bits, the data is scanned to us in different */
    /* directions; account for this */
    int sprx_start, sprdx;
    if (m_sprite_control_latched[0x04 / 2] & 1)
    {
        sprx_start = cliprect.max_x;
        sprdx = -1;
    }
    else
    {
        sprx_start = cliprect.min_x;
        sprdx = 1;
    }

    int spry, sprdy;
    if (m_sprite_control_latched[0x04 / 2] & 2)
    {
        spry = cliprect.max_y;
        sprdy = -1;
    }
    else
    {
        spry = cliprect.min_y;
        sprdy = 1;
    }

    /* loop over rows */
    for (int y = cliprect.min_y; y <= cliprect.max_y; y++, spry += sprdy)
    {
        uint32_t* const dest = &bitmap[(size_t) y * rowpixels + xoffs];
        uint16_t* layerbase[8];

        /* get the starting address for each layer */
        layerbase[MIXER_LAYER_TEXT] = get_layer_scanline(MIXER_LAYER_TEXT, y);
        layerbase[MIXER_LAYER_NBG0] = get_layer_scanline(MIXER_LAYER_NBG0, y);
        layerbase[MIXER_LAYER_NBG1] = get_layer_scanline(MIXER_LAYER_NBG1, y);
        layerbase[MIXER_LAYER_NBG2] = get_layer_scanline(MIXER_LAYER_NBG2, y);
        layerbase[MIXER_LAYER_NBG3] = get_layer_scanline(MIXER_LAYER_NBG3, y);
        layerbase[MIXER_LAYER_BITMAP] = get_layer_scanline(MIXER_LAYER_BITMAP, y);
        layerbase[MIXER_LAYER_SPRITES] = get_layer_scanline(MIXER_LAYER_SPRITES, spry);
        layerbase[MIXER_LAYER_BACKGROUND] = get_layer_scanline(MIXER_LAYER_BACKGROUND, y);

        /* loop over columns */
        for (int x = cliprect.min_x, sprx = sprx_start; x <= cliprect.max_x; x++, sprx += sprdx)
        {
            mixer_layer_info const* first;
            int laynum, firstpix;
            int shadow = 0;

            /* first grab the current sprite pixel and determine the group */
            int sprpix = layerbase[MIXER_LAYER_SPRITES][sprx];
            int sprgroup = (sprpix >> sprgroup_shift) & sprgroup_mask;

            /* now scan the layers to find the topmost non-transparent pixel */
            for (first = &layerorder[sprgroup][0]; ; first++)
            {
                laynum = first->index;

                /* non-sprite layers are treated similarly */
                if (laynum != MIXER_LAYER_SPRITES)
                {
                    firstpix = layerbase[laynum][x] & 0x1fff;
                    if (firstpix != 0 || laynum == MIXER_LAYER_BACKGROUND)
                        break;
                }

                /* sprite layers are special */
                else
                {
                    firstpix = sprpix;
                    shadow = ~firstpix & sprshadowmask;
                    if ((firstpix & 0x7fff) != 0x7fff)
                    {
                        firstpix &= sprpixmask;
                        if ((firstpix & 0x7ffe) != sprshadow)
                            break;
                        shadow = 1;
                    }
                }
            }

            /* adjust the first pixel */
            firstpix = m_paletteram[which][(first->palbase + ((firstpix >> first->mixshift) & 0xfff0) + (firstpix & 0x0f)) & 0x3fff];

            /* compute R, G, B */
            int const* rgbdelta = &rgboffs[first->coloroffs][0];
            int r = ((firstpix >>  0) & 0x1f) + rgbdelta[0];
            int g = ((firstpix >>  5) & 0x1f) + rgbdelta[1];
            int b = ((firstpix >> 10) & 0x1f) + rgbdelta[2];

            /* if there are potential blends, keep looking */
            if (first->blendmask != 0)
            {
                mixer_layer_info const* second;
                int secondpix;

                /* now scan the layers to find the topmost non-transparent pixel */
                for (second = first + 1; ; second++)
                {
                    laynum = second->index;

                    /* non-sprite layers are treated similarly */
                    if (laynum != MIXER_LAYER_SPRITES)
                    {
                        secondpix = layerbase[laynum][x] & 0x1fff;
                        if (secondpix != 0 || laynum == MIXER_LAYER_BACKGROUND)
                            break;
                    }

                    /* sprite layers are special */
                    else
                    {
                        secondpix = sprpix;
                        shadow = ~secondpix & sprshadowmask;
                        if ((secondpix & 0x7fff) != 0x7fff)
                        {
                            secondpix &= sprpixmask;
                            if ((secondpix & 0x7ffe) != sprshadow)
                                break;
                            shadow = 1;
                        }
                    }
                }

                /* are we blending with that layer? */
                if ((first->blendmask & (1 << laynum)) &&
                    (laynum != MIXER_LAYER_SPRITES || (first->sprblendmask & (1 << sprgroup))))
                {
                    /* adjust the second pixel */
                    secondpix = m_paletteram[which][(second->palbase + ((secondpix >> second->mixshift) & 0xfff0) + (secondpix & 0x0f)) & 0x3fff];

                    /* compute first RGB */
                    r *= 7 - blendfactor;
                    g *= 7 - blendfactor;
                    b *= 7 - blendfactor;

                    /* add in second RGB */
                    rgbdelta = &rgboffs[second->coloroffs][0];
                    r += (((secondpix >>  0) & 0x1f) + rgbdelta[0]) * (blendfactor + 1);
                    g += (((secondpix >>  5) & 0x1f) + rgbdelta[1]) * (blendfactor + 1);
                    b += (((secondpix >> 10) & 0x1f) + rgbdelta[2]) * (blendfactor + 1);

                    /* shift off the extra bits */
                    r >>= 3;
                    g >>= 3;
                    b >>= 3;
                }
            }

            /* apply shadow/hilight */
            if (shadow)
            {
                r >>= 1;
                g >>= 1;
                b >>= 1;
            }

            /* clamp and combine */
            if (r > 31)
                firstpix = 31 << (16 + 3);
            else if (r > 0)
                firstpix = r << (16 + 3);
            else
                firstpix = 0;

            if (g > 31)
                firstpix |= 31 << (8 + 3);
            else if (g > 0)
                firstpix |= g << (8 + 3);

            if (b > 31)
                firstpix |= 31 << (0 + 3);
            else if (b > 0)
                firstpix |= b << (0 + 3);
            dest[x] = firstpix;
        }
    }

    /* if we are the second monitor on multi32, swap back the sprite layer */
    if (which == 1)
    {
        std::swap(m_layer_data[MIXER_LAYER_SPRITES].bitmap, m_layer_data[MIXER_LAYER_MULTISPR].bitmap);
        std::swap(m_layer_data[MIXER_LAYER_SPRITES].transparent, m_layer_data[MIXER_LAYER_MULTISPR].transparent);
        std::swap(m_layer_data[MIXER_LAYER_SPRITES].num, m_layer_data[MIXER_LAYER_MULTISPR].num);
    }
}


/*************************************
 *
 *  Master update routine
 *
 *************************************/

uint32_t S32Video::multi32_update(int index, const rectangle& cliprect)
{
    uint8_t enablemask;
    uint32_t* bitmap = m_screen_bitmap[index][m_curbitmap[index]].data();

    /* update the visible area */
    // PORT: screen.set_visible_area() -> screen_device::configure(): only the visible area
    // matters here (no bitmap reallocation: the screen size stays 416 x 262)
    if (m_videoram[0x1ff00 / 2] & 0x8000)
        m_visarea[index] = rectangle(0, 52 * 8 - 1, 0, 28 * 8 - 1);
    else
        m_visarea[index] = rectangle(0, 40 * 8 - 1, 0, 28 * 8 - 1);

    /* if the display is off, punt */
    if (!m_system32_displayenable[index])
    {
        // bitmap.fill(m_palette->black_pen(), cliprect): the palette is RGB32 format, so
        // black_pen() is rgb_t::black() = 0xff000000
        for (int y = cliprect.min_y; y <= cliprect.max_y; y++)
            for (int x = cliprect.min_x; x <= cliprect.max_x; x++)
                bitmap[(size_t) y * SCREEN_W_MAX + x] = 0xff000000;
        return 0;
    }

    /* update the tilemaps */
    enablemask = update_tilemaps(index, cliprect);

    /* do the mixing */
    mix_all_layers(index, 0, bitmap, SCREEN_W_MAX, cliprect, enablemask);

    return 0;
}


uint32_t S32Video::screen_update(int which)
{
    // PORT: what video_manager::finish_screen_updates does for one screen:
    // screen.update_partial(screen.visible_area().max_y) with no earlier partial update this
    // frame, i.e. cliprect = the visible area at the start of the update, lines 0 to
    // visible max_y, drawn into m_bitmap[m_curbitmap]; then update_quads() shows that bitmap
    // (with the visible area as the update left it) and flips m_curbitmap.
    rectangle clip = m_visarea[which];
    clip.min_y = 0;
    clip.max_y = m_visarea[which].max_y;
    uint32_t r = multi32_update(which, clip);
    m_curtexture[which] = m_curbitmap[which];
    m_curbitmap[which] = 1 - m_curbitmap[which];
    return r;
}


const uint32_t* S32Video::screen_pixels(int which) const
{
    return m_screen_bitmap[which][m_curtexture[which]].data();
}


void S32Video::get_frame(int which, uint32_t* dst) const
{
    // PORT: MAME's mixer writes 0x00RRGGBB, its display-off fill 0xff000000 (opaque black);
    // the alpha byte is dropped here
    const uint32_t* src = screen_pixels(which);
    int w = visible_width(which);
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < w; x++)
            dst[y * w + x] = src[(size_t) y * SCREEN_W_MAX + x] & 0x00ffffff;
}
