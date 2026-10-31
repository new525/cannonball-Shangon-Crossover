// orbtest: runs the emulated System Multi 32 board of OutRunners (crossover/orboard) from
// power-on with a schedule of inputs and compares it with a recording of MAME 0.264.
//
// The reference (MAME 0.264, orunnersj, fresh cfg; recorded with record.sh and orb.lua in this
// folder; session1.sched and session2.sched are the two sessions it was checked with):
//   <ref>.sched  the inputs, by MAME's Lua frame number f (frame_done callbacks; f = 1 is the
//                start-up frame at time 0, f = n + 1 the n-th VBLANK at n/60 s):
//                "<f> set PORT mask 0|1", "<f> aset PORT mask value", "<f> snap", "<f> exit"
//   <ref>.hash   per frame f: FNV-1a 64 of work RAM ($200000-$21FFFF) and of shared RAM ($700000-$701FFF)
//   <ref>.fgz    per frame f: work RAM + shared RAM, each frame XOR the previous (gzip)
//   <ref>.io     every main-CPU read of $800000-$801FFF, $C00000-$CFFFFF, $D80000-$DFFFFF and write of
//                $800000-$801FFF, $C00000-$D7FFFF: "R|W <s> <atto> <addr> <data> <mask>"; "F <f> <s> <atto>"
//   <ref>.wav    MAME's sound output (-wavwrite, 44,100 Hz)
//   snaps_<ref>/orunnersj/NNNN.png  -snapview native: two files per "snap" (left, right screen)
//
// usage: orbtest <romdir> <refprefix> [options]
//   -frames N    stop after Lua frame N (default: the schedule's exit)
//   -wav file    write the board's sound (left: MAME's left speaker, right: MAME's right)
//   -noio        do not compare the I/O accesses
//   -bench       no comparison or logging: report the speed only
//   -eeprom file load the EEPROM from file (MAME nvram format) instead of a fresh one
//   -save file   save the EEPROM at the end
//   -draw mask   screens drawn (1 left, 2 right; default 3)
//   -chips file  the Z80's port and MultiPCM writes ("<Z80 cycle> io|pcm <addr> <data>"; the
//                time is cycle * (1e18 / 8053975) attoseconds), for comparison with MAME's (OR_ZLOG)
#include "crossover/orboard.hpp"
#include "crossover/resample.hpp"

#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <png.h>
#include <zlib.h>

namespace
{

struct SchedEntry { int frame; std::string cmd; };

std::vector<SchedEntry> load_sched(const std::string& path)
{
    std::vector<SchedEntry> s;
    FILE* f = fopen(path.c_str(), "r");
    if (!f) { fprintf(stderr, "cannot read %s\n", path.c_str()); exit(1); }
    char line[256];
    while (fgets(line, sizeof line, f))
    {
        int fr; char cmd[200];
        if (sscanf(line, "%d %199[^\n]", &fr, cmd) == 2) s.push_back({ fr, cmd });
    }
    fclose(f);
    return s;
}

uint64_t fnv(const uint8_t* p, size_t n)
{
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

// MAME's port fields as the board's input bytes
uint8_t* port_byte(ORBoard& b, const std::string& port)
{
    ORBoard::Inputs& in = b.in;
    if (port == "P1_A") return &in.p1_a;
    if (port == "P2_A") return &in.p2_a;
    if (port == "PORTC_A") return &in.portc_a;
    if (port == "SERVICE12_A") return &in.service12_a;
    if (port == "SERVICE34_A") return &in.service34_a;
    if (port == "P1_B") return &in.p1_b;
    if (port == "P2_B") return &in.p2_b;
    if (port == "PORTC_B") return &in.portc_b;
    if (port == "SERVICE12_B") return &in.service12_b;
    if (port == "SERVICE34_B") return &in.service34_b;
    if (port.compare(0, 6, "ANALOG") == 0) return &in.analog[atoi(port.c_str() + 6) - 1];
    return nullptr;
}

// MAME's I/O log
struct IoRec { bool write; int64_t s, a; uint32_t addr, data, mask; };

struct IoLog
{
    FILE* f = nullptr;
    char line[256];
    bool next(IoRec& r)
    {
        while (fgets(line, sizeof line, f))
        {
            if (line[0] == 'F') continue;
            long long s, a; unsigned ad, d, m;
            if (sscanf(line + 2, "%lld %lld %x %x %x", &s, &a, &ad, &d, &m) != 5) continue;
            r.write = line[0] == 'W'; r.s = s; r.a = a; r.addr = ad; r.data = d; r.mask = m;
            return true;
        }
        return false;
    }
};

// MAME's frames: XOR deltas of work RAM + shared RAM
struct FrameLog
{
    gzFile g = nullptr;
    std::vector<uint8_t> cur;
    uint32_t frame = 0;
    bool open(const std::string& p) { g = gzopen(p.c_str(), "rb"); return g != nullptr; }
    bool advance_to(uint32_t f)
    {
        while (frame < f)
        {
            uint8_t hdr[12];
            if (gzread(g, hdr, 12) != 12) return false;
            uint32_t fr, len;
            memcpy(&fr, hdr + 4, 4); memcpy(&len, hdr + 8, 4);
            std::vector<uint8_t> x(len);
            if (gzread(g, x.data(), len) != (int) len) return false;
            if (cur.size() != len) cur.assign(len, 0);
            for (uint32_t i = 0; i < len; i++) cur[i] ^= x[i];
            frame = fr;
        }
        return frame == f;
    }
};

bool read_png(const std::string& path, std::vector<uint32_t>& px, int& w, int& h)
{
    png_image img;
    memset(&img, 0, sizeof img);
    img.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&img, path.c_str())) return false;
    img.format = PNG_FORMAT_RGB;
    std::vector<uint8_t> buf(PNG_IMAGE_SIZE(img));
    if (!png_image_finish_read(&img, nullptr, buf.data(), 0, nullptr)) return false;
    w = (int) img.width; h = (int) img.height;
    px.resize((size_t) w * h);
    for (size_t i = 0; i < px.size(); i++) px[i] = (uint32_t) buf[3 * i] << 16 | (uint32_t) buf[3 * i + 1] << 8 | buf[3 * i + 2];
    return true;
}

void put16(FILE* f, int v) { fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f); }
void put32(FILE* f, uint32_t v) { put16(f, (int)(v & 0xffff)); put16(f, (int)(v >> 16)); }

bool read_wav(const std::string& path, std::vector<int16_t>& left, std::vector<int16_t>& right)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<uint8_t> d;
    uint8_t buf[65536]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + n);
    fclose(f);
    size_t p = 12;
    while (p + 8 <= d.size())
    {
        uint32_t len; memcpy(&len, &d[p + 4], 4);
        if (!memcmp(&d[p], "data", 4))
        {
            size_t cnt = std::min<size_t>(len, d.size() - p - 8) / 4;
            left.resize(cnt); right.resize(cnt);
            for (size_t i = 0; i < cnt; i++) { memcpy(&left[i], &d[p + 8 + 4 * i], 2); memcpy(&right[i], &d[p + 10 + 4 * i], 2); }
            return true;
        }
        p += 8 + len + (len & 1);
    }
    return false;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) { fprintf(stderr, "usage: orbtest romdir refprefix [-frames N] [-wav file] [-noio] [-bench] [-eeprom file] [-save file]\n"); return 2; }
    std::string romdir = argv[1], ref = argv[2], wavpath, eepath, savepath, chipspath;
    int max_frames = -1;
    bool check_io = true, bench = false;
    int draw_mask = 3;
    for (int i = 3; i < argc; i++)
    {
        std::string a = argv[i];
        if (a == "-frames" && i + 1 < argc) max_frames = atoi(argv[++i]);
        else if (a == "-wav" && i + 1 < argc) wavpath = argv[++i];
        else if (a == "-noio") check_io = false;
        else if (a == "-bench") bench = true;
        else if (a == "-eeprom" && i + 1 < argc) eepath = argv[++i];
        else if (a == "-save" && i + 1 < argc) savepath = argv[++i];
        else if (a == "-draw" && i + 1 < argc) draw_mask = atoi(argv[++i]);
        else if (a == "-chips" && i + 1 < argc) chipspath = argv[++i];
    }

    std::vector<SchedEntry> sched = load_sched(ref + ".sched");
    int exit_frame = 0;
    for (auto& e : sched) if (e.cmd == "exit") exit_frame = e.frame;
    if (max_frames < 0) max_frames = exit_frame;

    ORBoard::Roms roms; std::string err;
    if (!ORBoard::load_roms(romdir, roms, err)) { fprintf(stderr, "ROMs: %s\n", err.c_str()); return 1; }
    static ORBoard b;
    if (!b.init(std::move(roms))) { fprintf(stderr, "board init failed\n"); return 1; }
    std::vector<uint8_t> ee(128);
    if (!eepath.empty())
    {
        FILE* f = fopen(eepath.c_str(), "rb");
        if (!f || fread(ee.data(), 1, 128, f) != 128) { fprintf(stderr, "cannot read %s\n", eepath.c_str()); return 1; }
        fclose(f);
    }
    b.power_on(eepath.empty() ? nullptr : ee.data());
    b.draw_mask = (uint8_t) draw_mask;
    if (!chipspath.empty()) b.sound().chip_log = fopen(chipspath.c_str(), "w");

    // ---- reference data ----
    std::map<int, std::pair<uint64_t, uint64_t>> hashes;
    FrameLog frames;
    IoLog io;
    if (!bench)
    {
        FILE* hf = fopen((ref + ".hash").c_str(), "r");
        if (!hf) { fprintf(stderr, "cannot read %s.hash\n", ref.c_str()); return 1; }
        unsigned fr; unsigned long long hw, hs;
        while (fscanf(hf, "%u %llx %llx", &fr, &hw, &hs) == 3) hashes[(int) fr] = { hw, hs };
        fclose(hf);
        if (!frames.open(ref + ".fgz")) { fprintf(stderr, "cannot read %s.fgz\n", ref.c_str()); return 1; }
        if (check_io)
        {
            io.f = fopen((ref + ".io").c_str(), "r");
            if (!io.f) { fprintf(stderr, "cannot read %s.io\n", ref.c_str()); return 1; }
        }
    }

    // ---- I/O comparison ----
    uint64_t io_count = 0;
    bool io_failed = false;
    std::string io_fail;
    if (!bench && check_io)
        b.io_tap = [&](bool write, uint32_t addr, uint32_t data, uint32_t mask, const OrTime& t)
        {
            if (io_failed) return;
            io_count++;
            IoRec r;
            if (!io.next(r)) { io_failed = true; io_fail = "board access beyond the end of the log"; return; }
            if (r.write != write || r.addr != addr || r.data != data || r.mask != mask || r.s != t.s || r.a != t.a)
            {
                io_failed = true;
                char m[400];
                snprintf(m, sizeof m, "I/O access %" PRIu64 " (frame %" PRIu64 "): MAME %c %lld.%018lld %06x %08x %08x, board %c %lld.%018lld %06x %08x %08x",
                         io_count, b.frame() + 1, r.write ? 'W' : 'R', (long long) r.s, (long long) r.a, r.addr, r.data, r.mask,
                         write ? 'W' : 'R', (long long) t.s, (long long) t.a, addr, data, mask);
                io_fail = m;
            }
        };

    // ---- frames ----
    int first_bad = -1, bad_frames = 0, compared = 0;
    std::string first_bad_text;
    int snap_index = 0, snaps_checked = 0, snaps_exact = 0;
    std::string snapdir;
    {
        size_t sl = ref.find_last_of('/');
        std::string dir = sl == std::string::npos ? "." : ref.substr(0, sl), name = sl == std::string::npos ? ref : ref.substr(sl + 1);
        snapdir = dir + "/snaps_" + name + "/orunnersj/";
    }
    std::vector<uint8_t> mine(0x20000 + 0x2000);

    auto at_frame = [&](int f)        // MAME's Lua frame f: compare, then the schedule
    {
        if (!bench)
        {
            memcpy(mine.data(), b.work_ram, 0x20000);
            memcpy(mine.data() + 0x20000, b.shared_ram(), 0x2000);
            auto h = hashes.find(f);
            if (h != hashes.end())
            {
                compared++;
                uint64_t hw = fnv(mine.data(), 0x20000), hs = fnv(mine.data() + 0x20000, 0x2000);
                if (hw != h->second.first || hs != h->second.second)
                {
                    bad_frames++;
                    if (first_bad < 0)
                    {
                        first_bad = f;
                        char m[200];
                        if (frames.advance_to((uint32_t) f))
                        {
                            int n = 0;
                            std::string list;
                            for (size_t i = 0; i < mine.size(); i++)
                                if (mine[i] != frames.cur[i])
                                {
                                    if (n < 24)
                                    {
                                        uint32_t addr = i < 0x20000 ? 0x200000 + (uint32_t) i : 0x700000 + (uint32_t)(i - 0x20000);
                                        snprintf(m, sizeof m, "  %06x MAME %02x board %02x\n", addr, frames.cur[i], mine[i]);
                                        list += m;
                                    }
                                    n++;
                                }
                            snprintf(m, sizeof m, "first differing frame %d (%.4f s): %d bytes differ\n", f, (f - 1) / 60.0, n);
                            first_bad_text = m + list;
                        }
                    }
                }
            }
        }
        for (auto& e : sched)
        {
            if (e.frame != f) continue;
            char port[64]; unsigned mask, v;
            if (e.cmd == "snap" && !bench)
            {
                for (int s = 0; s < 2; s++)
                {
                    char fn[32]; snprintf(fn, sizeof fn, "%04d.png", snap_index++);
                    std::vector<uint32_t> px; int w = 0, h = 0;
                    if (!read_png(snapdir + fn, px, w, h)) { printf("snapshot %s: cannot read\n", fn); continue; }
                    snaps_checked++;
                    int bw = b.screen_width(s);
                    const uint32_t* p = b.screen_pixels(s);
                    long diff = 0;
                    if (w != bw || h != 224) diff = -1;
                    else
                        for (int y = 0; y < h; y++)
                            for (int x = 0; x < w; x++)
                                if ((p[(size_t) y * S32Video::SCREEN_W_MAX + x] & 0xffffff) != px[(size_t) y * w + x]) diff++;
                    if (diff == 0) snaps_exact++;
                    printf("snapshot frame %d screen %d (%s): %s %ld\n", f, s, fn, diff < 0 ? "size differs" : "pixels differing:", diff < 0 ? (long) bw : diff);
                }
            }
            else if (sscanf(e.cmd.c_str(), "aset %63s %x %u", port, &mask, &v) == 3)
            {
                uint8_t* pb = port_byte(b, port);
                if (pb) *pb = (uint8_t) v;              // the 8-bit fields: set_value (adjoverride)
            }
            else if (sscanf(e.cmd.c_str(), "set %63s %x %u", port, &mask, &v) == 3)
            {
                uint8_t* pb = port_byte(b, port);       // active-low digital fields
                if (pb) *pb = v ? (uint8_t)(*pb & ~mask) : (uint8_t)(*pb | mask);
            }
        }
    };

    // ---- sound ----
    FILE* w = nullptr;
    uint32_t wav_frames = 0;
    FrameResampler ry, rp;
    std::vector<int32_t> mix(2 * 800);
    std::vector<int16_t> outl;
    if (!wavpath.empty())
    {
        w = fopen(wavpath.c_str(), "wb");
        fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, w);
        put32(w, 16); put16(w, 1); put16(w, 2); put32(w, 44100); put32(w, 44100 * 4); put16(w, 4); put16(w, 16);
        fwrite("data\0\0\0\0", 1, 8, w);
    }
    ORSound& snd = b.sound();
    auto take_sound = [&](uint64_t n)
    {
        snd.flush_streams();
        if (!w && bench) { snd.ym0.clear(); snd.ym1.clear(); snd.pcm0.clear(); snd.pcm1.clear(); return; }
        for (size_t i = 0; i < snd.ym1.size(); i++) ry.push(snd.ym1[i] * 0.40f * 32768.0f, snd.ym0[i] * 0.40f * 32768.0f);
        for (size_t i = 0; i < snd.pcm1.size(); i++) rp.push(snd.pcm1[i] * 32768.0f, snd.pcm0[i] * 32768.0f);
        snd.ym0.clear(); snd.ym1.clear(); snd.pcm0.clear(); snd.pcm1.clear();
        int ns = (int)((n * 44100) / 60 - ((n - 1) * 44100) / 60);
        std::fill(mix.begin(), mix.end(), 0);
        ry.pull((double) snd.ym_rate() / 44100, mix.data(), ns);
        rp.pull((double) snd.pcm_rate() / 44100, mix.data(), ns);
        for (int i = 0; i < ns; i++)
        {
            int l = std::max(-32768, std::min(32767, mix[2 * (size_t) i])), r = std::max(-32768, std::min(32767, mix[2 * (size_t) i + 1]));
            if (w) { put16(w, l); put16(w, r); }
            outl.push_back((int16_t) l);
        }
        wav_frames += (uint32_t) ns;
    };

    // ---- run ----
    b.on_frame = [&](uint64_t n) { at_frame((int) n + 1); };
    at_frame(1);                                        // MAME's start-up frame_done at time 0
    auto t0 = std::chrono::steady_clock::now();
    for (int f = 2; f <= max_frames; f++)
    {
        if (io_failed && !bench) { /* keep running: the RAM comparison goes on */ }
        b.run_frame();                                  // calls at_frame(f) at the VBLANK
        take_sound(b.frame());
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    double emu = b.now().sec();

    printf("ran %d frames (%.3f s emulated) in %.2f s: %.2fx real time\n", max_frames, emu, secs, emu / secs);
    if (!bench)
    {
        printf("RAM: %d frames compared, %d differ\n", compared, bad_frames);
        if (first_bad >= 0) printf("%s", first_bad_text.c_str());
        else printf("work RAM and shared RAM identical to MAME's at every frame\n");
        if (check_io)
        {
            if (io_failed) printf("I/O: first difference: %s\n", io_fail.c_str());
            else
            {
                IoRec r;
                bool more = io.next(r);
                printf("I/O: %" PRIu64 " accesses identical to MAME's (address, data, mask, time)%s\n", io_count, more ? "; MAME's log has more" : "");
            }
        }
        printf("snapshots: %d of %d identical\n", snaps_exact, snaps_checked);
        printf("random numbers drawn: %" PRIu64 "\n", b.rand_calls);
    }
    if (w)
    {
        fseek(w, 4, SEEK_SET); put32(w, 36 + wav_frames * 4);
        fseek(w, 40, SEEK_SET); put32(w, wav_frames * 4);
        fclose(w);
    }
    if (!bench)
    {
        std::vector<int16_t> ml, mr;
        if (read_wav(ref + ".wav", ml, mr))
        {
            // correlation of the left channels over the common span, at the best lag (+-200 samples)
            size_t n = std::min(ml.size(), outl.size());
            double best = -2; int bestlag = 0;
            for (int lag = -200; lag <= 200; lag++)
            {
                double sxy = 0, sxx = 0, syy = 0;
                for (size_t i = 200; i + 200 < n; i++)
                {
                    double x = ml[i], y = outl[(size_t)((long) i + lag)];
                    sxy += x * y; sxx += x * x; syy += y * y;
                }
                double c = (sxx > 0 && syy > 0) ? sxy / std::sqrt(sxx * syy) : 0;
                if (c > best) { best = c; bestlag = lag; }
            }
            double rm = 0, rb = 0;
            for (size_t i = 0; i < n; i++) { rm += (double) ml[i] * ml[i]; rb += (double) outl[i] * outl[i]; }
            printf("sound: %zu samples, left channel correlation with MAME's WAV %.5f at lag %d samples; RMS MAME %.1f board %.1f\n",
                   n, best, bestlag, std::sqrt(rm / (double) std::max<size_t>(n, 1)), std::sqrt(rb / (double) std::max<size_t>(n, 1)));
        }
    }
    if (!savepath.empty())
    {
        b.eeprom_save(ee.data());
        FILE* f = fopen(savepath.c_str(), "wb");
        if (f) { fwrite(ee.data(), 1, 128, f); fclose(f); }
    }
    return (first_bad >= 0 || io_failed) ? 1 : 0;
}
