// ortest: run the emulated System Multi 32 sound board (crossover/orsound) from a log of the main
// CPU's writes to the shared RAM, as recorded in MAME ("f<frame> <s> <atto> <addr> <data> <mask>",
// 32-bit lanes of the little-endian V60 bus).
// usage: ortest <orunners rom dir> <share log> <release s> <seconds> <out prefix>
//   <release s>: when the main CPU releases the Z80 from reset (MAME: the Z80's first fetch)
// writes <prefix>.chips (the Z80's YM3438, port and MultiPCM writes: "<s> io|pcm <addr> <data>")
// and <prefix>.wav (44,100 Hz: player 1's side, MAME's left speaker, both channels)
#include "crossover/orsound.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <iterator>
#include <algorithm>

static std::vector<uint8_t> rd(const std::string& p)
{
    std::ifstream f(p, std::ios::binary);
    if (!f) { fprintf(stderr, "missing %s\n", p.c_str()); exit(1); }
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
static void put16(FILE* f, int v) { fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f); }
static void put32(FILE* f, uint32_t v) { put16(f, (int)(v & 0xffff)); put16(f, (int)(v >> 16)); }

int main(int argc, char** argv)
{
    if (argc < 6) { fprintf(stderr, "usage: ortest romdir sharelog release_s seconds prefix\n"); return 2; }
    std::string R = std::string(argv[1]) + "/";
    std::vector<uint8_t> z = rd(R + "epr-15550.ic31"), p1 = rd(R + "mpr-15551.ic1"), p2 = rd(R + "mpr-15552.ic2");
    std::vector<uint8_t> pcm(p1); pcm.insert(pcm.end(), p2.begin(), p2.end());
    static ORSound b;
    if (!b.init(z, pcm)) { fprintf(stderr, "init failed\n"); return 1; }
    std::string pre = argv[5];
    b.chip_log = fopen((pre + ".chips").c_str(), "w");
    if (getenv("ORT_OPLOG")) b.op_log = fopen(getenv("ORT_OPLOG"), "w");
    if (getenv("ORT_TRACE"))    // ORT_TRACE=<from s>,<to s>
    {
        double f0 = 0, f1 = 0;
        sscanf(getenv("ORT_TRACE"), "%lf,%lf", &f0, &f1);
        b.trace = fopen((pre + ".tr").c_str(), "w");
        b.trace_from = (int64_t)(f0 * ORSound::CLOCK); b.trace_to = (int64_t)(f1 * ORSound::CLOCK);
    }

    FILE* sl = fopen(argv[2], "r");
    char fr[32]; long long sec, att; unsigned addr, data, mask; int n = 0;
    while (fscanf(sl, "%31s %lld %lld %x %x %x", fr, &sec, &att, &addr, &data, &mask) == 6)
    {
        if (addr < 0x700000 || addr > 0x7fffff) continue;
        long double t = (long double) sec + (long double) att * 1e-18L;
        int64_t c = (int64_t)(t * (long double) ORSound::CLOCK);
        for (int lane = 0; lane < 4; lane++)
            if ((mask >> (8 * lane)) & 0xff) { b.main_write(c, (addr + (unsigned) lane) & 0x1fff, (uint8_t)(data >> (8 * lane))); n++; }
    }
    fclose(sl);
    fprintf(stderr, "%d main byte writes\n", n);

    double rel = atof(argv[3]), total = atof(argv[4]);
    b.run_until((int64_t)(rel * ORSound::CLOCK));
    b.release_cpu();

    FILE* w = fopen((pre + ".wav").c_str(), "wb");
    fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, w);
    put32(w, 16); put16(w, 1); put16(w, 2); put32(w, 44100); put32(w, 44100 * 4); put16(w, 4); put16(w, 16);
    fwrite("data\0\0\0\0", 1, 8, w);
    FrameResampler ry, rp;
    uint32_t frames = 0;
    std::vector<int32_t> mix(2 * 735 * 2);
    for (int step = 1; step <= (int)(total * 60); step++)
    {
        int64_t t = (int64_t)((long double) step / 60 * ORSound::CLOCK);
        b.run_until(t);
        // MAME's left speaker: output 1 of each chip (YM3438 x 0.40, MultiPCM x 1.0), 16-bit scale
        // ORT_BOTH: left = MAME's left speaker, right = MAME's right speaker (tests)
        bool both = getenv("ORT_BOTH") != nullptr;
        for (size_t i = 0; i < b.ym1.size(); i++) ry.push(b.ym1[i] * 0.40f * 32768.0f, (both ? b.ym0[i] : b.ym1[i]) * 0.40f * 32768.0f);
        for (size_t i = 0; i < b.pcm1.size(); i++) rp.push(b.pcm1[i] * 32768.0f, (both ? b.pcm0[i] : b.pcm1[i]) * 32768.0f);
        b.ym0.clear(); b.ym1.clear(); b.pcm0.clear(); b.pcm1.clear();
        int ns = 735;
        std::fill(mix.begin(), mix.end(), 0);
        ry.pull((double) b.ym_rate() / 44100, mix.data(), ns);
        rp.pull((double) b.pcm_rate() / 44100, mix.data(), ns);
        for (int i = 0; i < 2 * ns; i++) put16(w, std::max(-32768, std::min(32767, mix[(size_t) i])));
        frames += (uint32_t) ns;
        if (step % 600 == 0) fprintf(stderr, "t %.0f s\n", step / 60.0);
    }
    fseek(w, 4, SEEK_SET); put32(w, 36 + frames * 4);
    fseek(w, 40, SEEK_SET); put32(w, frames * 4);
    fclose(w);
    fclose(b.chip_log);
    return 0;
}
