/***************************************************************************
    ymref - host (Linux/PC) reference for the FM-on-DSP work.

    Replays a YM2151 register log written by a -DMUSIC_RENDER -DMUSIC_LOGONLY build
    (4 bytes per write: tick high, tick low, register, value) through the game's own
    YM2151 emulation (src/main/hwaudio/ym2151.cpp, compiled for the host with the same
    PLATFORM_ATARI code paths as the Atari build) and writes the result as a 16-bit
    stereo WAV at 12517 Hz, 30 game steps per second - exactly what the game computes.

    Build: g++ -O2 -DPLATFORM_ATARI -Isrc/main tools/fmdsp/ymref.cpp \
               src/main/hwaudio/ym2151.cpp src/main/hwaudio/soundchip.cpp -o ymref
    Use:   ymref MAGICAL.LOG magical_ref.wav [seconds]

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

#include <cstdio>
#include <cstdlib>
#include <vector>
#include "hwaudio/ym2151.hpp"

static void put32(FILE* f, uint32_t v) { fputc(v, f); fputc(v >> 8, f); fputc(v >> 16, f); fputc(v >> 24, f); }
static void put16(FILE* f, uint16_t v) { fputc(v, f); fputc(v >> 8, f); }

int main(int argc, char** argv)
{
    if (argc < 3) { fprintf(stderr, "usage: ymref LOG out.wav [seconds]\n"); return 1; }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    std::vector<unsigned char> log;
    int c;
    while ((c = fgetc(f)) != EOF) log.push_back((unsigned char)c);
    fclose(f);

    const int RATE = 12517, FPS = 30;
    int ticks = (argc > 3) ? atoi(argv[3]) * FPS : 0;
    if (!ticks && log.size() >= 4) ticks = ((log[log.size() - 4] << 8) | log[log.size() - 3]) + 1;

    YM2151 ym(0.5f, 4000000);
    ym.init(RATE, FPS);

    FILE* w = fopen(argv[2], "wb");
    fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, w);
    put32(w, 16); put16(w, 1); put16(w, 2); put32(w, RATE); put32(w, RATE * 4); put16(w, 4); put16(w, 16);
    fwrite("data\0\0\0\0", 1, 8, w);

    size_t pos = 0;
    uint32_t frames_total = 0;
    for (int t = 0; t < ticks; t++)
    {
        while (pos + 4 <= log.size() && ((log[pos] << 8) | log[pos + 1]) <= t)
        {
            ym.write_reg(log[pos + 2], log[pos + 3]);
            pos += 4;
        }
        ym.stream_update();
        const int16_t* b = ym.get_buffer();
        const uint32_t frames = ym.buffer_size / 2;
        for (uint32_t i = 0; i < frames * 2; i++) put16(w, (uint16_t)b[i]);
        frames_total += frames;
    }
    fseek(w, 4, SEEK_SET);  put32(w, 36 + frames_total * 4);
    fseek(w, 40, SEEK_SET); put32(w, frames_total * 4);
    fclose(w);
    printf("%s: %d ticks, %u frames (%.1f s), %zu register writes\n", argv[2], ticks, frames_total,
           frames_total / (double)RATE, pos / 4);
    return 0;
}
