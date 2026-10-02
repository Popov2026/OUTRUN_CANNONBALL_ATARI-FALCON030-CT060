/***************************************************************************
    Atari Mega STE Video Backend - Implementation.

    See video.hpp for the full design rationale (why 16 colours, why a
    per-frame histogram reduction, what was deliberately NOT attempted).

    Tested in Hatari (Mega STE, Falcon 030/060 emulation); see README_ATARI.md.

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

#ifndef PLATFORM_FALCON  // STE bitplane backend; Falcon uses video_falcon.cpp
#include <cstring>
#include <cstdio>
#include "atari/video.hpp"

// --------------------------------------------------------------------------
// STE hardware registers used here (exercised in Hatari). All are word-wide unless noted, big-endian (m68k
// native, matches memory order directly).
// --------------------------------------------------------------------------
#define VID_SCREEN_BASE_HI   (*(volatile uint8_t*)0xFF8201)  // bits 8-15 of screen base >>8
#define VID_SCREEN_BASE_MID  (*(volatile uint8_t*)0xFF8203)  // bits 0-7  of screen base >>8
#define VID_SYNC_MODE        (*(volatile uint8_t*)0xFF820A)
#define VID_RES              (*(volatile uint16_t*)0xFF8260) // low 2 bits: 0 = low-res
#define VID_PALETTE_BASE     ((volatile uint16_t*)0xFF8240)  // 16 consecutive words

Render render;

// No screen memory yet (see init()); empty palette tables.
Render::Render()
{
    screen_buffer[0] = screen_buffer[1] = nullptr;
    back_buffer = 0;
    shadow_multi = 255;
    std::memset(rgb444, 0, sizeof(rgb444));
    std::memset(current_palette, 0, sizeof(current_palette));
    std::memset(idx_hist, 0, sizeof(idx_hist));
    std::memset(idx_slot, 0, sizeof(idx_slot));
}

// Frees the two screen buffers.
Render::~Render()
{
    disable();
}

// Sets the ST low resolution (320x200, 16 colours) and allocates the two screen buffers.
// Only the standard 320x224 engine picture is accepted; false otherwise.
bool Render::init(int in_src_width, int in_src_height, int /*scale*/, int /*video_mode*/, int /*scanlines*/)
{
    src_width  = in_src_width;
    src_height = in_src_height;

    // The engine's internal buffer (S16_WIDTH x S16_HEIGHT, or wide/hires
    // variants) is produced regardless of what we can display; we crop it
    // down to what the STE video shifter can actually show. Widescreen and
    // hi-res video_settings are simply not usable on this target - the
    // frontend/config layer for this port forces standard low-res 320x224
    // before this is ever called (see main_atari.cpp).
    if (src_width != S16_WIDTH || src_height != S16_HEIGHT)
    {
        // Not fatal, but the caller asked for a mode this backend cannot
        // honour; main_atari.cpp is responsible for never doing this.
        return false;
    }

    // Screen memory must live on a 256-byte boundary (STE screen base
    // register only stores address >> 8) and needs double-buffering so we
    // can build the next frame while the shifter reads the current one.
    // NOTE: a real port should allocate this via a MiNT ST-RAM-aware
    // allocator (Mxalloc(..., MX_STRAM)) rather than plain `new`, since
    // screen memory must be in the bottom 16MB / ST-RAM, and must be
    // 256-byte aligned. Left as a TODO for actual bring-up.
    // Over-allocate and round up to a 256-byte boundary: the shifter only
    // stores address bits 8-23, so an unaligned buffer is displayed shifted.
    for (int i = 0; i < 2; i++)
    {
        uint8_t* raw = new uint8_t[SCREEN_BYTES + 256];
        screen_buffer[i] = (uint8_t*)(((uint32_t)raw + 255) & ~255u);
    }

    VID_RES = 0;           // low resolution, 4 planes / 16 colours
    VID_SYNC_MODE = 0;     // 60Hz timing bit off -> PAL 50Hz (matches config.fps region choice elsewhere)

    return true;
}

// Frees the two screen buffers.
void Render::disable()
{
    for (int i = 0; i < 2; i++)
    {
        delete[] screen_buffer[i];
        screen_buffer[i] = nullptr;
    }
}

// Interface shared with the other backends; nothing to prepare here.
bool Render::start_frame()
{
    return true;
}

// Mirrors RenderBase::convert_palette() exactly (see sdl2/renderbase.cpp)
// but stores a packed 12-bit RGB444 value (4 bits/channel, matching STE
// palette precision exactly) instead of a 32-bit true-colour value.
void Render::convert_palette(uint32_t adr, uint32_t r1, uint32_t g1, uint32_t b1)
{
    adr >>= 1;

    uint16_t r4 = (uint16_t)(r1 >> 1); // 5-bit (0-31) -> 4-bit (0-15)
    uint16_t g4 = (uint16_t)(g1 >> 1);
    uint16_t b4 = (uint16_t)(b1 >> 1);
    rgb444[adr] = (r4 << 8) | (g4 << 4) | b4;

    // Shadow variant, same formula as RenderBase, stored past S16_PALETTE_ENTRIES
    uint32_t rs = r1 * (uint32_t)shadow_multi / 31;
    uint32_t gs = g1 * (uint32_t)shadow_multi / 31;
    uint32_t bs = b1 * (uint32_t)shadow_multi / 31;
    // rs/gs/bs are on the 0-255 scale here (RenderBase's shadow formula)
    r4 = (uint16_t)((rs > 255 ? 255 : rs) >> 4);
    g4 = (uint16_t)((gs > 255 ? 255 : gs) >> 4);
    b4 = (uint16_t)((bs > 255 ? 255 : bs) >> 4);
    rgb444[adr + S16_PALETTE_ENTRIES] = (r4 << 8) | (g4 << 4) | b4;
}

// Interface shared with the Falcon backend, which compares with the previous picture to
// skip unchanged rows. Not used here: every picture is converted in full.
void Render::set_prev_frame(const uint16_t*)
{
}

// Brightness of the shadowed copy of the palette (1.0 = same as the normal colours).
void Render::set_shadow_intensity(float f)
{
    shadow_multi = (int)(255.0f * f);
}

// --------------------------------------------------------------------------
// Frame palette selection.
//  1. atari_hist (68000 asm): count pixels per engine colour index.
//  2. Merge indices that share one RGB444 value, pick the 16 most frequent.
//  3. resolve_slots(): nearest chosen colour for each index that occurs.
// The engine buffer is 320 px wide like the screen, so the 200 visible lines
// are one contiguous run of 64000 pixels starting 12 lines down.
// --------------------------------------------------------------------------
extern "C" void atari_hist(const uint16_t* px, uint16_t* hist, uint32_t count);
extern "C" void atari_c2p(const uint16_t* px, const uint8_t* slot, uint16_t* dst, uint32_t groups);

static const int Y_OFF = (S16_HEIGHT - ATARI_SCREEN_HEIGHT) / 2; // crop 12 lines top+bottom

// Chooses this picture's 16 hardware colours: counts how many visible pixels use each
// 12-bit colour and keeps the 16 most used.
void Render::build_frame_palette(uint16_t* pixels)
{
    static uint32_t chist[4096];
    static uint16_t touched[S16_PALETTE_ENTRIES * 2];
    int nt = 0;

    std::memset(idx_hist, 0, sizeof(idx_hist));
    atari_hist(pixels + Y_OFF * S16_WIDTH, idx_hist, (uint32_t)ATARI_SCREEN_WIDTH * ATARI_SCREEN_HEIGHT);

    for (int i = 0; i < S16_PALETTE_ENTRIES * 2; i++)
    {
        if (!idx_hist[i]) continue;
        uint16_t c = rgb444[i] & 0x0FFF;
        if (!chist[c]) touched[nt++] = c;
        chist[c] += idx_hist[i];
    }

    for (int slot = 0; slot < ATARI_PALETTE_SIZE; slot++)
    {
        int best = -1;
        uint32_t best_count = 0;
        for (int k = 0; k < nt; k++)
        {
            uint16_t c = touched[k];
            if (chist[c] > best_count) { best_count = chist[c]; best = c; }
        }
        current_palette[slot] = (best >= 0) ? (uint16_t)best : 0;
        if (best >= 0) chist[best] = 0;
    }
    // chist[] of the picked colours is already 0; clear the rest for next frame
    for (int k = 0; k < nt; k++) chist[touched[k]] = 0;
}

// Squared 4-bit channel difference without any multiply (68000 has no fast
// 32-bit mul): sq[d + 15] == d*d.
static const uint8_t sq[32] = {
    225,196,169,144,121,100,81,64,49,36,25,16,9,4,1,
    0,
    1,4,9,16,25,36,49,64,81,100,121,144,169,196,225, 0 }; // padded to an even size: a.out sections are not re-aligned per object file

// Maps every engine colour present in the picture to the closest of the 16 hardware
// colours chosen by build_frame_palette().
void Render::resolve_slots()
{
    for (int i = 0; i < S16_PALETTE_ENTRIES * 2; i++)
    {
        if (!idx_hist[i]) continue;
        int c = rgb444[i] & 0x0FFF;
        int r = (c >> 8) & 0xF, g = (c >> 4) & 0xF, b = c & 0xF;
        int best_slot = 0, best_dist = 0x7FFF;
        for (int slot = 0; slot < ATARI_PALETTE_SIZE; slot++)
        {
            int p = current_palette[slot];
            int dist = sq[r - ((p >> 8) & 0xF) + 15] + sq[g - ((p >> 4) & 0xF) + 15] + sq[b - (p & 0xF) + 15];
            if (dist < best_dist) { best_dist = dist; best_slot = slot; if (!dist) break; }
        }
        idx_slot[i] = (uint8_t)best_slot;
    }
}
// Writes the 16 chosen colours to the palette registers.
void Render::write_hw_palette()
{
    volatile uint16_t* pal = VID_PALETTE_BASE;
    for (int i = 0; i < ATARI_PALETTE_SIZE; i++)
    {
        // STE palette register: one nibble per channel (0RGB), but within each nibble bit 3
        // holds the LOWEST bit of the 4-bit value and bits 2-0 its upper three - that is how
        // the STE added a fourth bit while keeping the ST's 3-bit colours in the same place.
        uint16_t c = current_palette[i];
        uint16_t r = (c >> 8) & 0xF, g = (c >> 4) & 0xF, b = c & 0xF;
        r = (uint16_t)(((r & 1) << 3) | (r >> 1));
        g = (uint16_t)(((g & 1) << 3) | (g >> 1));
        b = (uint16_t)(((b & 1) << 3) | (b >> 1));
        pal[i] = (uint16_t)((r << 8) | (g << 4) | b);
    }
}

// Points the video shifter at the buffer just drawn.
void Render::flip_screen_base()
{
    uint32_t addr = (uint32_t)screen_buffer[back_buffer];
    VID_SCREEN_BASE_HI  = (uint8_t)(addr >> 16);
    VID_SCREEN_BASE_MID = (uint8_t)(addr >> 8);
    // Low byte is implicitly 0 - screen base is forced to a 256-byte
    // boundary by the hardware, which is why the buffers above must be
    // allocated aligned to that.
}

// Converts the engine's picture (one 16-bit colour index per pixel) to the 4-bitplane screen
// format, into the buffer that is not on screen: palette choice, colour mapping, then the
// chunky-to-planar conversion (video_asm.S).
void Render::draw_frame(uint16_t* pixels)
{
    int draw_buffer = 1 - back_buffer; // write into the buffer NOT currently on screen

    uint32_t t0 = PERF_NOW();
    build_frame_palette(pixels);
    resolve_slots();
    uint32_t t1 = PERF_NOW();
    atari_c2p(pixels + Y_OFF * S16_WIDTH, idx_slot, (uint16_t*)screen_buffer[draw_buffer],
              (uint32_t)ATARI_SCREEN_WIDTH * ATARI_SCREEN_HEIGHT / 16);
    uint32_t t2 = PERF_NOW();
    write_hw_palette();
    PERF_PRINTF("DRAW pal=%lu c2p=%lu (5ms)\r\n", (unsigned long)(t1-t0),(unsigned long)(t2-t1));

    back_buffer = draw_buffer;
}
// Shows the picture just drawn.
bool Render::finalize_frame()
{
    // Swap on the next Vsync so the shifter never reads a half-written
    // buffer. atari/timer.cpp's frame pacing is expected to have already
    // waited for VBL before draw_frame() was called for this to be tear-free;
    // if not, this is a plain immediate swap (may tear).
    flip_screen_base();
    return true;
}
#endif // !PLATFORM_FALCON
