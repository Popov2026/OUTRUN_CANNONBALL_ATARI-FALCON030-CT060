#pragma once

/***************************************************************************
    Atari STE DMA Sound Backend.

    Same responsibility as src/main/sdl2/audio.cpp: pull the already-
    software-emulated YM2151 + SegaPCM channel buffers (produced by
    engine/audio/osoundint.* and hwaudio/*, none of which are touched by
    this port) and osoundint's OSoundInt::tick()-driven mix, and hand the
    result to the actual sound hardware. On real OutRun/Cannonball this is
    an SDL callback-driven ring buffer into whatever the host OS mixer is;
    on the Mega STE the target is the STE's own DMA sound hardware (NOT the
    YM2149 PSG - that chip exists on STE only for backward compatibility
    and STE game audio normally bypasses it entirely, exactly as PC/arcade
    Cannonball's own YM2151+PCM emulation already does).

    STE/Falcon DMA sound facts used here (register offsets checked against
    EmuTOS's dmasound.c):
      - 8-bit signed PCM, stereo. The hardware only offers four sample
        rates (6258/12517/25033/50066 Hz), so the engine itself mixes at
        one of them - 12517 Hz, set in frontend/config.cpp - and no
        resampling is needed. (6258 Hz was tried and gives no sound at all
        on real hardware.)
      - The DMA reads a buffer straight out of ST RAM between a start and
        an end address; there is no FIFO or callback. Each game step fills
        one of four buffers and queues it; service() hands the next queued
        buffer to the DMA as soon as it has finished the previous one.
      - With the DSP .mod replay running (outrun.ini's mod_dsp=1) the DAC
        listens to the DSP instead: tick() then sends the mix to
        atari/dsp_replay.cpp rather than to the DMA.
      - outrun.ini's fm_half=1 makes the FM chip produce half as many
        samples; tick() interpolates them back to the mix rate.

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

#include "../globals.hpp"
#include "../stdint.hpp"

struct wav_t {
    uint8_t loaded;
    int16_t *data;
    uint32_t pos;
    uint32_t length;
};

class Audio
{
public:
    Audio();
    ~Audio();

    void init();
    void tick();
    void tick_muted();   // keep chip timers/flags moving without synthesising or playing audio
    void service();      // start the queued buffer as soon as the DMA has finished the current one
    void start_audio();
    void stop_audio();
    double adjust_speed();
    void load_wav(const char* filename);
    void clear_wav();

    bool sound_enabled;

private:
    static const uint32_t STE_RATE = 12517; // closest STE hw rate to 44.1/22kHz source; see .cpp
    static const uint32_t DMA_BUFFER_SAMPLES = 2048; // per half-buffer, stereo interleaved bytes = *2

    // Four 8-bit signed stereo PCM buffers in ST RAM: one is being played by the DMA, up to
    // two finished ones wait behind it, the fourth is filled by tick().  The DMA registers are
    // never rewritten while it is playing (that clicks): service() hands the next queued
    // buffer over once the DMA has finished the current one.  If more than two pile up
    // (the game running faster than the sound), the oldest is dropped.
    static const int NUM_DMA_BUFFERS = 4;
    int8_t* dma_buffer[NUM_DMA_BUFFERS];
    int playing;                 // buffer being played, -1 if none
    int queue[2];                // queued buffers, oldest first
    int queue_len;
    uint32_t queued_bytes;
    bool dma_busy();
    int16_t* mix_buffer;
    wav_t wavfile;

    void clear_buffers();
    void start_dma(int8_t* buffer, uint32_t bytes);
};
