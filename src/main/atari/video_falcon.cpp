/***************************************************************************
    Atari Falcon 030 video backend: 16-bit true colour (RGB565).

    Unlike the STE backend (video.cpp) there is no palette reduction and no
    bitplane packing: each engine palette index is looked up once in a
    65536-colour table and written straight to screen memory.

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

#ifdef PLATFORM_FALCON

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <mint/osbind.h>
#include <mint/falcon.h>
#include <mint/cookie.h>
#include "atari/video.hpp"
#include "atari/screenshot.hpp"

extern "C" void atari_truecolor(const uint16_t* px, const uint16_t* pal, uint16_t* dst, uint32_t blocks4);
#include "atari/options.hpp"
extern "C" uint32_t atari_rowcmp(const uint32_t* a, const uint32_t* b, uint32_t longs);
#ifdef MOVE16
extern "C" void atari_copy16(void* dst, const void* src, uint32_t lines);
static bool use_move16 = false;                                   // set in init(): 68060 and aligned screen rows
static uint16_t* linebuf = 0;   // one converted row, 16-byte aligned at run time (a.out data sections are not), in fast RAM
#endif

// Converts `rows` rows of indices (320 wide, contiguous) to RGB565 at dst (dst_stride pixels per row).
// With -DMOVE16 on a 68060 every row is converted into a fast-RAM buffer and pushed to the ST-RAM
// screen with MOVE16 bursts instead of long-word stores.
static void convert_rows(const uint16_t* src, const uint16_t* pal, uint16_t* dst, int rows, int dst_stride)
{
#ifdef MOVE16
    if (use_move16)
    {
        for (int q = 0; q < rows; q++)
        {
            atari_truecolor(src + q * S16_WIDTH, pal, linebuf, S16_WIDTH / 4);
            atari_copy16(dst + q * dst_stride, linebuf, S16_WIDTH * 2 / 16);
        }
        return;
    }
#endif
    if (dst_stride == S16_WIDTH)
        atari_truecolor(src, pal, dst, (uint32_t)rows * S16_WIDTH / 4);
    else
        for (int q = 0; q < rows; q++)
            atari_truecolor(src + q * S16_WIDTH, pal, dst + q * dst_stride, S16_WIDTH / 4);
}

extern "C" void atari_truecolor_x2(const uint16_t* px, const uint16_t* pal, uint16_t* dst, uint32_t rows);

// Set/cleared by main_atari.cpp's P-key toggle (see main_atari.cpp's STATE_GAME case).
extern bool pause_engine;
// Set by main_atari.cpp's C-key check, cleared here once the capture is done.
extern bool g_take_screenshot;
// Set by main_atari.cpp's Escape/F10 check when Escape/F10 is first seen down; resolved by Y
// (quit) or N (cancel) - see main_atari.cpp's STATE_GAME case and draw_quit_confirm() below.
extern bool g_quit_confirm;

// A tiny hardcoded 5x7 bitmap font, blitted straight into the RGB565 output after the normal
// frame is drawn (see draw_pause_text()/draw_quit_confirm() below). This exists because the
// engine's own HUD text layer (engine/ohud.cpp's blit_text_new(), used by the FPS counter/debug
// overlay) turned out not to render anything visible with this ROM set's tile data - not worth
// reverse-engineering the ROM's own glyph encoding, when drawing on the final pixels sidesteps
// the question entirely and looks identical regardless of game/palette state. Only the letters
// actually used by "PAUSE" and "QUIT Y N" are defined.
namespace pause_font
{
    const uint8_t P[7] = { 0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000 };
    const uint8_t A[7] = { 0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001 };
    const uint8_t U[7] = { 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110 };
    const uint8_t S[7] = { 0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110 };
    const uint8_t E[7] = { 0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111 };
    const uint8_t Q[7] = { 0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010, 0b01101 };
    const uint8_t I[7] = { 0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b11111 };
    const uint8_t T[7] = { 0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100 };
    const uint8_t Y[7] = { 0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100 };
    const uint8_t N[7] = { 0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001 };
    const uint8_t D[7] = { 0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11110 };
    const uint8_t O[7] = { 0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110 };
    const uint8_t W[7] = { 0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b10101, 0b01010 };

    // NULL = space (skip, no glyph drawn, still advances the cursor - see draw_text()).
    const uint8_t* glyph_for(char c)
    {
        switch (c)
        {
            case 'P': return P; case 'A': return A; case 'U': return U;
            case 'S': return S; case 'E': return E; case 'Q': return Q;
            case 'I': return I; case 'T': return T; case 'Y': return Y;
            case 'N': return N; case 'D': return D; case 'O': return O;
            case 'W': return W; default: return 0;
        }
    }
}

// Blits one glyph (5x7, from pause_font above) at 4x scale (20x28 px), solid black on solid
// bright yellow (a filled box, not just outlined strokes - the largest, simplest possible shape
// so there is no risk of a thin 1px line being lost to scaling/rounding).
static void blit_pause_glyph(uint16_t* dst, int stride, int x0, int y0, const uint8_t* glyph)
{
    const uint16_t ON = 0xFFE0, OFF = 0x0000;
    const int SCALE = 4;
    // Solid yellow backing box first (2px margin), so the glyph reads as a filled block even if
    // the "on" bit pattern is sparse in places.
    for (int y = -2; y < 7 * SCALE + 2; y++)
        for (int x = -2; x < 5 * SCALE + 2; x++)
            dst[(y0 + y) * stride + (x0 + x)] = ON;
    for (int row = 0; row < 7; row++)
    {
        for (int col = 0; col < 5; col++)
        {
            const bool on = (glyph[row] >> (4 - col)) & 1;
            const uint16_t colour = on ? OFF : ON;
            for (int sy = 0; sy < SCALE; sy++)
            {
                uint16_t* p = dst + (y0 + row * SCALE + sy) * stride + (x0 + col * SCALE);
                for (int sx = 0; sx < SCALE; sx++) p[sx] = colour;
            }
        }
    }
}

// Darkens the whole picture with a checkerboard pattern (every other pixel halved) - a real
// alpha blend would need a divide per pixel, this is the classic cheap equivalent and is
// unmistakable even if the text drawn on top of it has a problem.
static void darken_screen(uint16_t* dst, int stride)
{
    for (int y = 0; y < S16_HEIGHT; y++)
    {
        uint16_t* row = dst + y * stride;
        for (int x = (y & 1); x < 320; x += 2)
        {
            const uint16_t c = row[x];
            const uint16_t r = (uint16_t)(((c >> 11) & 0x1F) >> 1) << 11;
            const uint16_t g = (uint16_t)(((c >> 5) & 0x3F) >> 1) << 5;
            const uint16_t b = (uint16_t)(((c) & 0x1F) >> 1);
            row[x] = (uint16_t)(r | g | b);
        }
    }
}

// Draws `text` (letters covered by pause_font::glyph_for(), plus spaces) centred at row y0 of
// the 320-wide game picture (dst points at the picture's own (0,0) - see the fal_xoff/centering
// adjustment in draw_frame() below, applied before this is called). A space still advances the
// cursor (so "QUIT Y N" keeps its letter groups apart) but draws nothing.
static void draw_text_centered(uint16_t* dst, int stride, int y0, const char* text)
{
    const int GLYPH_W = 24; // 20px glyph + 4px gap
    int len = 0;
    for (const char* p = text; *p; p++) len++;
    const int x0 = (320 - len * GLYPH_W) / 2;
    for (int i = 0; text[i]; i++)
    {
        const uint8_t* glyph = pause_font::glyph_for(text[i]);
        if (glyph) blit_pause_glyph(dst, stride, x0 + i * GLYPH_W, y0, glyph);
    }
}

// Darkens the picture and writes PAUSE across it (drawn here, after the palette conversion,
// rather than through the engine's own text layer).
static void draw_pause_text(uint16_t* dst, int stride)
{
    darken_screen(dst, stride);
    draw_text_centered(dst, stride, 40, "PAUSE");
}

// Escape/F10 was pressed once: ask for confirmation before actually quitting, instead of quitting
// immediately - both because a stray/repeated keypress should not silently exit the game, and
// because this doubles as a visible proof that Escape/F10 really was detected at all, independent
// of whatever happens afterwards in quit_func()/exit() (see main_atari.cpp's g_quit_confirm).
static void draw_quit_confirm(uint16_t* dst, int stride)
{
    darken_screen(dst, stride);
    draw_text_centered(dst, stride, 24, "QUIT");
    draw_text_centered(dst, stride, 96, "UP YES");
    draw_text_centered(dst, stride, 140, "DOWN NO");
}

Render render;

// No screen memory yet (see init()); every row marked as never converted.
Render::Render()
{
    screen_buffer[0] = screen_buffer[1] = screen_buffer[2] = 0;
    back_buffer = 0;
    shadow_multi = 255;
    fal_lines = 0;
    fal_stride = 320;
    fal_xoff = 0;
    std::memset(rgb565, 0, sizeof(rgb565));
    prev_frame = 0;
    pal_dirty = true;
    std::memset(buf_ver, 0, sizeof(buf_ver));
    for (int i = 0; i < S16_HEIGHT; i++) row_ver[i] = 1;
}

// Puts the display back as it was found.
Render::~Render()
{
    disable();
}

// Picks a 16-bit video mode at least 224 lines high for the monitor in use, allocates the
// three screen buffers in ST-RAM and switches to that mode. The mode and screen address
// found on entry are saved for disable(). False if no suitable mode exists.
bool Render::init(int in_src_width, int in_src_height, int /*scale*/, int /*video_mode*/, int /*scanlines*/)
{
    // Captured before anything below changes the display - see disable() and video.hpp.
    saved_physbase = Physbase();
    saved_mode = VsetMode(-1);

    src_width  = in_src_width;
    src_height = in_src_height;
#ifdef LOWRES
    if (src_width != S16_WIDTH / 2 || src_height != S16_HEIGHT / 2)
        return false;
#else
    if (src_width != S16_WIDTH || src_height != S16_HEIGHT)
        return false;
#endif

    // Pick a 16-bit mode that really holds all 224 game lines: the size of every candidate
    // is asked from the video hardware (VgetSize) instead of assumed, and a mode that is too
    // small is refused rather than cutting the picture.
    short mon = VgetMonitor(); // 0 mono, 1 ST colour (RGB), 2 VGA, 3 TV
    short cur = VsetMode(-1);  // keep the current PAL/NTSC setting
    short candidates[3];
    int   ncand = 0;
    if (mon == 2)
        candidates[ncand++] = BPS16 | COL40 | VGA | VERTFLAG;                       // 320x240 (line doubled)
    else if (mon == 1 || mon == 3)
    {
        short pal = cur & PAL;
        candidates[ncand++] = BPS16 | COL40 | pal | OVERSCAN;                       // 384x240
        candidates[ncand++] = BPS16 | COL40 | pal | VERTFLAG;                       // 320x400 interlaced
    }
    short mode = 0;
    bool found = false;
    for (int i = 0; i < ncand && !found; i++)
    {
        const bool overscan = (candidates[i] & OVERSCAN) && !(candidates[i] & VGA);
        const int  width = overscan ? 384 : 320;
        const long bytes = VgetSize(candidates[i]);
        const int  lines = (int)(bytes / (width * 2));
        if (lines >= S16_HEIGHT)
        {
            mode = candidates[i];
            fal_stride = width;
            fal_lines = lines;
            found = true;
        }
    }
#ifdef LOWRES
    if (found && fal_stride != 320) found = false;
#endif
    if (!found)
    {
        printf("No 16-bit video mode with at least %d lines is available on this monitor.\r\nPress a key.\r\n", S16_HEIGHT);
        Cconin();
        return false;
    }
    fal_xoff = (fal_stride - 320) / 2;
    PERF_PRINTF("VIDEO mon=%d mode=%x %dx%d%c%c", (int)mon, (unsigned)mode, fal_stride, fal_lines, 13, 10);

    long size = (long)fal_stride * fal_lines * 2;
    for (int i = 0; i < 3; i++)
    {
        // Video memory must be ST-RAM (Mxalloc mode 0); pad for 4-byte alignment.
        long raw = Mxalloc(size + 32, 0);
        if (raw <= 0) return false;
        screen_buffer[i] = (uint8_t*)((raw + 15) & ~15L);
        std::memset(screen_buffer[i], 0, size);
    }

#ifdef MOVE16
    {
        long cpu = 0;
        Getcookie(C__CPU, &cpu);
        void* raw = std::malloc(S16_WIDTH * 2 + 32);
        if (raw) linebuf = (uint16_t*)(((unsigned long)raw + 15) & ~15UL);
        use_move16 = (cpu == 60) && linebuf && (fal_stride * 2) % 16 == 0 && (fal_xoff * 2) % 16 == 0;
        PERF_PRINTF("MOVE16 %s%c%c", use_move16 ? "on" : "off (not a 68060)", 13, 10);
    }
#endif
    VsetScreen(-1L, (long)screen_buffer[0], (short)3, mode);
    return true;
}

// Puts back the video mode and screen address saved by init().
void Render::disable()
{
    // Buffers stay allocated: TOS reclaims them at exit.
    // Puts the display back the way init() found it (see saved_physbase/saved_mode there), so
    // EmuTOS's own desktop is actually visible again after quitting instead of the last game
    // picture staying on screen forever with the display hardware still pointed at our now-freed
    // buffer. An earlier attempt at this (this session, since reverted) caused a black screen from
    // boot rather than at quit - that version restored a *guessed* mode read at the wrong time,
    // not the one actually saved at the top of init() before anything changes the display, which
    // is what this version does; confirm on both CPU targets before assuming this is airtight.
    if (saved_physbase)
        VsetScreen(-1L, (long)saved_physbase, (short)3, saved_mode);
}

// Interface shared with the other backends; nothing to prepare here.
bool Render::start_frame()
{
    return true;
}

// Same inputs as RenderBase::convert_palette(): 5-bit r1,g1,b1.
void Render::convert_palette(uint32_t adr, uint32_t r1, uint32_t g1, uint32_t b1)
{
    adr >>= 1;

    uint32_t g6 = (g1 << 1) | (g1 >> 4);
    uint16_t n0 = (uint16_t)((r1 << 11) | (g6 << 5) | b1);

    uint32_t rs = r1 * (uint32_t)shadow_multi / 31; // 0-255 scale
    uint32_t gs = g1 * (uint32_t)shadow_multi / 31;
    uint32_t bs = b1 * (uint32_t)shadow_multi / 31;
    if (rs > 255) rs = 255;
    if (gs > 255) gs = 255;
    if (bs > 255) bs = 255;
    uint16_t n1 = (uint16_t)(((rs >> 3) << 11) | ((gs >> 2) << 5) | (bs >> 3));
    if (rgb565[adr] != n0 || rgb565[adr + S16_PALETTE_ENTRIES] != n1)
        pal_dirty = true;
    rgb565[adr] = n0;
    rgb565[adr + S16_PALETTE_ENTRIES] = n1;
}

// The picture drawn before this one: draw_frame() compares the two row by row and converts
// only the rows that changed.
void Render::set_prev_frame(const uint16_t* prev)
{
    prev_frame = prev;
}

// Brightness of the shadowed copy of the palette (1.0 = same as the normal colours).
void Render::set_shadow_intensity(float f)
{
    shadow_multi = (int)(255.0f * f);
}

// Shows the buffer just drawn, from the next vertical blank.
void Render::flip_screen_base()
{
    VsetScreen(-1L, (long)screen_buffer[back_buffer], (short)-1, (short)-1);
}

// Converts the engine's picture (one 16-bit colour index per pixel) to RGB565 into a screen
// buffer that is neither shown nor waiting to be shown. Rows unchanged since that buffer last
// received them are skipped. The PAUSE / QUIT captions and the screenshot are handled here,
// on the converted picture.
void Render::draw_frame(uint16_t* pixels)
{
    // Triple buffering: any buffer that is neither on screen nor already queued for the next retrace.
    const uint8_t* shown = (const uint8_t*)Physbase();
    int draw_buffer = 0;
    while (draw_buffer < 2 && (screen_buffer[draw_buffer] == shown || draw_buffer == back_buffer))
        draw_buffer++;

    uint32_t t0 = PERF_NOW();
    uint16_t* dst = (uint16_t*)screen_buffer[draw_buffer];
    const uint16_t* src = pixels;
#ifdef LOWRES
    // 160x112 source -> 320x224 centred in the mode (init() guarantees a 320-wide mode): 2x2 doubling.
    dst += ((fal_lines - S16_HEIGHT) / 2) * fal_stride;
    atari_truecolor_x2(src, rgb565, dst, (uint32_t)(S16_HEIGHT / 2));
#else
    const int r0 = 0, lines = S16_HEIGHT;
    // Centre the whole 224-line frame in the mode; the border stays black.
    dst += ((fal_lines - S16_HEIGHT) / 2) * fal_stride + fal_xoff;

    // Row cache: skip rows that are unchanged since this buffer last received them.
    if (pal_dirty)
    {
        for (int r = 0; r < S16_HEIGHT; r++) row_ver[r]++;
        pal_dirty = false;
    }
    uint32_t* ver = buf_ver[draw_buffer];
    const uint32_t* src32 = (const uint32_t*)pixels;
    const uint32_t* prev32 = (const uint32_t*)prev_frame;
    int run = -1; // first row of the pending run of rows to convert
    const bool reduced = g_row_reduced != 0;
    for (int r = r0; r <= r0 + lines; r++)
    {
        bool need = false;
        if (r < r0 + lines)
        {
            const int sr = g_row_src[r];     // the drawn row this row shows (itself unless vscale < 100)
            if (g_row_draw[r] && atari_rowcmp(src32 + sr * (S16_WIDTH / 2), prev32 + sr * (S16_WIDTH / 2), S16_WIDTH / 2))
                row_ver[sr]++;
            need = ver[r] != row_ver[sr];
#ifdef NO_ROWCACHE
            need = true; // test option: convert every row every time
#endif
            if (need) ver[r] = row_ver[sr];
            if (need && reduced)
            {
                // Rows repeat drawn rows, so they are converted one by one.
                convert_rows(src + sr * S16_WIDTH, rgb565, dst + (r - r0) * fal_stride, 1, fal_stride);
                need = false;
            }
        }
        if (need)
        {
            if (run < 0) run = r;
        }
        else if (run >= 0)
        {
            convert_rows(src + run * S16_WIDTH, rgb565, dst + (run - r0) * fal_stride, r - run, fal_stride);
            run = -1;
        }
    }
#endif
    if (pause_engine)
        draw_pause_text(dst, fal_stride);
    if (g_quit_confirm)
        draw_quit_confirm(dst, fal_stride);
    if (g_take_screenshot)
    {
        // Captures exactly what draw_frame() just put together, including the PAUSE overlay
        // above if paused - a one-off, CPU-heavy call (no lookup tables, see screenshot.cpp) that
        // is fine to let stall this one picture since it only ever runs on a manual keypress.
        atari_save_screenshot(dst, S16_WIDTH, S16_HEIGHT, fal_stride);
        g_take_screenshot = false;
    }
#ifdef ROWCHECK
    {
        // Test aid: the row cache must give exactly what a full conversion gives.
        static uint16_t* ref = (uint16_t*)std::malloc(S16_WIDTH * 2);
        static int bad = 0, frames = 0;
        bool ok = true;
        for (int q = 0; q < lines; q++)
        {
            const uint16_t* sr = src + g_row_src[q + r0] * S16_WIDTH;
            for (int xx = 0; xx < S16_WIDTH; xx++) ref[xx] = rgb565[sr[xx]];      // independent of the assembler conversion
            if (std::memcmp(ref, dst + q * fal_stride, S16_WIDTH * 2) != 0) ok = false;
        }
        frames++;
        if (!ok) bad++;
        if ((frames % 20) == 0) std::printf("ROWCHECK frames=%d bad=%d%c%c", frames, bad, 13, 10);
    }
#endif
    uint32_t t1 = PERF_NOW();
    PERF_PRINTF("DRAW tc=%lu (5ms)\r\n", (unsigned long)(t1 - t0));
#ifdef DUMP_FRAME
    // Test aid (-DDUMP_FRAME): write frames 30 and 300 to files as raw big-endian RGB565.
    static int fc = 0;
    fc++;
    if (fc == 30 || fc == 300)
    {
        FILE* fp = fopen(fc == 30 ? "frame30.bin" : "frame300.bin", "wb");
        if (fp) { for (int q = 0; q < S16_HEIGHT; q++) fwrite(dst + q * fal_stride, 2, S16_WIDTH, fp); fclose(fp); }
    }
#endif

    back_buffer = draw_buffer;
}

// Shows the picture just drawn.
bool Render::finalize_frame()
{
    flip_screen_base();
    return true;
}

#endif // PLATFORM_FALCON
