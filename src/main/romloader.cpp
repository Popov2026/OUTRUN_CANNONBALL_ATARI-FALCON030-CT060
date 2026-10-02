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
#include "crc32.hpp"     // Minimal CRC32 (replaces boost/crc.hpp; see crc32.hpp)
namespace crc32_detail { const Table table; } // single definition for the extern declared in crc32.hpp
#include <unordered_map>

#include "stdint.hpp"
#include "romloader.hpp"
#include "frontend/config.hpp"

#ifdef PLATFORM_ATARI
#include <cstring>
#include "atari/options.hpp"
// Plain TOS/GEMDOS filenames are 8.3 (8-character base, 3-character extension) unless the target
// is running FreeMiNT (or another long-filename-capable kernel/filesystem). Default behaviour
// (freemint=1 in cannonball.ini) keeps the ROM set's original, well-known filenames (e.g.
// "epr-10380b.133") and requires FreeMiNT for them to exist on disk at all. With freemint=0, the
// well-known "xpr-NNNNNc.ext" naming (x = e/m/o, c = optional a/b/f suffix) is remapped to an
// 8.3-safe "XNNNNNc.ext" form here (drop the "pr-", uppercase the prefix letter) - rename the ROM
// set on disk to match (full table in README_ATARI.md) and no long-filename support is needed at
// all. Any name that isn't in this exact "xpr-" shape, or that already fits 8.3, is unaffected by
// either setting.
static std::string atari_short_name(const char* filename)
{
    const char* dot = strrchr(filename, '.');
    size_t base_len = dot ? (size_t)(dot - filename) : strlen(filename);
    if (base_len <= 8) return filename;                          // already fits, nothing to do
    if (base_len < 9 || filename[3] != '-') return filename;      // not the "xpr-NNNNN..." shape we know how to remap

    std::string out;
    out += (char)toupper((unsigned char)filename[0]);              // e/m/o -> E/M/O
    out += (filename + 4);                                         // NNNNN + optional letter suffix + ".ext"
    return out;
}
#endif

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
#ifdef PLATFORM_ATARI
    path += atari_opt.freemint ? std::string(filename) : atari_short_name(filename);
#else
    path += std::string(filename);
#endif

    // Open rom file
    std::ifstream src(path.c_str(), std::ios::in | std::ios::binary);
    if (!src)
    {
        if (verbose) std::cout << "cannot open rom: " << path << std::endl;
        loaded = false;
        return 1; // fail
    }

    // Read file
    char* buffer = new char[length];
    src.read(buffer, length);

    // Check CRC on file
    crc32_calc result;
    result.process_bytes(buffer, (size_t) src.gcount());

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
        crc32_calc result;
        result.process_bytes(buffer, (size_t)src.gcount());

        // Insert file into MAP between CRC and filename
        map.insert({ result.checksum(), file });
        delete[] buffer;
        src.close();
    }

    if (map.empty())
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

    if (map.empty())
        return 1;

    auto search = map.find(expected_crc);

    // Cannot find file by CRC value in map
    if (search == map.end())
    {
        if (verbose) std::cout << "Unable to locate rom in path: " << config.data.rom_path << " possible name: " << debug << " crc32: 0x" << std::hex << expected_crc << std::endl;
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