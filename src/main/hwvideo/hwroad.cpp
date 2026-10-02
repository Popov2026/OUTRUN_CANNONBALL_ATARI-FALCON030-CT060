#include <cstring> // memcpy
#include "hwvideo/hwroad.hpp"
#include <cstdio>
#include "globals.hpp"
#include "frontend/config.hpp"

/***************************************************************************
    Video Emulation: OutRun Road Rendering Hardware.
    Based on MAME source code.

    Copyright Aaron Giles.
    All rights reserved.
***************************************************************************/

/*******************************************************************************************
 *
 *  Out Run/X-Board-style road chip
 *
 *  Road control register:
 *      Bits               Usage
 *      -------- -----d--  (X-board only) Direct scanline mode (1) or indirect mode (0)
 *      -------- ------pp  Road enable/priorities:
 *                            0 = road 0 only visible
 *                            1 = both roads visible, road 0 has priority
 *                            2 = both roads visible, road 1 has priority
 *                            3 = road 1 only visible
 *
 *  Road RAM:
 *      Offset   Bits               Usage
 *      000-1FF  ----s--- --------  Road 0: Solid fill (1) or ROM fill
 *               -------- -ccccccc  Road 0: Solid color (if solid fill)
 *               -------i iiiiiiii  Road 0: Index for other tables (if in indirect mode)
 *               -------r rrrrrrr-  Road 0: Road ROM line select
 *      200-3FF  ----s--- --------  Road 1: Solid fill (1) or ROM fill
 *               -------- -ccccccc  Road 1: Solid color (if solid fill)
 *               -------i iiiiiiii  Road 1: Index for other tables (if in indirect mode)
 *               -------r rrrrrrr-  Road 1: Road ROM line select
 *      400-7FF  ----hhhh hhhhhhhh  Road 0: horizontal scroll
 *      800-BFF  ----hhhh hhhhhhhh  Road 1: horizontal scroll
 *      C00-FFF  ----bbbb --------  Background color index
 *               -------- s-------  Road 1: stripe color index
 *               -------- -a------  Road 1: pixel value 2 color index
 *               -------- --b-----  Road 1: pixel value 1 color index
 *               -------- ---c----  Road 1: pixel value 0 color index
 *               -------- ----s---  Road 0: stripe color index
 *               -------- -----a--  Road 0: pixel value 2 color index
 *               -------- ------b-  Road 0: pixel value 1 color index
 *               -------- -------c  Road 0: pixel value 0 color index
 *
 *  Logic:
 *      First, the scanline is used to index into the tables at 000-1FF/200-3FF
 *          - if solid fill, the background is filled with the specified color index
 *          - otherwise, the remaining tables are used
 *
 *      If indirect mode is selected, the index is taken from the low 9 bits of the
 *          table value from 000-1FF/200-3FF
 *      If direct scanline mode is selected, the index is set equal to the scanline
 *          for road 0, or the scanline + 256 for road 1
 *
 *      The horizontal scroll value is looked up using the index in the tables at
 *          400-7FF/800-BFF
 *
 *      The color information is looked up using the index in the table at C00-FFF. Note
 *          that the same table is used for both roads.
 *
 *
 *  Out Run road priorities are controlled by a PAL that maps as indicated below.
 *  This was used to generate the priority_map. It is assumed that X-board is the
 *  same, though this logic is locked inside a Sega custom.
 *
 *  RRC0 =  CENTA & (RDA == 3) & !RRC2
 *      | CENTB & (RDB == 3) & RRC2
 *      | (RDA == 1) & !RRC2
 *      | (RDB == 1) & RRC2
 *
 *  RRC1 =  CENTA & (RDA == 3) & !RRC2
 *      | CENTB & (RDB == 3) & RRC2
 *      | (RDA == 2) & !RRC2
 *      | (RDB == 2) & RRC2
 *
 *  RRC2 = !/HSYNC & IIQ
 *      | (CTRL == 3)
 *      | !CENTA & (RDA == 3) & !CENTB & (RDB == 3) & (CTRL == 2)
 *      | CENTB & (RDB == 3) & (CTRL == 2)
 *      | !CENTA & (RDA == 3) & !M2 & (CTRL == 2)
 *      | !CENTA & (RDA == 3) & !M3 & (CTRL == 2)
 *      | !M0 & (RDB == 0) & (CTRL == 2)
 *      | !M1 & (RDB == 0) & (CTRL == 2)
 *      | !CENTA & (RDA == 3) & CENTB & (RDB == 3) & (CTRL == 1)
 *      | !M0 & CENTB & (RDB == 3) & (CTRL == 1)
 *      | !M1 & CENTB & (RDB == 3) & (CTRL == 1)
 *      | !CENTA & M0 & (RDB == 0) & (CTRL == 1)
 *      | !CENTA & M1 & (RDB == 0) & (CTRL == 1)
 *      | !CENTA & (RDA == 3) & (RDB == 1) & (CTRL == 1)
 *      | !CENTA & (RDA == 3) & (RDB == 2) & (CTRL == 1)
 *
 *  RRC3 =  VA11 & VB11
 *      | VA11 & (CTRL == 0)
 *      | (CTRL == 3) & VB11
 *
 *  RRC4 =  !CENTA & (RDA == 3) & !CENTB & (RDB == 3)
 *      | VA11 & VB11
 *      | VA11 & (CTRL == 0)
 *      | (CTRL == 3) & VB11
 *      | !CENTB & (RDB == 3) & (CTRL == 3)
 *      | !CENTA & (RDA == 3) & (CTRL == 0)
 *
 *******************************************************************************************/

HWRoad hwroad;

HWRoad::HWRoad()
{
    ram     = ramA;
    ramBuff = ramB;
}

HWRoad::~HWRoad()
{
}

// Convert road to a more useable format
void HWRoad::init(const uint8_t* src_road, const bool hires)
{
    road_control = 0;
    color_offset1 = 0x400;
    color_offset2 = 0x420;
    color_offset3 = 0x780;
    x_offset = 0;

    if (src_road)
        decode_road(src_road);
    
#ifdef LOWRES
    render_background = &HWRoad::render_background_half;
    render_foreground = &HWRoad::render_foreground_half;
    return;
#endif
    if (hires)
    {
        render_background = &HWRoad::render_background_hires;
        render_foreground = &HWRoad::render_foreground_hires;
    }
    else
    {
        render_background = &HWRoad::render_background_lores;
        render_foreground = &HWRoad::render_foreground_lores;   
    }
}

/*
    There are TWO (identical) roads we need to decode.
    Each of these roads is represented using a 512x256 map.
    See: http://www.extentofthejam.com/pseudo/
      
    512 x 256 x 2bpp map 
    0x8000 bytes of data. 
    2 Bits Per Pixel.
       
    Per Road:
    Bit 0 of each pixel is stored at offset 0x0000 - 0x3FFF
    Bit 1 of each pixel is stored at offset 0x4000 - 0x7FFF

    This means: 80 bytes per X Row [2 x 0x40 Bytes from the two separate locations]

    Decoded Format:  
    0 = Road Colour
    1 = Road Inner Stripe
    2 = Road Outer Stripe
    3 = Road Exterior
    7 = Central Stripe
*/

void HWRoad::decode_road(const uint8_t* src_road)
{
    for (int y = 0; y < 256 * 2; y++) 
    {
        const int src = ((y & 0xff) * 0x40 + (y >> 8) * 0x8000) % rom_size; // tempGfx
        const int dst = y * 512; // System16Roads

        // loop over columns
        for (int x = 0; x < 512; x++) 
        {
            roads[dst + x] = (((src_road[src + (x / 8)] >> (~x & 7)) & 1) << 0) | (((src_road[src + (x / 8 + 0x4000)] >> (~x & 7)) & 1) << 1);

            // pre-mark road data in the "stripe" area with a high bit
            if (x >= 256 - 8 && x < 256 && roads[dst + x] == 3)
                roads[dst + x] |= 4;
        }
    }

    // set up a dummy road in the last entry
    for (int i = 0; i < 512; i++) 
    {
        roads[256 * 2 * 512 + i] = 3;
    }
}

// Writes go to RAM, but we read from the RAM Buffer.
void HWRoad::write16(uint32_t adr, const uint16_t data)
{
    ram[(adr >> 1) & 0x7FF] = data;
}

void HWRoad::write16(uint32_t* adr, const uint16_t data)
{
    uint32_t a = *adr;
    ram[(a >> 1) & 0x7FF] = data;
    *adr += 2;
}

void HWRoad::write32(uint32_t* adr, const uint32_t data)
{
    uint32_t a = *adr;
    ram[(a >> 1) & 0x7FF] = data >> 16;
    ram[((a >> 1) + 1) & 0x7FF] = data & 0xFFFF;
    *adr += 4;
}

// Rows on which render_foreground_lores() writes every pixel: it skips a row only when neither
// road is visible on it (same tests as there), and otherwise fills the whole width (road span plus
// the fills on both sides).  Used to skip the work under the road on those rows.
void HWRoad::compute_cover(unsigned char* cover)
{
    const uint16_t* roadram = ramBuff;
    const int control = road_control & 3;
    for (int y = 0; y < 224; y++)
    {
        const uint32_t data0 = roadram[0x000 + y];
        const uint32_t data1 = roadram[0x100 + y];
        bool drawn = true;
        if (((data0 & 0x800) != 0) && ((data1 & 0x800) != 0))
            drawn = false;
        else if ((control == 0 && (data0 & 0x800)) || (control == 3 && (data1 & 0x800)))
            drawn = false;
        cover[y] = drawn ? 1 : 0;
    }
}

uint16_t HWRoad::read_road_control()
{
    // Swap the halves of the road RAM by exchanging the pointers (same result as copying).
    uint16_t* t = ram;
    ram = ramBuff;
    ramBuff = t;

    return 0xffff;
}

void HWRoad::write_road_control(const uint8_t road_control)
{
    this->road_control = road_control;
}

// ------------------------------------------------------------------------------------------------
// Road Rendering: Lores Version
// ------------------------------------------------------------------------------------------------

// Background: Look for solid fill scanlines
#ifdef PLATFORM_FALCON
// 68020+ inner loops, see atari/road_asm.S.
extern "C" void atari_fill16(uint16_t* dst, uint32_t color, uint32_t count);
extern "C" void atari_road_copy1(uint16_t* dst, const uint8_t* src, const uint16_t* tab, uint32_t n);
extern "C" void atari_road_copy2(uint16_t* dst, const uint8_t* s0, const uint8_t* s1, const uint16_t* lut16, uint32_t n);
extern "C" void atari_road_copy1_half(uint16_t* dst, const uint8_t* src, const uint16_t* tab, uint32_t n, uint32_t phase);
extern "C" void atari_road_copy2_half(uint16_t* dst, const uint8_t* s0, const uint8_t* s1, const uint16_t* lut64, uint32_t n, uint32_t phase);
extern "C" void atari_road_lut(uint16_t* lut, const uint16_t* ct, const uint8_t* pmap);

namespace
{
    // The part of one road (a 512 pixel wide strip, viewed through a 4096 pixel
    // wrap-around counter) that lands on the current scanline: pixels
    // [ab, ae) read src (starting at sp); everything else is the constant
    // colour 3.  Identical to the per-pixel "hpos < 0x200 ? src[hpos] : 3"
    // walk of the original renderer.
    struct RoadSpan { int ab, ae; const uint8_t* sp; };

    inline void road_span(int hpos, const uint8_t* src, int width, RoadSpan& r)
    {
        if (hpos < 0x200)
        {
            r.ab = 0;
            r.ae = (0x200 - hpos < width) ? 0x200 - hpos : width;
            r.sp = src + hpos;
        }
        else
        {
            int ab = 0x1000 - hpos;
            r.sp = src;
            if (ab >= width) { r.ab = r.ae = 0; }
            else { r.ab = ab; r.ae = (ab + 0x200 < width) ? ab + 0x200 : width; }
        }
    }

    inline void road_fill(uint16_t* dst, int x, int y, uint16_t c)
    {
        if (y > x) atari_fill16(dst + x, c, (uint32_t)(y - x));
    }
}
#endif
#include "atari/options.hpp"   // g_row_draw: rows drawn in a picture (vscale option)
void HWRoad::render_background_lores(uint16_t* pixels)
{
    int x, y;
    uint16_t* roadram = ramBuff;

    for (y = 0; y < S16_HEIGHT; y++) 
    {
        if (!g_row_draw[y]) continue;
        int data0 = roadram[0x000 + y];
        int data1 = roadram[0x100 + y];

        int color = -1;

        // based on the info->control, we can figure out which sky to draw
        switch (road_control & 3) 
        {
            case 0:
                if (data0 & 0x800)
                    color = data0 & 0x7f;
                break;

            case 1:
                if (data0 & 0x800)
                    color = data0 & 0x7f;
                else if (data1 & 0x800)
                    color = data1 & 0x7f;
                break;

            case 2:
                if (data1 & 0x800)
                    color = data1 & 0x7f;
                else if (data0 & 0x800)
                    color = data0 & 0x7f;
                break;

            case 3:
                if (data1 & 0x800)
                    color = data1 & 0x7f;
                break;
        }

        // fill the scanline with color
        if (color != -1) 
        {
            uint16_t* pPixel = pixels + (y * config.s16_width);
            color |= color_offset3;
            
#ifdef PLATFORM_FALCON
            atari_fill16(pPixel, color, (uint32_t)config.s16_width);
#else
            for (x = 0; x < config.s16_width; x++)
                *(pPixel)++ = color;
#endif
        }
    }
}

// Foreground: Render From ROM
void HWRoad::render_foreground_lores(uint16_t* pixels)
{
    int x, y;
    uint16_t* roadram = ramBuff;
    
    for (y = 0; y < S16_HEIGHT; y++) 
    {
        if (!g_row_draw[y]) continue;
        uint16_t color_table[32];

        static const uint8_t priority_map[2][8] =
        {
            { 0x80,0x81,0x81,0x87,0,0,0,0x00 },
            { 0x81,0x81,0x81,0x8f,0,0,0,0x80 }
        };

        const uint32_t data0 = roadram[0x000 + y];
        const uint32_t data1 = roadram[0x100 + y];

        // if both roads are low priority, skip
        if (((data0 & 0x800) != 0) && ((data1 & 0x800) != 0))
            continue;

        uint16_t* pPixel = pixels + (y * config.s16_width);
        int32_t hpos0, hpos1, color0, color1;
        int32_t control = road_control & 3;

        uint8_t *src0, *src1;
        int32_t bgcolor; // 8 bits

        // get road 0 data
        src0   = ((data0 & 0x800) != 0) ? roads + 256 * 2 * 512 : (roads + (0x000 + ((data0 >> 1) & 0xff)) * 512);
        hpos0  = roadram[0x200 + (((road_control & 4) != 0) ? y : (data0 & 0x1ff))] & 0xfff;
        color0 = roadram[0x600 + (((road_control & 4) != 0) ? y : (data0 & 0x1ff))];

        // get road 1 data
        src1   = ((data1 & 0x800) != 0) ? roads + 256 * 2 * 512 : (roads + (0x100 + ((data1 >> 1) & 0xff)) * 512);
        hpos1  = roadram[0x400 + (((road_control & 4) != 0) ? (0x100 + y) : (data1 & 0x1ff))] & 0xfff;
        color1 = roadram[0x600 + (((road_control & 4) != 0) ? (0x100 + y) : (data1 & 0x1ff))];

        // determine the 5 colors for road 0
        color_table[0x00] = color_offset1 ^ 0x00 ^ ((color0 >> 0) & 1);
        color_table[0x01] = color_offset1 ^ 0x02 ^ ((color0 >> 1) & 1);
        color_table[0x02] = color_offset1 ^ 0x04 ^ ((color0 >> 2) & 1);
        bgcolor = (color0 >> 8) & 0xf;
        color_table[0x03] = ((data0 & 0x200) != 0) ? color_table[0x00] : (color_offset2 ^ 0x00 ^ bgcolor);
        color_table[0x07] = color_offset1 ^ 0x06 ^ ((color0 >> 3) & 1);

        // determine the 5 colors for road 1
        color_table[0x10] = color_offset1 ^ 0x08 ^ ((color1 >> 4) & 1);
        color_table[0x11] = color_offset1 ^ 0x0a ^ ((color1 >> 5) & 1);
        color_table[0x12] = color_offset1 ^ 0x0c ^ ((color1 >> 6) & 1);
        bgcolor = (color1 >> 8) & 0xf;
        color_table[0x13] = ((data1 & 0x200) != 0) ? color_table[0x10] : (color_offset2 ^ 0x10 ^ bgcolor);
        color_table[0x17] = color_offset1 ^ 0x0e ^ ((color1 >> 7) & 1);

        // Shift road dependent on whether we are in widescreen mode or not
        uint16_t s16_x = 0x5f8 + config.s16_x_off;

#ifdef PLATFORM_FALCON
        {
            const int W = config.s16_width;
            if ((control == 0 && (data0 & 0x800)) || (control == 3 && (data1 & 0x800)))
                continue;

            if (control == 0 || control == 3)
            {
                // single road: road 0 uses colour_table[0..7], road 1 colour_table[0x10..0x17]
                const uint16_t* tab = (control == 0) ? &color_table[0x00] : &color_table[0x10];
                int hp = ((control == 0 ? hpos0 : hpos1) - (s16_x + x_offset)) & 0xfff;
                RoadSpan r;
                road_span(hp, control == 0 ? src0 : src1, W, r);
                road_fill(pPixel, 0, r.ab, tab[3]);
                if (r.ae > r.ab)
                {
                    if (atari_opt.road_hres)
                        atari_road_copy1_half(pPixel + r.ab, r.sp, tab, (uint32_t)(r.ae - r.ab), (uint32_t)(r.ab & 1));
                    else
                        atari_road_copy1(pPixel + r.ab, r.sp, tab, (uint32_t)(r.ae - r.ab));
                }
                road_fill(pPixel, r.ae, W, tab[3]);
                continue;
            }

            // two roads: pick road 1 over road 0 according to the priority map
            // Road pixels are 0..3 or 7 (colour_table has entries 0-3 and 7 per road),
            // so tables are indexed over 8 values.
            uint16_t lut16[64];
#ifdef ROADLUTCHECK
            {
                uint16_t ref[64];
                for (int p0 = 0; p0 < 8; p0++)
                    for (int p1 = 0; p1 < 8; p1++)
                        ref[p0 * 8 + p1] = (((priority_map[control - 1][p0] >> p1) & 1) != 0) ? color_table[0x10 + p1] : color_table[p0];
                atari_road_lut(lut16, color_table, priority_map[control - 1]);
                static int bad = 0, n = 0;
                if (memcmp(ref, lut16, sizeof(ref)) != 0) bad++;
                if (++n % 2000 == 0) printf("ROADLUT n=%d bad=%d%c%c", n, bad, 13, 10);
            }
#else
            atari_road_lut(lut16, color_table, priority_map[control - 1]);
#endif

            int h0 = (hpos0 - (s16_x + x_offset)) & 0xfff;
            int h1 = (hpos1 - (s16_x + x_offset)) & 0xfff;
            RoadSpan r0, r1;
            road_span(h0, src0, W, r0);
            road_span(h1, src1, W, r1);

            int b[6] = { 0, r0.ab, r0.ae, r1.ab, r1.ae, W };
            for (int i = 1; i < 6; i++)                       // insertion sort
                for (int k = i; k > 0 && b[k - 1] > b[k]; k--) { int t = b[k]; b[k] = b[k - 1]; b[k - 1] = t; }

            uint16_t tab0[8];
            for (int p = 0; p < 8; p++) tab0[p] = lut16[p * 8 + 3];

            for (int i = 0; i < 5; i++)
            {
                int x = b[i], y = b[i + 1];
                if (y <= x) continue;
                bool a0 = (r0.ab <= x && y <= r0.ae);
                bool a1 = (r1.ab <= x && y <= r1.ae);
                if (a0 && a1)
                {
                    if (atari_opt.road_hres) atari_road_copy2_half(pPixel + x, r0.sp + (x - r0.ab), r1.sp + (x - r1.ab), lut16, (uint32_t)(y - x), (uint32_t)(x & 1));
                    else                     atari_road_copy2(pPixel + x, r0.sp + (x - r0.ab), r1.sp + (x - r1.ab), lut16, (uint32_t)(y - x));
                }
                else if (a0)
                {
                    if (atari_opt.road_hres) atari_road_copy1_half(pPixel + x, r0.sp + (x - r0.ab), tab0, (uint32_t)(y - x), (uint32_t)(x & 1));
                    else                     atari_road_copy1(pPixel + x, r0.sp + (x - r0.ab), tab0, (uint32_t)(y - x));
                }
                else if (a1)
                {
                    if (atari_opt.road_hres) atari_road_copy1_half(pPixel + x, r1.sp + (x - r1.ab), &lut16[24], (uint32_t)(y - x), (uint32_t)(x & 1));
                    else                     atari_road_copy1(pPixel + x, r1.sp + (x - r1.ab), &lut16[24], (uint32_t)(y - x));
                }
                else               atari_fill16(pPixel + x, lut16[27], (uint32_t)(y - x));
            }
#ifdef ROADCHECK
            {
                static int reported = 0;
                int hh0 = h0, hh1 = h1;
                for (int xx = 0; xx < W && reported < 6; xx++)
                {
                    int q0 = (hh0 < 0x200) ? src0[hh0] : 3;
                    int q1 = (hh1 < 0x200) ? src1[hh1] : 3;
                    uint16_t want = (((priority_map[control - 1][q0] >> q1) & 1) != 0) ? color_table[0x10 + q1] : color_table[q0];
                    if (pPixel[xx] != want) { printf("ROADCHK y=%d x=%d ctl=%d q0=%d q1=%d got=%04x want=%04x h0=%d h1=%d r0=[%d,%d) r1=[%d,%d)\r\n", y, xx, control, q0, q1, (unsigned)pPixel[xx], (unsigned)want, h0, h1, r0.ab, r0.ae, r1.ab, r1.ae); reported++; }
                    hh0 = (hh0 + 1) & 0xfff; hh1 = (hh1 + 1) & 0xfff;
                }
            }
#endif
            continue;
        }
#endif        // draw the road
        switch (control) 
        {
            case 0:
                if (data0 & 0x800)
                    continue;
                hpos0 = (hpos0 - (s16_x + x_offset)) & 0xfff;
                for (x = 0; x < config.s16_width; x++) 
                {
                    int pix0 = (hpos0 < 0x200) ? src0[hpos0] : 3;
                    pPixel[x] = color_table[0x00 + pix0];
                    hpos0 = (hpos0 + 1) & 0xfff;
                }
                break;

            case 1:
                hpos0 = (hpos0 - (s16_x + x_offset)) & 0xfff;
                hpos1 = (hpos1 - (s16_x + x_offset)) & 0xfff;
                for (x = 0; x < config.s16_width; x++) 
                {
                    int pix0 = (hpos0 < 0x200) ? src0[hpos0] : 3;
                    int pix1 = (hpos1 < 0x200) ? src1[hpos1] : 3;
                    if (((priority_map[0][pix0] >> pix1) & 1) != 0)
                        pPixel[x] = color_table[0x10 + pix1];
                    else
                        pPixel[x] = color_table[0x00 + pix0];

                    hpos0 = (hpos0 + 1) & 0xfff;
                    hpos1 = (hpos1 + 1) & 0xfff;
                }
                break;

            case 2:
                hpos0 = (hpos0 - (s16_x + x_offset)) & 0xfff;
                hpos1 = (hpos1 - (s16_x + x_offset)) & 0xfff;
                for (x = 0; x < config.s16_width; x++) 
                {
                    int pix0 = (hpos0 < 0x200) ? src0[hpos0] : 3;
                    int pix1 = (hpos1 < 0x200) ? src1[hpos1] : 3;
                    if (((priority_map[1][pix0] >> pix1) & 1) != 0)
                        pPixel[x] = color_table[0x10 + pix1];
                    else
                        pPixel[x] = color_table[0x00 + pix0];

                    hpos0 = (hpos0 + 1) & 0xfff;
                    hpos1 = (hpos1 + 1) & 0xfff;
                }
                break;

            case 3:
                if (data1 & 0x800)
                    continue;
                hpos1 = (hpos1 - (s16_x + x_offset)) & 0xfff;
                for (x = 0; x < config.s16_width; x++) 
                {
                    int pix1 = (hpos1 < 0x200) ? src1[hpos1] : 3;
                    pPixel[x] = color_table[0x10 + pix1];
                    hpos1 = (hpos1 + 1) & 0xfff;
                }
                break;
            } // end switch
    } // end for
}

// ------------------------------------------------------------------------------------------------
// High Resolution (Double Resolution) Road Rendering
// ------------------------------------------------------------------------------------------------
void HWRoad::render_background_hires(uint16_t* pixels)
{
    int x, y;
    uint16_t* roadram = ramBuff;

    for (y = 0; y < config.s16_height; y += 2) 
    {
        int data0 = roadram[0x000 + (y >> 1)];
        int data1 = roadram[0x100 + (y >> 1)];

        int color = -1;

        // based on the info->control, we can figure out which sky to draw
        switch (road_control & 3) 
        {
            case 0:
                if (data0 & 0x800)
                    color = data0 & 0x7f;
                break;

            case 1:
                if (data0 & 0x800)
                    color = data0 & 0x7f;
                else if (data1 & 0x800)
                    color = data1 & 0x7f;
                break;

            case 2:
                if (data1 & 0x800)
                    color = data1 & 0x7f;
                else if (data0 & 0x800)
                    color = data0 & 0x7f;
                break;

            case 3:
                if (data1 & 0x800)
                    color = data1 & 0x7f;
                break;
        }

        // fill the scanline with color
        if (color != -1) 
        {
            uint16_t* pPixel = pixels + (y * config.s16_width);
            color |= color_offset3;
            
            for (x = 0; x < config.s16_width; x++)
                *(pPixel)++ = color;
        }

        // Hi-Res Mode: Copy extra line of background
        memcpy(pixels + ((y+1) * config.s16_width), pixels + (y * config.s16_width), sizeof(uint16_t) * config.s16_width);
    }
}

// ------------------------------------------------------------------------------------------------
// Render Road Foreground - High Resolution Version
// Interpolates previous scanline with next.
// ------------------------------------------------------------------------------------------------
void HWRoad::render_foreground_hires(uint16_t* pixels)
{
    int x, y, yy;
    uint16_t* roadram = ramBuff;
    
    uint16_t color_table[32];
    int32_t color0, color1;
    int32_t bgcolor; // 8 bits

    for (y = 0; y < config.s16_height; y++) 
    {
        yy = y >> 1;
       
        static const uint8_t priority_map[2][8] =
        {
            { 0x80,0x81,0x81,0x87,0,0,0,0x00 },
            { 0x81,0x81,0x81,0x8f,0,0,0,0x80 }
        };

        uint32_t data0 = roadram[0x000 + yy];
        uint32_t data1 = roadram[0x100 + yy];

        // if both roads are low priority, skip
        if (((data0 & 0x800) != 0) && ((data1 & 0x800) != 0))
        {
            y++; 
            continue;
        }

        uint8_t *src0 = NULL, *src1 = NULL;

        // get road 0 data
        int32_t hpos0  = roadram[0x200 + (((road_control & 4) != 0) ? yy : (data0 & 0x1ff))] & 0xfff;

        // get road 1 data       
        int32_t hpos1  = roadram[0x400 + (((road_control & 4) != 0) ? (0x100 + yy) : (data1 & 0x1ff))] & 0xfff;
        
        // ----------------------------------------------------------------------------------------
        // Interpolate Scanlines when in hi-resolution mode.
        // ----------------------------------------------------------------------------------------
        if (y & 1 && yy < S16_HEIGHT - 1)
        {
            uint32_t data0_next = roadram[0x000 + yy + 1];
            uint32_t data1_next = roadram[0x100 + yy + 1];

            int32_t  hpos0_next = roadram[0x200 + (((road_control & 4) != 0) ? yy + 1 : (data0_next & 0x1ff))] & 0xfff;
            int32_t  hpos1_next = roadram[0x400 + (((road_control & 4) != 0) ? yy + 1 : (data1_next & 0x1ff))] & 0xfff;

            // Interpolate road 1 position
            if (((data0 & 0x800) == 0) && (data0_next & 0x800) == 0)
            {
                data0      = (data0      >> 1) & 0xFF;
                data0_next = (data0_next >> 1) & 0xFF;
                int32_t diff = (data0 + ((data0_next - data0) >> 1)) & 0xFF;
                src0 = (roads + (0x000 + diff) * 512);
                hpos0 = (hpos0 + ((hpos0_next - hpos0) >> 1)) & 0xFFF;
            }
            // Interpolate road 2 source position
            if (((data1 & 0x800) == 0) && (data1_next & 0x800) == 0)
            {
                data1      = (data1      >> 1) & 0xFF;
                data1_next = (data1_next >> 1) & 0xFF;
                int32_t diff = (data1 + ((data1_next - data1) >> 1)) & 0xFF;
                src1 = (roads + (0x100 + diff) * 512);
                hpos1 = (hpos1 + ((hpos1_next - hpos1) >> 1)) & 0xFFF;
            }     
        }
        // ----------------------------------------------------------------------------------------
        // Recalculate for non-interpolated scanlines
        // ----------------------------------------------------------------------------------------
        else
        {            
            color0 = roadram[0x600 + (((road_control & 4) != 0) ? yy :           (data0 & 0x1ff))];
            color1 = roadram[0x600 + (((road_control & 4) != 0) ? (0x100 + yy) : (data1 & 0x1ff))];
        
            // determine the 5 colors for road 0
            color_table[0x00] = color_offset1 ^ 0x00 ^ ((color0 >> 0) & 1);
            color_table[0x01] = color_offset1 ^ 0x02 ^ ((color0 >> 1) & 1);
            color_table[0x02] = color_offset1 ^ 0x04 ^ ((color0 >> 2) & 1);
            bgcolor = (color0 >> 8) & 0xf;
            color_table[0x03] = ((data0 & 0x200) != 0) ? color_table[0x00] : (color_offset2 ^ 0x00 ^ bgcolor);
            color_table[0x07] = color_offset1 ^ 0x06 ^ ((color0 >> 3) & 1);

            // determine the 5 colors for road 1
            color_table[0x10] = color_offset1 ^ 0x08 ^ ((color1 >> 4) & 1);
            color_table[0x11] = color_offset1 ^ 0x0a ^ ((color1 >> 5) & 1);
            color_table[0x12] = color_offset1 ^ 0x0c ^ ((color1 >> 6) & 1);
            bgcolor = (color1 >> 8) & 0xf;
            color_table[0x13] = ((data1 & 0x200) != 0) ? color_table[0x10] : (color_offset2 ^ 0x10 ^ bgcolor);
            color_table[0x17] = color_offset1 ^ 0x0e ^ ((color1 >> 7) & 1);        
        }
        
        if (src0 == NULL)
            src0 = ((data0 & 0x800) != 0) ? roads + 256 * 2 * 512 : (roads + (0x000 + ((data0 >> 1) & 0xff)) * 512);
        if (src1 == NULL)
            src1 = ((data1 & 0x800) != 0) ? roads + 256 * 2 * 512 : (roads + (0x100 + ((data1 >> 1) & 0xff)) * 512);

        // Shift road dependent on whether we are in widescreen mode or not
        uint16_t s16_x = 0x5f8 + config.s16_x_off;
        uint16_t* const pPixel = pixels + (y * config.s16_width);

        // draw the road
        switch (road_control & 3)
        {
            case 0:
                if (data0 & 0x800)
                    continue;
                hpos0 = (hpos0 - (s16_x + x_offset)) & 0xfff;
                for (x = 0; x < config.s16_width; x++) 
                {
                    int pix0 = (hpos0 < 0x200) ? src0[hpos0] : 3;
                    pPixel[x] = color_table[0x00 + pix0];
                    if (x & 1)
                        hpos0 = (hpos0 + 1) & 0xfff;
                }
                break;

            case 1:
                hpos0 = (hpos0 - (s16_x + x_offset)) & 0xfff;
                hpos1 = (hpos1 - (s16_x + x_offset)) & 0xfff;
                for (x = 0; x < config.s16_width; x++) 
                {
                    int pix0 = (hpos0 < 0x200) ? src0[hpos0] : 3;
                    int pix1 = (hpos1 < 0x200) ? src1[hpos1] : 3;
                    if (((priority_map[0][pix0] >> pix1) & 1) != 0)
                        pPixel[x] = color_table[0x10 + pix1];
                    else
                        pPixel[x] = color_table[0x00 + pix0];

                    if (x & 1)
                    {
                        hpos0 = (hpos0 + 1) & 0xfff;
                        hpos1 = (hpos1 + 1) & 0xfff;
                    }
                }
                break;

            case 2:
                hpos0 = (hpos0 - (s16_x + x_offset)) & 0xfff;
                hpos1 = (hpos1 - (s16_x + x_offset)) & 0xfff;
                for (x = 0; x < config.s16_width; x++) 
                {
                    int pix0 = (hpos0 < 0x200) ? src0[hpos0] : 3;
                    int pix1 = (hpos1 < 0x200) ? src1[hpos1] : 3;
                    if (((priority_map[1][pix0] >> pix1) & 1) != 0)
                        pPixel[x] = color_table[0x10 + pix1];
                    else
                        pPixel[x] = color_table[0x00 + pix0];
                      
                    if (x & 1)
                    {
                        hpos0 = (hpos0 + 1) & 0xfff;
                        hpos1 = (hpos1 + 1) & 0xfff;
                    }
                }
                break;

            case 3:
                if (data1 & 0x800)
                    continue;
                hpos1 = (hpos1 - (s16_x + x_offset)) & 0xfff;
                for (x = 0; x < config.s16_width; x++) 
                {
                    int pix1 = (hpos1 < 0x200) ? src1[hpos1] : 3;
                    pPixel[x] = color_table[0x10 + pix1];                   
                    if (x & 1)
                        hpos1 = (hpos1 + 1) & 0xfff;
                }
                break;
            } // end switch
    } // end for
}

#ifdef LOWRES
// Half-resolution road: every second engine scanline, every second pixel.
void HWRoad::render_background_half(uint16_t* pixels)
{
    int x, y;
    uint16_t* roadram = ramBuff;

    for (int j = 0; j < config.s16_height; j++) 
    {
        y = j << 1; // engine scanline for this half-resolution row
        int data0 = roadram[0x000 + y];
        int data1 = roadram[0x100 + y];

        int color = -1;

        // based on the info->control, we can figure out which sky to draw
        switch (road_control & 3) 
        {
            case 0:
                if (data0 & 0x800)
                    color = data0 & 0x7f;
                break;

            case 1:
                if (data0 & 0x800)
                    color = data0 & 0x7f;
                else if (data1 & 0x800)
                    color = data1 & 0x7f;
                break;

            case 2:
                if (data1 & 0x800)
                    color = data1 & 0x7f;
                else if (data0 & 0x800)
                    color = data0 & 0x7f;
                break;

            case 3:
                if (data1 & 0x800)
                    color = data1 & 0x7f;
                break;
        }

        // fill the scanline with color
        if (color != -1) 
        {
            uint16_t* pPixel = pixels + (j * config.s16_width);
            color |= color_offset3;
            
            for (x = 0; x < config.s16_width; x++)
                *(pPixel)++ = color;
        }
    }
}

void HWRoad::render_foreground_half(uint16_t* pixels)
{
    int x, y;
    uint16_t* roadram = ramBuff;
    
    for (int j = 0; j < config.s16_height; j++) 
    {
        y = j << 1; // engine scanline for this half-resolution row
        uint16_t color_table[32];

        static const uint8_t priority_map[2][8] =
        {
            { 0x80,0x81,0x81,0x87,0,0,0,0x00 },
            { 0x81,0x81,0x81,0x8f,0,0,0,0x80 }
        };

        const uint32_t data0 = roadram[0x000 + y];
        const uint32_t data1 = roadram[0x100 + y];

        // if both roads are low priority, skip
        if (((data0 & 0x800) != 0) && ((data1 & 0x800) != 0))
            continue;

        uint16_t* pPixel = pixels + (j * config.s16_width);
        int32_t hpos0, hpos1, color0, color1;
        int32_t control = road_control & 3;

        uint8_t *src0, *src1;
        int32_t bgcolor; // 8 bits

        // get road 0 data
        src0   = ((data0 & 0x800) != 0) ? roads + 256 * 2 * 512 : (roads + (0x000 + ((data0 >> 1) & 0xff)) * 512);
        hpos0  = roadram[0x200 + (((road_control & 4) != 0) ? y : (data0 & 0x1ff))] & 0xfff;
        color0 = roadram[0x600 + (((road_control & 4) != 0) ? y : (data0 & 0x1ff))];

        // get road 1 data
        src1   = ((data1 & 0x800) != 0) ? roads + 256 * 2 * 512 : (roads + (0x100 + ((data1 >> 1) & 0xff)) * 512);
        hpos1  = roadram[0x400 + (((road_control & 4) != 0) ? (0x100 + y) : (data1 & 0x1ff))] & 0xfff;
        color1 = roadram[0x600 + (((road_control & 4) != 0) ? (0x100 + y) : (data1 & 0x1ff))];

        // determine the 5 colors for road 0
        color_table[0x00] = color_offset1 ^ 0x00 ^ ((color0 >> 0) & 1);
        color_table[0x01] = color_offset1 ^ 0x02 ^ ((color0 >> 1) & 1);
        color_table[0x02] = color_offset1 ^ 0x04 ^ ((color0 >> 2) & 1);
        bgcolor = (color0 >> 8) & 0xf;
        color_table[0x03] = ((data0 & 0x200) != 0) ? color_table[0x00] : (color_offset2 ^ 0x00 ^ bgcolor);
        color_table[0x07] = color_offset1 ^ 0x06 ^ ((color0 >> 3) & 1);

        // determine the 5 colors for road 1
        color_table[0x10] = color_offset1 ^ 0x08 ^ ((color1 >> 4) & 1);
        color_table[0x11] = color_offset1 ^ 0x0a ^ ((color1 >> 5) & 1);
        color_table[0x12] = color_offset1 ^ 0x0c ^ ((color1 >> 6) & 1);
        bgcolor = (color1 >> 8) & 0xf;
        color_table[0x13] = ((data1 & 0x200) != 0) ? color_table[0x10] : (color_offset2 ^ 0x10 ^ bgcolor);
        color_table[0x17] = color_offset1 ^ 0x0e ^ ((color1 >> 7) & 1);

        // Shift road dependent on whether we are in widescreen mode or not
        uint16_t s16_x = 0x5f8 + config.s16_x_off;

        // draw the road
        switch (control) 
        {
            case 0:
                if (data0 & 0x800)
                    continue;
                hpos0 = (hpos0 - (s16_x + x_offset)) & 0xfff;
                for (x = 0; x < config.s16_width; x++) 
                {
                    int pix0 = (hpos0 < 0x200) ? src0[hpos0] : 3;
                    pPixel[x] = color_table[0x00 + pix0];
                    hpos0 = (hpos0 + 2) & 0xfff;
                }
                break;

            case 1:
                hpos0 = (hpos0 - (s16_x + x_offset)) & 0xfff;
                hpos1 = (hpos1 - (s16_x + x_offset)) & 0xfff;
                for (x = 0; x < config.s16_width; x++) 
                {
                    int pix0 = (hpos0 < 0x200) ? src0[hpos0] : 3;
                    int pix1 = (hpos1 < 0x200) ? src1[hpos1] : 3;
                    if (((priority_map[0][pix0] >> pix1) & 1) != 0)
                        pPixel[x] = color_table[0x10 + pix1];
                    else
                        pPixel[x] = color_table[0x00 + pix0];

                    hpos0 = (hpos0 + 2) & 0xfff;
                    hpos1 = (hpos1 + 2) & 0xfff;
                }
                break;

            case 2:
                hpos0 = (hpos0 - (s16_x + x_offset)) & 0xfff;
                hpos1 = (hpos1 - (s16_x + x_offset)) & 0xfff;
                for (x = 0; x < config.s16_width; x++) 
                {
                    int pix0 = (hpos0 < 0x200) ? src0[hpos0] : 3;
                    int pix1 = (hpos1 < 0x200) ? src1[hpos1] : 3;
                    if (((priority_map[1][pix0] >> pix1) & 1) != 0)
                        pPixel[x] = color_table[0x10 + pix1];
                    else
                        pPixel[x] = color_table[0x00 + pix0];

                    hpos0 = (hpos0 + 2) & 0xfff;
                    hpos1 = (hpos1 + 2) & 0xfff;
                }
                break;

            case 3:
                if (data1 & 0x800)
                    continue;
                hpos1 = (hpos1 - (s16_x + x_offset)) & 0xfff;
                for (x = 0; x < config.s16_width; x++) 
                {
                    int pix1 = (hpos1 < 0x200) ? src1[hpos1] : 3;
                    pPixel[x] = color_table[0x10 + pix1];
                    hpos1 = (hpos1 + 2) & 0xfff;
                }
                break;
            } // end switch
    } // end for
}

#endif
