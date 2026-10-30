/***************************************************************************
    ROM sets for the other two games: Super Hang-On (shangon1) and Hang-On.

    Files are found in a folder by CRC32 first (any file name), then by MAME file name.
    Region layouts follow MAME 0.264 (segaorun.cpp ROM_START(shangon1),
    segahang.cpp ROM_START(hangon)).

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct RomRegion
{
    std::vector<uint8_t> data;
    uint32_t size() const { return (uint32_t) data.size(); }
    uint16_t r16(uint32_t a) const { return (uint16_t)(data[a % data.size()] << 8 | data[(a + 1) % data.size()]); }
    uint32_t r32(uint32_t a) const { return (uint32_t) r16(a) << 16 | r16(a + 2); }
};

class RomSet
{
public:
    // Region name -> contents. Empty map if not loaded.
    std::map<std::string, RomRegion> regions;
    std::string error;          // first problem found, for the front end

    bool loaded() const { return !regions.empty(); }
    const RomRegion& operator[](const std::string& r) const { return regions.at(r); }

protected:
    struct RomEntry
    {
        const char* name;
        const char* region;
        uint32_t    region_size;
        uint32_t    offset;
        uint32_t    length;
        uint32_t    crc;
        int         step;       // 1 = linear, 2 = every other byte (ROM_LOAD16_BYTE)
        uint32_t    reload;     // if nonzero: also copy to this offset (ROM_RELOAD)
        uint8_t     fill;       // region fill byte
    };
    bool load(const std::string& dir, const RomEntry* list, int count);
};

class SuperHangOnRoms : public RomSet
{
public:
    // After load(): "maincpu" holds the raw (encrypted) image, "maincpu_data"/"maincpu_opcodes" the two
    // FD1089B views (key 317-0034). Other regions as in MAME.
    bool load(const std::string& dir);
};

class HangOnRoms : public RomSet
{
public:
    bool load(const std::string& dir);
};

// Cool Riders (MAME coolridr), for its sound board only. Regions, big-endian as the SH CPUs see
// them: "maincpu" (2 MB, the SH-2 program; the sound DMA reads it), "gfx_data" (16 MB, the
// sample data), "sub" (128 KB, the SH-1 program). The compressed sprite ROMs are not needed.
class CoolRidersRoms : public RomSet
{
public:
    bool load(const std::string& dir);
};

extern SuperHangOnRoms sh_roms;
extern HangOnRoms      ho_roms;
extern CoolRidersRoms  cr_roms;
