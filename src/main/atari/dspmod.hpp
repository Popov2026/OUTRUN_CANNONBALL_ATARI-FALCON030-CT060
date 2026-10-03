#pragma once

/***************************************************************************
    .mod replay with DSPMOD 3.4 (bITmASTER of TCE) - mod_dsp = 2.

    DSPMOD is a ready-made 68k + DSP56001 replay (src/main/atari/dspmod_tce.h, the
    original dspmod.tce embedded unmodified). Its 68k part interprets the module and
    streams the sample data to its DSP program 50 times a second; the DSP mixes at
    49170 Hz and feeds the DAC directly.

    DSPMOD has four extra "fx" voices meant for game sound effects. Two of them
    carry the game's own FM + PCM mix: each plays a looping ring buffer (left voice
    panned left, right voice panned right) that Audio::tick() fills through
    push_fx(), just ahead of the replay's read position. While a module plays the
    DAC listens to the DSP only, so this is how the sound effects stay audible.

    DSPMOD is set up with the first module and stays set up between modules
    (stop_song()); shutdown() hands the sound matrix, the DSP and Timer A back, which
    is also what happens when a track has no .mod file, so it falls back to the normal
    DMA path with FM music.

    Driven from MFP Timer A at 50 Hz (dspmod_asm.S), like dsp_replay.

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

#include "../stdint.hpp"

class DspMod
{
public:
    // Starts playing `mod` (the whole .mod file, which must stay allocated until
    // stop_song()). Sets DSPMOD up the first time. False if the module is not accepted.
    bool play(uint8_t* mod);
    // Stops the module; DSPMOD stays set up for the next one.
    void stop_song();
    // Stops everything and gives the sound matrix, the DSP and Timer A back.
    void shutdown();
    bool active() const { return playing; }

    // One game step of the FM + PCM mix: `frames` interleaved L,R signed 8-bit samples at
    // the engine's mix rate. Called from Audio::tick().
    void push_fx(const int8_t* lr, uint32_t frames);

    void frame();   // one 1/50 s frame - called from the Timer A interrupt only

private:
    volatile bool playing;
    bool relocated, initialised;
    uint8_t saved_regs[14];

    static const uint32_t RING = 16384;   // bytes per channel, even (DSPMOD needs even addresses)
    static const uint32_t TARGET = 2048;  // how far ahead of the replay the writer aims to be
    uint32_t wr_abs, rd_abs, rd_prev;     // positions in samples since play()
    int base_period, period;

    bool init_once();
};

extern DspMod dspmod;
