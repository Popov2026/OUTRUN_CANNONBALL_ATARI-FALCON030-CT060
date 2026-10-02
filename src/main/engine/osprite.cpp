/***************************************************************************
    Hardware Sprites.
    
    This class stores sprites in the converted format expected by the
    OutRun graphics hardware.

    Copyright Chris White.
    See license.txt for more details.
***************************************************************************/

#include "engine/osprite.hpp"

//  Out Run/X-Board-style sprites
//
//      Offs  Bits               Usage
//       +0   e------- --------  Signify end of sprite list
//       +0   -h-h---- --------  Hide this sprite if either bit is set
//       +0   ----bbb- --------  Sprite bank
//       +0   -------t tttttttt  Top scanline of sprite + 256
//       +2   oooooooo oooooooo  Offset within selected sprite bank
//       +4   ppppppp- --------  Signed 7-bit pitch value between scanlines
//       +4   -------x xxxxxxxx  X position of sprite (position $BE is screen position 0)
//       +6   -s------ --------  Enable shadows
//       +6   --pp---- --------  Sprite priority, relative to tilemaps
//       +6   ------vv vvvvvvvv  Vertical zoom factor (0x200 = full size, 0x100 = half size, 0x300 = 2x size)
//       +8   y------- --------  Render from top-to-bottom (1) or bottom-to-top (0) on screen
//       +8   -f------ --------  Horizontal flip: read the data backwards if set
//       +8   --x----- --------  Render from left-to-right (1) or right-to-left (0) on screen
//       +8   ------hh hhhhhhhh  Horizontal zoom factor (0x200 = full size, 0x100 = half size, 0x300 = 2x size)
//       +E   dddddddd dddddddd  Scratch space for current address
//
//    Out Run only:
//       +A   hhhhhhhh --------  Height in scanlines - 1
//       +A   -------- -ccccccc  Sprite color palette

osprite::osprite(void)
{

}

osprite::~osprite(void)
{
}

void osprite::init()
{
    data[0] = 0;
    data[1] = 0;
    data[2] = 0;
    data[3] = 0;
    data[4] = 0;
    data[5] = 0;
    data[6] = 0;
    scratch = 0;
}

