/***************************************************************************
    Screen of the emulated Super Hang-On board. See shvideo.hpp.
***************************************************************************/
#include "crossover/shvideo.hpp"
#include "crossover/shboard.hpp"
#include "crossover/romset.hpp"

#include <cstring>
#include <cmath>

SHVideo shvideo;

static inline uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
static inline void put16(uint8_t* p, uint16_t v) { p[0] = v >> 8; p[1] = (uint8_t) v; }

// MAME segaic16 palette_init: each 5-bit gun through 3.9K/2K/1K/500/250 ohm resistors; the
// shadow/highlight entries add a 470 ohm resistor pulled low (shadow) or high (highlight).
// Weights normalised so that every input high gives 255 (compute_resistor_weights, auto scale).
static void weights(const double* r, int n, double* w)
{
    double g = 0;
    for (int i = 0; i < n; i++) g += 1.0 / r[i];
    for (int i = 0; i < n; i++) w[i] = 255.0 * (1.0 / r[i]) / g;
}

bool SHVideo::init(const RomSet& roms)
{
    ok = false;
    try
    {
        const auto& g = roms["gfx1"].data;            // 3 bitplanes of 8x8 tiles
        int plane = (int) g.size() / 3;
        num_tiles = plane / 8;
        tiles.assign((size_t) num_tiles * 64, 0);
        for (int t = 0; t < num_tiles; t++)
            for (int y = 0; y < 8; y++)
            {
                uint8_t p0 = g[t * 8 + y], p1 = g[plane + t * 8 + y], p2 = g[2 * plane + t * 8 + y];
                for (int x = 0; x < 8; x++)
                    tiles[(size_t) t * 64 + y * 8 + x] =
                        (uint8_t)((((p2 >> (7 - x)) & 1) << 2) | (((p1 >> (7 - x)) & 1) << 1) | ((p0 >> (7 - x)) & 1));
            }

        // segaic16_road_outrun_decode
        const auto& r = roms["segaic16road"].data;
        int len = (int) r.size();
        road.assign((size_t)(256 * 2 + 1) * 512, 0);
        for (int y = 0; y < 256 * 2; y++)
        {
            const uint8_t* src = &r[((y & 0xff) * 0x40 + (y >> 8) * 0x8000) % len];
            uint8_t* dst = &road[(size_t) y * 512];
            for (int x = 0; x < 512; x++)
            {
                dst[x] = (uint8_t)((((src[x >> 3] >> (~x & 7)) & 1) << 0) | (((src[(x >> 3) + 0x4000] >> (~x & 7)) & 1) << 1));
                if (x >= 256 - 8 && x < 256 && dst[x] == 3) dst[x] |= 4;
            }
        }
        std::memset(&road[(size_t) 256 * 2 * 512], 3, 512);

        const auto& s = roms["sprites"].data;
        sprites.resize(s.size() / 2);
        for (size_t i = 0; i < sprites.size(); i++) sprites[i] = (uint16_t)(s[2 * i] << 8 | s[2 * i + 1]);
    }
    catch (...) { return false; }

    static const double rn[5] = { 3900, 2000, 1000, 1000.0 / 2, 1000.0 / 4 };
    static const double rs[6] = { 3900, 2000, 1000, 1000.0 / 2, 1000.0 / 4, 470 };
    double wn[5], ws[6];
    weights(rn, 5, wn);
    weights(rs, 6, ws);
    for (int v = 0; v < 32; v++)
    {
        double n = 0, s = 0;
        for (int i = 0; i < 5; i++) if ((v >> i) & 1) { n += wn[i]; s += ws[i]; }
        pal_normal[v]  = (uint8_t)(int)(n + 0.5);
        pal_shadow[v]  = (uint8_t)(int)(s + 0.5);
        pal_hilight[v] = (uint8_t)(int)(s + ws[5] + 0.5);
    }
    std::memset(pixels, 0, sizeof pixels);
    ok = true;
    return true;
}

uint32_t SHVideo::rgb(const SHBoard& b, uint16_t index) const
{
    uint16_t v = be16(&b.palram[(index & 0xfff) * 2]);
    uint8_t r = (uint8_t)(((v >> 12) & 0x01) | ((v << 1) & 0x1e));
    uint8_t g = (uint8_t)(((v >> 13) & 0x01) | ((v >> 3) & 0x1e));
    uint8_t bl = (uint8_t)(((v >> 14) & 0x01) | ((v >> 7) & 0x1e));
    const uint8_t* t = index < 0x1000 ? pal_normal : ((v & 0x8000) ? pal_hilight : pal_shadow);
    return (uint32_t) t[r] << 16 | (uint32_t) t[g] << 8 | t[bl];
}

void SHVideo::palette_rgb(const SHBoard& b, uint32_t* out) const
{
    for (int i = 0; i < 0x2000; i++) out[i] = rgb(b, (uint16_t) i);
}

// ------------------------------------------------------------------------------------------------
// Road (segaic16_road_outrun_draw), reading the half the road chip displays (roadbuf)
// ------------------------------------------------------------------------------------------------
void SHVideo::draw_road(SHBoard& b, bool foreground)
{
    const int colorbase1 = 0x7f6, colorbase2 = 0x7c0, colorbase3 = 0x7c0, xoffs = 0;
    auto rr = [&](int w) { return be16(&b.roadbuf[(w & 0x7ff) * 2]); };
    static const uint8_t priority_map[2][8] = { { 0x80,0x81,0x81,0x87,0,0,0,0x00 }, { 0x81,0x81,0x81,0x8f,0,0,0,0x80 } };
    const uint8_t ctl = b.road_control;
    for (int y = 0; y < H; y++)
    {
        uint16_t* dest = &pixels[y * W];
        const uint16_t data0 = rr(0x000 + y), data1 = rr(0x100 + y);
        if (!foreground)
        {
            int color = -1;
            switch (ctl & 3)
            {
                case 0: if (data0 & 0x800) color = data0 & 0x7f; break;
                case 1: if (data0 & 0x800) color = data0 & 0x7f; else if (data1 & 0x800) color = data1 & 0x7f; break;
                case 2: if (data1 & 0x800) color = data1 & 0x7f; else if (data0 & 0x800) color = data0 & 0x7f; break;
                case 3: if (data1 & 0x800) color = data1 & 0x7f; break;
            }
            if (color != -1)
                for (int x = 0; x < W; x++) dest[x] = (uint16_t)(color | colorbase3);
            continue;
        }
        if ((data0 & 0x800) && (data1 & 0x800)) continue;
        const uint8_t* src0 = (data0 & 0x800) ? &road[(size_t) 256 * 2 * 512] : &road[(size_t)(0x000 + ((data0 >> 1) & 0xff)) * 512];
        int hpos0 = rr(0x200 + ((ctl & 4) ? y : (data0 & 0x1ff))) & 0xfff;
        int color0 = rr(0x600 + ((ctl & 4) ? y : (data0 & 0x1ff)));
        const uint8_t* src1 = (data1 & 0x800) ? &road[(size_t) 256 * 2 * 512] : &road[(size_t)(0x100 + ((data1 >> 1) & 0xff)) * 512];
        int hpos1 = rr(0x400 + ((ctl & 4) ? (0x100 + y) : (data1 & 0x1ff))) & 0xfff;
        int color1 = rr(0x600 + ((ctl & 4) ? (0x100 + y) : (data1 & 0x1ff)));
        uint16_t ct[32] = {};
        ct[0x00] = (uint16_t)(colorbase1 ^ 0x00 ^ ((color0 >> 0) & 1));
        ct[0x01] = (uint16_t)(colorbase1 ^ 0x02 ^ ((color0 >> 1) & 1));
        ct[0x02] = (uint16_t)(colorbase1 ^ 0x04 ^ ((color0 >> 2) & 1));
        ct[0x03] = (data0 & 0x200) ? ct[0x00] : (uint16_t)(colorbase2 ^ 0x00 ^ ((color0 >> 8) & 0xf));
        ct[0x07] = (uint16_t)(colorbase1 ^ 0x06 ^ ((color0 >> 3) & 1));
        ct[0x10] = (uint16_t)(colorbase1 ^ 0x08 ^ ((color1 >> 4) & 1));
        ct[0x11] = (uint16_t)(colorbase1 ^ 0x0a ^ ((color1 >> 5) & 1));
        ct[0x12] = (uint16_t)(colorbase1 ^ 0x0c ^ ((color1 >> 6) & 1));
        ct[0x13] = (data1 & 0x200) ? ct[0x10] : (uint16_t)(colorbase2 ^ 0x10 ^ ((color1 >> 8) & 0xf));
        ct[0x17] = (uint16_t)(colorbase1 ^ 0x0e ^ ((color1 >> 7) & 1));
        switch (ctl & 3)
        {
            case 0:
                if (data0 & 0x800) continue;
                hpos0 = (hpos0 - (0x5f8 + xoffs)) & 0xfff;
                for (int x = 0; x < W; x++) { int p0 = hpos0 < 0x200 ? src0[hpos0] : 3; dest[x] = ct[p0]; hpos0 = (hpos0 + 1) & 0xfff; }
                break;
            case 1: case 2:
            {
                const uint8_t* pm = priority_map[(ctl & 3) - 1];
                hpos0 = (hpos0 - (0x5f8 + xoffs)) & 0xfff;
                hpos1 = (hpos1 - (0x5f8 + xoffs)) & 0xfff;
                for (int x = 0; x < W; x++)
                {
                    int p0 = hpos0 < 0x200 ? src0[hpos0] : 3, p1 = hpos1 < 0x200 ? src1[hpos1] : 3;
                    dest[x] = ((pm[p0] >> p1) & 1) ? ct[0x10 + p1] : ct[p0];
                    hpos0 = (hpos0 + 1) & 0xfff; hpos1 = (hpos1 + 1) & 0xfff;
                }
                break;
            }
            case 3:
                if (data1 & 0x800) continue;
                hpos1 = (hpos1 - (0x5f8 + xoffs)) & 0xfff;
                for (int x = 0; x < W; x++) { int p1 = hpos1 < 0x200 ? src1[hpos1] : 3; dest[x] = ct[0x10 + p1]; hpos1 = (hpos1 + 1) & 0xfff; }
                break;
        }
    }
}

// ------------------------------------------------------------------------------------------------
// Tilemaps (System 16B, TILEMAP_16B_ALT tile format)
// ------------------------------------------------------------------------------------------------
// The virtual tilemap is 2x2 pages of 64x32 tiles (1024x512 pixels); the page of each quarter
// comes from one nibble of 'pages' (upper-left, upper-right, lower-left, lower-right).
void SHVideo::draw_virtual(SHBoard& b, int x0, int x1, int y0, int y1, uint16_t pages,
                           uint16_t xscroll, uint16_t yscroll, int category, uint8_t mark)
{
    for (int y = y0; y <= y1; y++)
    {
        int vy = (y + yscroll) & 0x1ff;
        for (int x = x0; x <= x1; x++)
        {
            int vx = (x + xscroll) & 0x3ff;
            int page = (pages >> (4 * ((vy >= 256 ? 2 : 0) + (vx >= 512 ? 1 : 0)))) & 0xf;
            int tx = (vx & 0x1ff) >> 3, ty = (vy & 0xff) >> 3;
            uint16_t data = be16(&b.tileram[(page * 64 * 32 + ty * 64 + tx) * 2]);
            if (((data >> 15) & 1) != category) continue;
            int code = (data & 0x1fff) % num_tiles;
            int color = (data >> 5) & 0x7f;
            uint8_t pen = tiles[(size_t) code * 64 + (vy & 7) * 8 + (vx & 7)];
            if (!pen) continue;
            pixels[y * W + x] = (uint16_t)(color * 8 + pen);
            pri[y * W + x] |= mark;
        }
    }
}

void SHVideo::draw_tile_layer(SHBoard& b, int which, int category, uint8_t mark)
{
    // tilemap_16b_draw_layer
    const uint16_t xscroll = b.latched_xscroll[which], yscroll = b.latched_yscroll[which];
    const uint16_t pages = b.latched_page[which];
    auto tr = [&](int w) { return be16(&b.textram[(w & 0x7ff) * 2]); };
    const int xoffs = 0;
    for (int y = 0; y < H; y += 8)
    {
        int rowscrollindex = y / 8;
        int ry0 = y, ry1 = std::min(y + 7, H - 1);
        uint16_t rowscroll = tr(0xf80 / 2 + 0x40 / 2 * which + rowscrollindex);
        if (yscroll & 0x8000)
        {
            // column and row scroll
            for (int x = ((0 + 8) & ~15) - 8; x < W; x += 16)
            {
                int cx0 = std::max(x, 0), cx1 = std::min(x + 15, W - 1);
                uint16_t effx = (xscroll & 0x8000) ? rowscroll : xscroll;
                uint16_t effy = tr(0xf16 / 2 + 0x40 / 2 * which + (x + 8) / 16);
                uint16_t effp = pages;
                if (rowscroll & 0x8000)
                {
                    effx = b.latched_xscroll[which + 2]; effy = b.latched_yscroll[which + 2]; effp = b.latched_page[which + 2];
                }
                effx = (0xc0 - effx + xoffs) & 0x3ff;
                effy = effy & 0x1ff;
                draw_virtual(b, cx0, cx1, ry0, ry1, effp, effx, effy, category, mark);
            }
        }
        else
        {
            uint16_t effx = (xscroll & 0x8000) ? rowscroll : xscroll;
            uint16_t effy = yscroll, effp = pages;
            if (rowscroll & 0x8000)
            {
                effx = b.latched_xscroll[which + 2]; effy = b.latched_yscroll[which + 2]; effp = b.latched_page[which + 2];
            }
            effx = (0xc0 - effx + xoffs) & 0x3ff;
            effy = effy & 0x1ff;
            draw_virtual(b, 0, W - 1, ry0, ry1, effp, effx, effy, category, mark);
        }
    }
}

void SHVideo::draw_text(SHBoard& b, int category, uint8_t mark)
{
    // 64x28 tiles; the visible part starts at column 24 (scrolldx -192); TILEMAP_16B_ALT text format
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
        {
            int tx = ((x + 192) >> 3) & 63, ty = y >> 3;
            uint16_t data = be16(&b.textram[(ty * 64 + tx) * 2]);
            if (((data >> 15) & 1) != category) continue;
            int code = (data & 0xff) % num_tiles;
            int color = (data >> 8) & 0x07;
            uint8_t pen = tiles[(size_t) code * 64 + (y & 7) * 8 + ((x + 192) & 7)];
            if (!pen) continue;
            pixels[y * W + x] = (uint16_t)(color * 8 + pen);
            pri[y * W + x] |= mark;
        }
}

// ------------------------------------------------------------------------------------------------
// Sprites (sega_sys16b_sprite_device::draw; sprite x 184 is screen x 0)
// ------------------------------------------------------------------------------------------------
void SHVideo::draw_sprites(SHBoard& b)
{
    const int XO = 184;
    const int clip_min = XO, clip_max = XO + W - 1;
    const int numbanks = (int)(sprites.size() * 2 / 0x20000);
    for (int i = 0; i < W * H; i++) spr[i] = 0xffff;
    for (int e = 0; e < 0x1000; e += 16)
    {
        uint8_t* d = &b.sprram[e];
        auto D = [&](int k) { return be16(&d[2 * k]); };
        if (D(2) & 0x8000) break;
        int bottom = D(0) >> 8, top = D(0) & 0xff;
        int xpos = D(1) & 0x1ff;
        int hide = D(2) & 0x4000, flip = D(2) & 0x100;
        int pitch = (int8_t)(D(2) & 0xff);
        uint16_t addr = D(3);
        int bank = (D(4) >> 8) & 0xf;
        int colpri = ((D(4) & 0xff) << 4) | (((D(1) >> 9) & 0xf) << 12);
        int vzoom = (D(5) >> 5) & 0x1f, hzoom = D(5) & 0x1f;
        put16(&d[14], addr);                                  // data[7] = addr
        if (hide || top >= bottom) continue;
        if (numbanks) bank %= numbanks;
        const uint16_t* sd = &sprites[(size_t) 0x10000 * bank];
        put16(&d[10], D(5) & 0x03ff);                         // reset the y zoom counter
        for (int y = top; y < bottom; y++)
        {
            addr += pitch;
            uint16_t d5 = (uint16_t)(D(5) + (vzoom << 10));
            if (d5 & 0x8000) { addr += pitch; d5 &= ~0x8000; }
            put16(&d[10], d5);
            if (y >= 0 && y < H)
            {
                uint16_t* dest = &spr[y * W];
                int x, xacc = 4 * hzoom;
                uint16_t d7;
                if (!flip)
                {
                    d7 = (uint16_t)(addr - 1);
                    for (x = xpos; ((xpos - x) & 0x1ff) != 1; )
                    {
                        uint16_t pixels_w = sd[++d7];
                        int pix = 0;
                        for (int n = 0; n < 4; n++)
                        {
                            pix = (pixels_w >> (12 - 4 * n)) & 0xf;
                            xacc = (xacc & 0x3f) + hzoom;
                            if (xacc < 0x40)
                            {
                                if (x >= clip_min && x <= clip_max && pix != 0 && pix != 15) dest[x - XO] = (uint16_t)(colpri | pix);
                                x++;
                            }
                        }
                        if (pix == 15) break;
                    }
                }
                else
                {
                    d7 = (uint16_t)(addr + 1);
                    for (x = xpos; ((xpos - x) & 0x1ff) != 1; )
                    {
                        uint16_t pixels_w = sd[--d7];
                        int pix = 0;
                        for (int n = 0; n < 4; n++)
                        {
                            pix = (pixels_w >> (4 * n)) & 0xf;
                            xacc = (xacc & 0x3f) + hzoom;
                            if (xacc < 0x40)
                            {
                                if (x >= clip_min && x <= clip_max && pix != 0 && pix != 15) dest[x - XO] = (uint16_t)(colpri | pix);
                                x++;
                            }
                        }
                        if (pix == 15) break;
                    }
                }
                put16(&d[14], d7);
            }
        }
    }
}

// ------------------------------------------------------------------------------------------------
// screen_update_shangon
// ------------------------------------------------------------------------------------------------
void SHVideo::render(SHBoard& b)
{
    if (!ok) return;
    std::memset(pri, 0, sizeof pri);
    draw_sprites(b);                        // sprites draw into their own bitmap first
    draw_road(b, false);
    draw_tile_layer(b, 1, 0, 0x01);         // background
    draw_tile_layer(b, 1, 1, 0x02);
    draw_tile_layer(b, 0, 0, 0x02);         // foreground
    draw_tile_layer(b, 0, 1, 0x04);
    draw_road(b, true);
    draw_text(b, 0, 0x08);
    draw_text(b, 1, 0x08);
    for (int i = 0; i < W * H; i++)
    {
        uint16_t p = spr[i];
        if (p == 0xffff) continue;
        int priority = (p >> 10) & 3;
        if ((1 << priority) > pri[i])
        {
            if ((p & 0x03f0) == 0x03f0) { if (pixels[i] < 0x1000) pixels[i] = (uint16_t)(pixels[i] + 0x1000); }
            else                        pixels[i] = (uint16_t)(0x400 | (p & 0x3ff));
        }
    }
}
