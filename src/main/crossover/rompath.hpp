/***************************************************************************
    Where a ROM file of a MAME set is. The program finds ROM files by CRC32 first, then by name,
    as loose files or in the folder's zips (crossover/romset.cpp, crossover/romzip.hpp); a loader
    that opens files by their MAME names asks here, and gets the file found for that name, or the
    loose file <dir>/<name> when none was registered.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include "crossover/romzip.hpp"

#include <map>
#include <string>
#include <vector>

inline std::map<std::string, RomFile>& rom_files()
{
    static std::map<std::string, RomFile> m;        // MAME file name -> the file found
    return m;
}

inline RomFile rom_file(const std::string& dir, const char* name)
{
    auto it = rom_files().find(name);
    return it != rom_files().end() ? it->second : RomFile(std::filesystem::path(dir + "/" + name));
}

// Reads the file of that MAME name (loose or in a zip). False if it cannot be read.
inline bool read_rom(const std::string& dir, const char* name, std::vector<uint8_t>& out)
{
    return read_rom_file(rom_file(dir, name), out);
}
