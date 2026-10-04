/***************************************************************************
    .mod replay on the Falcon's DSP56001 - 68k side. See dsp_replay.hpp.

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

#include <cstring>
#include "atari/dsp_replay.hpp"
#include "atari/modplayer.hpp"
#include "frontend/config.hpp"

#ifdef __MINT__
#include <mint/osbind.h>
#include <mint/falcon.h>
#include "dsp_tracker_p56.h"

extern "C" void dsp_replay_isr();
extern "C" void dsp_replay_frame();

struct dsp_host { volatile unsigned char icr, cvr, isr, ivr, unused, high, mid, low; };
#define HOST ((dsp_host*)0xffffa200L)
#define ISR_RXDF 1
#define ISR_TXDE 2

#define REG8(a)  (*(volatile unsigned char*)(a))
#define REG16(a) (*(volatile unsigned short*)(a))

// Output samples computed per 1/50 s frame at 49170 Hz - Simplet's own figure for 50 Hz.
static const uint32_t FRAME_SAMPLES = 984;
// Volumes are 24-bit fractions. Each side adds two module voices and one effects voice:
// 2 x 0.25 + 0.5 cannot overflow.
static const uint32_t VOL_VOICE_STEP = 0x200000 / 64;   // per ProTracker volume unit (0..64)
static const uint32_t VOL_FX         = 0x400000;

static const unsigned long SOUND_REGS[14] = {
    0xffff8930L, 0xffff8931L, 0xffff8932L, 0xffff8933L, 0xffff8934L, 0xffff8935L, 0xffff8936L,
    0xffff8937L, 0xffff8920L, 0xffff8921L, 0xffff8900L, 0xffff8901L, 0xffff8938L, 0xffff8939L
};

// The effects queue is filled in bursts (a few game steps, then nothing while the picture is
// drawn) and emptied steadily. Worse, when the machine cannot run the game's 30 steps a second
// the queue receives less than real time (measured: 27.5 steps/s on the emulated 68060 in
// heavy scenes, 9 % short) - at a fixed read rate it then ran dry several times a second, each
// time a short hole in the sound effects. So playing starts once FX_TARGET samples are waiting,
// and the read rate follows the smoothed level of the queue: 0.2 per mille per sample away
// from the target, between -25 % and +5 %. The effects then play slightly lower when the game
// itself runs slow, instead of breaking up. The music is not affected: it has its own voices.
static const int32_t FX_TARGET = 1600;

static void (*old_timer_a)() = 0;
static bool fx_playing = false;
static uint32_t fx_rate = 0;        // read rate of the effects queue for the current frame
static int32_t fx_level_avg = 0;    // level of the queue, smoothed over about 16 frames
static uint32_t last_hz200 = 0;

// A DSP that stops answering must not hang the game inside an interrupt: every wait is bounded.
static inline bool wait_tx()
{
    for (int i = 0; i < 20000; i++)
        if (HOST->isr & ISR_TXDE) return true;
    return false;
}

// Sends one 24-bit word to the DSP through the host port. False if the DSP did not take it in time.
static inline bool host_send(uint32_t v)
{
    if (!wait_tx()) return false;
    HOST->high = (unsigned char)(v >> 16);
    HOST->mid  = (unsigned char)(v >> 8);
    HOST->low  = (unsigned char)v;
    return true;
}

// Receives one 24-bit word from the DSP through the host port. False if none came in time.
static inline bool host_recv(uint32_t* v)
{
    for (int i = 0; i < 20000; i++)
        if (HOST->isr & ISR_RXDF)
        {
            *v = ((uint32_t)HOST->high << 16) | ((uint32_t)HOST->mid << 8) | HOST->low;
            return true;
        }
    return false;
}
#endif

DspReplay dsp_replay;

// See dsp_replay.hpp. Order: lock the DSP, save the sound registers, switch the sound matrix,
// load the DSP program, check that it answers, then start the Timer A interrupt.
bool DspReplay::start()
{
#ifdef __MINT__
    if (running) return true;
    if (Dsp_Lock() != 0) return false;

    for (int i = 0; i < 14; i++) saved_regs[i] = REG8(SOUND_REGS[i]);

    // Same sound matrix as Simplet's Init_Sound: DAC fed by the DSP, 49170 Hz.
    REG8(0xffff8901L)  = 0;        // DMA playback off
    REG8(0xffff8920L)  = 0x0f;
    REG16(0xffff8930L) = 0x0091;   // DSP transmit and DMA playback on the internal 25.175 MHz clock, DSP connected
    REG16(0xffff8932L) = 0x2213;   // DAC, DMA record and external output listen to the DSP
    REG8(0xffff8935L)  = 1;        // 49170 Hz
    REG8(0xffff8937L)  = 2;        // DAC gets the matrix only

    Dsp_ExecProg((char*)dsp_tracker_p56, (long)DSP_TRACKER_P56_WORDS, Dsp_RequestUniqueAbility());

    // The program answers the first word it is sent with 12345678.
    uint32_t answer = 0;
    if (!host_send(87654321UL & 0xffffff) || !host_recv(&answer) || answer != 12345678UL)
    {
        for (int i = 0; i < 14; i++) REG8(SOUND_REGS[i]) = saved_regs[i];
        Dsp_Unlock();
        return false;
    }

    fifo_head = fifo_tail_l = fifo_tail_r = 0;
    fx_freq = (uint32_t)(((uint64_t)config.sound.rate << 23) / 49170);
    fx_playing = false;
    stat_underruns = stat_late = 0;
    last_hz200 = *(volatile uint32_t*)0x4baL;
    dead = false;
    running = true;

    // Timer A: 2457600 / 64 / 192 = 200 Hz, divided by four in the interrupt stub.
    REG8(0xfffffa19L) = 0;
    old_timer_a = *(void (**)())0x134L;
    *(void (**)())0x134L = dsp_replay_isr;
    REG8(0xfffffa07L) |= 0x20;
    REG8(0xfffffa13L) |= 0x20;
    REG8(0xfffffa1fL) = 192;
    REG8(0xfffffa19L) = 5;
    return true;
#else
    return false;
#endif
}

// Stops the Timer A interrupt, tells the DSP program to stop its output, puts the sound
// registers back as they were found and releases the DSP. Safe to call when not running.
void DspReplay::stop()
{
#ifdef __MINT__
    if (!running) return;
    REG8(0xfffffa19L) = 0;
    REG8(0xfffffa07L) &= ~0x20;
    REG8(0xfffffa13L) &= ~0x20;
    *(void (**)())0x134L = old_timer_a;
    running = false;

    HOST->cvr = 0x80 + 0x28 / 2;   // host command, vector $28: the program turns its output off
    for (int i = 0; i < 14; i++) REG8(SOUND_REGS[i]) = saved_regs[i];
    REG8(0xffff8901L) = 0;         // the saved value may have had a DMA buffer playing
    Dsp_Unlock();
#endif
}

// Main-loop side of the effects queue: appends one game step of the FM + PCM mix. Never
// blocks; when the queue is full the samples that do not fit are dropped.
void DspReplay::push_fx(const int8_t* lr, uint32_t frames)
{
    if (!running) return;
    uint32_t head = fifo_head;
    // The slower reader of the two sides bounds the room; a full queue drops the new samples.
    uint32_t used_l = head - fifo_tail_l, used_r = head - fifo_tail_r;
    uint32_t used = used_l > used_r ? used_l : used_r;
    if (used > FIFO_SIZE) used = FIFO_SIZE;
    uint32_t room = FIFO_SIZE - used;
    if (frames > room) frames = room;
    for (uint32_t i = 0; i < frames; i++)
    {
        fifo_l[head & (FIFO_SIZE - 1)] = lr[0];
        fifo_r[head & (FIFO_SIZE - 1)] = lr[1];
        lr += 2;
        head++;
    }
    fifo_head = head;
}

// kind 0: module channel `index`; kind 1: effects pair, index 0 = left, 1 = right.
bool DspReplay::send_voice(uint32_t vol, uint32_t freq, int kind, int index)
{
#ifdef __MINT__
    static int8_t fx_buf[1408];
    uint32_t n = 0;
    if (!host_send(vol) || !host_send(freq) || !host_recv(&n)) return false;
    if (n > 1390) n = 1390;   // a voice's buffer on the DSP holds 1400 samples

    const int8_t* p;
    if (kind == 0)
        p = modplayer.dsp_fetch(index, n);
    else
    {
        // Take n samples from the queue; when it has run dry (sound muted by the main loop, or
        // the game running slower than real time) the frame is silence until it has refilled.
        const int8_t* fifo = index ? fifo_r : fifo_l;
        uint32_t tail = index ? fifo_tail_r : fifo_tail_l;
        uint32_t avail = fx_playing ? fifo_head - tail : 0;
        uint32_t take = n < avail ? n : avail;
        uint32_t i = 0;
        for (; i < take; i++) fx_buf[i] = fifo[(tail + i) & (FIFO_SIZE - 1)];
        for (; i < n + 6; i++) fx_buf[i] = 0;
        if (fx_playing && take < n && index == 0) stat_underruns++;
        if (index) fifo_tail_r = tail + take; else fifo_tail_l = tail + take;
        p = fx_buf;
    }

    // Six samples per packet, one more packet than needed (same as the original 68030 code:
    // the DSP reads one sample past the end of what the frame consumes).
    uint32_t packets = n / 6 + 1;
    if (!host_send(packets)) return false;
    const unsigned char* b = (const unsigned char*)p;
    for (uint32_t i = packets * 2; i; i--)
    {
        if (!wait_tx()) return false;
        HOST->high = b[0];
        HOST->mid  = b[1];
        HOST->low  = b[2];
        b += 3;
    }
    return true;
#else
    return false;
#endif
}

// One 1/50 s frame, from the Timer A interrupt: tells the DSP how many output samples to
// compute, then sends each of the six voices (volume, pitch, and the source bytes the DSP
// asks for), and finally advances the song.
void DspReplay::frame()
{
#ifdef __MINT__
    if (!running || dead) return;

    // Order expected by the DSP: left, right, then each extra pair left, right.
    static const int CHANNEL_ORDER[4] = { 0, 1, 3, 2 };
    uint32_t vol[4], freq[4], top = 1;
    for (int v = 0; v < 4; v++)
    {
        modplayer.dsp_params(CHANNEL_ORDER[v], &vol[v], &freq[v]);
        if (freq[v] > top) top = freq[v];
    }

    // Effects queue: start once it has a cushion, stop when it is empty, steer its read rate.
    const int32_t level = (int32_t)(fifo_head - fifo_tail_l);
    if (!fx_playing && level >= FX_TARGET) { fx_playing = true; fx_level_avg = level; }
    else if (fx_playing && level == 0) fx_playing = false;
    fx_level_avg += (level - fx_level_avg) / 16;
    int32_t adj = (fx_level_avg - FX_TARGET) / 5;   // per mille
    if (adj < -250) adj = -250;
    if (adj > 50) adj = 50;
    fx_rate = (uint32_t)((int32_t)fx_freq + (int32_t)(fx_freq / 1000) * adj);
    if (fx_rate > top) top = fx_rate;

    // Normally one frame every four ticks of the 200 Hz system clock. If interrupts were held
    // off long enough for a frame to be missed, the DSP has already played into stale data;
    // computing the missed frames now as well stops the same hole from being heard again one
    // lap of its buffer later. Bounded by the DSP's per-voice buffer (1400 source bytes for the
    // fastest voice) and its 3700-sample ring.
    const uint32_t now = *(volatile uint32_t*)0x4baL;
    uint32_t frames = (now - last_hz200 + 2) / 4;
    last_hz200 = now;
    if (frames < 1) frames = 1;
    if (frames > 1) stat_late++;
    if (frames > 3) frames = 3;
    uint32_t length = FRAME_SAMPLES * frames;
    const uint32_t limit = (uint32_t)(((uint64_t)1380 << 23) / top);
    if (length > limit) length = limit;
    if (length > 3000) length = 3000;
    if (length < FRAME_SAMPLES) { length = FRAME_SAMPLES; frames = 1; }

    HOST->cvr = 0x80 + 0x26 / 2;   // host command, vector $26: the replay routine
    bool ok = host_send(length) && host_send(2);   // two extra pairs of voices
    for (int v = 0; ok && v < 4; v++)
        ok = send_voice(vol[v] * VOL_VOICE_STEP, freq[v], 0, CHANNEL_ORDER[v]);
    ok = ok && send_voice(VOL_FX, fx_rate, 1, 0) && send_voice(VOL_FX, fx_rate, 1, 1);
    if (!ok) { dead = true; return; }

    for (uint32_t f = 0; f < frames; f++) modplayer.dsp_tick();
#endif
}

#ifdef __MINT__
#ifdef AUDIO_TIMING
extern uint32_t g_t_replay; uint32_t atari_fine_time();
extern "C" void dsp_replay_frame() { const uint32_t t = atari_fine_time(); dsp_replay.frame(); g_t_replay += atari_fine_time() - t; }
#else
extern "C" void dsp_replay_frame() { dsp_replay.frame(); }
#endif
#endif
