// crtest: run the emulated System H1 sound board (crossover/crsound) from a log of the main CPU's writes
// to the shared RAM, as recorded in MAME ("f<frame> <seconds> <attoseconds> <address> <data> <mask>").
// usage: crtest <coolridr rom dir> <share log> <seconds> <out prefix> [nvram]
// writes <prefix>.wav (44099 Hz stereo 16-bit, MAME's final clamp) and <prefix>.scsp (68000 SCSP writes)
#include "crossover/crsound.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <iterator>
#include <cmath>
#include <algorithm>

static std::vector<uint8_t> rd(const std::string& p)
{
    std::ifstream f(p, std::ios::binary);
    if (!f) { fprintf(stderr, "missing %s\n", p.c_str()); exit(1); }
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
// ROM_LOAD32_WORD_SWAP: 16-bit words, byte-swapped, every other word of a 32-bit bus
static void load32ws(std::vector<uint8_t>& img, const std::vector<uint8_t>& d, uint32_t off)
{
    for (size_t i = 0; i + 1 < d.size(); i += 2) { size_t a = off + (i / 2) * 4; img[a] = d[i + 1]; img[a + 1] = d[i]; }
}
static void put16(FILE* f, int v) { fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f); }
static void put32(FILE* f, uint32_t v) { put16(f, v & 0xffff); put16(f, v >> 16); }

int main(int argc, char** argv)
{
    if (argc < 5) { fprintf(stderr, "usage: crtest romdir sharelog seconds prefix [nvram]\n"); return 2; }
    std::string R = std::string(argv[1]) + "/";
    std::vector<uint8_t> main_rom(0x200000), gfx(0x1000000), sub;
    load32ws(main_rom, rd(R + "ep17659.30"), 0);       load32ws(main_rom, rd(R + "ep17658.29"), 2);
    load32ws(main_rom, rd(R + "ep17661.32"), 0x100000); load32ws(main_rom, rd(R + "ep17660.31"), 0x100002);
    const char* g[8] = { "mp17651.12", "mp17650.11", "mp17653.14", "mp17652.13", "mp17655.16", "mp17654.15", "mp17657.18", "mp17656.17" };
    for (int i = 0; i < 8; i++) load32ws(gfx, rd(R + g[i]), (uint32_t)((i / 2) * 0x400000 + (i & 1) * 2));
    std::vector<uint8_t> s = rd(R + "ep17662.12");
    sub.resize(s.size());
    for (size_t i = 0; i + 1 < s.size(); i += 2) { sub[i] = s[i + 1]; sub[i + 1] = s[i]; }

    static CRSound b;
    if (!b.init(main_rom, gfx, sub)) { fprintf(stderr, "init failed\n"); return 1; }
    std::vector<uint8_t> nv;
    if (argc > 5) nv = rd(argv[5]);
    b.power_on(nv.size() == 512 ? nv.data() : nullptr);
    std::string pre = argv[4];
    b.scsp_log = fopen((pre + ".scsp").c_str(), "w");
    if (getenv("CRT_TRACE68")) { b.m68k_trace = fopen((pre + ".tr68").c_str(), "w"); b.trace_from = atof(getenv("CRT_TRACE68")); b.trace_to = b.trace_from + 0.04; }
    if (getenv("CRT_DMALOG")) b.dma_log = fopen(getenv("CRT_DMALOG"), "w");

    FILE* sl = fopen(argv[2], "r");
    char fr[32]; long long sec, att; unsigned addr, data, mask; int n = 0;
    while (fscanf(sl, "%31s %lld %lld %x %x %x", fr, &sec, &att, &addr, &data, &mask) == 6)
    { b.main_write(CrTime(sec, att), addr - 0x03f00000, data, mask); n++; }
    fclose(sl);
    fprintf(stderr, "%d main writes\n", n);

    double total = atof(argv[3]);
    FILE* w = fopen((pre + ".wav").c_str(), "wb");
    fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, w);
    put32(w, 16); put16(w, 1); put16(w, 2); put32(w, b.sample_rate()); put32(w, b.sample_rate() * 4); put16(w, 4); put16(w, 16);
    fwrite("data\0\0\0\0", 1, 8, w);
    uint32_t frames = 0;
    for (int step = 1; step <= (int)(total * 57); step++)
    {
        b.run_until(CrTime(step / 57, (int64_t)(step % 57) * (CrTime::ATTO / 57)));
        for (size_t i = 0; i < b.out_l.size(); i++)
        {
            float l = std::max(-1.0f, std::min(1.0f, b.out_l[i])), r = std::max(-1.0f, std::min(1.0f, b.out_r[i]));
            put16(w, (int16_t)(l * 32767.0f)); put16(w, (int16_t)(r * 32767.0f));
            frames++;
        }
        b.out_l.clear(); b.out_r.clear();
        if (step % 570 == 0) fprintf(stderr, "t %.1f s: sh %llu 68k %llu cycles, sh pc %08x\n", step / 57.0,
                                     (unsigned long long) b.sh_cycles, (unsigned long long) b.m68k_cycles, b.sh1().pc());
    }
    fseek(w, 4, SEEK_SET); put32(w, 36 + frames * 4);
    fseek(w, 40, SEEK_SET); put32(w, frames * 4);
    fclose(w);
    fclose(b.scsp_log);
    return 0;
}
