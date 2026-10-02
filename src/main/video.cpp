/***************************************************************************
    Video Rendering. 
    
    - Renders the System 16 Video Layers
    - Handles Reads and Writes to these layers from the main game code
    - Interfaces with platform specific rendering code

    Copyright Chris White.
    See license.txt for more details.
***************************************************************************/

#include <iostream>
#include <cstdio>

#include "video.hpp"
#ifdef PLATFORM_ATARI
#include <cstring>
#include "atari/options.hpp"
#endif
#include "globals.hpp"
#include "frontend/config.hpp"
#include "engine/oroad.hpp"

#ifdef PLATFORM_ATARI
#include "atari/video.hpp"
#elif WITH_OPENGL
#include "sdl2/rendergl.hpp"
#elif WITH_OPENGLES
#include "sdl2/rendergles.hpp"
#else
#include "sdl2/rendersurface.hpp"
#endif

Video video;

Video::Video(void)
{
    renderer     = new Render();
    pixels       = NULL;
    pixels_alt   = NULL;
    sprite_layer = new hwsprites();
    tile_layer   = new hwtiles();

    set_shadow_intensity(shadow::ORIGINAL);
    enabled      = false;
}

Video::~Video(void)
{
    delete sprite_layer;
    delete tile_layer;
    if (pixels) delete[] pixels;
    if (pixels_alt) delete[] pixels_alt;
    renderer->disable();
    delete renderer;
}

int Video::init(Roms* roms, video_settings_t* settings)
{
    if (!set_video_mode(settings))
        return 0;

    // Internal pixel array. The size of this is always constant
    if (pixels) delete[] pixels;
    pixels = new uint16_t[config.s16_width * config.s16_height];
    if (pixels_alt) delete[] pixels_alt;
    pixels_alt = new uint16_t[config.s16_width * config.s16_height];

    // Convert S16 tiles to a more useable format
    tile_layer->init(roms->tiles.rom, config.video.hires != 0);
    
    clear_tile_ram();
    clear_text_ram();
    if (roms->tiles.rom)
    {
        delete[] roms->tiles.rom;
        roms->tiles.rom = NULL;
    }

    // Convert S16 sprites
    sprite_layer->init(roms->sprites.rom);
    if (roms->sprites.rom)
    {
        delete[] roms->sprites.rom;
        roms->sprites.rom = NULL;
    }

    // Convert S16 Road Stuff
    hwroad.init(roms->road.rom, config.video.hires != 0);
    if (roms->road.rom)
    {
        delete[] roms->road.rom;
        roms->road.rom = NULL;
    }

    enabled = true;
    return 1;
}

void Video::disable()
{
    renderer->disable();
    enabled = false;
}

// ------------------------------------------------------------------------------------------------
// Configure video settings from config file
// ------------------------------------------------------------------------------------------------

int Video::set_video_mode(video_settings_t* settings)
{
    if (settings->widescreen)
    {
        config.s16_width  = S16_WIDTH_WIDE;
        config.s16_x_off = (S16_WIDTH_WIDE - S16_WIDTH) / 2;
    }
    else
    {
        config.s16_width = S16_WIDTH;
        config.s16_x_off = 0;
    }

    config.s16_height = S16_HEIGHT;

    // Internal video buffer is doubled in hi-res mode.
    if (settings->hires)
    {
        config.s16_width  <<= 1;
        config.s16_height <<= 1;
    }

#ifdef LOWRES
    // Half-resolution rendering buffer (Falcon LOWRES build): the engine keeps
    // working in 320x224 coordinates, the layers draw into a 160x112 buffer.
    config.s16_width  >>= 1;
    config.s16_height >>= 1;
#endif

    if (settings->scanlines < 0) settings->scanlines = 0;
    else if (settings->scanlines > 100) settings->scanlines = 100;

    if (settings->scale < 1)
        settings->scale = 1;

    set_shadow_intensity(settings->shadow == 0 ? shadow::ORIGINAL : shadow::MAME);

    renderer->init(config.s16_width, config.s16_height, settings->scale, settings->mode, settings->scanlines);

    return 1;
}

// --------------------------------------------------------------------------------------------
// Shadow Colours. 
// 63% Intensity is the correct value derived from hardware as follows:
//
// 1/ Shadows are just an extra 220 ohm resistor that goes to ground when enabled.
// 2/ This is in parallel with the resistor-"DAC" (3.9k, 2k, 1k, 0.5k, 0.25k), 
//    and otherwise left floating.
//
// Static calculation example:
// 
// const float rDAC   = 1.f / (1.f/3900.f + 1.f/2000.f + 1.f/1000.f + 1.f/500.f + 1.f/250.f); 
// const float rShade = 220.f;                                                             
// const float shadeAttenuation = rShade / (rShade + rDAC); // 0.63f
// 
// (MAME uses an incorrect value which is closer to 78% Intensity)
// --------------------------------------------------------------------------------------------

void Video::set_shadow_intensity(float f)
{
    renderer->set_shadow_intensity(f);
}

void Video::prepare_frame()
{
    // Renderer Specific Frame Setup
    if (!renderer->start_frame())
        return;

    if (!enabled)
    {
        // Fill with black pixels
        for (int i = 0; i < config.s16_width * config.s16_height; i++)
            pixels[i] = 0;
    }
    else
    {
        // OutRun Hardware Video Emulation
        {
            const bool fg_on = !config.engine.fix_bugs || oroad.horizon_base != ORoad::HORIZON_OFF;
            if (fg_on) hwroad.compute_cover(g_row_cover);
            else       memset(g_row_cover, 0, sizeof(g_row_cover));
            atari_frame_rows();
        }
#ifdef COVERCHECK
        for (int i = 0; i < config.s16_width * config.s16_height; i++) pixels[i] = 0xFFFF;   // sentinel: no valid index has it
#endif
        uint32_t t0 = PERF_NOW();
        tile_layer->update_tile_values();

        uint32_t t1 = PERF_NOW();
#ifndef NO_SKY   // -DNO_SKY (speed test): sky/background layer is not drawn at all
        (hwroad.*hwroad.render_background)(pixels);
#endif
        uint32_t t2 = PERF_NOW();
        tile_layer->render_tile_layer(pixels, 1, 0);      // background layer
        tile_layer->render_tile_layer(pixels, 0, 0);      // foreground layer
        uint32_t t3 = PERF_NOW();

        if (!config.engine.fix_bugs || oroad.horizon_base != ORoad::HORIZON_OFF)
            (hwroad.*hwroad.render_foreground)(pixels);
        uint32_t t4 = PERF_NOW();
        sprite_layer->render(8);
        uint32_t t5 = PERF_NOW();
        tile_layer->render_text_layer(pixels, 1);
        uint32_t t6 = PERF_NOW();
#ifdef PERF_PRINT
        // Printed after t6: this line used to sit between t5 and t6, so the time it takes to
        // write it to the log was counted as text layer time.
        { extern uint32_t g_spr_lines, g_spr_fast; PERF_PRINTF("SPRLINES lines=%lu fastwin=%lu%c%c", (unsigned long)g_spr_lines, (unsigned long)g_spr_fast, 13, 10); g_spr_lines = g_spr_fast = 0; }
#endif
#ifdef COVERSKIPCHECK
        {
            // Test aid: the picture drawn without the rows under the road must be identical, on every
            // drawn row, to the picture drawn with all of them.
            static uint16_t* saved = new uint16_t[320 * 224];
            static int n = 0, bad = 0, badrows = 0, covered = 0;
            unsigned char cover[232];
            memcpy(saved, pixels, 320 * 224 * 2);
            memcpy(cover, g_row_cover, sizeof(cover));
            for (int y = 0; y < 224; y++) covered += cover[y];
            memset(g_row_cover, 0, sizeof(g_row_cover));
            atari_frame_rows();
            (hwroad.*hwroad.render_background)(pixels);
            tile_layer->render_tile_layer(pixels, 1, 0);
            tile_layer->render_tile_layer(pixels, 0, 0);
            (hwroad.*hwroad.render_foreground)(pixels);
            sprite_layer->render(8);
            tile_layer->render_text_layer(pixels, 1);
            int rows_bad = 0;
            for (int y = 0; y < 224; y++)
                if (g_row_draw[y] && memcmp(saved + y * 320, pixels + y * 320, 640) != 0) rows_bad++;
            memcpy(g_row_cover, cover, sizeof(cover));
            atari_frame_rows();
            memcpy(pixels, saved, 320 * 224 * 2);
            if (rows_bad) bad++;
            badrows += rows_bad;
            if (++n % 50 == 0) printf("COVERSKIP n=%d wrong_frames=%d wrong_rows=%d avg_covered_rows=%d%c%c", n, bad, badrows, covered / n, 13, 10);
        }
#endif
#ifdef VSCALECHECK
        if (g_row_reduced)
        {
            // Test aid: draw the same frame again with every row and compare the rows that were drawn.
            static uint16_t* saved = new uint16_t[320 * 224];
            static int n = 0, bad = 0, badrows = 0;
            unsigned char draw[232], mask[232];
            memcpy(saved, pixels, 320 * 224 * 2);
            memcpy(draw, g_row_draw, sizeof(draw));  memcpy(mask, g_row_mask8, sizeof(mask));
            for (int i = 0; i < 224; i++) g_row_draw[i] = 1;
            for (int i = 0; i < 232; i++) g_row_mask8[i] = 0xFF;
            (hwroad.*hwroad.render_background)(pixels);
            tile_layer->render_tile_layer(pixels, 1, 0);
            tile_layer->render_tile_layer(pixels, 0, 0);
            (hwroad.*hwroad.render_foreground)(pixels);
            sprite_layer->render(8);
            tile_layer->render_text_layer(pixels, 1);
            int rows_bad = 0;
            for (int y = 0; y < 224; y++)
                if (draw[y] && memcmp(saved + y * 320, pixels + y * 320, 640) != 0) rows_bad++;
            memcpy(g_row_draw, draw, sizeof(draw));  memcpy(g_row_mask8, mask, sizeof(mask));
            memcpy(pixels, saved, 320 * 224 * 2);
            if (rows_bad) bad++;
            badrows += rows_bad;
            if (++n % 50 == 0) printf("VSCALE n=%d frames_with_wrong_rows=%d rows=%d%c%c", n, bad, badrows, 13, 10);
        }
#endif
#ifdef COVERCHECK
        {
            static int n = 0, bad = 0;
            int left = 0;
            for (int i = 0; i < config.s16_width * config.s16_height; i++) if (pixels[i] == 0xFFFF) left++;
            if (left) bad++;
            if (++n % 100 == 0) printf("COVER n=%d frames_with_unwritten_pixels=%d last=%d%c%c", n, bad, left, 13, 10);
        }
#endif
#ifdef PERF_PRINT
        { extern uint32_t g_txt_fast, g_txt_clip; PERF_PRINTF("TXT fast=%lu clip=%lu%c%c", (unsigned long)g_txt_fast, (unsigned long)g_txt_clip, 13, 10); g_txt_fast = g_txt_clip = 0; }
#endif
        PERF_PRINTF("PREP upd=%lu roadbg=%lu tiles=%lu roadfg=%lu sprites=%lu text=%lu (5ms)\r\n", (unsigned long)(t1-t0),(unsigned long)(t2-t1),(unsigned long)(t3-t2),(unsigned long)(t4-t3),(unsigned long)(t5-t4),(unsigned long)(t6-t5));
     }
}

void Video::render_frame()
{
#ifdef PLATFORM_FALCON
    // Two index buffers take turns: the picture before this one is still in the other, so the
    // Falcon row cache can compare against it without keeping a separate copy.
    // Two index buffers take turns: the picture before this one is still in the other, so the
    // Falcon row cache can compare against it without keeping a separate copy.
    renderer->set_prev_frame(pixels_alt);
    renderer->draw_frame(pixels);
    { uint16_t* t = pixels; pixels = pixels_alt; pixels_alt = t; }
#else
    renderer->draw_frame(pixels);
#endif
    renderer->finalize_frame();
}

bool Video::supports_window()
{
    return renderer->supports_window();
}

bool Video::supports_vsync()
{
    return renderer->supports_vsync();
}

// ---------------------------------------------------------------------------
// Text Handling Code
// ---------------------------------------------------------------------------

void Video::clear_text_ram()
{
    for (uint32_t i = 0; i <= 0xFFF; i++)
        tile_layer->text_ram[i] = 0;
}

void Video::write_text8(uint32_t addr, const uint8_t data)
{
    tile_layer->text_ram[addr & 0xFFF] = data;
}

void Video::write_text16(uint32_t* addr, const uint16_t data)
{
    tile_layer->text_ram[*addr & 0xFFF] = (data >> 8) & 0xFF;
    tile_layer->text_ram[(*addr+1) & 0xFFF] = data & 0xFF;

    *addr += 2;
}

void Video::write_text16(uint32_t addr, const uint16_t data)
{
    tile_layer->text_ram[addr & 0xFFF] = (data >> 8) & 0xFF;
    tile_layer->text_ram[(addr+1) & 0xFFF] = data & 0xFF;
}

void Video::write_text32(uint32_t* addr, const uint32_t data)
{
    tile_layer->text_ram[*addr & 0xFFF] = (data >> 24) & 0xFF;
    tile_layer->text_ram[(*addr+1) & 0xFFF] = (data >> 16) & 0xFF;
    tile_layer->text_ram[(*addr+2) & 0xFFF] = (data >> 8) & 0xFF;
    tile_layer->text_ram[(*addr+3) & 0xFFF] = data & 0xFF;

    *addr += 4;
}

void Video::write_text32(uint32_t addr, const uint32_t data)
{
    tile_layer->text_ram[addr & 0xFFF] = (data >> 24) & 0xFF;
    tile_layer->text_ram[(addr+1) & 0xFFF] = (data >> 16) & 0xFF;
    tile_layer->text_ram[(addr+2) & 0xFFF] = (data >> 8) & 0xFF;
    tile_layer->text_ram[(addr+3) & 0xFFF] = data & 0xFF;
}

uint8_t Video::read_text8(uint32_t addr)
{
    return tile_layer->text_ram[addr & 0xFFF];
}

// ---------------------------------------------------------------------------
// Tile Handling Code
// ---------------------------------------------------------------------------

void Video::clear_tile_ram()
{
    for (uint32_t i = 0; i <= 0xFFFF; i++)
        tile_layer->tile_ram[i] = 0;
}

void Video::write_tile8(uint32_t addr, const uint8_t data)
{
    tile_layer->tile_ram[addr & 0xFFFF] = data;
} 

void Video::write_tile16(uint32_t* addr, const uint16_t data)
{
    tile_layer->tile_ram[*addr & 0xFFFF] = (data >> 8) & 0xFF;
    tile_layer->tile_ram[(*addr+1) & 0xFFFF] = data & 0xFF;

    *addr += 2;
}

void Video::write_tile16(uint32_t addr, const uint16_t data)
{
    tile_layer->tile_ram[addr & 0xFFFF] = (data >> 8) & 0xFF;
    tile_layer->tile_ram[(addr+1) & 0xFFFF] = data & 0xFF;
}   

void Video::write_tile32(uint32_t* addr, const uint32_t data)
{
    tile_layer->tile_ram[*addr & 0xFFFF] = (data >> 24) & 0xFF;
    tile_layer->tile_ram[(*addr+1) & 0xFFFF] = (data >> 16) & 0xFF;
    tile_layer->tile_ram[(*addr+2) & 0xFFFF] = (data >> 8) & 0xFF;
    tile_layer->tile_ram[(*addr+3) & 0xFFFF] = data & 0xFF;

    *addr += 4;
}

void Video::write_tile32(uint32_t addr, const uint32_t data)
{
    tile_layer->tile_ram[addr & 0xFFFF] = (data >> 24) & 0xFF;
    tile_layer->tile_ram[(addr+1) & 0xFFFF] = (data >> 16) & 0xFF;
    tile_layer->tile_ram[(addr+2) & 0xFFFF] = (data >> 8) & 0xFF;
    tile_layer->tile_ram[(addr+3) & 0xFFFF] = data & 0xFF;
}

uint8_t Video::read_tile8(uint32_t addr)
{
    return tile_layer->tile_ram[addr & 0xFFFF];
}


// ---------------------------------------------------------------------------
// Sprite Handling Code
// ---------------------------------------------------------------------------

void Video::write_sprite16(uint32_t* addr, const uint16_t data)
{
    sprite_layer->write(*addr & 0xfff, data);
    *addr += 2;
}

// ---------------------------------------------------------------------------
// Palette Handling Code
// ---------------------------------------------------------------------------

void Video::write_pal8(uint32_t* palAddr, const uint8_t data)
{
    palette[*palAddr & 0x1fff] = data;
    refresh_palette(*palAddr & 0x1fff);
    *palAddr += 1;
}

void Video::write_pal16(uint32_t* palAddr, const uint16_t data)
{    
    uint32_t adr = *palAddr & 0x1fff;
    palette[adr]   = (data >> 8) & 0xFF;
    palette[adr+1] = data & 0xFF;
    refresh_palette(adr);
    *palAddr += 2;
}

void Video::write_pal32(uint32_t* palAddr, const uint32_t data)
{    
    uint32_t adr = *palAddr & 0x1fff;

    palette[adr]   = (data >> 24) & 0xFF;
    palette[adr+1] = (data >> 16) & 0xFF;
    palette[adr+2] = (data >> 8) & 0xFF;
    palette[adr+3] = data & 0xFF;

    refresh_palette(adr);
    refresh_palette(adr+2);

    *palAddr += 4;
}

void Video::write_pal32(uint32_t adr, const uint32_t data)
{    
    adr &= 0x1fff;

    palette[adr]   = (data >> 24) & 0xFF;
    palette[adr+1] = (data >> 16) & 0xFF;
    palette[adr+2] = (data >> 8) & 0xFF;
    palette[adr+3] = data & 0xFF;
    refresh_palette(adr);
    refresh_palette(adr+2);
}

uint8_t Video::read_pal8(uint32_t palAddr)
{
    return palette[palAddr & 0x1fff];
}

uint16_t Video::read_pal16(uint32_t palAddr)
{
    uint32_t adr = palAddr & 0x1fff;
    return (palette[adr] << 8) | palette[adr+1];
}

uint16_t Video::read_pal16(uint32_t* palAddr)
{
    uint32_t adr = *palAddr & 0x1fff;
    *palAddr += 2;
    return (palette[adr] << 8)| palette[adr+1];
}

uint32_t Video::read_pal32(uint32_t* palAddr)
{
    uint32_t adr = *palAddr & 0x1fff;
    *palAddr += 4;
    return (palette[adr] << 24) | (palette[adr+1] << 16) | (palette[adr+2] << 8) | palette[adr+3];
}

// Convert internal System 16 RRRR GGGG BBBB format palette to renderer output format
void Video::refresh_palette(uint32_t palAddr)
{
    palAddr &= ~1;
    uint32_t a = (palette[palAddr] << 8) | palette[palAddr + 1];
    uint32_t r = (a & 0x000f) << 1; // r rrr0
    uint32_t g = (a & 0x00f0) >> 3; // g ggg0
    uint32_t b = (a & 0x0f00) >> 7; // b bbb0
    if ((a & 0x1000) != 0)
        r |= 1; // r rrrr
    if ((a & 0x2000) != 0)
        g |= 1; // g gggg
    if ((a & 0x4000) != 0)
        b |= 1; // b bbbb

    renderer->convert_palette(palAddr, r, g, b);
}
