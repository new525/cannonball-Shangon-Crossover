/***************************************************************************
    Where a ROM file of a MAME set is. The program finds ROM files by CRC32 first, then by name
    (crossover/romset.cpp); a loader that opens files by their MAME names asks here, and gets the
    file found for that name, or <dir>/<name> when none was registered.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <map>
#include <string>

inline std::map<std::string, std::string>& rom_paths()
{
    static std::map<std::string, std::string> m;    // MAME file name -> path of the file found
    return m;
}

inline std::string rom_path(const std::string& dir, const char* name)
{
    auto it = rom_paths().find(name);
    return it != rom_paths().end() ? it->second : dir + "/" + name;
}
