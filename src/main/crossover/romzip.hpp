/***************************************************************************
    ROM files in zips (MAME's sets as distributed).

    A ROM path is a folder. Besides its loose files, every .zip directly in it is read, so a MAME
    set works without unzipping: one zip, a split set (a clone's zip next to its parent's, e.g.
    orunnersj.zip next to orunners.zip) or a merged set (the parent's zip holding the clones'
    files). A ROM path may also name a single .zip. The zips' entries are indexed from each zip's
    central directory (CRC32, size, name): nothing is decompressed until a file is read. Callers
    look at loose files first, so a loose file wins over a zip entry that matches the same ROM.

    The zips are read with miniz (crossover/miniz, MIT licence).

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// A ROM file: a loose file, or an entry of a zip
struct RomFile
{
    std::filesystem::path path;     // the loose file, or the zip
    std::string entry;              // the entry's name in the zip (with any folder); empty for a loose file
    uint32_t    index = 0;          // the entry's number in the zip
    uint32_t    size  = 0;          // zip entries: uncompressed size, from the central directory
    uint32_t    crc   = 0;          // zip entries: CRC32, from the central directory

    RomFile() {}
    RomFile(const std::filesystem::path& p) : path(p) {}
    bool in_zip() const { return !entry.empty(); }
    std::string where() const;      // for messages: the file's path, or "<zip> (<entry>)"
};

// Reads the whole file: a loose file as it is, a zip entry decompressed (miniz checks the
// entry's CRC32). The bytes are the same as the file unzipped. False if it cannot be read.
bool read_rom_file(const RomFile& f, std::vector<uint8_t>& out);

// True if `path` names an existing file whose name ends in .zip (any case)
bool is_zip_file(const std::string& path);

// The entries of the zips of one ROM path
class RomZips
{
public:
    // Indexes every .zip directly in the folder `path` (in name order), or `path` itself if it
    // names a .zip file. Replaces any earlier index. A zip that cannot be read is reported on
    // stderr and left out.
    void index(const std::string& path);

    bool empty() const { return entries.empty(); }

    // The first entry with this CRC32 and size (size 0: any size), or null
    const RomFile* find(uint32_t crc, uint32_t size) const;

    // The first entry with this file name (the part after any folder in the zip, any case),
    // preferring one whose CRC32 is `crc`; or null
    const RomFile* find(const std::string& name, uint32_t crc) const;

    // The zips indexed, for messages: "a.zip, b.zip"; empty if none
    std::string zip_list() const;

    std::vector<std::filesystem::path> zips;
    std::vector<RomFile> entries;
};
