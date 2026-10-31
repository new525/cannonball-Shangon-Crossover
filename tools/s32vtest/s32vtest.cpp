// s32vtest: renders the two screens of the System Multi 32 (OutRunners, orunnersj) from MAME 0.264
// video state dumps with crossover/s32video and compares them with MAME's snapshots.
//
// Two modes: a single frame from dumps (below), or a replay of MAME's logged writes and timer
// events through the handlers (s32vtest <romdir> --replay ..., see replay()).
//
// usage: s32vtest <romdir> <dumpdir> <frame> [options]
//   Reads <dumpdir>/v<frame>_{videoram,spriteram,spritectl,palette0,mixer0,palette1,mixer1}.bin:
//   the V70's address space read with read_u32 (little-endian dwords) at frame_done, i.e. at the
//   start of VBLANK right after MAME's screen updates.
// options:
//   --snap <png>          MAME snapshot of both screens side by side (default layout "Dual
//                         Side-by-Side": 642x224, left screen at x 0, right screen at x 322)
//   --snap0/--snap1 <png> native per-screen snapshots (-snapview native)
//   --sprlist <file>      spriteram the displayed sprite list was rendered from (default: the
//                         frame's own spriteram dump; see below)
//   --sprctl <file>       sprite control reads at that time (default: the frame's spritectl)
//   --tilebank <hex>      io_chip_0 port H value (tilebank_external_w; default 0)
//   --nosprites           render with empty sprite buffers (tilemaps, text, bitmap, background)
//   --out <prefix>        write <prefix>_s0.png/_s1.png (render) and _d0.png/_d1.png (differences)
//
// The sprite layer MAME shows at frame F was rendered by the update_sprites() before the last
// one, 50 us after the VBLANK end that began frame F-1; frame_done(F) dumps hold spriteram as the
// game left it at the start of frame F+1's VBLANK, so the default (--sprlist = the frame's own
// spriteram) is only right where the list did not change in between.
#include "crossover/s32video.hpp"

#include <png.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>

static bool read_file(const std::string& path, std::vector<uint8_t>& out, size_t size)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path.c_str()); return false; }
    out.assign(size, 0);
    size_t n = fread(out.data(), 1, size, f);
    fclose(f);
    if (n != size) { fprintf(stderr, "%s: %zu bytes, expected %zu\n", path.c_str(), n, size); return false; }
    return true;
}

static uint16_t le16(const std::vector<uint8_t>& b, size_t word) { return (uint16_t)(b[word * 2] | b[word * 2 + 1] << 8); }

struct Image { int w = 0, h = 0; std::vector<uint32_t> px; };      // 0x00RRGGBB

static bool read_png(const std::string& path, Image& img)
{
    png_image pi; memset(&pi, 0, sizeof pi); pi.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&pi, path.c_str())) { fprintf(stderr, "%s: %s\n", path.c_str(), pi.message); return false; }
    pi.format = PNG_FORMAT_RGB;
    std::vector<uint8_t> buf(PNG_IMAGE_SIZE(pi));
    if (!png_image_finish_read(&pi, nullptr, buf.data(), 0, nullptr)) { fprintf(stderr, "%s: %s\n", path.c_str(), pi.message); return false; }
    img.w = pi.width; img.h = pi.height; img.px.resize((size_t) img.w * img.h);
    for (size_t i = 0; i < img.px.size(); i++) img.px[i] = buf[i * 3] << 16 | buf[i * 3 + 1] << 8 | buf[i * 3 + 2];
    return true;
}

static void write_png(const std::string& path, const Image& img)
{
    png_image pi; memset(&pi, 0, sizeof pi); pi.version = PNG_IMAGE_VERSION;
    pi.width = img.w; pi.height = img.h; pi.format = PNG_FORMAT_RGB;
    std::vector<uint8_t> buf((size_t) img.w * img.h * 3);
    for (size_t i = 0; i < img.px.size(); i++) { buf[i * 3] = img.px[i] >> 16; buf[i * 3 + 1] = img.px[i] >> 8; buf[i * 3 + 2] = img.px[i]; }
    if (!png_image_write_to_file(&pi, path.c_str(), 0, buf.data(), 0, nullptr)) fprintf(stderr, "%s: %s\n", path.c_str(), pi.message);
}

// The layer the mixer takes the first pixel from (the search loop of mix_all_layers, repeated
// here only to classify differences): MIXER_LAYER_* index, or 6 for sprites
static int topmost_layer(S32Video& v, int which, uint8_t enablemask, int x, int y)
{
    const uint16_t* m = v.m_mixer_control[which];
    struct L { int index, effpri; };
    L sort[8]; int n = 0;
    for (int lay = 0; lay <= 5; lay++)
    {
        int pri = m[0x10 + lay] & 0x0f;
        if ((enablemask & (1 << lay)) && pri != 0) sort[n++] = { lay, (pri << 3) | (6 - lay) };
    }
    sort[n++] = { 7, (1 << 3) | 0 };
    for (int a = 0; a < n; a++) for (int b = a + 1; b < n; b++) if (sort[b].effpri > sort[a].effpri) std::swap(sort[a], sort[b]);
    int shift, mask, orv;
    static const int tab[16][3] = { {14,0,1},{14,1,2},{13,3,4},{12,7,8},{14,1,0},{13,3,0},{12,7,0},{11,15,0},
                                    {14,1,0},{13,3,0},{12,7,0},{11,15,0},{13,1,0},{12,3,0},{11,7,0},{10,15,0} };
    shift = tab[m[0x26] & 0x0f][0]; mask = tab[m[0x26] & 0x0f][1]; orv = tab[m[0x26] & 0x0f][2];
    int sprshadowmask = (m[0x26] & 0x04) ? 0x8000 : 0;
    int sprpixmask = ((1 << shift) - 1) & 0x3fff, sprshadow = 0x7ffe & sprpixmask;
    int splayer = which == 1 ? S32Video::MIXER_LAYER_MULTISPR : S32Video::MIXER_LAYER_SPRITES;
    int sprx = (v.m_sprite_control_latched[2] & 1) ? v.visible_width(which) - 1 - x : x;
    int spry = (v.m_sprite_control_latched[2] & 2) ? 223 - y : y;
    int sprpix = v.m_layer_data[splayer].pix(spry)[sprx];
    int group = (sprpix >> shift) & mask;
    int effpri = ((m[orv | group] & 0x0f) << 3) | 7;
    L order[9]; int k = 0, sprindex = -1;
    for (int a = 0; a < n; a++) { if (effpri > sort[a].effpri && sprindex < 0) { sprindex = k; order[k++] = { 6, effpri }; } order[k++] = sort[a]; }
    if (sprindex < 0) order[k++] = { 6, effpri };
    for (int a = 0; a < k; a++)
    {
        int lay = order[a].index;
        if (lay != 6)
        {
            if (lay == 7) return 7;
            const S32Video::layer_info& li = v.m_layer_data[lay];
            if (li.transparent[y]) continue;
            if ((li.bitmap[(size_t) y * S32Video::SCREEN_W_MAX + x] & 0x1fff) != 0) return lay;
        }
        else
        {
            (void) sprshadowmask;
            if ((sprpix & 0x7fff) != 0x7fff && ((sprpix & sprpixmask) & 0x7ffe) != sprshadow) return 6;
        }
    }
    return 7;
}

static const char* layer_name(int l)
{
    static const char* n[] = { "TEXT", "NBG0", "NBG1", "NBG2", "NBG3", "BITMAP", "SPRITES", "BACKGROUND" };
    return n[l];
}

static bool compare(const std::vector<uint32_t>& a, const Image& b, int w, long& diff)
{
    diff = 0;
    if (b.w != w || b.h != 224) return false;
    for (size_t i = 0; i < a.size(); i++) if (a[i] != b.px[i]) diff++;
    return true;
}

// Replay mode: s32vtest <romdir> --replay <dir> <start frame> <snapdir> [tilebank hex]
// <dir>/r<start>_*.bin: memories at frame_done(start); <dir>/r<start>_events.log: from then on, in
// MAME's execution order, "W addr data mask" (the V70's 32-bit writes to $300000-$6FFFFF and to
// the I/O chips' $C0000C, $C0001C, $C8001C), "U" (MAME's update_sprites timer) and
// "F frame snap" (frame_done: both screens were updated; native snapshots <snap>, <snap>+1).
// The writes go through the handlers as the board must call them (see s32video.hpp).
static int replay(const std::string& romdir, const std::string& dir, const std::string& start,
                  const std::string& snapdir, unsigned tilebank)
{
    std::vector<uint8_t> gfx1; std::vector<uint32_t> sprites; std::string err;
    if (!S32Video::load_orunnersj_regions(romdir, gfx1, sprites, err)) { fprintf(stderr, "ROMs: %s\n", err.c_str()); return 1; }
    static S32Video v;
    if (!v.init(gfx1.data(), (uint32_t) gfx1.size(), sprites.data(), (uint32_t) sprites.size())) return 1;
    std::string p = dir + "/r" + start + "_";
    std::vector<uint8_t> vram, sram, sctl, pal[2], mix[2];
    if (!read_file(p + "videoram.bin", vram, 0x20000) || !read_file(p + "spriteram.bin", sram, 0x20000) ||
        !read_file(p + "spritectl.bin", sctl, 16) ||
        !read_file(p + "palette0.bin", pal[0], 0x10000) || !read_file(p + "palette1.bin", pal[1], 0x10000) ||
        !read_file(p + "mixer0.bin", mix[0], 0x80) || !read_file(p + "mixer1.bin", mix[1], 0x80)) return 1;
    // start state: memories as dumped; the sprite control registers the game wrote at boot are
    // what sprite_control_r returns (latched = pending for offsets 2-6); OutRunners' display
    // enables (CNT1 of both I/O chips) are set at boot
    for (uint32_t i = 0; i < 0x10000; i++) { v.m_videoram[i] = le16(vram, i); v.m_spriteram[i] = le16(sram, i); }
    for (int s = 0; s < 2; s++)
    {
        for (uint32_t i = 0; i < 0x4000; i++) v.m_paletteram[s][i] = le16(pal[s], i);
        for (uint32_t i = 0; i < 0x40; i++) v.m_mixer_control[s][i] = le16(mix[s], i);
    }
    for (int k = 2; k <= 6; k++) v.m_sprite_control_latched[k] = v.m_sprite_control[k] = sctl[(k >> 1) * 4 + (k & 1) * 2] & 3;
    v.state_loaded();
    v.tilebank_external_w(tilebank);
    v.display_enable_w(0, 1); v.display_enable_w(1, 1);
    // MAME updated both screens just before the dump: one update sets the visible areas
    v.screen_update(0); v.screen_update(1);

    // the 315-5296's port H output (io_chip_0): out_ph is called on a write while port H is an
    // output (direction register bit 7), and with latch / 0 when the direction changes
    uint8_t dir0 = 0xc8, latch_h = (uint8_t) tilebank;
    FILE* f = fopen((p + "events.log").c_str(), "r");
    if (!f) { fprintf(stderr, "no %sevents.log\n", p.c_str()); return 1; }
    char line[128];
    int updates = 0, frames = 0, checked = 0, exact = 0, status = 0;
    long writes = 0;
    while (fgets(line, sizeof line, f))
    {
        if (line[0] == 'W')
        {
            unsigned addr, data, mask;
            if (sscanf(line + 2, "%x %x %x", &addr, &data, &mask) != 3) continue;
            writes++;
            uint16_t lo = data & 0xffff, hi = data >> 16, mlo = mask & 0xffff, mhi = mask >> 16;
            if (addr >= 0x300000 && addr < 0x400000)
            {
                uint32_t off = (addr & 0x1ffff) >> 1;
                if (mlo) v.videoram_w(off, lo, mlo);
                if (mhi) v.videoram_w(off + 1, hi, mhi);
            }
            else if (addr >= 0x400000 && addr < 0x500000)
            {
                uint32_t off = (addr & 0x1ffff) >> 1;
                if (mlo) v.spriteram_w(off, lo, mlo);
                if (mhi) v.spriteram_w(off + 1, hi, mhi);
            }
            else if (addr >= 0x500000 && addr < 0x600000)
            {
                uint32_t n = (addr & 0xf) >> 2;
                if (mask & 0x000000ff) v.sprite_control_w(2 * n, data & 0xff);
                if (mask & 0x00ff0000) v.sprite_control_w(2 * n + 1, (data >> 16) & 0xff);
            }
            else if (addr >= 0x600000 && addr < 0x700000)
            {
                int which = (addr >> 19) & 1;
                if (addr & 0x10000)
                {
                    uint32_t off = (addr & 0x7f) >> 1;
                    if (mlo) v.mixer_w(which, off, lo, mlo);
                    if (mhi) v.mixer_w(which, off + 1, hi, mhi);
                }
                else
                {
                    uint32_t off = (addr & 0xffff) >> 1;
                    if (mlo) v.paletteram_w(which, off, lo, mlo);
                    if (mhi) v.paletteram_w(which, off + 1, hi, mhi);
                }
            }
            else if (addr == 0xc0000c)
            {
                if (mask & 0x00ff0000) { latch_h = (data >> 16) & 0xff; if (dir0 & 0x80) v.tilebank_external_w(latch_h); }
            }
            else if (addr == 0xc0001c)
            {
                if (mask & 0x000000ff) v.display_enable_w(0, (data >> 1) & 1);
                if (mask & 0x00ff0000)
                {
                    uint8_t nd = (data >> 16) & 0xff;
                    if ((dir0 ^ nd) & 0x80) v.tilebank_external_w((nd & 0x80) ? latch_h : 0);
                    dir0 = nd;
                }
            }
            else if (addr == 0xc8001c)
            {
                if (mask & 0x000000ff) v.display_enable_w(1, (data >> 1) & 1);
            }
        }
        else if (line[0] == 'U') { v.update_sprites(); updates++; }
        else if (line[0] == 'F')
        {
            int fr, snap;
            if (sscanf(line + 2, "%d %d", &fr, &snap) != 2) continue;
            v.screen_update(0); v.screen_update(1); frames++;
            // the sprite buffers hold MAME's contents once two update_sprites have run here
            bool checkable = updates >= 2;
            printf("frame %d (%ld writes so far, %d sprite updates):", fr, writes, updates);
            for (int s = 0; s < 2; s++)
            {
                int w = v.visible_width(s);
                std::vector<uint32_t> mine((size_t) w * 224);
                v.get_frame(s, mine.data());
                char name[32]; snprintf(name, sizeof name, "/%04d.png", snap + s);
                Image ref; long d = -1;
                if (!read_png(snapdir + name, ref) || !compare(mine, ref, w, d)) { printf(" screen %d: reference %dx%d vs %dx224", s, ref.w, ref.h, w); if (checkable) status = 1; continue; }
                printf(" screen %d (%d wide): %ld different", s, w, d);
                if (checkable) { checked++; if (d == 0) exact++; else status = 1; }
            }
            printf("%s\n", checkable ? "" : "  (sprite buffers not yet known: not counted)");
        }
    }
    fclose(f);
    printf("replay %s: %d frames, %ld writes, %d sprite updates; %d screens checked, %d exact\n",
           start.c_str(), frames, writes, updates, checked, exact);
    return status;
}

int main(int argc, char** argv)
{
    if (argc >= 6 && std::string(argv[2]) == "--replay")
        return replay(argv[1], argv[3], argv[4], argv[5], argc > 6 ? strtoul(argv[6], nullptr, 16) : 0);
    if (argc < 4) { fprintf(stderr, "usage: s32vtest <romdir> <dumpdir> <frame> [--snap png] [--snap0 png --snap1 png] [--sprlist f] [--sprctl f] [--tilebank hex] [--nosprites] [--out prefix]\n"
                                    "       s32vtest <romdir> --replay <dir> <start frame> <snapdir> [tilebank hex]\n"); return 2; }
    std::string romdir = argv[1], dir = argv[2], frame = argv[3];
    std::string snap, snapn[2], sprlist, sprctl, out;
    unsigned tilebank = 0; bool nosprites = false;
    for (int i = 4; i < argc; i++)
    {
        std::string a = argv[i];
        auto next = [&]() { if (i + 1 >= argc) { fprintf(stderr, "%s needs a value\n", a.c_str()); exit(2); } return std::string(argv[++i]); };
        if (a == "--snap") snap = next();
        else if (a == "--snap0") snapn[0] = next();
        else if (a == "--snap1") snapn[1] = next();
        else if (a == "--sprlist") sprlist = next();
        else if (a == "--sprctl") sprctl = next();
        else if (a == "--tilebank") tilebank = strtoul(next().c_str(), nullptr, 16);
        else if (a == "--nosprites") nosprites = true;
        else if (a == "--out") out = next();
        else { fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    std::string p = dir + "/v" + frame + "_";
    if (sprlist.empty()) sprlist = p + "spriteram.bin";
    if (sprctl.empty()) sprctl = p + "spritectl.bin";

    std::vector<uint8_t> gfx1; std::vector<uint32_t> sprites; std::string err;
    if (!S32Video::load_orunnersj_regions(romdir, gfx1, sprites, err)) { fprintf(stderr, "ROMs: %s\n", err.c_str()); return 1; }
    static S32Video v;
    if (!v.init(gfx1.data(), (uint32_t) gfx1.size(), sprites.data(), (uint32_t) sprites.size())) { fprintf(stderr, "init failed\n"); return 1; }

    std::vector<uint8_t> vram, sram, slist, sctl, pal[2], mix[2];
    if (!read_file(p + "videoram.bin", vram, 0x20000) || !read_file(p + "spriteram.bin", sram, 0x20000) ||
        !read_file(sprlist, slist, 0x20000) || !read_file(sprctl, sctl, 16) ||
        !read_file(p + "palette0.bin", pal[0], 0x10000) || !read_file(p + "palette1.bin", pal[1], 0x10000) ||
        !read_file(p + "mixer0.bin", mix[0], 0x80) || !read_file(p + "mixer1.bin", mix[1], 0x80)) return 1;

    // video RAM, mixers through the handlers
    for (uint32_t i = 0; i < 0x10000; i++) v.videoram_w(i, le16(vram, i), 0xffff);
    for (int s = 0; s < 2; s++) for (uint32_t i = 0; i < 0x40; i++) v.mixer_w(s, i, le16(mix[s], i), 0xffff);
    // palette RAM directly (paletteram_w would also write the other half: blending is enabled);
    // the upper half of the dump is paletteram_r's converted view of the same storage
    int palbad = 0;
    for (int s = 0; s < 2; s++)
    {
        for (uint32_t i = 0; i < 0x4000; i++) v.m_paletteram[s][i] = le16(pal[s], i);
        for (uint32_t i = 0; i < 0x8000; i++) if (v.paletteram_r(s, i) != le16(pal[s], i)) palbad++;
    }
    printf("palette dump vs paletteram_r: %d words differ\n", palbad);

    // sprite control: the dump holds sprite_control_r values; byte lane 0 of dword n = offset 2n,
    // lane 2 = offset 2n+1. Offsets 2-6 return 0xfc | latched value.
    uint8_t rd[8];
    for (int k = 0; k < 8; k++) rd[k] = sctl[(k >> 1) * 4 + (k & 1) * 2];
    printf("sprite_control reads: ");
    for (int k = 0; k < 8; k++) printf("%02x ", rd[k]);
    printf("\n");
    for (int k = 2; k <= 6; k++) v.sprite_control_w(k, rd[k] & 3);

    // render the list twice: update_sprites() erases, swaps, then renders into the back buffer,
    // so after the second call the front buffers hold the list
    for (uint32_t i = 0; i < 0x10000; i++) v.spriteram_w(i, le16(slist, i), 0xffff);
    for (int n = 0; n < 2; n++) { v.sprite_control_w(0, 3); v.update_sprites(); }
    for (uint32_t i = 0; i < 0x10000; i++) v.spriteram_w(i, le16(sram, i), 0xffff);
    printf("sprite_control_r(0) after: %02x (dump %02x)\n", v.sprite_control_r(0), rd[0]);
    if (nosprites)
        for (int l : { S32Video::MIXER_LAYER_SPRITES, S32Video::MIXER_LAYER_MULTISPR })
            std::fill(v.m_layer_data[l].bitmap.begin(), v.m_layer_data[l].bitmap.end(), 0xffff);

    v.tilebank_external_w(tilebank);
    v.display_enable_w(0, 1); v.display_enable_w(1, 1);
    // twice: the screens start at MAME's configured 416-pixel visible area, the first update
    // switches to the width $31FF00 selects
    for (int n = 0; n < 2; n++) { v.screen_update(0); v.screen_update(1); }

    uint8_t enablemask = 0;
    {
        uint16_t* vr = v.m_videoram;
        int e0 = !(vr[0xff81] & 1) && !(vr[0xffc7] & 2), e1 = !(vr[0xff81] & 2) && !(vr[0xffc7] & 4);
        int e2 = !(vr[0xff81] & 4) && !(vr[0xffc7] & 8) && !(vr[0xff80] & 0x1000);
        int e3 = !(vr[0xff81] & 8) && !(vr[0xffc7] & 0x10) && !(vr[0xff80] & 0x2000);
        int et = !(vr[0xff81] & 0x10) && !(vr[0xffc7] & 1), eb = !(vr[0xff81] & 0x20) && !(vr[0xffc7] & 0x20);
        enablemask = et | e0 << 1 | e1 << 2 | e2 << 3 | e3 << 4 | eb << 5;
    }

    Image ref; bool dual = !snap.empty() && read_png(snap, ref);
    Image refn[2]; bool native[2] = { !snapn[0].empty() && read_png(snapn[0], refn[0]), !snapn[1].empty() && read_png(snapn[1], refn[1]) };
    int status = 0;
    for (int s = 0; s < 2; s++)
    {
        int w = v.visible_width(s);
        Image mine; mine.w = w; mine.h = 224; mine.px.resize((size_t) w * 224);
        v.get_frame(s, mine.px.data());
        if (!out.empty()) write_png(out + "_s" + std::to_string(s) + ".png", mine);
        const Image* r = nullptr; int xo = 0;
        if (native[s]) { r = &refn[s]; xo = 0; }
        else if (dual) { r = &ref; xo = s ? 322 : 0; }
        if (!r) continue;
        if (r->h != 224 || (native[s] && r->w != w)) { printf("screen %d: reference is %dx%d, render %dx224\n", s, r->w, r->h, w); status = 1; continue; }
        long exact = 0, blend = 0, bad = 0;
        std::map<int, long> by_layer, total_layer;
        Image diff = mine;
        for (int y = 0; y < 224; y++)
            for (int x = 0; x < w && x + xo < r->w; x++)
            {
                uint32_t a = mine.px[y * w + x], b = r->px[y * r->w + x + xo];
                int lay = topmost_layer(v, s, enablemask, x, y);
                total_layer[lay]++;
                diff.px[y * w + x] = (a >> 2) & 0x3f3f3f;
                if (a == b) { exact++; continue; }
                // the dual-screen snapshot's right screen is resampled by MAME's renderer
                // (bounds 4.03..8.03 of 8.03 units in 642 pixels): accept a value between this
                // column and a neighbour, per channel
                bool between = false;
                if (!native[s] && s == 1)
                    for (int dx : { -1, 1 })
                    {
                        if (x + dx < 0 || x + dx >= w) continue;
                        uint32_t c = mine.px[y * w + x + dx];
                        bool ok = true;
                        for (int sh : { 16, 8, 0 })
                        {
                            int va = (a >> sh) & 0xff, vb = (b >> sh) & 0xff, vc = (c >> sh) & 0xff;
                            if (vb < std::min(va, vc) || vb > std::max(va, vc)) ok = false;
                        }
                        if (ok) between = true;
                    }
                if (between) { blend++; diff.px[y * w + x] = 0x0000ff; continue; }
                bad++; by_layer[lay]++;
                diff.px[y * w + x] = 0xff0000;
            }
        printf("screen %d (%s, %d wide): %ld exact, %ld resampling-consistent, %ld different\n", s,
               native[s] ? "native snapshot" : "dual snapshot", w, exact, blend, bad);
        for (auto& t : total_layer)
            printf("   topmost %-10s %6ld pixels, %6ld different\n", layer_name(t.first), t.second, by_layer[t.first]);
        if (!out.empty()) write_png(out + "_d" + std::to_string(s) + ".png", diff);
        if (bad) status = 1;
    }
    return status;
}
