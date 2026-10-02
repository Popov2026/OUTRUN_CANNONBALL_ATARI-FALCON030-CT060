#include <cstring> // memcpy
#include <cstdio>
#include "globals.hpp"
#include "romloader.hpp"
#include "hwvideo/hwtiles.hpp"
#include "frontend/config.hpp"
#include <cstring>

/***************************************************************************
    Video Emulation: OutRun Tilemap Hardware.
    Based on MAME source code.

    Copyright Aaron Giles.
    All rights reserved.
***************************************************************************/

/*******************************************************************************************
 *
 *  System 16B-style tilemaps
 *
 *  16 total pages
 *  Column/rowscroll enabled via bits in text layer
 *  Alternate tilemap support
 *
 *  Tile format:
 *      Bits               Usage
 *      p------- --------  Tile priority versus sprites
 *      -??----- --------  Unknown
 *      ---ccccc cc------  Tile color palette
 *      ---nnnnn nnnnnnnn  Tile index
 *
 *  Text format:
 *      Bits               Usage
 *      p------- --------  Tile priority versus sprites
 *      -???---- --------  Unknown
 *      ----ccc- --------  Tile color palette
 *      -------n nnnnnnnn  Tile index
 *
 *  Alternate tile format:
 *      Bits               Usage
 *      p------- --------  Tile priority versus sprites
 *      -??----- --------  Unknown
 *      ----cccc ccc-----  Tile color palette
 *      ---nnnnn nnnnnnnn  Tile index
 *
 *  Alternate text format:
 *      Bits               Usage
 *      p------- --------  Tile priority versus sprites
 *      -???---- --------  Unknown
 *      -----ccc --------  Tile color palette
 *      -------- nnnnnnnn  Tile index
 *
 *  Text RAM:
 *      Offset   Bits               Usage
 *      E80      aaaabbbb ccccdddd  Foreground tilemap page select
 *      E82      aaaabbbb ccccdddd  Background tilemap page select
 *      E84      aaaabbbb ccccdddd  Alternate foreground tilemap page select
 *      E86      aaaabbbb ccccdddd  Alternate background tilemap page select
 *      E90      c------- --------  Foreground tilemap column scroll enable
 *               -------v vvvvvvvv  Foreground tilemap vertical scroll
 *      E92      c------- --------  Background tilemap column scroll enable
 *               -------v vvvvvvvv  Background tilemap vertical scroll
 *      E94      -------v vvvvvvvv  Alternate foreground tilemap vertical scroll
 *      E96      -------v vvvvvvvv  Alternate background tilemap vertical scroll
 *      E98      r------- --------  Foreground tilemap row scroll enable
 *               ------hh hhhhhhhh  Foreground tilemap horizontal scroll
 *      E9A      r------- --------  Background tilemap row scroll enable
 *               ------hh hhhhhhhh  Background tilemap horizontal scroll
 *      E9C      ------hh hhhhhhhh  Alternate foreground tilemap horizontal scroll
 *      E9E      ------hh hhhhhhhh  Alternate background tilemap horizontal scroll
 *      F16-F3F  -------- vvvvvvvv  Foreground tilemap per-16-pixel-column vertical scroll
 *      F56-F7F  -------- vvvvvvvv  Background tilemap per-16-pixel-column vertical scroll
 *      F80-FB7  a------- --------  Foreground tilemap per-8-pixel-row alternate tilemap enable
 *               -------h hhhhhhhh  Foreground tilemap per-8-pixel-row horizontal scroll
 *      FC0-FF7  a------- --------  Background tilemap per-8-pixel-row alternate tilemap enable
 *               -------h hhhhhhhh  Background tilemap per-8-pixel-row horizontal scroll
 *
 *******************************************************************************************/

hwtiles::hwtiles(void)
{
    for (int i = 0; i < 2; i++)
        tile_banks[i] = i;

    set_x_clamp(CENTRE);
}

hwtiles::~hwtiles(void)
{

}

// Convert S16 tiles to a more useable format
void hwtiles::init(uint8_t* src_tiles, const bool hires)
{
    if (src_tiles)
    {
        for (int i = 0; i < TILES_LENGTH; i++)
        {
            uint8_t p0 = src_tiles[i];
            uint8_t p1 = src_tiles[i + 0x10000];
            uint8_t p2 = src_tiles[i + 0x20000];

            uint32_t val = 0;

            for (int ii = 0; ii < 8; ii++) 
            {
                uint8_t bit = 7 - ii;
                uint8_t pix = ((((p0 >> bit)) & 1) | (((p1 >> bit) << 1) & 2) | (((p2 >> bit) << 2) & 4));
                val = (val << 4) | pix;
            }
            tiles[i] = val; // Store converted value
        }
        memcpy(tiles_backup, tiles, TILES_LENGTH * sizeof(uint32_t));
    }
    
#ifdef LOWRES
    s16_width_noscale = config.s16_width << 1;
    render8x8_tile_mask      = &hwtiles::render8x8_tile_mask_half;
    render8x8_tile_mask_clip = &hwtiles::render8x8_tile_mask_clip_half;
    build_half_tiles();
    return;
#endif
    if (hires)
    {
        s16_width_noscale = config.s16_width >> 1;
        render8x8_tile_mask      = &hwtiles::render8x8_tile_mask_hires;
        render8x8_tile_mask_clip = &hwtiles::render8x8_tile_mask_clip_hires;
    }
    else
    {
        s16_width_noscale = config.s16_width;
        render8x8_tile_mask      = &hwtiles::render8x8_tile_mask_lores;
        render8x8_tile_mask_clip = &hwtiles::render8x8_tile_mask_clip_lores;
    }
}

// Patch Tileset with new data
void hwtiles::patch_tiles(RomLoader* patch)
{
    memcpy(tiles_backup, tiles, TILES_LENGTH * sizeof(uint32_t));

    for (uint32_t i = 0; i < patch->length;)
    {
        uint32_t tile_index = patch->read16(&i) << 3;
        tiles[tile_index++] = patch->read32(&i);
        tiles[tile_index++] = patch->read32(&i);
        tiles[tile_index++] = patch->read32(&i);
        tiles[tile_index++] = patch->read32(&i);
        tiles[tile_index++] = patch->read32(&i);
        tiles[tile_index++] = patch->read32(&i);
        tiles[tile_index++] = patch->read32(&i);
        tiles[tile_index++] = patch->read32(&i);
    }
#ifdef LOWRES
    build_half_tiles();
#endif
}

void hwtiles::restore_tiles()
{
    memcpy(tiles, tiles_backup, TILES_LENGTH * sizeof(uint32_t));
#ifdef LOWRES
    build_half_tiles();
#endif
}

// Set Tilemap X Clamp
//
// This is used for the widescreen mode, in order to clamp the tilemap to
// a location of the screen. 
//
// In-Game we must clamp right to avoid page scrolling issues.
//
// The clamp will always be 192 for the non-widescreen mode.
void hwtiles::set_x_clamp(const uint16_t props)
{
    if (props == LEFT)
    {
        x_clamp = 192;
    }
    else if (props == RIGHT)
    {
        x_clamp = (512 - s16_width_noscale);
    }
    else if (props == CENTRE)
    {
        x_clamp = 192 - config.s16_x_off;
    }
}

void hwtiles::update_tile_values()
{
    for (int i = 0; i < 4; i++)
    {
        page[i] = ((text_ram[0xe80 + (i * 2) + 0] << 8) | text_ram[0xe80 + (i * 2) + 1]);

        scroll_x[i] = ((text_ram[0xe98 + (i * 2) + 0] << 8) | text_ram[0xe98 + (i * 2) + 1]);
        scroll_y[i] = ((text_ram[0xe90 + (i * 2) + 0] << 8) | text_ram[0xe90 + (i * 2) + 1]);
    }
}

// A quick and dirty debug function to display the contents of tile memory.
void hwtiles::render_all_tiles(uint16_t* buf)
{
    uint32_t Code = 0, Colour = 5, x, y;
    for (y = 0; y < 224; y += 8) 
    {
        for (x = 0; x < 320; x += 8) 
        {
            (this->*render8x8_tile_mask)(buf, Code, x, y, Colour, 3, 0, TILEMAP_COLOUR_OFFSET);
            Code++;
        }
    }
}

#ifdef PLATFORM_FALCON
// ------------------------------------------------------------------------------------------------
// Faster tile / text layer walk for the Falcon build: visible columns and rows are worked out once,
// map words are read directly as 16 bits (the 68k is big endian, matching the map layout) and full
// 8x8 tiles are drawn by atari_tile8 (atari/tile_asm.S).  Same visibility limits and same output as
// the generic versions below.
// ------------------------------------------------------------------------------------------------
extern "C" void atari_tile8(uint16_t* buf, const uint32_t* tile, uint32_t palette, uint32_t stride_bytes);
#include "atari/options.hpp"   // g_row_mask8: which rows of a tile are drawn (vscale option)
extern "C" uint32_t atari_tile_decode(uint32_t mx, int32_t base_l, int32_t base_r, const uint8_t* tile_ram,
                                       uint32_t priority_draw, const uint8_t* tile_banks, uint32_t tiles_mask);
extern "C" void atari_tile8_mask(uint16_t* buf, const uint32_t* tile, uint32_t palette, uint32_t stride_bytes, uint32_t rows);
extern "C" void atari_tile8_clip(uint16_t* buf, const uint32_t* tile, uint32_t palette, uint32_t stride_bytes,
                                 int32_t x, int32_t y, int32_t width, int32_t height);
typedef uint16_t __attribute__((may_alias)) u16_alias;

void hwtiles::render_tile_layer(uint16_t* buf, uint8_t page_index, uint8_t priority_draw)
{
    g_clip_rows = g_tile_draw;   // edge tiles: the rows drawn under the road
    uint16_t EffPage = page[page_index];
    uint16_t xScroll = scroll_x[page_index];
    uint16_t yScroll = scroll_y[page_index];

    if ((xScroll & 0x8000) != 0)
        xScroll = (text_ram[0xf80 + (0x40 * page_index) + 0] << 8) | text_ram[0xf80 + (0x40 * page_index) + 1];
    if ((yScroll & 0x8000) != 0)
        yScroll = (text_ram[0xf16 + (0x40 * page_index) + 0] << 8) | text_ram[0xf16 + (0x40 * page_index) + 1];

    uint8_t vis_mx[128];
    int16_t vis_x[128];
    int nvis = 0;
    for (int mx = 0; mx < 128; mx++)
    {
        int16_t xx = 8 * mx;
        xx -= (x_clamp - xScroll) & 0x3ff;
        if (xx < -x_clamp)
            xx += 1024;
        if (xx > -8 && xx < s16_width_noscale)
        {
            vis_mx[nvis] = (uint8_t)mx;
            vis_x[nvis]  = xx;
            nvis++;
        }
    }

    const int W = config.s16_width;

    for (int my = 0; my < 64; my++)
    {
        int16_t y = 8 * my;
        y -= yScroll & 0x1ff;
        if (y < -288)
            y += 512;
        if (!(y > -8 && y < S16_HEIGHT))
            continue;
        if (y >= 0 && y < S16_HEIGHT && g_tile_mask8[y] == 0)
            continue;              // every row of this tile line is covered by the road or not drawn

        uint16_t* row = buf + y * W;
        uint32_t rowofs = (128 * my) & 0xfff;
        uint32_t base_l = 4096 * ((EffPage >> (my < 32 ? 0 : 8)) & 0x0f) + rowofs;
        uint32_t base_r = 4096 * ((EffPage >> (my < 32 ? 4 : 12)) & 0x0f) + rowofs;

        for (int k = 0; k < nvis; k++)
        {
#ifdef TILEDEC_OLD
            // Test aid (-DTILEDEC_OLD): the original per-column lookup, to compare frame output against.
            int mx = vis_mx[k];
            uint32_t idx = (mx < 64 ? base_l : base_r) + ((2 * mx) & 0x7f);
            uint16_t Data = *(const u16_alias*)(tile_ram + idx);
            if ((uint8_t)(Data >> 15) != priority_draw)
                continue;
            uint32_t Code = Data & 0x1fff;
            Code = ((uint32_t)tile_banks[Code >> 12] << 12) | (Code & 0xfff);
            Code &= (NUM_TILES - 1);
            if (Code == 0)
                continue;
            int16_t Colour = (Data >> 6) & 0x7f;
#else
            uint32_t r = atari_tile_decode((uint32_t)vis_mx[k], (int32_t)base_l, (int32_t)base_r,
                                            tile_ram, priority_draw, tile_banks, NUM_TILES - 1);
#ifdef TILEDECCHECK
            {
                // Test aid: compare the assembler decode against the original C++ formula on live data.
                static int n = 0, bad = 0;
                int mxc = vis_mx[k];
                uint32_t idxc = (mxc < 64 ? base_l : base_r) + ((2 * mxc) & 0x7f);
                uint16_t Datac = *(const u16_alias*)(tile_ram + idxc);
                uint32_t ref;
                if ((uint8_t)(Datac >> 15) != priority_draw)
                    ref = 0;
                else
                {
                    uint32_t Codec = Datac & 0x1fff;
                    Codec = ((uint32_t)tile_banks[Codec >> 12] << 12) | (Codec & 0xfff);
                    Codec &= (NUM_TILES - 1);
                    if (Codec == 0)
                        ref = 0;
                    else
                        ref = ((uint32_t)((Datac >> 6) & 0x7f) << 16) | Codec;
                }
                if (ref != r) bad++;
                if (++n % 20000 == 0) printf("TILEDEC n=%d bad=%d%c%c", n, bad, 13, 10);
            }
#endif
            if (r == 0)
                continue;
            uint32_t Code = r & 0xffff;
            int16_t Colour = (int16_t)(r >> 16);
#endif
            int16_t x = vis_x[k];

#ifndef LOWRES
            if (x > 7 && x < (s16_width_noscale - 8) && y > 7 && y <= (S16_HEIGHT - 8))
            {
                const uint32_t m = g_tile_mask8[y];
                if (m == 0xFF)
                    atari_tile8(row + x, tiles + (Code << 3), (uint32_t)Colour << 3, W * 2);
                else if (m)
                    atari_tile8_mask(row + x, tiles + (Code << 3), (uint32_t)Colour << 3, W * 2, m);
            }
            else
                atari_tile8_clip(buf, tiles + (Code << 3), (uint32_t)Colour << 3, W * 2, x, y, W, S16_HEIGHT);
#else
            if (x > 7 && x < (s16_width_noscale - 8) && y > 7 && y <= (S16_HEIGHT - 8))
                (this->*render8x8_tile_mask)(buf, Code, x, y, Colour, 3, 0, TILEMAP_COLOUR_OFFSET);
            else
                (this->*render8x8_tile_mask_clip)(buf, Code, x, y, Colour, 3, 0, TILEMAP_COLOUR_OFFSET);
#endif
        }
    }
}

#ifdef PERF_PRINT
uint32_t g_txt_fast = 0, g_txt_clip = 0;   // text tiles drawn by the fast / clipping routine (statistics)
#endif
void hwtiles::render_text_layer(uint16_t* buf, uint8_t priority_draw)
{
    const int W = config.s16_width;
    g_clip_rows = g_row_draw;   // text is drawn over the road: every drawn row

    // Columns 24..63 are the ones that land inside 0..319 (x = 8*mx - 192); rows above 27 are below the screen.
    for (int my = 0; my < 28; my++)
    {
        int y = 8 * my;
        uint32_t ti = (uint32_t)my * 128 + 2 * 24;

        for (int mx = 24; mx < 64; mx++, ti += 2)
        {
            uint16_t Code = *(const u16_alias*)(text_ram + ti);
            if ((uint8_t)(Code >> 15) != priority_draw)
                continue;

            uint16_t Colour = (Code >> 9) & 0x07;
            Code &= 0x1ff;
            Code += tile_banks[0] * 0x1000;
            Code &= (NUM_TILES - 1);
            if (Code == 0)
                continue;

            int x = 8 * mx - 192;

#ifndef LOWRES
            if (x > 7 && x < (s16_width_noscale - 8) && y > 7 && y <= (S16_HEIGHT - 8))
            {
#ifdef PERF_PRINT
                g_txt_fast++;
#endif
                {
                    const uint32_t m = g_row_mask8[y];
                    if (m == 0xFF)
                        atari_tile8(buf + y * W + x + config.s16_x_off, tiles + (Code << 3), (uint32_t)Colour << 3, W * 2);
                    else if (m)
                        atari_tile8_mask(buf + y * W + x + config.s16_x_off, tiles + (Code << 3), (uint32_t)Colour << 3, W * 2, m);
                }
            }
            else
            {
#ifdef PERF_PRINT
                g_txt_clip++;
#endif
                atari_tile8_clip(buf, tiles + (Code << 3), (uint32_t)Colour << 3, W * 2, x + config.s16_x_off, y, W, S16_HEIGHT);
            }
#else
            if (x > 7 && x < (s16_width_noscale - 8) && y > 7 && y <= (S16_HEIGHT - 8))
                (this->*render8x8_tile_mask)(buf, Code, x + config.s16_x_off, y, Colour, 3, 0, TILEMAP_COLOUR_OFFSET);
            else
                (this->*render8x8_tile_mask_clip)(buf, Code, x + config.s16_x_off, y, Colour, 3, 0, TILEMAP_COLOUR_OFFSET);
#endif
        }
    }
}

#else // generic versions
void hwtiles::render_tile_layer(uint16_t* buf, uint8_t page_index, uint8_t priority_draw)
{
    int16_t Colour, x, y, Priority = 0;

    uint16_t ActPage = 0;
    uint16_t EffPage = page[page_index];
    uint16_t xScroll = scroll_x[page_index];
    uint16_t yScroll = scroll_y[page_index];

    // Need to support this at each row/column
    if ((xScroll & 0x8000) != 0)
        xScroll = (text_ram[0xf80 + (0x40 * page_index) + 0] << 8) | text_ram[0xf80 + (0x40 * page_index) + 1];
    if ((yScroll & 0x8000) != 0)
        yScroll = (text_ram[0xf16 + (0x40 * page_index) + 0] << 8) | text_ram[0xf16 + (0x40 * page_index) + 1];

    // Screen position of every column / row depends only on that column /
    // row, so work it out once and skip the (majority of) cells that are off
    // screen instead of testing all 128x64 cells individually. Same
    // arithmetic and same visibility limits as the per-cell version.
    int16_t col_x[128];
    bool    col_vis[128];
    int16_t row_y[64];
    bool    row_vis[64];

    for (int mx = 0; mx < 128; mx++)
    {
        int16_t xx = 8 * mx;
        xx -= (x_clamp - xScroll) & 0x3ff;
        if (xx < -x_clamp)
            xx += 1024;
        col_x[mx]   = xx;
        col_vis[mx] = (xx > -8 && xx < s16_width_noscale);
    }
    for (int my = 0; my < 64; my++)
    {
        int16_t yy = 8 * my;
        yy -= yScroll & 0x1ff;
        if (yy < -288)
            yy += 512;
        row_y[my]   = yy;
        row_vis[my] = (yy > -8 && yy < S16_HEIGHT);
    }

    for (int my = 0; my < 64; my++) 
    {
        if (!row_vis[my]) continue;
        y = row_y[my];

        for (int mx = 0; mx < 128; mx++) 
        {
            if (!col_vis[mx]) continue;

            if (my < 32 && mx < 64)                    // top left
                ActPage = (EffPage >> 0) & 0x0f;
            if (my < 32 && mx >= 64)                   // top right
                ActPage = (EffPage >> 4) & 0x0f;
            if (my >= 32 && mx < 64)                   // bottom left
                ActPage = (EffPage >> 8) & 0x0f;
            if (my >= 32 && mx >= 64)                  // bottom right page
                ActPage = (EffPage >> 12) & 0x0f;

            uint32_t TileIndex = 64 * 32 * 2 * ActPage + ((2 * 64 * my) & 0xfff) + ((2 * mx) & 0x7f);

            uint16_t Data = (tile_ram[TileIndex + 0] << 8) | tile_ram[TileIndex + 1];

            Priority = (Data >> 15) & 1;

            if (Priority == priority_draw) 
            {
                uint32_t Code = Data & 0x1fff;
                Code = tile_banks[Code / 0x1000] * 0x1000 + Code % 0x1000;
                Code &= (NUM_TILES - 1);

                if (Code == 0) continue;

                Colour = (Data >> 6) & 0x7f;

                x = col_x[mx];

                uint16_t ColourOff = TILEMAP_COLOUR_OFFSET;
                if (Colour >= 0x20)
					ColourOff = 0x100 | TILEMAP_COLOUR_OFFSET;
                if (Colour >= 0x40)
					ColourOff = 0x200 | TILEMAP_COLOUR_OFFSET;
                if (Colour >= 0x60)
					ColourOff = 0x300 | TILEMAP_COLOUR_OFFSET;

                if (x > 7 && x < (s16_width_noscale - 8) && y > 7 && y <= (S16_HEIGHT - 8))
                    (this->*render8x8_tile_mask)(buf, Code, x, y, Colour, 3, 0, ColourOff);
                else
                    (this->*render8x8_tile_mask_clip)(buf, Code, x, y, Colour, 3, 0, ColourOff);
            } // end priority check
        }
    } // end for loop
}

void hwtiles::render_text_layer(uint16_t* buf, uint8_t priority_draw)
{
    uint16_t mx, my, Code, Colour, x, y, Priority, TileIndex = 0;

    for (my = 0; my < 32; my++) 
    {
        for (mx = 0; mx < 64; mx++) 
        {
            Code = (text_ram[TileIndex + 0] << 8) | text_ram[TileIndex + 1];
            Priority = (Code >> 15) & 1;

            if (Priority == priority_draw) 
            {
                Colour = (Code >> 9) & 0x07;
                Code &= 0x1ff;
                Code += tile_banks[0] * 0x1000;
                Code &= (NUM_TILES - 1);

                if (Code != 0) 
                {
                    x = 8 * mx;
                    y = 8 * my;

                    x -= 192;

                    // We also adjust the text layer for wide-screen below. But don't allow painting in the 
                    // wide-screen areas to avoid graphical glitches.
                    if (x > 7 && x < (s16_width_noscale - 8) && y > 7 && y <= (S16_HEIGHT - 8))
                        (this->*render8x8_tile_mask)(buf, Code, x + config.s16_x_off, y, Colour, 3, 0, TILEMAP_COLOUR_OFFSET);
                    else if (x > -8 && x < s16_width_noscale && y >= 0 && y < S16_HEIGHT) 
                        (this->*render8x8_tile_mask_clip)(buf, Code, x + config.s16_x_off, y, Colour, 3, 0, TILEMAP_COLOUR_OFFSET);
                }
            }
            TileIndex += 2;
        }
    }
}
#endif // PLATFORM_FALCON

void hwtiles::render8x8_tile_mask_lores(
    uint16_t *buf,
    uint16_t nTileNumber, 
    uint16_t StartX, 
    uint16_t StartY, 
    uint16_t nTilePalette, 
    uint16_t nColourDepth, 
    uint16_t nMaskColour, 
    uint16_t nPaletteOffset) 
{
    uint32_t nPalette = (nTilePalette << nColourDepth) | nMaskColour;
    uint32_t* pTileData = tiles + (nTileNumber << 3);
    buf += (StartY * config.s16_width) + StartX;

    for (int y = 0; y < 8; y++) 
    {
        uint32_t p0 = *pTileData;

        if (p0 != nMaskColour) 
        {
            uint32_t c7 = p0 & 0xf;
            uint32_t c6 = (p0 >> 4) & 0xf;
            uint32_t c5 = (p0 >> 8) & 0xf;
            uint32_t c4 = (p0 >> 12) & 0xf;
            uint32_t c3 = (p0 >> 16) & 0xf;
            uint32_t c2 = (p0 >> 20) & 0xf;
            uint32_t c1 = (p0 >> 24) & 0xf;
            uint32_t c0 = (p0 >> 28);

            if (c0) buf[0] = nPalette + c0;
            if (c1) buf[1] = nPalette + c1;
            if (c2) buf[2] = nPalette + c2;
            if (c3) buf[3] = nPalette + c3;
            if (c4) buf[4] = nPalette + c4;
            if (c5) buf[5] = nPalette + c5;
            if (c6) buf[6] = nPalette + c6;
            if (c7) buf[7] = nPalette + c7;
        }
        buf += config.s16_width;
        pTileData++;
    }
}

void hwtiles::render8x8_tile_mask_clip_lores(
    uint16_t *buf,
    uint16_t nTileNumber, 
    int16_t StartX, 
    int16_t StartY, 
    uint16_t nTilePalette, 
    uint16_t nColourDepth, 
    uint16_t nMaskColour, 
    uint16_t nPaletteOffset) 
{
    uint32_t nPalette = (nTilePalette << nColourDepth) | nMaskColour;
    uint32_t* pTileData = tiles + (nTileNumber << 3);
    buf += (StartY * config.s16_width) + StartX;

    for (int y = 0; y < 8; y++) 
    {
        if ((StartY + y) >= 0 && (StartY + y) < S16_HEIGHT) 
        {
            uint32_t p0 = *pTileData;

            if (p0 != nMaskColour) 
            {
                uint32_t c7 = p0 & 0xf;
                uint32_t c6 = (p0 >> 4) & 0xf;
                uint32_t c5 = (p0 >> 8) & 0xf;
                uint32_t c4 = (p0 >> 12) & 0xf;
                uint32_t c3 = (p0 >> 16) & 0xf;
                uint32_t c2 = (p0 >> 20) & 0xf;
                uint32_t c1 = (p0 >> 24) & 0xf;
                uint32_t c0 = (p0 >> 28);

                if (c0 && 0 + StartX >= 0 && 0 + StartX < config.s16_width) buf[0] = nPalette + c0;
                if (c1 && 1 + StartX >= 0 && 1 + StartX < config.s16_width) buf[1] = nPalette + c1;
                if (c2 && 2 + StartX >= 0 && 2 + StartX < config.s16_width) buf[2] = nPalette + c2;
                if (c3 && 3 + StartX >= 0 && 3 + StartX < config.s16_width) buf[3] = nPalette + c3;
                if (c4 && 4 + StartX >= 0 && 4 + StartX < config.s16_width) buf[4] = nPalette + c4;
                if (c5 && 5 + StartX >= 0 && 5 + StartX < config.s16_width) buf[5] = nPalette + c5;
                if (c6 && 6 + StartX >= 0 && 6 + StartX < config.s16_width) buf[6] = nPalette + c6;
                if (c7 && 7 + StartX >= 0 && 7 + StartX < config.s16_width) buf[7] = nPalette + c7;
            }
        }
        buf += config.s16_width;
        pTileData++;
    }
}

// ------------------------------------------------------------------------------------------------
// Additional routines for Hi-Res Mode.
// Note that the tilemaps are displayed at the same resolution, we just want everything to be
// proportional.
// ------------------------------------------------------------------------------------------------
void hwtiles::render8x8_tile_mask_hires(
    uint16_t *buf,
    uint16_t nTileNumber, 
    uint16_t StartX, 
    uint16_t StartY, 
    uint16_t nTilePalette, 
    uint16_t nColourDepth, 
    uint16_t nMaskColour, 
    uint16_t nPaletteOffset) 
{
    uint32_t nPalette = (nTilePalette << nColourDepth) | nMaskColour;
    uint32_t* pTileData = tiles + (nTileNumber << 3);
    buf += ((StartY << 1) * config.s16_width) + (StartX << 1);

    for (int y = 0; y < 8; y++) 
    {
        uint32_t p0 = *pTileData;

        if (p0 != nMaskColour) 
        {
            uint32_t c7 = p0 & 0xf;
            uint32_t c6 = (p0 >> 4) & 0xf;
            uint32_t c5 = (p0 >> 8) & 0xf;
            uint32_t c4 = (p0 >> 12) & 0xf;
            uint32_t c3 = (p0 >> 16) & 0xf;
            uint32_t c2 = (p0 >> 20) & 0xf;
            uint32_t c1 = (p0 >> 24) & 0xf;
            uint32_t c0 = (p0 >> 28);

            if (c0) set_pixel_x4(&buf[0],  nPalette + c0);
            if (c1) set_pixel_x4(&buf[2],  nPalette + c1);
            if (c2) set_pixel_x4(&buf[4],  nPalette + c2);
            if (c3) set_pixel_x4(&buf[6],  nPalette + c3);
            if (c4) set_pixel_x4(&buf[8],  nPalette + c4);
            if (c5) set_pixel_x4(&buf[10], nPalette + c5);
            if (c6) set_pixel_x4(&buf[12], nPalette + c6);
            if (c7) set_pixel_x4(&buf[14], nPalette + c7);
        }
        buf += (config.s16_width << 1);
        pTileData++;
    }
}

void hwtiles::render8x8_tile_mask_clip_hires(
    uint16_t *buf,
    uint16_t nTileNumber, 
    int16_t StartX, 
    int16_t StartY, 
    uint16_t nTilePalette, 
    uint16_t nColourDepth, 
    uint16_t nMaskColour, 
    uint16_t nPaletteOffset) 
{
    uint32_t nPalette = (nTilePalette << nColourDepth) | nMaskColour;
    uint32_t* pTileData = tiles + (nTileNumber << 3);
    buf += ((StartY << 1) * config.s16_width) + (StartX << 1);

    for (int y = 0; y < 8; y++) 
    {
        if ((StartY + y) >= 0 && (StartY + y) < S16_HEIGHT) 
        {
            uint32_t p0 = *pTileData;

            if (p0 != nMaskColour) 
            {
                uint32_t c7 = p0 & 0xf;
                uint32_t c6 = (p0 >> 4) & 0xf;
                uint32_t c5 = (p0 >> 8) & 0xf;
                uint32_t c4 = (p0 >> 12) & 0xf;
                uint32_t c3 = (p0 >> 16) & 0xf;
                uint32_t c2 = (p0 >> 20) & 0xf;
                uint32_t c1 = (p0 >> 24) & 0xf;
                uint32_t c0 = (p0 >> 28);

                if (c0 && 0 + StartX >= 0 && 0 + StartX < s16_width_noscale) set_pixel_x4(&buf[0],  nPalette + c0);
                if (c1 && 1 + StartX >= 0 && 1 + StartX < s16_width_noscale) set_pixel_x4(&buf[2],  nPalette + c1);
                if (c2 && 2 + StartX >= 0 && 2 + StartX < s16_width_noscale) set_pixel_x4(&buf[4],  nPalette + c2);
                if (c3 && 3 + StartX >= 0 && 3 + StartX < s16_width_noscale) set_pixel_x4(&buf[6],  nPalette + c3);
                if (c4 && 4 + StartX >= 0 && 4 + StartX < s16_width_noscale) set_pixel_x4(&buf[8],  nPalette + c4);
                if (c5 && 5 + StartX >= 0 && 5 + StartX < s16_width_noscale) set_pixel_x4(&buf[10], nPalette + c5);
                if (c6 && 6 + StartX >= 0 && 6 + StartX < s16_width_noscale) set_pixel_x4(&buf[12], nPalette + c6);
                if (c7 && 7 + StartX >= 0 && 7 + StartX < s16_width_noscale) set_pixel_x4(&buf[14], nPalette + c7);
            }
        }
        buf += (config.s16_width << 1);
        pTileData++;
    }
}

// Hires Mode: Set 4 pixels instead of one.
void hwtiles::set_pixel_x4(uint16_t *buf, uint32_t data)
{
    buf[0] = buf[1] = buf[0  + config.s16_width] = buf[1 + config.s16_width] = data;
}

#ifdef LOWRES
// ------------------------------------------------------------------------------------------------
// Half-resolution tile rendering (see hwtiles.hpp).
// ------------------------------------------------------------------------------------------------
void hwtiles::build_half_tiles()
{
    for (int t = 0; t < NUM_TILES; t++)
    {
        const uint32_t* src = tiles + (t << 3);
        for (int r = 0; r < 4; r++)
        {
            uint32_t top = src[2 * r], bot = src[2 * r + 1];
            uint16_t out = 0;
            for (int cx = 0; cx < 4; cx++)
            {
                int sh = 28 - 8 * cx;                 // nibble of pixel 2*cx; pixel 2*cx+1 is 4 lower
                uint32_t v = (top >> sh) & 0xf;
                if (!v) v = (top >> (sh - 4)) & 0xf;
                if (!v) v = (bot >> sh) & 0xf;
                if (!v) v = (bot >> (sh - 4)) & 0xf;
                out = (uint16_t)((out << 4) | v);
            }
            tiles_half[(t << 2) + r] = out;
        }
    }
}

void hwtiles::render8x8_tile_mask_half(
    uint16_t *buf, uint16_t nTileNumber, uint16_t StartX, uint16_t StartY,
    uint16_t nTilePalette, uint16_t nColourDepth, uint16_t nMaskColour, uint16_t nPaletteOffset)
{
    uint32_t nPalette = (nTilePalette << nColourDepth) | nMaskColour;
    const uint16_t* th = tiles_half + (nTileNumber << 2);
    buf += ((StartY >> 1) * config.s16_width) + (StartX >> 1);

    for (int r = 0; r < 4; r++)
    {
        uint16_t h = th[r];
        if (h)
        {
            uint32_t c;
            if ((c = (h >> 12)))       buf[0] = nPalette + c;
            if ((c = (h >> 8) & 0xf))  buf[1] = nPalette + c;
            if ((c = (h >> 4) & 0xf))  buf[2] = nPalette + c;
            if ((c = h & 0xf))         buf[3] = nPalette + c;
        }
        buf += config.s16_width;
    }
}

void hwtiles::render8x8_tile_mask_clip_half(
    uint16_t *buf, uint16_t nTileNumber, int16_t StartX, int16_t StartY,
    uint16_t nTilePalette, uint16_t nColourDepth, uint16_t nMaskColour, uint16_t nPaletteOffset)
{
    uint32_t nPalette = (nTilePalette << nColourDepth) | nMaskColour;
    const uint16_t* th = tiles_half + (nTileNumber << 2);
    int hx = StartX >> 1, hy = StartY >> 1;
    buf += (hy * config.s16_width) + hx;

    for (int r = 0; r < 4; r++)
    {
        if ((hy + r) >= 0 && (hy + r) < config.s16_height)
        {
            uint16_t h = th[r];
            if (h)
            {
                uint32_t c;
                if ((c = (h >> 12))      && hx + 0 >= 0 && hx + 0 < config.s16_width) buf[0] = nPalette + c;
                if ((c = (h >> 8) & 0xf) && hx + 1 >= 0 && hx + 1 < config.s16_width) buf[1] = nPalette + c;
                if ((c = (h >> 4) & 0xf) && hx + 2 >= 0 && hx + 2 < config.s16_width) buf[2] = nPalette + c;
                if ((c = h & 0xf)        && hx + 3 >= 0 && hx + 3 < config.s16_width) buf[3] = nPalette + c;
            }
        }
        buf += config.s16_width;
    }
}
#endif