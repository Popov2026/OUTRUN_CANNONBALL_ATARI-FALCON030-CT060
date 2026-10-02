#pragma once

/***************************************************************************
    .mod replay on the Falcon's DSP56001 - 68k side.

    The DSP side is the SoundTracker DSP replay by Simplet / ABSTRACT (1994),
    used unmodified (dsp_tracker_p56.h, from the "dsptrack" archive on
    dhs.nu). It is a streaming mixer: every 1/50 s the 68k tells the DSP how
    many output samples to compute, then for each voice sends a volume and a
    pitch, is told how many source bytes that voice needs, and sends them.
    Nothing is stored on the DSP beyond one frame, so the size of the module
    does not matter (the earlier home-made mixer uploaded every sample to
    the DSP and could not hold a real module).

    Six voices are streamed: the module's four (Amiga panning: 0 and 3 left,
    1 and 2 right) plus one stereo pair that carries the game's own FM + PCM
    mix, which Audio::tick() queues here instead of handing it to the DMA -
    the DAC listens to the DSP only while this is active, so that pair is
    how sound effects stay audible.

    Driven from MFP Timer A (dsp_replay_asm.S), so the music keeps its tempo
    whatever the picture rate is.

    Verified in Hatari's DSP emulation only (the output samples the DSP
    hands to the DAC were captured and checked); enabled by mod_dsp=1.

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

#include "../stdint.hpp"

class DspReplay
{
public:
    // Loads the DSP program, switches the sound matrix to DSP -> DAC and starts Timer A.
    // Returns false (and changes nothing) if the DSP is not available or does not answer.
    bool start();
    void stop();
    bool active() const { return running; }

    // One game step of the FM + PCM mix: `frames` interleaved L,R signed 8-bit samples at the
    // engine's mix rate. Called from Audio::tick(); played by the extra voice pair.
    void push_fx(const int8_t* lr, uint32_t frames);

    void frame();   // one 1/50 s frame - called from the Timer A interrupt only

    // Statistics (-DDSPFX_LOG builds log them): frames in which the effects queue ran dry
    // while playing, and frames that arrived late enough for another one to have been missed.
    volatile uint32_t stat_underruns, stat_late;
    uint32_t fx_level() const { return fifo_head - fifo_tail_l; }

private:
    volatile bool running;
    volatile bool dead;      // the DSP stopped answering: frames are skipped from then on

    static const uint32_t FIFO_SIZE = 4096;   // power of two
    int8_t fifo_l[FIFO_SIZE], fifo_r[FIFO_SIZE];
    volatile uint32_t fifo_head;              // written by push_fx()
    volatile uint32_t fifo_tail_l, fifo_tail_r;   // read by the interrupt
    uint32_t fx_freq;

    uint8_t saved_regs[14];

    bool send_voice(uint32_t vol, uint32_t freq, int kind, int index);
};

extern DspReplay dsp_replay;
