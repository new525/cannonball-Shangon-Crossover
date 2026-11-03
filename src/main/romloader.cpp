/***************************************************************************
    Binary File Loader. 
    
    Handles loading an individual binary file to memory.
    Supports reading bytes, words and longs from this area of memory.

    Copyright Chris White.
    See license.txt for more details.
***************************************************************************/

#include <iostream>
#include <fstream>
#include <cstddef>       // for std::size_t
#include <boost/crc.hpp> // CRC Checking via Boost library.
#include <unordered_map>
#include <algorithm>
#include <vector>

#include "stdint.hpp"
#include "romloader.hpp"
#include "frontend/config.hpp"
#include "crossover/romzip.hpp"

// In order to get a cross-platform directory listing I'm using a Visual Studio
// version of Linux's Dirent from here: https://github.com/tronkko/dirent
//
// This appears to be the most lightweight solution available without resorting 
// to enormous boost libraries or switching to C++17.
#ifdef _MSC_VER
#include "windirent.h"
#else
#include <dirent.h>
#endif

// Unordered Map to store contents of directory by CRC 32 value. Similar to Hashmap.
static std::unordered_map<int, std::string> map;
static bool map_created;

// Crossover: the entries of the .zip files in the ROM directory (or of the .zip that rom_path
// names), looked at after the loose files. See crossover/romzip.hpp.
static RomZips zips;
static bool zips_indexed = false;

static const RomZips& rom_zips()
{
    if (!zips_indexed)
    {
        zips.index(config.data.rom_path);
        zips_indexed = true;
    }
    return zips;
}

// For messages: the zips looked in, if any
static std::string zips_looked_in()
{
    if (rom_zips().empty() || is_zip_file(config.data.rom_path)) return "";
    return " (also looked in: " + rom_zips().zip_list() + ")";
}


RomLoader::RomLoader()
{
    rom = NULL;
    map_created = false;
    loaded = false;
}

RomLoader::~RomLoader()
{
    if (rom != NULL)
        delete[] rom;
}

void RomLoader::init(const uint32_t length)
{
    // Setup pointer to function we want to use (either load_crc32 or load_rom)
    load = config.data.crc32 ? &RomLoader::load_crc32 : &RomLoader::load_rom;

    this->length = length;
    rom = new uint8_t[length];
}

void RomLoader::unload(void)
{
    delete[] rom;
    rom = NULL;
}

// ------------------------------------------------------------------------------------------------
// Filename based ROM loader
// Advantage: Simpler. Does not require <dirent.h>
// ------------------------------------------------------------------------------------------------

int RomLoader::load_rom(const char* filename, const int offset, const int length, const int expected_crc, const uint8_t interleave, const bool verbose)
{
    std::string path = config.data.rom_path;
    path += std::string(filename);

    // Open rom file
    std::ifstream src(path.c_str(), std::ios::in | std::ios::binary);

    // Crossover: no loose file of that name, so the entry of that name in the zips
    std::vector<uint8_t> zipped;
    if (!src)
    {
        const RomFile* z = rom_zips().find(filename, (uint32_t) expected_crc);
        if (z == NULL || !read_rom_file(*z, zipped))
        {
            if (verbose) std::cout << (z ? "cannot read rom: " + z->where() : "cannot open rom: " + path + zips_looked_in()) << std::endl;
            loaded = false;
            return 1; // fail
        }
    }

    // Read file
    char* buffer = new char[length];
    size_t got;
    if (src)
    {
        src.read(buffer, length);
        got = (size_t) src.gcount();
    }
    else
    {
        got = std::min(zipped.size(), (size_t) length);
        std::copy(zipped.begin(), zipped.begin() + got, buffer);
    }

    // Check CRC on file
    boost::crc_32_type result;
    result.process_bytes(buffer, got);

    if (expected_crc != result.checksum())
    {
        if (verbose) 
        std::cout << std::hex << 
            filename << " has incorrect checksum.\nExpected: " << expected_crc << " Found: " << result.checksum() << std::endl;

        return 1;
    }

    // Interleave file as necessary
    for (int i = 0; i < length; i++)
    {
        rom[(i * interleave) + offset] = buffer[i];
    }

    // Clean Up
    delete[] buffer;
    src.close();
    loaded = true;
    return 0; // success
}

// --------------------------------------------------------------------------------------------
// Create Unordered Map of files in ROM directory by CRC32 value
// This should be faster than brute force searching every file in the directory every time.
// --------------------------------------------------------------------------------------------

int RomLoader::create_map()
{
    map_created = true;

    std::string path = config.data.rom_path;
    DIR* dir;
    struct dirent* ent;

    if ((dir = opendir(path.c_str())) == NULL)
    {
        // Crossover: rom_path may name a .zip (looked at in load_crc32)
        if (is_zip_file(path)) return 0;
        std::cout << "Warning: Could not open ROM directory - " << path << std::endl;
        return 1; // Failure (Could not open directory)
    }

    // Iterate all files in directory
    while ((ent = readdir(dir)) != NULL)
    {
        std::string file = path + ent->d_name;
        std::ifstream src(file, std::ios::in | std::ios::binary);

        if (!src) continue;

        // Read file
        char* buffer = new char[length];
        src.read(buffer, length);

        // Check CRC on file
        boost::crc_32_type result;
        result.process_bytes(buffer, (size_t)src.gcount());

        // Insert file into MAP between CRC and filename
        map.insert({ result.checksum(), file });
        delete[] buffer;
        src.close();
    }

    if (map.empty() && rom_zips().empty())
        std::cout << "Warning: Could not create CRC32 Map. Did you copy the ROM files into the directory? " << std::endl;

    closedir(dir);
    return 0; //success
}


// ------------------------------------------------------------------------------------------------
// Search and load ROM by CRC32 value as opposed to filename.
// Advantage: More resilient to renamed romsets.
// ------------------------------------------------------------------------------------------------

int RomLoader::load_crc32(const char* debug, const int offset, const int length, const int expected_crc, const uint8_t interleave, const bool verbose)
{
    if (!map_created)
        create_map();

    if (map.empty() && rom_zips().empty())
        return 1;

    auto search = map.find(expected_crc);

    // Crossover: no loose file with that CRC, so an entry of the zips with that CRC and size
    if (search == map.end())
    {
        const RomFile* z = rom_zips().find((uint32_t) expected_crc, (uint32_t) length);
        std::vector<uint8_t> zipped;
        if (z != NULL && read_rom_file(*z, zipped) && zipped.size() == (size_t) length)
        {
            for (int i = 0; i < length; i++)
                rom[(i * interleave) + offset] = zipped[i];
            loaded = true;
            return 0; // success
        }
        if (z != NULL)
        {
            if (verbose) std::cout << "cannot read rom: " << z->where() << std::endl;
            loaded = false;
            return 1;
        }
    }

    // Cannot find file by CRC value in map
    if (search == map.end())
    {
        if (verbose) std::cout << "Unable to locate rom in path: " << config.data.rom_path << zips_looked_in() << " possible name: " << debug << " crc32: 0x" << std::hex << expected_crc << std::endl;
        loaded = false;
        return 1;
    }

    // Correct ROM found
    std::string file = search->second;

    std::ifstream src(file, std::ios::in | std::ios::binary);
    if (!src)
    {
        if (verbose) std::cout << "cannot open rom: " << file << std::endl;
        loaded = false;
        return 1; // fail
    }

    // Read file
    char* buffer = new char[length];
    src.read(buffer, length);

    // Interleave file as necessary
    for (int i = 0; i < length; i++)
        rom[(i * interleave) + offset] = buffer[i];

    // Clean Up
    delete[] buffer;
    src.close();
    loaded = true;
    return 0; // success
}

// --------------------------------------------------------------------------------------------
// Load Binary File (LayOut Levels, Tilemap Data etc.)
// --------------------------------------------------------------------------------------------

int RomLoader::load_binary(const char* filename)
{
    std::ifstream src(filename, std::ios::in | std::ios::binary);
    if (!src)
    {
        std::cout << "cannot open file: " << filename << std::endl;
        loaded = false;
        return 1; // fail
    }

    length = filesize(filename);

    // Read file
    char* buffer = new char[length];
    src.read(buffer, length);
    rom = (uint8_t*) buffer;

    // Clean Up
    src.close();

    loaded = true;
    return 0; // success
}

int RomLoader::filesize(const char* filename)
{
    std::ifstream in(filename, std::ifstream::in | std::ifstream::binary);
    in.seekg(0, std::ifstream::end);
    int size = (int) in.tellg();
    in.close();
    return size; 
}