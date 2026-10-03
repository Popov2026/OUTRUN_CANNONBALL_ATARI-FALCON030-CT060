#pragma once

/***************************************************************************
    FM synthesis on the Falcon's DSP56001 (outrun.ini's fm_dsp).

    The FM chip (YM2151) is the most expensive part of the sound: on a 68030
    its emulation alone costs more than a game step. With fm_dsp = 1 the DSP
    computes the FM sound instead, exactly as the game's own emulation would
    (src/main/hwaudio/ym2151.cpp): the DSP program (tools/fmdsp/fm_dsp.asm,
    built into atari/fm_dsp_p56.h) gives the same samples, bit for bit - see
    tools/fmdsp/ for how this was checked.

    The 68k keeps its YM2151 object for what the game reads back (timers,
    status) and to decode the register writes: after each write, what it
    changed (operator frequency, level, envelope rates; channel algorithm,
    feedback, pan; key on/off) is queued as events, and each game step the
    queue is sent through the host port, followed by a "render" command. The
    DSP answers with the samples of the PREVIOUS step and then computes this
    one while the 68k gets on with the game: the FM sound comes one game
    step late, so audio.cpp delays the PCM sound by one step as well, to keep
    the music's drums with its melody.

    The program is (re)started when the game resets the sound chips
    (OSoundInt::init(), via atari_fm_restart() in audio.cpp), so that the
    DSP and the 68k's chip start from the same state; from then on the DSP's
    output is the same as the 68k's emulation, step for step (checked with
    -DFMDSP_VERIFY, which has the 68k synthesise the FM too and compares).

    The DSP is shared with the DSP .mod players (mod_dsp = 1 or 2): those call
    stop() before they take it, and audio.cpp starts the FM program again once
    the DSP is free (the FM sound is computed by the 68k in between).

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

#include "../stdint.hpp"

class FmDsp
{
public:
    FmDsp() : running(false), broken(false), full_sync(false), dirty_ops(0), dirty_ch(0), prev_frames(0), queued(0) {}
    // Loads the DSP program and sends it the YM2151 tables. False if there is no DSP, it is
    // in use or does not answer (then never tried again).
    bool start();
    // Hands the DSP back. The FM sound is the 68k's again.
    void stop();
    bool active() const { return running; }

    // From YM2151::write_reg(): register r was just written with v.
    void reg_written(int r, int v);

    // One game step of `frames` stereo frames: sends what changed and the render command.
    // Returns the previous step's samples (16-bit, L, R interleaved), or 0 (silence: no previous
    // step, a silent one, or the DSP stopped answering - then stopped).
    const int16_t* step(uint32_t frames);

    static const uint32_t MAX_FRAMES = 512;   // the DSP's output buffers

private:
    bool running;
    bool broken;
    bool full_sync;              // next flush sends every operator and channel
    uint32_t dirty_ops;          // operators touched since the last flush (bit = operator)
    uint32_t dirty_ch;           // channels touched (bit = channel)
    uint32_t prev_frames;        // frames of the step being computed by the DSP

    static const int QUEUE = 2048;
    uint32_t queue[QUEUE];       // events waiting for the next step (24-bit words)
    int queued;
    int16_t out[MAX_FRAMES * 2];

    void flush();
    void put(uint32_t w);
    bool send_queue();
    void fail();
};

extern FmDsp fmdsp;
