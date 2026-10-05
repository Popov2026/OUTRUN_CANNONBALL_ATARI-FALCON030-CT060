#include "video.hpp"
#include "hwvideo/hwsprites.hpp"
#include "globals.hpp"
#include "frontend/config.hpp"
#include <cstdlib>
#include <cstring>
#if defined(PLATFORM_ATARI) && (defined(__mc68030__) || defined(__mc68060__))
static void spr_build(const uint32_t* sprites, uint32_t n);
#endif

/***************************************************************************
    Video Emulation: OutRun Sprite Rendering Hardware.
    Based on MAME source code.

    Copyright Aaron Giles.
    All rights reserved.
***************************************************************************/

/*******************************************************************************************
*  Out Run/X-Board-style sprites
*
*      Offs  Bits               Usage
*       +0   e------- --------  Signify end of sprite list
*       +0   -h-h---- --------  Hide this sprite if either bit is set
*       +0   ----bbb- --------  Sprite bank
*       +0   -------t tttttttt  Top scanline of sprite + 256
*       +2   oooooooo oooooooo  Offset within selected sprite bank
*       +4   ppppppp- --------  Signed 7-bit pitch value between scanlines
*       +4   -------x xxxxxxxx  X position of sprite (position $BE is screen position 0)
*       +6   -s------ --------  Enable shadows
*       +6   --pp---- --------  Sprite priority, relative to tilemaps
*       +6   ------vv vvvvvvvv  Vertical zoom factor (0x200 = full size, 0x100 = half size, 0x300 = 2x size)
*       +8   y------- --------  Render from top-to-bottom (1) or bottom-to-top (0) on screen
*       +8   -f------ --------  Horizontal flip: read the data backwards if set
*       +8   --x----- --------  Render from left-to-right (1) or right-to-left (0) on screen
*       +8   ------hh hhhhhhhh  Horizontal zoom factor (0x200 = full size, 0x100 = half size, 0x300 = 2x size)
*       +E   dddddddd dddddddd  Scratch space for current address
*
*  Out Run only:
*       +A   hhhhhhhh --------  Height in scanlines - 1
*       +A   -------- -ccccccc  Sprite color palette
*
*  X-Board only:
*       +A   ----hhhh hhhhhhhh  Height in scanlines - 1
*       +C   -------- cccccccc  Sprite color palette
*
*  Final bitmap format:
*
*            -s------ --------  Shadow control
*            --pp---- --------  Sprite priority
*            ----cccc cccc----  Sprite color palette
*            -------- ----llll  4-bit pixel data
*
 *******************************************************************************************/

// Enable for hardware pixel accuracy, where sprite shadowing delayed by 1 clock cycle (slower)
#define PIXEL_ACCURACY 0 

hwsprites::hwsprites()
{
    ram     = ramA;
    ramBuff = ramB;
}

hwsprites::~hwsprites()
{
}

void hwsprites::init(const uint8_t* src_sprites)
{
    reset();

    if (src_sprites)
    {
        // Convert S16 tiles to a more useable format
        const uint8_t *spr = src_sprites;

        for (uint32_t i = 0; i < SPRITES_LENGTH; i++)
        {
            uint8_t d3 = *spr++;
            uint8_t d2 = *spr++;
            uint8_t d1 = *spr++;
            uint8_t d0 = *spr++;

            sprites[i] = (d0 << 24) | (d1 << 16) | (d2 << 8) | d3;
        }
#if defined(PLATFORM_ATARI) && (defined(__mc68030__) || defined(__mc68060__)) && !defined(SPR_NOSPAN)
        spr_build(sprites, SPRITES_LENGTH);   // span renderer tables (see below)
#endif
    }
}

void hwsprites::reset()
{
    // Clear Sprite RAM buffers
    for (uint16_t i = 0; i < SPRITE_RAM_SIZE; i++)
    {
        ram[i] = 0;
        ramBuff[i] = 0;
    }
}

// Clip areas of the screen in wide-screen mode
void hwsprites::set_x_clip(bool on)
{
    // Clip to central 320 width window.
    if (on)
    {
        x1 = config.s16_x_off;
        x2 = x1 + S16_WIDTH;

        if (config.video.hires)
        {
            x1 <<= 1;
            x2 <<= 1;
        }
#ifdef LOWRES
        x1 >>= 1;
        x2 >>= 1;
#endif
    }
    // Allow full wide-screen.
    else
    {
        x1 = 0;
        x2 = config.s16_width;
    }
}

uint8_t hwsprites::read(const uint16_t adr)
{
    uint16_t a = adr >> 1;
    if ((adr & 1) == 1)
        return ram[a] & 0xff;
    else
        return ram[a] >> 8;
}

void hwsprites::write(const uint16_t adr, const uint16_t data)
{
    ram[adr >> 1] = data;
}

// Copy back buffer to main ram, ready for blit
void hwsprites::swap()
{
    // Exchange the two halves of sprite RAM by swapping the pointers (same result as copying).
    uint16_t* t = ram;
    ram = ramBuff;
    ramBuff = t;
}

#if PIXEL_ACCURACY

// Reproduces glowy edge around sprites on top of shadows as seen on Hardware.
// Believed to be caused by shadowing being out by one clock cycle / pixel.
//
// 1/ Sprites Drawn on top of Shadow clears the shadow flags for its opaque pixels.
// 2/ Either the flag clear or the sprite itself is offset by one pixel horizontally.
// 
// Thanks to Alex B. for this implementation.

#define draw_pixel()                                                                                  \
{                                                                                                     \
    if (x >= x1 && x < x2)                                                                            \
    {                                                                                                 \
        if (shadow && pix == 0xa)                                                                     \
        {                                                                                             \
            pPixel[x] &= 0xfff;                                                                       \
            pPixel[x] += S16_PALETTE_ENTRIES;                                                         \
        }                                                                                             \
        else if (pix != 0 && pix != 15)                                                               \
        {                                                                                             \
            if (x > x1) pPixel[x-1] &= 0xfff;                                                         \
            pPixel[x] = (pix | color);                                                                \
        }                                                                                             \
    }                                                                                                 \
}

#else

#define draw_pixel()                                                                                  \
{                                                                                                     \
    if (x >= x1 && x < x2 && pix != 0 && pix != 15)                                                   \
    {                                                                                                 \
        if (shadow && pix == 0xa)                                                                     \
        {                                                                                             \
            pPixel[x] &= 0xfff;                                                                       \
            pPixel[x] += S16_PALETTE_ENTRIES;                                                         \
        }                                                                                             \
        else                                                                                          \
        {                                                                                             \
            pPixel[x] = (pix | color);                                                                \
        }                                                                                             \
    }                                                                                                 \
}

#endif

#ifdef PLATFORM_ATARI
// 68000 inner loop, see atari/sprite_asm.S for the field meanings.
struct SprLine
{
    uint16_t* p;  const uint32_t* spr;  int32_t sprstep;  int32_t pstep;
    uint32_t hzoom;  uint16_t* p1;  uint16_t* p2;  uint16_t* pbound;
    uint32_t color;  uint32_t shadow;  uint32_t flip;
    uint32_t lo;  uint32_t range;  const uint16_t* ttab;   // fast-path window and pix->colour table
};
extern "C" const uint32_t* atari_sprite_line(SprLine* s);

#if defined(__mc68030__) || defined(__mc68060__)
// 68030/68060: the whole row loop of a sprite is in assembly (atari/sprite_asm030.S), driven
// by this one global description. Field order = the R_* offsets in that file.
#define ATARI_SPRITE_ROWS 1
struct SprRows
{
    int32_t y, ytarget, ydelta, yacc, vzoom, addr, pitch;
    uint16_t* row;  int32_t rowstep, xoff, x1off, x2off, looff, range;
    const uint32_t* sprdata;  int32_t flip, height;
    const unsigned char* rowdraw;  uint16_t* endslot;
    int32_t sprstep, pstep;  uint32_t hzoom, color, shadow;  const uint16_t* ttab;
    const uint8_t* dec;  const uint16_t* endtab;   // span renderer (see spr_dec below), 0 = not used
};
extern "C" { SprRows atari_sr; void atari_sprite_rows(void); }

// Span renderer (sprite_asm030.S, span_row): the sprite graphics with one byte per pixel
// (spr_dec, MSB nibble first, as the source words are read), and for every source word the
// number of words up to and including the one that ends its row - forwards (spr_endf: the
// second-to-last nibble in reading order, bits 4-7, is 0xF) and backwards for flipped sprites
// (spr_endb: bits 24-27). A row is then drawn screen pixel by screen pixel: pixel k shows source
// nibble floor(k * hzoom / 0x200), the same as the nibble-by-nibble loop, without walking the
// nibbles a shrunk sprite skips nor testing the clip window per pixel.
static uint8_t*  spr_dec  = 0;
static uint16_t* spr_endf = 0;
static uint16_t* spr_endb = 0;
static const uint32_t SPR_PAD = 4096 * 8;   // bytes of padding around spr_dec (rows read past a bank)
static const uint16_t SPR_ENDCAP = 4095;    // longest row, in words (keeps nibble indices < 32768)

static void spr_build(const uint32_t* sprites, uint32_t n)
{
    if (spr_dec) return;   // built once (init() may run again)
    uint8_t* raw = (uint8_t*)std::malloc(n * 8 + 2 * SPR_PAD);
    spr_endf = (uint16_t*)std::malloc(n * 2);
    spr_endb = (uint16_t*)std::malloc(n * 2);
    if (!raw || !spr_endf || !spr_endb)
    {
        std::free(raw); std::free(spr_endf); std::free(spr_endb);
        spr_endf = spr_endb = 0;
        return;
    }
    std::memset(raw, 0, n * 8 + 2 * SPR_PAD);
    spr_dec = raw + SPR_PAD;
    for (uint32_t i = 0; i < n; i++)
    {
        const uint32_t w = sprites[i];
        for (int j = 0; j < 8; j++) spr_dec[i * 8 + j] = (uint8_t)((w >> (28 - 4 * j)) & 15);
    }
    uint32_t run = SPR_ENDCAP;
    for (uint32_t i = n; i-- > 0; )
    {
        run = ((sprites[i] & 0xf0) == 0xf0) ? 1 : (run < SPR_ENDCAP ? run + 1 : SPR_ENDCAP);
        spr_endf[i] = (uint16_t)run;
    }
    run = SPR_ENDCAP;
    for (uint32_t i = 0; i < n; i++)
    {
        run = ((sprites[i] & 0x0f000000) == 0x0f000000) ? 1 : (run < SPR_ENDCAP ? run + 1 : SPR_ENDCAP);
        spr_endb[i] = (uint16_t)run;
    }
}
#endif
#endif
#include "atari/options.hpp"   // g_row_draw: rows drawn in a picture (vscale option)
extern "C" { uint32_t g_spr_lines = 0, g_spr_fast = 0; }   // sprite lines drawn / lines with a fast-path window (statistics)
void hwsprites::render(const uint8_t priority)
{
    const uint32_t numbanks = SPRITES_LENGTH / 0x10000;

    for (uint16_t data = 0; data < SPRITE_RAM_SIZE; data += 8) 
    {
        // stop when we hit the end of sprite list
        if ((ramBuff[data+0] & 0x8000) != 0) break;

        uint32_t sprpri  = 1 << ((ramBuff[data+3] >> 12) & 3);
        if (sprpri != priority) continue;

        // if hidden, or top greater than/equal to bottom, or invalid bank, punt
        int16_t hide    = (ramBuff[data+0] & 0x5000);
        int32_t height  = (ramBuff[data+5] >> 8) + 1;       
        if (hide != 0 || height == 0) continue;
        
        int16_t bank    = (ramBuff[data+0] >> 9) & 7;
        int32_t top     = (ramBuff[data+0] & 0x1ff) - 0x100;
        uint32_t addr    = ramBuff[data+1];
        int32_t pitch  = ((ramBuff[data+2] >> 1) | ((ramBuff[data+4] & 0x1000) << 3)) >> 8;
        int32_t xpos    =  ramBuff[data+6]; // moved from original structure to accomodate widescreen
        uint8_t shadow  = (ramBuff[data+3] >> 14) & 1;
        int32_t vzoom    = ramBuff[data+3] & 0x7ff;
        int32_t ydelta = ((ramBuff[data+4] & 0x8000) != 0) ? 1 : -1;
        int32_t flip   = (~ramBuff[data+4] >> 14) & 1;
        int32_t xdelta = ((ramBuff[data+4] & 0x2000) != 0) ? 1 : -1;
        int32_t hzoom    = ramBuff[data+4] & 0x7ff;     
        int32_t color   = COLOR_BASE + ((ramBuff[data+5] & 0x7f) << 4);
        int32_t x, y, ytarget, yacc = 0, pix;
            
        // adjust X coordinate
        // note: the threshhold below is a guess. If it is too high, rachero will draw garbage
        // If it is too low, smgp won't draw the bottom part of the road
        if (xpos < 0x80 && xdelta < 0)
            xpos += 0x200;
        xpos -= 0xbe;

        // initialize the end address to the start address
        ramBuff[data+7] = addr;

        // clamp to within the memory region size
        if (numbanks)
            bank %= numbanks;

        const uint32_t* spritedata = sprites + 0x10000 * bank;

        // clamp to a maximum of 8x (not 100% confirmed)
        if (vzoom < 0x40) vzoom = 0x40;
        if (hzoom < 0x40) hzoom = 0x40;

        // loop from top to bottom
        ytarget = top + ydelta * height;

        // Adjust for widescreen mode
        xpos += config.s16_x_off;

        // Adjust for hi-res mode
        if (config.video.hires)
        {
            xpos <<= 1;
            top <<= 1;
            ytarget <<= 1;
            hzoom >>= 1;
            vzoom >>= 1;
        }
#ifdef LOWRES
        // Half-resolution buffer: half the columns and rows, twice the source consumed per output pixel/row.
        xpos    >>= 1;
        top     >>= 1;
        ytarget >>= 1;
        hzoom   <<= 1;
        vzoom   <<= 1;
#endif

#ifdef PLATFORM_ATARI
        // Fast-path data, constant for the whole sprite: pixel value -> colour (0 = not drawn), and
        // the most bytes one source word (8 nibbles) can write at this zoom.
        uint16_t ttab[16];
        for (int q = 0; q < 16; q++)
            ttab[q] = (q == 0 || q == 15) ? 0 : (uint16_t)(q | color);
        if (shadow)
            ttab[10] = 0x8000; // marker: this pixel darkens what is already there
        const uint32_t fspan = 16 * (uint32_t)((0x200 + hzoom - 1) / hzoom);
#endif

#ifdef ATARI_SPRITE_ROWS
        {
            const int32_t stride = config.s16_width * 2;   // bytes per row
            const int32_t lo = (xdelta > 0) ? x1 * 2 : x1 * 2 + (int32_t)fspan;
            const int32_t hi = (xdelta > 0) ? x2 * 2 - (int32_t)fspan : x2 * 2;
            atari_sr.y = top;  atari_sr.ytarget = ytarget;  atari_sr.ydelta = ydelta;
            atari_sr.yacc = 0;  atari_sr.vzoom = vzoom;  atari_sr.addr = (int32_t)addr;  atari_sr.pitch = pitch;
            atari_sr.row = (uint16_t*)((uint8_t*)video.pixels + top * stride);
            atari_sr.rowstep = ydelta * stride;
            atari_sr.xoff = xpos * 2;  atari_sr.x1off = x1 * 2;  atari_sr.x2off = x2 * 2;
            atari_sr.looff = lo;  atari_sr.range = (hi > lo) ? hi - lo : 0;
            atari_sr.sprdata = spritedata;  atari_sr.flip = flip;  atari_sr.height = config.s16_height;
            atari_sr.rowdraw = g_row_draw;  atari_sr.endslot = &ramBuff[data+7];
            atari_sr.sprstep = (flip == 0) ? 4 : -4;  atari_sr.pstep = (xdelta > 0) ? 2 : -2;
            atari_sr.hzoom = hzoom;  atari_sr.color = color;  atari_sr.shadow = shadow;  atari_sr.ttab = ttab;
            atari_sr.dec = spr_dec ? spr_dec + 0x10000 * 8 * bank : 0;
            atari_sr.endtab = spr_dec ? (flip ? spr_endb : spr_endf) + 0x10000 * bank : 0;
            atari_sprite_rows();
            continue;
        }
#endif

        for (y = top; y != ytarget; y += ydelta)
        {
            // skip drawing if not within the cliprect
            if (y >= 0 && y < config.s16_height && g_row_draw[y])
            {
                uint16_t* pPixel = &video.pixels[y * config.s16_width];
                int32_t xacc = 0;

#ifdef PLATFORM_ATARI
                {
                    SprLine s;
                    s.p       = pPixel + xpos;
                    s.spr     = spritedata + (flip == 0 ? (int32_t)addr - 1 : (int32_t)addr + 1);
                    s.sprstep = (flip == 0) ? 4 : -4;
                    s.pstep   = (xdelta > 0) ? 2 : -2;
                    s.hzoom   = hzoom;
                    s.p1      = pPixel + x1;
                    s.p2      = pPixel + x2;
                    s.pbound  = (xdelta > 0) ? pPixel + x2 : pPixel + x1; // nothing beyond the clip window can be drawn
                    s.color   = color;
                    s.shadow  = shadow;
                    s.flip    = flip;
                    s.ttab    = ttab;
                    {
                        uint32_t p1b = (uint32_t)s.p1, p2b = (uint32_t)s.p2;
                        uint32_t lo  = (xdelta > 0) ? p1b : p1b + fspan;
                        uint32_t hi  = (xdelta > 0) ? p2b - fspan : p2b;
                        s.lo    = lo;
                        s.range = (hi > lo) ? hi - lo : 0;
                    }
#ifdef PERF_PRINT
                    g_spr_lines++;
                    g_spr_fast += (s.range != 0);
#endif
                    ramBuff[data+7] = (uint16_t)(atari_sprite_line(&s) - spritedata);
                }
                if (false)
                {
#endif
                // non-flipped case
                if (flip == 0)
                {

                    // start at the word before because we preincrement below
                    ramBuff[data+7] = (addr - 1);

                    for (x = xpos; (xdelta > 0 && x < config.s16_width) || (xdelta < 0 && x >= 0); )
                    {
                        uint32_t pixels = spritedata[++ramBuff[data+7]]; // Add to base sprite data the vzoom value

                        // draw four pixels
                        pix = (pixels >> 28) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;
                        pix = (pixels >> 24) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;
                        pix = (pixels >> 20) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;
                        pix = (pixels >> 16) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;
                        pix = (pixels >> 12) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;
                        pix = (pixels >>  8) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;
                        pix = (pixels >>  4) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;
                        pix = (pixels >>  0) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;

                        // stop if the second-to-last pixel in the group was 0xf
                        if ((pixels & 0x000000f0) == 0x000000f0)
                            break;
                    }
                }
                // flipped case
                else
                {
                    // start at the word after because we predecrement below
                    ramBuff[data+7] = (addr + 1);

                    for (x = xpos; (xdelta > 0 && x < config.s16_width) || (xdelta < 0 && x >= 0); )
                    {
                        uint32_t pixels = spritedata[--ramBuff[data+7]];

                        // draw four pixels
                        pix = (pixels >>  0) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;
                        pix = (pixels >>  4) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;
                        pix = (pixels >>  8) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;
                        pix = (pixels >> 12) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;
                        pix = (pixels >> 16) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;
                        pix = (pixels >> 20) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;
                        pix = (pixels >> 24) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;
                        pix = (pixels >> 28) & 0xf; while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;

                        // stop if the second-to-last pixel in the group was 0xf
                        if ((pixels & 0x0f000000) == 0x0f000000)
                            break;
                    }
                }
#ifdef PLATFORM_ATARI
                }
#endif
            }
            // accumulate zoom factors; if we carry into the high bit, skip an extra row
            yacc += vzoom; 
            addr += pitch * (yacc >> 9);
            yacc &= 0x1ff;
        }
    }
}