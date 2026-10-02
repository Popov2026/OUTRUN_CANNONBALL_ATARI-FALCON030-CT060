#pragma once

/***************************************************************************
    Atari Mega STE Video Backend.

    ============================================================================
    THE CORE PROBLEM
    ============================================================================
    The engine (src/main/video.cpp, which is NOT modified by this port) composes
    one frame by having three software layers write already-composited palette
    INDICES into a single shared buffer:

        uint16_t pixels[S16_WIDTH * S16_HEIGHT];   // 320 x 224 (398 wide in widescreen)

    hwroad, hwtiles and hwsprites collectively address up to S16_PALETTE_ENTRIES*2
    == 0x2000 (8192) distinct index values per frame (0x1000 real colours, doubled
    for the "shadow" darkened variants). Video::refresh_palette() decodes each
    index's 16-bit System 16 word (format RRRR GGGG BBBB + shadow/hilite bits) into
    r/g/b and calls RenderBase::convert_palette(), which every existing platform
    backend (SDL2 surface/GL/GLES, DirectX) turns into a native-depth RGB LUT of
    up to 8192 entries, then draw_frame() does a 1:1 `screen[i] = rgb[pixels[i]]`
    lookup. That is only possible because every existing target has thousands of
    simultaneous on-screen colours available.

    The Mega STE video shifter does NOT. Its relevant native mode is:

        320 x 200, 4 bitplanes  ->  16 simultaneous on-screen colours,
        chosen from a 4096-colour (4-bit-per-channel RGB) hardware palette.

    Two encouraging facts limit the damage:
      1. STE hardware palette precision is 4 bits/channel - IDENTICAL to System
         16's native RRRR/GGGG/BBBB precision. There is no colour-quantization
         error to worry about, only a *simultaneous colour count* problem.
      2. The Mega STE (unlike a plain STE) has the STE Blitter, which can help
         with the chunky->planar repacking step once the pipeline below works.
    There is no hardware scaler/rotator of any kind for the road or sprites
    (that silicon is what made the original arcade board special) - all of
    hwroad/hwtiles/hwsprites' pixel-pushing work still has to happen on a bare
    16 MHz 68000, on top of whatever this file costs. See timing note at the
    bottom of this file and the port README notes in the top-level report.

    ============================================================================
    THE PLAN IMPLEMENTED HERE (v1 / MVP)
    ============================================================================
    a) Crop the 224-line frame to the centre 200 lines (drop 12 lines top and
       bottom) to fit the STE's 320x200 low-res mode exactly. (An "overscan"
       225/240-line mode is possible on STE via well-known sync tricks, but it
       is timing-critical and not attempted in this pass - noted as future work.)
    b) Once per frame, build a 16-colour hardware palette from the ACTUAL
       colours present in that frame:
         - Take a histogram over the already-composited index buffer (cheap:
           one pass, using the same 8192-entry rgb[] LUT every other backend
           already builds via convert_palette()).
         - Pick the 16 most-used *distinct final RGB444 colours* (not indices -
           several indices can map to the same visible colour, which happens a
           lot with the shadow/hilite duplication).
       This is a global "whole frame" reduction, not per-line, so it is a real,
       visible quality cut versus arcade OutRun: expect visible banding/loss on
       any frame using more than 16 genuinely distinct colours at once (which is
       most frames - sky gradient + road + cars + HUD easily exceeds 16). This
       is the single biggest FIDELITY compromise of the port; see the "future
       work" note below for the fix that was deliberately NOT attempted yet.
    c) Build a 4096-entry nearest-colour lookup table (RGB444 key -> one of the
       16 chosen palette slots) once per frame, then walk the pixel buffer
       converting each index -> RGB444 -> nearest slot -> 4-bit chunky nibble.
    d) Pack the resulting 320x200 4-bit chunky image into ST bitplane format
       (4 planes, 16 pixels/word/plane, standard interleaved layout) and hand
       the finished bitplane buffer to the video shifter (screen base register).
    Steps (b)+(c)+(d) are pure added CPU cost that the arcade/PC ports never
    had to pay; they are the "software scaler tax" for lacking dedicated video
    hardware, on top of hwroad/hwtiles/hwsprites' own (already fairly heavy)
    compositing cost. Both are profiled together - see PROFILING NOTES below.

    ============================================================================
    FUTURE WORK (NOT implemented in this pass - flagged, not attempted)
    ============================================================================
    hwroad's background/foreground renderers already vary colour primarily
    PER SCANLINE (sky gradient bands, road surface/edge/stripe colours change
    a few times a frame but are constant across most of a line's width). This
    is exactly the shape of problem the classic ST/STE "per-line palette
    reload" demo trick was built for: reload some/all of the 16 palette
    registers during each HBL, driven by a scanline-indexed palette table,
    which can raise the *effective* simultaneous colour count well past 16
    across a frame (though not within a single line - sprites/cars overlaid
    on a line still have to share that line's palette). This was NOT attempted
    here because:
      - It requires accurately-timed HBL interrupt work (~200+ interrupts/frame,
        each having to write up to 16 words to $FF8240-$FF825E) that competes
        directly with the CPU time hwroad/hwtiles/hwsprites need, and getting
        the timing right needs iterative testing against real Hatari cycle-exact
        timing that this pass could not do (see report: toolchain/emulator
        install is pending a decision, so nothing here has been run yet).
      - It would still need the same histogram/reduction machinery, just
        computed per-scanline-band instead of per-frame, which is strictly more
        CPU work, not less - it buys fidelity, not speed.
    Recommendation for whoever picks this up next: get the v1 pipeline in this
    file profiled and boot-tested in Hatari first (real timings beat estimates),
    then decide whether there is *any* CPU budget left to spend on per-line
    palette cycling, because right now the honest expectation (see report) is
    that there is not.

    ============================================================================
    PROFILING NOTES
    ============================================================================
    This file's draw_frame() is deliberately structured as four separately
    timeable passes (histogram -> palette select -> LUT build -> chunky+pack)
    so each can be wrapped in a cycle counter during Hatari bring-up. Until
    this actually runs on/under an emulator, any millisecond figures would be
    invented, not measured - see the top-level report for the honest,
    profiler-free order-of-magnitude estimate instead.

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

#include "../stdint.hpp"
#include "../globals.hpp"

// Native STE low-res geometry actually pushed to the video shifter.
const int ATARI_SCREEN_WIDTH  = 320;
const int ATARI_SCREEN_HEIGHT = 200;
const int ATARI_PLANES        = 4;                 // 16 colours
const int ATARI_PALETTE_SIZE  = 1 << ATARI_PLANES;  // 16 hardware palette slots

// Same public surface every other Cannonball renderer implements
// (see src/main/sdl2/renderbase.hpp), deliberately NOT inheriting from
// RenderBase since that header pulls in <SDL.h>.
class Render
{
public:
    Render();
    ~Render();

    bool init(int src_width, int src_height, int scale, int video_mode, int scanlines);
    void disable();
    bool start_frame();
    bool finalize_frame();
    void draw_frame(uint16_t* pixels);
    void set_prev_frame(const uint16_t* prev);   // the previous picture (Falcon row cache); ignored elsewhere
    void convert_palette(uint32_t adr, uint32_t r1, uint32_t g1, uint32_t b1);
    void set_shadow_intensity(float f);
    bool supports_window() { return false; }
    bool supports_vsync()  { return true; }  // VBL-synced swap; see atari/timer.cpp

private:
#ifdef PLATFORM_FALCON
    // Falcon 030: 16-bit true colour (RGB565), one table lookup per pixel -
    // no palette reduction or bitplane conversion needed.
    int src_width, src_height;
    uint16_t rgb565[S16_PALETTE_ENTRIES * 2];
    int shadow_multi;
    uint8_t* screen_buffer[3];   // triple buffering: never draw into the shown or the pending one
    int back_buffer;
    int fal_lines;        // lines of the chosen video mode (always >= S16_HEIGHT: the game is never cropped)
    int fal_stride;       // pixels per line of the chosen video mode (320, or 384 in overscan)
    int fal_xoff;         // first game column inside a line (centres the 320 pixels)
    // Row cache: a row is converted again only if its indices (or the palette) changed
    // since the last time this particular screen buffer received it.
    const uint16_t* prev_frame;     // the previous frame's indices (the other pixel buffer of Video)
    uint32_t row_ver[S16_HEIGHT];   // bumped whenever a row's content changes
    uint32_t buf_ver[3][S16_HEIGHT];// row_ver value each screen buffer was last converted at
    bool pal_dirty;
    void flip_screen_base();
    // The video mode/screen address in effect when init() first ran, i.e. whatever EmuTOS's own
    // desktop was using before the game took over the display - restored in disable(), so the
    // desktop is actually visible again after quitting instead of showing our last game picture
    // forever (see main_atari.cpp's quit_func()).
    void* saved_physbase;
    short saved_mode;
#else
    int src_width, src_height;

    // Full RGB444 (0-4095) value for every one of the engine's palette
    // indices (0x2000 entries, matching RenderBase::rgb[] elsewhere).
    // Built by convert_palette(), same as every other backend.
    uint16_t rgb444[S16_PALETTE_ENTRIES * 2];

    int shadow_multi;

    // Double-buffered bitplane screen (STE screen base must be word-aligned
    // and is swapped via Vsync-timed write of the base address register).
    static const uint32_t PLANE_BYTES_PER_LINE = ATARI_SCREEN_WIDTH / 8; // 40
    static const uint32_t SCREEN_BYTES = PLANE_BYTES_PER_LINE * ATARI_PLANES * ATARI_SCREEN_HEIGHT; // 32000
    uint8_t* screen_buffer[2];
    int back_buffer;

    // Chosen 16-colour palette (RGB444) for the CURRENT frame, and the
    // 4096-entry nearest-colour LUT rebuilt from it every frame.
    uint16_t current_palette[ATARI_PALETTE_SIZE];
    // Per engine-colour-index: pixel count this frame (filled by the asm
    // histogram) and the chosen palette slot (filled lazily, only for
    // indices that actually occur - a few hundred, not 4096).
    uint16_t idx_hist[S16_PALETTE_ENTRIES * 2];
    uint8_t  idx_slot[S16_PALETTE_ENTRIES * 2];

    void build_frame_palette(uint16_t* pixels);
    void resolve_slots();
    void chunky_to_planar(uint16_t* pixels, uint8_t* dest);
    void write_hw_palette();
    void flip_screen_base();
#endif
};

extern Render render;
