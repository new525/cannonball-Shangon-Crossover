// framesink <out.fgz> <hashes.txt>: reads frame records from stdin
// ("FRAM" u32 frame, u32 len, then len bytes) and writes each frame XOR the previous one
// into a gzip stream (same record header), plus one line per frame with an FNV-1a hash of
// each part: work RAM (first 0x20000 bytes) and shared RAM (rest).
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <zlib.h>

static uint64_t fnv(const uint8_t* p, size_t n)
{
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

int main(int argc, char** argv)
{
    if (argc < 3) return 1;
    gzFile out = gzopen(argv[1], "wb1");
    FILE* hs = fopen(argv[2], "w");
    std::vector<uint8_t> prev, cur, x;
    for (;;)
    {
        uint8_t hdr[12];
        if (fread(hdr, 1, 12, stdin) != 12) break;
        if (memcmp(hdr, "FRAM", 4)) { fprintf(stderr, "framesink: bad header\n"); return 1; }
        uint32_t f, len;
        memcpy(&f, hdr + 4, 4); memcpy(&len, hdr + 8, 4);
        cur.resize(len);
        if (fread(cur.data(), 1, len, stdin) != len) break;
        if (prev.size() != len) prev.assign(len, 0);
        x.resize(len);
        for (uint32_t i = 0; i < len; i++) x[i] = cur[i] ^ prev[i];
        gzwrite(out, hdr, 12);
        gzwrite(out, x.data(), len);
        size_t w = len < 0x20000 ? len : 0x20000;
        fprintf(hs, "%u %016llx %016llx\n", f, (unsigned long long) fnv(cur.data(), w),
                (unsigned long long) fnv(cur.data() + w, len - w));
        prev.swap(cur);
    }
    gzclose(out);
    fclose(hs);
    return 0;
}
