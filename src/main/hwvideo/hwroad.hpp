#pragma once

#include "stdint.hpp"

class HWRoad
{
public:
    HWRoad();
    ~HWRoad();

    void init(const uint8_t*, const bool hires);
    void write16(uint32_t adr, const uint16_t data);
    void write16(uint32_t* adr, const uint16_t data);
    void write32(uint32_t* adr, const uint32_t data);

    // Direct access to the road RAM words at byte address adr (no wrap-around: callers copy
    // blocks that lie inside the RAM).  Used by the Atari port's block copies in oroad.cpp.
    uint16_t* ram_words(uint32_t adr) { return &ram[(adr >> 1) & 0x7FF]; }
    uint16_t read_road_control();
    void compute_cover(unsigned char* cover);   // rows render_foreground_lores() overwrites completely
    void write_road_control(const uint8_t);
    void (HWRoad::*render_background)(uint16_t*);
    void (HWRoad::*render_foreground)(uint16_t*);
  
private:
    uint8_t road_control;
    uint16_t color_offset1;
    uint16_t color_offset2;
    uint16_t color_offset3;
    int32_t x_offset;

    static const uint16_t ROAD_RAM_SIZE = 0x1000;
    static const uint16_t rom_size = 0x8000;

    // Decoded road graphics
    uint8_t roads[0x40200];

    // Two halves of RAM
    uint16_t ramA[ROAD_RAM_SIZE / 2];
    uint16_t ramB[ROAD_RAM_SIZE / 2];
    uint16_t* ram;       // written by the game
    uint16_t* ramBuff;   // read by the renderer; read_road_control() exchanges the two pointers

    void decode_road(const uint8_t*);
    void render_background_lores(uint16_t*);
    void render_foreground_lores(uint16_t*);
    void render_background_hires(uint16_t*);
    void render_foreground_hires(uint16_t*);
#ifdef LOWRES
    void render_background_half(uint16_t*);
    void render_foreground_half(uint16_t*);
#endif
};

extern HWRoad hwroad;