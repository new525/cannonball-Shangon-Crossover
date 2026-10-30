/***************************************************************************
    ROM sets for Super Hang-On (shangon1) and Hang-On. See romset.hpp.
***************************************************************************/
#include "crossover/romset.hpp"
#include "crossover/fd1089.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

SuperHangOnRoms sh_roms;
HangOnRoms      ho_roms;
CoolRidersRoms  cr_roms;

static uint32_t crc32_of(const std::vector<uint8_t>& d)
{
    static uint32_t table[256];
    static bool init = false;
    if (!init)
    {
        for (uint32_t i = 0; i < 256; i++)
        {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (uint8_t b : d) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static bool read_file(const fs::path& p, std::vector<uint8_t>& out)
{
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool RomSet::load(const std::string& dir, const RomEntry* list, int count)
{
    regions.clear();
    error.clear();

    // Index the folder by CRC32 (small files only; ROMs here are at most 64 KiB)
    std::map<uint32_t, fs::path> by_crc;
    std::map<std::string, fs::path> by_name;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec))
    {
        if (!e.is_regular_file(ec) || e.file_size(ec) > 0x100000) continue;
        std::vector<uint8_t> d;
        if (!read_file(e.path(), d)) continue;
        by_crc[crc32_of(d)] = e.path();
        by_name[e.path().filename().string()] = e.path();
    }
    if (ec) { error = "cannot read folder " + dir; return false; }

    std::map<std::string, RomRegion> out;
    for (int i = 0; i < count; i++)
    {
        const RomEntry& r = list[i];
        RomRegion& reg = out[r.region];
        if (reg.data.empty()) reg.data.assign(r.region_size, r.fill);

        fs::path p;
        auto c = by_crc.find(r.crc);
        if (c != by_crc.end()) p = c->second;
        else
        {
            auto n = by_name.find(r.name);
            if (n == by_name.end())
            {
                std::ostringstream s;
                s << "missing ROM " << r.name << " (crc " << std::hex << r.crc << ") in " << dir;
                error = s.str();
                return false;
            }
            p = n->second;
            std::cerr << "warning: " << r.name << " found by name but its CRC differs" << std::endl;
        }
        std::vector<uint8_t> d;
        read_file(p, d);
        if (d.size() < r.length) { error = std::string("short ROM file ") + r.name; return false; }
        for (uint32_t k = 0; k < r.length; k++)
        {
            reg.data[r.offset + k * r.step] = d[k];
            if (r.reload) reg.data[r.reload + k * r.step] = d[k];
        }
    }
    regions = std::move(out);
    return true;
}

// MAME 0.264 segaorun.cpp, ROM_START( shangon1 )
bool SuperHangOnRoms::load(const std::string& dir)
{
    static const RomEntry list[] = {
        { "epr-10636.133", "maincpu", 0x60000, 0x000000, 0x10000, 0xe52721fe, 2, 0, 0 },
        { "epr-10634.118", "maincpu", 0x60000, 0x000001, 0x10000, 0x08feca97, 2, 0, 0 },
        { "epr-10637.132", "maincpu", 0x60000, 0x020000, 0x10000, 0x5d55d65f, 2, 0, 0 },
        { "epr-10635.117", "maincpu", 0x60000, 0x020001, 0x10000, 0xb967e8c3, 2, 0, 0 },
        { "epr-10640.76",  "subcpu",  0x60000, 0x000000, 0x10000, 0x02be68db, 2, 0, 0 },
        { "epr-10638.58",  "subcpu",  0x60000, 0x000001, 0x10000, 0xf13e8bee, 2, 0, 0 },
        { "epr-10641.75",  "subcpu",  0x60000, 0x020000, 0x10000, 0x38c3f808, 2, 0, 0 },
        { "epr-10639.57",  "subcpu",  0x60000, 0x020001, 0x10000, 0x8cdbcde8, 2, 0, 0 },
        { "epr-10652.54",  "gfx1",    0x18000, 0x00000, 0x08000, 0x260286f9, 1, 0, 0 },
        { "epr-10651.55",  "gfx1",    0x18000, 0x08000, 0x08000, 0xc609ee7b, 1, 0, 0 },
        { "epr-10650.56",  "gfx1",    0x18000, 0x10000, 0x08000, 0xb236a403, 1, 0, 0 },
        { "epr-10675.8",   "sprites", 0xe0000, 0x000001, 0x10000, 0xd6ac012b, 2, 0, 0 },
        { "epr-10682.16",  "sprites", 0xe0000, 0x000000, 0x10000, 0xd9d83250, 2, 0, 0 },
        { "epr-10676.7",   "sprites", 0xe0000, 0x020001, 0x10000, 0x25ebf2c5, 2, 0, 0 },
        { "epr-10683.15",  "sprites", 0xe0000, 0x020000, 0x10000, 0x6365d2e9, 2, 0, 0 },
        { "epr-10677.6",   "sprites", 0xe0000, 0x040001, 0x10000, 0x8a57b8d6, 2, 0, 0 },
        { "epr-10684.14",  "sprites", 0xe0000, 0x040000, 0x10000, 0x3aff8910, 2, 0, 0 },
        { "epr-10678.5",   "sprites", 0xe0000, 0x060001, 0x10000, 0xaf473098, 2, 0, 0 },
        { "epr-10685.13",  "sprites", 0xe0000, 0x060000, 0x10000, 0x80bafeef, 2, 0, 0 },
        { "epr-10679.4",   "sprites", 0xe0000, 0x080001, 0x10000, 0x03bc4878, 2, 0, 0 },
        { "epr-10686.12",  "sprites", 0xe0000, 0x080000, 0x10000, 0x274b734e, 2, 0, 0 },
        { "epr-10680.3",   "sprites", 0xe0000, 0x0a0001, 0x10000, 0x9f0677ed, 2, 0, 0 },
        { "epr-10687.11",  "sprites", 0xe0000, 0x0a0000, 0x10000, 0x508a4701, 2, 0, 0 },
        { "epr-10681.2",   "sprites", 0xe0000, 0x0c0001, 0x10000, 0xb176ea72, 2, 0, 0 },
        { "epr-10688.10",  "sprites", 0xe0000, 0x0c0000, 0x10000, 0x42fcd51d, 2, 0, 0 },
        { "epr-10642.47",  "segaic16road", 0x10000, 0x0000, 0x8000, 0x7836bcc3, 1, 0, 0 },
        { "ic88",          "soundcpu", 0x10000, 0x0000, 0x8000, 0x1254efa6, 1, 0, 0 },
        { "epr-10643.66",  "pcm",     0x40000, 0x00000, 0x8000, 0x06f55364, 1, 0x08000, 0xff },
        { "epr-10644.67",  "pcm",     0x40000, 0x10000, 0x8000, 0xb41d541d, 1, 0x18000, 0xff },
        { "epr-10645.68",  "pcm",     0x40000, 0x20000, 0x8000, 0xa60dabff, 1, 0x28000, 0xff },
        { "epr-10646.69",  "pcm",     0x40000, 0x30000, 0x8000, 0x473cc411, 1, 0x38000, 0xff },
        { "317-0034.key",  "key",     0x2000,  0x0000, 0x2000, 0xad61539a, 1, 0, 0 },
    };
    if (!RomSet::load(dir, list, sizeof(list) / sizeof(list[0]))) return false;

    // FD1089B: two decrypted views of the main program
    const RomRegion& raw = regions["maincpu"];
    RomRegion ops, dat;
    ops.data.resize(raw.size());
    dat.data.resize(raw.size());
    fd1089b_decrypt(raw.data.data(), raw.size(), regions["key"].data.data(), ops.data.data(), dat.data.data());
    regions["maincpu_opcodes"] = std::move(ops);
    regions["maincpu_data"]    = std::move(dat);
    return true;
}

// MAME 0.264 segahang.cpp, ROM_START( hangon )
bool HangOnRoms::load(const std::string& dir)
{
    static const RomEntry list[] = {
        { "epr-6918a.ic22", "maincpu", 0x40000, 0x000000, 0x8000, 0x20b1c2b0, 2, 0, 0 },
        { "epr-6916a.ic8",  "maincpu", 0x40000, 0x000001, 0x8000, 0x7d9db1bf, 2, 0, 0 },
        { "epr-6917a.ic20", "maincpu", 0x40000, 0x010000, 0x8000, 0xfea12367, 2, 0, 0 },
        { "epr-6915a.ic6",  "maincpu", 0x40000, 0x010001, 0x8000, 0xac883240, 2, 0, 0 },
        { "epr-6920.ic63",  "subcpu",  0x40000, 0x000000, 0x8000, 0x1c95013e, 2, 0, 0 },
        { "epr-6919.ic51",  "subcpu",  0x40000, 0x000001, 0x8000, 0x6ca30d69, 2, 0, 0 },
        { "epr-6841.ic38",  "gfx1",    0x18000, 0x00000, 0x8000, 0x54d295dc, 1, 0, 0 },
        { "epr-6842.ic23",  "gfx1",    0x18000, 0x08000, 0x8000, 0xf677b568, 1, 0, 0 },
        { "epr-6843.ic7",   "gfx1",    0x18000, 0x10000, 0x8000, 0xa257f0da, 1, 0, 0 },
        { "epr-6819.ic27",  "sprites", 0x80000, 0x000001, 0x8000, 0x469dad07, 2, 0, 0 },
        { "epr-6820.ic34",  "sprites", 0x80000, 0x000000, 0x8000, 0x87cbc6de, 2, 0, 0 },
        { "epr-6821.ic28",  "sprites", 0x80000, 0x010001, 0x8000, 0x15792969, 2, 0, 0 },
        { "epr-6822.ic35",  "sprites", 0x80000, 0x010000, 0x8000, 0xe9718de5, 2, 0, 0 },
        { "epr-6823.ic29",  "sprites", 0x80000, 0x020001, 0x8000, 0x49422691, 2, 0, 0 },
        { "epr-6824.ic36",  "sprites", 0x80000, 0x020000, 0x8000, 0x701deaa4, 2, 0, 0 },
        { "epr-6825.ic30",  "sprites", 0x80000, 0x030001, 0x8000, 0x6e23c8b4, 2, 0, 0 },
        { "epr-6826.ic37",  "sprites", 0x80000, 0x030000, 0x8000, 0x77d0de2c, 2, 0, 0 },
        { "epr-6827.ic31",  "sprites", 0x80000, 0x040001, 0x8000, 0x7fa1bfb6, 2, 0, 0 },
        { "epr-6828.ic38",  "sprites", 0x80000, 0x040000, 0x8000, 0x8e880c93, 2, 0, 0 },
        { "epr-6829.ic32",  "sprites", 0x80000, 0x050001, 0x8000, 0x7ca0952d, 2, 0, 0 },
        { "epr-6830.ic39",  "sprites", 0x80000, 0x050000, 0x8000, 0xb1a63aef, 2, 0, 0 },
        { "epr-6845.ic18",  "sprites", 0x80000, 0x060001, 0x8000, 0xba08c9b8, 2, 0, 0 },
        { "epr-6846.ic25",  "sprites", 0x80000, 0x060000, 0x8000, 0xf21e57a3, 2, 0, 0 },
        { "epr-6840.ic108", "segaic16road", 0x8000, 0x0000, 0x8000, 0x581230e3, 1, 0, 0 },
        { "epr-6833.ic73",  "soundcpu", 0x10000, 0x0000, 0x4000, 0x3b942f5f, 1, 0, 0 },
        { "epr-6831.ic5",   "pcm",     0x10000, 0x00000, 0x8000, 0xcfef5481, 1, 0, 0 },
        { "epr-6832.ic6",   "pcm",     0x10000, 0x08000, 0x8000, 0x4165aea5, 1, 0, 0 },
        { "epr-6844.ic123", "sprites:zoom", 0x2000, 0x0000, 0x2000, 0xe3ec7bd6, 1, 0, 0 },
    };
    return RomSet::load(dir, list, sizeof(list) / sizeof(list[0]));
}

// MAME 0.264 coolridr.cpp, ROM_START( coolridr ): the maincpu, gfx_data and sub regions.
// ROM_LOAD32_WORD_SWAP / ROM_LOAD16_WORD_SWAP: the files hold byte-swapped 16-bit words.
bool CoolRidersRoms::load(const std::string& dir)
{
    struct E { const char* name; const char* region; uint32_t region_size, offset, length, crc; int step; };
    // step 4: every other word of a 32-bit bus (ROM_LOAD32_WORD_SWAP); step 2: contiguous words
    static const E list[] =
    {
        { "ep17659.30", "maincpu",  0x200000,  0x000000, 0x080000, 0x473027b0, 4 },
        { "ep17658.29", "maincpu",  0x200000,  0x000002, 0x080000, 0x7ecfdfcc, 4 },
        { "ep17661.32", "maincpu",  0x200000,  0x100000, 0x080000, 0x81a7d90b, 4 },
        { "ep17660.31", "maincpu",  0x200000,  0x100002, 0x080000, 0x27b7a507, 4 },
        { "mp17650.11", "gfx_data", 0x1000000, 0x000002, 0x200000, 0x0ccc84a1, 4 },
        { "mp17651.12", "gfx_data", 0x1000000, 0x000000, 0x200000, 0x25fd7dde, 4 },
        { "mp17652.13", "gfx_data", 0x1000000, 0x400002, 0x200000, 0xbe9b4d05, 4 },
        { "mp17653.14", "gfx_data", 0x1000000, 0x400000, 0x200000, 0x64d1406d, 4 },
        { "mp17654.15", "gfx_data", 0x1000000, 0x800002, 0x200000, 0x5dee5cba, 4 },
        { "mp17655.16", "gfx_data", 0x1000000, 0x800000, 0x200000, 0x02903cf2, 4 },
        { "mp17656.17", "gfx_data", 0x1000000, 0xc00002, 0x200000, 0x945c89e3, 4 },
        { "mp17657.18", "gfx_data", 0x1000000, 0xc00000, 0x200000, 0x74676b1f, 4 },
        { "ep17662.12", "sub",      0x020000,  0x000000, 0x020000, 0x50d66b1f, 2 },
    };
    regions.clear();
    error.clear();

    // Index the folder by CRC32, files of the sizes above only (the set's 4 MB sprite ROMs are skipped)
    std::map<uint32_t, fs::path> by_crc;
    std::map<std::string, fs::path> by_name;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec))
    {
        if (!e.is_regular_file(ec)) continue;
        uintmax_t sz = e.file_size(ec);
        by_name[e.path().filename().string()] = e.path();
        if (sz != 0x80000 && sz != 0x200000 && sz != 0x20000) continue;
        std::vector<uint8_t> d;
        if (!read_file(e.path(), d)) continue;
        by_crc[crc32_of(d)] = e.path();
    }
    if (ec) { error = "cannot read folder " + dir; return false; }

    std::map<std::string, RomRegion> out;
    for (const E& r : list)
    {
        RomRegion& reg = out[r.region];
        if (reg.data.empty()) reg.data.assign(r.region_size, 0);
        fs::path p;
        auto c = by_crc.find(r.crc);
        if (c != by_crc.end()) p = c->second;
        else
        {
            auto n = by_name.find(r.name);
            if (n == by_name.end())
            {
                std::ostringstream s;
                s << "missing ROM " << r.name << " (crc " << std::hex << r.crc << ") in " << dir;
                error = s.str();
                return false;
            }
            p = n->second;
            std::cerr << "warning: " << r.name << " found by name but its CRC differs" << std::endl;
        }
        std::vector<uint8_t> d;
        read_file(p, d);
        if (d.size() < r.length) { error = std::string("short ROM file ") + r.name; return false; }
        for (uint32_t k = 0; k + 1 < r.length; k += 2)
        {
            uint32_t a = r.offset + (r.step == 4 ? k * 2 : k);
            reg.data[a]     = d[k + 1];
            reg.data[a + 1] = d[k];
        }
    }
    regions = std::move(out);
    return true;
}
