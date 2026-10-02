/***************************************************************************
    Screenshot capture (C key) - see screenshot.hpp.

    Writes a real, standard PNG file: 8-bit truecolour RGB (colour type 2),
    one IDAT chunk holding a valid zlib stream whose DEFLATE data uses only
    "stored" (uncompressed) blocks. That is fully valid PNG - the format
    does not require the data to actually be compressed - and is the only
    practical choice here: real DEFLATE (LZ77 + Huffman) is a lot of code
    and CPU for a feature that only ever runs once per manual keypress, so
    a bigger file (roughly width*height*3 bytes, ~210 KB at 320x224) in
    exchange for a tiny, simple encoder is the right trade here. The whole
    zlib stream is built in one malloc'd buffer (the Falcon has plenty of
    RAM for a one-off ~210 KB allocation) rather than via a scratch file.

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "atari/screenshot.hpp"

namespace
{
    // Continuable CRC-32 (PNG's, polynomial 0xEDB88320), no lookup table - this runs once per
    // manual screenshot, so the ~8x slowdown vs. a table-driven version is not worth 1 KB of
    // static table data. `crc` is carried in already-inverted form between calls.
    uint32_t crc32_continue(uint32_t crc, const uint8_t* buf, uint32_t len)
    {
        for (uint32_t i = 0; i < len; i++)
        {
            crc ^= buf[i];
            for (int k = 0; k < 8; k++)
                crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
        }
        return crc;
    }

    void write_be32(FILE* f, uint32_t v)
    {
        uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
        fwrite(b, 1, 4, f);
    }

    void write_chunk(FILE* f, const char* type, const uint8_t* data, uint32_t len)
    {
        write_be32(f, len);
        uint32_t crc = crc32_continue(0xFFFFFFFFu, (const uint8_t*)type, 4);
        if (len) crc = crc32_continue(crc, data, len);
        fwrite(type, 1, 4, f);
        if (len) fwrite(data, 1, len, f);
        write_be32(f, crc ^ 0xFFFFFFFFu);
    }
}

// Saves the picture as SHOTnnnn.PNG (next free number) in the current folder: an uncompressed
// PNG, written without any lookup table since it only runs on a key press. False if the file
// cannot be written.
bool atari_save_screenshot(const uint16_t* rgb565, int width, int height, int stride)
{
    // Next free SHOTnnnn.PNG name in the current folder.
    char name[13];
    int n = 1;
    for (; n <= 9999; n++)
    {
        std::sprintf(name, "SHOT%04d.PNG", n);
        FILE* test = fopen(name, "rb");
        if (!test) break;
        fclose(test);
    }
    if (n > 9999) return false;

    // Raw (pre-DEFLATE) scanline data: one filter-type byte (0 = None) then width*3 RGB888
    // bytes, per row - built once, then wrapped in "stored" DEFLATE blocks below.
    const uint32_t row_bytes = 1 + (uint32_t)width * 3;
    const uint32_t total = row_bytes * (uint32_t)height;
    uint8_t* raw = (uint8_t*)std::malloc(total);
    if (!raw) return false;

    uint32_t a = 1, b = 0; // Adler-32 running totals
    for (int y = 0; y < height; y++)
    {
        uint8_t* p = raw + (uint32_t)y * row_bytes;
        *p++ = 0; // filter type: None
        const uint16_t* src = rgb565 + y * stride;
        for (int x = 0; x < width; x++)
        {
            uint16_t c = src[x];
            uint8_t r5 = (uint8_t)((c >> 11) & 0x1F), g6 = (uint8_t)((c >> 5) & 0x3F), b5 = (uint8_t)(c & 0x1F);
            *p++ = (uint8_t)((r5 << 3) | (r5 >> 2));
            *p++ = (uint8_t)((g6 << 2) | (g6 >> 4));
            *p++ = (uint8_t)((b5 << 3) | (b5 >> 2));
        }
    }
    for (uint32_t i = 0; i < total; i++) { a = (a + raw[i]) % 65521u; b = (b + a) % 65521u; }
    const uint32_t adler = (b << 16) | a;

    // zlib stream: 2-byte header, the raw data split into <=65535-byte "stored" DEFLATE blocks
    // (5 bytes of header per block: 1 byte BFINAL/BTYPE + 2-byte LEN + 2-byte ~LEN, all already
    // byte-aligned since every block we emit starts on a byte boundary), then the Adler-32.
    const uint32_t num_blocks = total ? (total + 65534) / 65535 : 1;
    const uint32_t zlen = 2 + total + num_blocks * 5 + 4;
    uint8_t* zbuf = (uint8_t*)std::malloc(zlen);
    if (!zbuf) { std::free(raw); return false; }

    uint8_t* z = zbuf;
    *z++ = 0x78; *z++ = 0x01; // zlib header
    uint32_t off = 0;
    while (off < total)
    {
        uint32_t chunk = total - off;
        if (chunk > 65535) chunk = 65535;
        const bool final_block = (off + chunk) >= total;
        *z++ = final_block ? 1 : 0;
        *z++ = (uint8_t)(chunk & 0xFF); *z++ = (uint8_t)(chunk >> 8);
        *z++ = (uint8_t)(~chunk & 0xFF); *z++ = (uint8_t)((~chunk >> 8) & 0xFF);
        std::memcpy(z, raw + off, chunk); z += chunk;
        off += chunk;
    }
    *z++ = (uint8_t)(adler >> 24); *z++ = (uint8_t)(adler >> 16);
    *z++ = (uint8_t)(adler >> 8);  *z++ = (uint8_t)adler;
    std::free(raw);

    FILE* f = fopen(name, "wb");
    if (!f) { std::free(zbuf); return false; }

    static const uint8_t sig[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
    fwrite(sig, 1, 8, f);

    uint8_t ihdr[13];
    ihdr[0] = (uint8_t)(width >> 24); ihdr[1] = (uint8_t)(width >> 16);
    ihdr[2] = (uint8_t)(width >> 8);  ihdr[3] = (uint8_t)width;
    ihdr[4] = (uint8_t)(height >> 24); ihdr[5] = (uint8_t)(height >> 16);
    ihdr[6] = (uint8_t)(height >> 8);  ihdr[7] = (uint8_t)height;
    ihdr[8] = 8;    // bit depth
    ihdr[9] = 2;    // colour type: truecolour RGB
    ihdr[10] = 0;   // compression method
    ihdr[11] = 0;   // filter method
    ihdr[12] = 0;   // interlace method
    write_chunk(f, "IHDR", ihdr, 13);
    write_chunk(f, "IDAT", zbuf, zlen);
    write_chunk(f, "IEND", 0, 0);

    std::free(zbuf);
    fclose(f);
    return true;
}
