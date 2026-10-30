/***************************************************************************
    Screen of the emulated Hang-On board. See hovideo.hpp.
***************************************************************************/
#include "crossover/hovideo.hpp"
#include "crossover/hoboard.hpp"
#include "crossover/romset.hpp"

#include <cstring>
#include <cstdlib>
#include <algorithm>

// definitions of the class constants (used by reference, e.g. in std::min, in unoptimised builds)
const int HOVideo::W, HOVideo::H, HOVideo::MAXW;

HOVideo hovideo;

static inline uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
static inline void put16(uint8_t* p, uint16_t v) { p[0] = v >> 8; p[1] = (uint8_t) v; }

// MAME segaic16 palette_init (as SHVideo)
static void weights(const double* r, int n, double* w)
{
    double g = 0;
    for (int i = 0; i < n; i++) g += 1.0 / r[i];
    for (int i = 0; i < n; i++) w[i] = 255.0 * (1.0 / r[i]) / g;
}

bool HOVideo::init(const RomSet& roms)
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

        // segaic16_road_hangon_decode
        const auto& r = roms["segaic16road"].data;
        int len = (int) r.size();
        road.assign((size_t) 256 * 512, 0);
        for (int y = 0; y < 256; y++)
        {
            const uint8_t* src = &r[((y & 0xff) * 0x40) % len];
            uint8_t* dst = &road[(size_t) y * 512];
            for (int x = 0; x < 512; x++)
                dst[x] = (uint8_t)((((src[x >> 3] >> (~x & 7)) & 1) << 0) | (((src[(x >> 3) + 0x4000] >> (~x & 7)) & 1) << 1));
        }

        const auto& s = roms["sprites"].data;
        sprites.resize(s.size() / 2);
        for (size_t i = 0; i < sprites.size(); i++) sprites[i] = (uint16_t)(s[2 * i] << 8 | s[2 * i + 1]);
        zoom = roms["sprites:zoom"].data;
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

void HOVideo::set_width(int w)
{
    width = std::max(W, std::min(w, MAXW));
    xoff = (width - W) / 2;
}

uint32_t HOVideo::rgb(const HOBoard& b, uint16_t index) const
{
    if (index >= 0x1800) return 0;
    uint16_t v = be16(&b.palram[(index & 0x7ff) * 2]);
    uint8_t r = (uint8_t)(((v >> 12) & 0x01) | ((v << 1) & 0x1e));
    uint8_t g = (uint8_t)(((v >> 13) & 0x01) | ((v >> 3) & 0x1e));
    uint8_t bl = (uint8_t)(((v >> 14) & 0x01) | ((v >> 7) & 0x1e));
    const uint8_t* t = index < 0x800 ? pal_normal : (index < 0x1000 ? pal_shadow : pal_hilight);
    return (uint32_t) t[r] << 16 | (uint32_t) t[g] << 8 | t[bl];
}

void HOVideo::palette_rgb(const HOBoard& b, uint32_t* out) const
{
    for (int i = 0; i < 0x2000; i++) out[i] = rgb(b, (uint16_t) i);
}

// ------------------------------------------------------------------------------------------------
// Road (segaic16_road_hangon_draw)
// ------------------------------------------------------------------------------------------------
void HOVideo::draw_road(HOBoard& b, bool foreground)
{
    const int colorbase1 = 0x038, colorbase2 = 0x7c0;
    auto rr = [&](int w) { return be16(&b.roadram[(w & 0x7ff) * 2]); };
    for (int y = 0; y < H; y++)
    {
        uint16_t* dest = &pixels[y * width];
        const uint16_t control = rr(0x000 + y);
        int hpos = rr(0x100 + (control & 0xff));
        const uint16_t color0 = rr(0x200 + (control & 0xff));
        const uint16_t color1 = rr(0x300 + (control & 0xff));
        const uint16_t plycont = (control >> 10) & 3;
        if ((plycont == 0) == foreground) continue;
        const uint8_t* src = &road[(size_t)(control & 0xff) * 512];
        // Widescreen: the counters start xoff pixels earlier, as if the line began further left. On the road's
        // lines (control bit 10) with the mirror enabled (bit 8) the counter runs up to $FF and then back down,
        // and a line whose hpos has bit 11 clear starts on the way down: step back along that whole run. (The
        // lines above the road keep a plain offset: stepping back there would show the end of the road ROM's
        // row 0, which the board keeps in the 24 pixels left of the screen.)
        if (xoff)
        {
            if ((control & 0x500) == 0x500)
            {
                int c = (hpos >> 3) & 0xff, m = hpos & 7;
                int p = (((hpos & 0x800) ? c : 0x1fe - c) << 3 | m) - xoff;
                if (p < 0) p = 0;
                int i = p >> 3;
                hpos = i <= 0xff ? (0x800 | i << 3 | (p & 7)) : ((0x1fe - i) << 3 | (p & 7));
            }
            else hpos = (hpos & 0x800) | ((hpos - xoff) & 0x7ff);
        }
        int ctr9m = hpos & 7, ctr9n9p = (hpos >> 3) & 0xff, ff9j1 = (hpos >> 11) & 1, ff9j2 = 1, ss8j = 0;
        for (int x = -24 - xoff; x < width - xoff; x++)
        {
            const bool ena = (ctr9m == 7);
            if ((ctr9n9p & 0xff) == 0xff) ff9j1 = 0;
            if (!(control & 0x100)) ff9j1 = 1;
            if (!(control & 0x200)) ff9j2 = 1;
            int md = 3, color;
            if ((ctr9n9p & 0xc0) == 0xc0)
                md = (ss8j & 1) ? src[((ctr9n9p & 0x3f) << 3) | ctr9m] : src[((ctr9n9p & 0x3f) << 3) | (ctr9m ^ 7)];
            int select = (ss8j >> 3) & 1;
            if (ff9j2 && md == 3)
                color = ((color0 >> (select ? 0 : 8)) & 0x3f) | colorbase2;
            else
            {
                if ((color1 & 0x80) && md == 3) md = 0;
                color = (color1 >> ((md << 1) | select)) & 1;
                color |= select << 3;
                color |= md << 1;
                color |= colorbase1;
            }
            if (x >= -xoff) dest[x + xoff] = (uint16_t) color;
            ctr9m = (ctr9m + 1) & 7;
            if (ena) { if (ff9j1) ctr9n9p++; else ctr9n9p--; }
            ff9j2 = !(!ff9j1 && (ss8j & 0x80));
            ss8j = (ss8j << 1) | ff9j1;
        }
    }
}

// ------------------------------------------------------------------------------------------------
// Tilemaps (16A-style, TILEMAP_HANGON: 4 pages)
// ------------------------------------------------------------------------------------------------
void HOVideo::draw_virtual(HOBoard& b, int x0, int x1, int y0, int y1, uint16_t pages,
                           uint16_t xscroll, uint16_t yscroll, int category, uint8_t mark)
{
    auto page_at = [&](int vx, int vy) { return (pages >> (4 * ((vy >= 256 ? 2 : 0) + (vx >= 512 ? 1 : 0)))) & 0xf; };
    for (int y = y0; y <= y1; y++)
    {
        int vy = (y + yscroll) & 0x1ff;
        int page_l = page_at((0 + xscroll) & 0x3ff, vy), page_r = page_at((W - 1 + xscroll) & 0x3ff, vy);
        for (int x = x0; x <= x1; x++)
        {
            int vx = (x - xoff + xscroll) & 0x3ff;
            int page = page_at(vx, vy);
            if ((x < xoff && page != page_l) || (x >= xoff + W && page != page_r)) continue;
            int tx = (vx & 0x1ff) >> 3, ty = (vy & 0xff) >> 3;
            uint16_t data = be16(&b.tileram[((page & 3) * 64 * 32 + ty * 64 + tx) * 2]);
            if (((data >> 12) & 1) != category) continue;
            int code = (((data >> 1) & 0x1000) | (data & 0xfff)) % num_tiles;
            int color = (data >> 5) & 0x7f;
            uint8_t pen = tiles[(size_t) code * 64 + (vy & 7) * 8 + (vx & 7)];
            if (!pen) continue;
            pixels[y * width + x] = (uint16_t)(color * 8 + pen);
            pri[y * width + x] |= mark;
        }
    }
}

void HOVideo::draw_tile_layer(HOBoard& b, int which, int category, uint8_t mark)
{
    // tilemap_16a_draw_layer (not flipped)
    auto tr = [&](int byte) { return be16(&b.textram[byte & 0xffe]); };
    uint16_t xscroll = tr(0xff8 + 2 * which) & 0x1ff;
    uint16_t yscroll = tr(0xf24 + 2 * which) & 0x0ff;
    uint16_t pages = tr(0xe9e - 2 * which);
    pages = (uint16_t)(((pages >> 4) & 0x0707) | ((pages << 4) & 0x7070));
    pages &= 0x3333;
    const int xoffs = 0;
    auto colx = [&](int x) { return std::max(0, std::min(x / 16, 19)); };   // game-screen column of x
    if (b.colscroll || b.rowscroll)
    {
        for (int y = 0; y < H; y += 8)
        {
            int ry1 = std::min(y + 7, H - 1);
            uint16_t effx = b.rowscroll ? (tr(0xf80 + (y / 8) * 4 + 2 * which) & 0x1ff) : xscroll;
            effx = (0xc8 - effx + xoffs) & 0x3ff;
            if (!b.colscroll)
            {
                draw_virtual(b, 0, width - 1, y, ry1, pages, effx, yscroll & 0x1ff, category, mark);
                continue;
            }
            for (int x = -((xoff + 15) / 16) * 16; x < width - xoff; x += 16)
            {
                int gx0 = std::max(x, -xoff), gx1 = std::min(x + 15, width - xoff - 1);
                if (gx0 > gx1) continue;
                uint16_t effy = tr(0xf30 + colx(std::max(x, 0)) * 4 + 2 * which) & 0xff;
                draw_virtual(b, gx0 + xoff, gx1 + xoff, y, ry1, pages, effx, effy & 0x1ff, category, mark);
            }
        }
    }
    else
        draw_virtual(b, 0, width - 1, 0, H - 1, pages, (uint16_t)((0xc8 - xscroll + xoffs) & 0x3ff), yscroll & 0x1ff, category, mark);
}

void HOVideo::draw_text(HOBoard& b, int category, uint8_t mark)
{
    // 64x28 tiles, visible from column 24 (scrolldx -192); 16A text format
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
        {
            int tx = ((x + 192) >> 3) & 63, ty = y >> 3;
            uint16_t data = be16(&b.textram[(ty * 64 + tx) * 2]);
            if (((data >> 11) & 1) != category) continue;
            int code = (data & 0xff) % num_tiles;
            int color = (data >> 8) & 0x07;
            uint8_t pen = tiles[(size_t) code * 64 + (y & 7) * 8 + ((x + 192) & 7)];
            if (!pen) continue;
            pixels[y * width + x + xoff] = (uint16_t)(color * 8 + pen);
            pri[y * width + x + xoff] |= mark;
        }
}

// ------------------------------------------------------------------------------------------------
// Sprites (sega_hangon_sprite_device::draw; origin 189, -1: sprite x 189 is screen x 0 and
// sprite line y is screen line y + 1)
// ------------------------------------------------------------------------------------------------
void HOVideo::draw_sprites(HOBoard& b)
{
    const int XO = 189 - xoff;
    const int clip_min = XO, clip_max = XO + width - 1, ymin = -1, ymax = H - 2;
    const int numbanks = (int)(sprites.size() * 2 / 0x10000);
    for (int i = 0; i < width * H; i++) spr[i] = 0xffff;
    overlay_placed = false;
    for (int e = 0; e < 0x800; e += 16)
    {
        uint8_t* d = &b.sprram[e];
        auto D = [&](int k) { return be16(&d[2 * k]); };
        int bottom = D(0) >> 8;
        if (bottom > 0xf0) break;
        int top = D(0) & 0xff;
        int bank = (D(1) >> 12) & 0xf;
        int xpos = D(1) & 0x1ff;
        int pitch = (int16_t) D(2);
        uint16_t addr = D(3);
        int colpri = (((D(4) >> 8) & 0x3f) << 4) | ((D(4) & 3) << 10);
        int vzoom = (D(4) >> 2) & 0x3f, hzoom = vzoom << 1;
        put16(&d[14], addr);
        if (addr == TAG_ADDR && tag_active)
        {
            if (!overlay || top >= bottom) continue;
            int pri2 = D(4) & 3;
            for (int i = 0; i < width * H; i++)
            {
                uint16_t o = overlay[i];
                if (!o) continue;
                if (o == 0x1000) spr[i] = (uint16_t)((pri2 << 10) | 0x3f0 | 0xa);
                else             spr[i] = (uint16_t)(0x8000 | (pri2 << 11) | (o & 0x7ff));
            }
            overlay_placed = true;
            continue;
        }
        if (top >= bottom) continue;
        if (numbanks) bank %= numbanks;
        const uint16_t* sd = &sprites[(size_t) 0x8000 * bank];
        int zaddr = (vzoom & 0x38) << 5, zmask = 1 << (vzoom & 7);
        uint16_t d7 = addr;
        for (int y = top; y < bottom; y++)
        {
            addr += pitch;
            if (zoom[zaddr++ & 0x1fff] & zmask) addr += pitch;
            if (y < ymin || y > ymax) continue;
            uint16_t* dest = &spr[(y + 1) * width];
            int xacc = 0, x;
            if (!(addr & 0x8000))
            {
                d7 = (uint16_t)(addr - 1);
                for (x = xpos; x <= clip_max; )
                {
                    uint16_t px = sd[++d7 & 0x7fff];
                    int pix = 0;
                    for (int n = 0; n < 4; n++)
                    {
                        pix = (px >> (12 - 4 * n)) & 0xf;
                        xacc = (xacc & 0xff) + hzoom;
                        if (xacc < 0x100) { if (x >= clip_min && pix != 0 && pix != 15) dest[x - XO] = (uint16_t)(colpri | pix); x++; }
                    }
                    if (pix == 15) break;
                }
            }
            else
            {
                d7 = (uint16_t)(addr + 1);
                for (x = xpos; x <= clip_max; )
                {
                    uint16_t px = sd[--d7 & 0x7fff];
                    int pix = 0;
                    for (int n = 0; n < 4; n++)
                    {
                        pix = (px >> (4 * n)) & 0xf;
                        xacc = (xacc & 0xff) + hzoom;
                        if (xacc < 0x100) { if (x >= clip_min && pix != 0 && pix != 15) dest[x - XO] = (uint16_t)(colpri | pix); x++; }
                    }
                    if (pix == 15) break;
                }
            }
            put16(&d[14], d7);
        }
    }
}

// ------------------------------------------------------------------------------------------------
// segahang_state::screen_update
// ------------------------------------------------------------------------------------------------
void HOVideo::render(HOBoard& b)
{
    if (!ok) return;
    if (!b.display_enable)
    {
        // MAME fills with the palette's black pen; draw_async (which updates the sprites' scratch
        // words) is skipped too
        for (int i = 0; i < width * H; i++) pixels[i] = 0x1800;   // black (past the palette)
        return;
    }
    draw_sprites(b);
    std::memset(pri, 0, sizeof pri);
    draw_road(b, false);
    draw_tile_layer(b, 1, 0, 0x01);
    draw_tile_layer(b, 1, 1, 0x02);
    draw_tile_layer(b, 0, 0, 0x02);
    draw_tile_layer(b, 0, 1, 0x04);
    draw_road(b, true);
    draw_text(b, 0, 0x08);
    draw_text(b, 1, 0x08);
    const int add = b.shadow ? 0x1000 : 0x800;
    for (int i = 0; i < width * H; i++)
    {
        uint16_t p = spr[i];
        if (p == 0xffff) continue;
        if (p & 0x8000)
        {
            if ((1 << ((p >> 11) & 3)) > pri[i]) pixels[i] = (uint16_t)(0x2800 + (p & 0x7ff));
            continue;
        }
        int priority = p >> 10;
        if ((1 << priority) > pri[i])
        {
            if ((p & 0x3f0) == 0x3f0) pixels[i] = (uint16_t)(pixels[i] + add);
            else                      pixels[i] = (uint16_t)(0x400 | (p & 0x3ff));
        }
    }
}
