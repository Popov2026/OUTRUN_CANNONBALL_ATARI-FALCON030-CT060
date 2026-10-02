/***************************************************************************
    Minimal CRC32 Implementation.

    romloader.cpp originally used boost::crc_32_type (boost/crc.hpp) to
    verify ROM checksums. Boost is not available on the m68k-atari-mint
    cross toolchain used by the Atari STE port (and pulling in Boost.CRC
    purely for a 30-line algorithm is unnecessary anywhere), so this
    self-contained, header-only replacement is used instead.

    Same algorithm/polynomial as boost::crc_32_type (standard CRC-32,
    poly 0x04C11DB7, reflected, init 0xFFFFFFFF, final XOR 0xFFFFFFFF) so
    checksums produced are identical to the previous implementation.

    Copyright Chris White.
    See license.txt for more details.
***************************************************************************/

#pragma once

#include "stdint.hpp"
#include <cstddef>

// Table is built by a plain namespace-scope object (crc32_table_init below),
// constructed during ordinary C++ static initialization - NOT via a
// function-local static (the original approach here). A function-local
// static with lazy init requires the compiler to emit __cxa_guard_acquire/
// release calls (thread-safety guard variables); on this m68k-atari-mint
// GCC 4.6.4 freestanding target that guard machinery hangs forever on
// first use (confirmed by direct testing - this was the actual cause of
// the program never progressing past the first ROM's CRC check). Ordinary
// global/namespace-scope initialization has none of that guard overhead
// and was already proven to work (every other global engine object -
// Video, Roms, etc. - initializes fine the same way).
namespace crc32_detail
{
    struct Table
    {
        uint32_t t[256];
        Table()
        {
            for (uint32_t i = 0; i < 256; i++)
            {
                uint32_t c = i;
                for (int k = 0; k < 8; k++)
                    c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                t[i] = c;
            }
        }
    };
    extern const Table table;
}

class crc32_calc
{
public:
    crc32_calc() : crc(0xFFFFFFFFu) {}

    void process_bytes(const void* buffer, std::size_t byte_count)
    {
        const uint8_t* p = static_cast<const uint8_t*>(buffer);
        for (std::size_t i = 0; i < byte_count; i++)
            crc = crc32_detail::table.t[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }

    uint32_t checksum() const
    {
        return crc ^ 0xFFFFFFFFu;
    }

private:
    uint32_t crc;
};
