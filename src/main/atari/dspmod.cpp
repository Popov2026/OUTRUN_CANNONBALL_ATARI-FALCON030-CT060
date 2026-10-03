/***************************************************************************
    .mod replay with DSPMOD 3.4 - see dspmod.hpp.

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

#include <cstring>
#include "atari/dspmod.hpp"

DspMod dspmod;

#ifdef __MINT__
#include <mint/osbind.h>
#include <mint/cookie.h>
#include "dspmod_tce.h"

extern "C" uint32_t dspmod_call(void* entry, uint32_t* regs);   // dspmod_asm.S
extern "C" void dspmod_isr();
extern "C" void dspmod_frame() { dspmod.frame(); }

#define REG8(a) (*(volatile unsigned char*)(a))

// Entry points and variables, as offsets into dspmod.tce (DSPMOD manual, docs/dspmod.txt).
enum { DM_INIT = 28, DM_OFF = 32, DM_PLAYER_ON = 36, DM_PLAYER_OFF = 40, DM_PLAY_MUSIC = 44,
       DM_MOD_TYPE = 52, DM_FX = 56, DM_DSP_TRACKS = 64, DM_SAMPLE_SETS = 70 };
// "Already relocated" flag tested by DSPMOD's own relocation routine, offset from its text start.
static const uint32_t DM_RELOCATED_FLAG = 0x12ab;

// One voice's state inside DSPMOD (28 bytes, manual "SampleSets").
struct DmVoice
{
    uint32_t ptr, end, rep_start, rep_len, frac;
    int16_t period, vol, main_vol, pos;
};

static const unsigned long SOUND_REGS[14] = {
    0xffff8930L, 0xffff8931L, 0xffff8932L, 0xffff8933L, 0xffff8934L, 0xffff8935L, 0xffff8936L,
    0xffff8937L, 0xffff8920L, 0xffff8921L, 0xffff8900L, 0xffff8901L, 0xffff8938L, 0xffff8939L
};

// Volumes passed to PlayerOn: the four module voices, then the four fx voices (0..$7fff).
static int16_t vol_tab[8] = { 0x6000, 0x6000, 0x6000, 0x6000, 0x7fff, 0x7fff, 0x7fff, 0x7fff };

// The game's FM + PCM mix, one looping buffer per side (fx voices 0 and 1 = DSPMOD voices 4, 5).
static int8_t ring_l[16384] __attribute__((aligned(4)));
static int8_t ring_r[16384] __attribute__((aligned(4)));

static void (*old_timer_a)() = 0;

static uint32_t call(int offset, uint32_t d0 = 0, uint32_t d1 = 0, uint32_t d2 = 0, uint32_t d3 = 0,
                     const void* a0 = 0, const void* a1 = 0, const void* a2 = 0, uint32_t a3 = 0)
{
    uint32_t regs[8] = { d0, d1, d2, d3, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, a3 };
    return dspmod_call(dspmod_tce + offset, regs);
}

static DmVoice* voices() { return *(DmVoice**)(dspmod_tce + DM_SAMPLE_SETS); }

// Writes back the data cache and empties the instruction cache, so code just patched in
// memory is what gets executed (a 68060 with copyback Fast RAM would otherwise run the
// unpatched bytes still sitting in memory).
static void flush_caches()
{
    long cpu = 0;
    Getcookie(C__CPU, &cpu);
    if (cpu >= 40)
        __asm__ volatile (".word 0xf4f8" ::: "memory");   // cpusha bc
    else if (cpu >= 20)
        __asm__ volatile ("movec %%cacr,%%d0\n\t"
                          "or.w #0x0808,%%d0\n\t"         // clear instruction and data caches
                          "movec %%d0,%%cacr" ::: "d0", "memory");
}

// DSPMOD relocates itself on its first init(), then carries on running the code it has just
// patched - unsafe with a copyback cache. The same relocation (TOS format: first offset as a
// long, then byte steps, 1 = skip 254) is done here instead, the caches flushed, and DSPMOD's
// own "already relocated" flag set so its routine does nothing.
static bool relocate()
{
    const uint8_t* h = dspmod_tce;
    if (h[0] != 0x60 || h[1] != 0x1a) return false;
    const uint32_t tlen = (h[2] << 24) | (h[3] << 16) | (h[4] << 8) | h[5];
    const uint32_t dlen = (h[6] << 24) | (h[7] << 16) | (h[8] << 8) | h[9];
    const uint32_t slen = (h[14] << 24) | (h[15] << 16) | (h[16] << 8) | h[17];
    uint8_t* text = dspmod_tce + 28;
    const uint8_t* rel = text + tlen + dlen + slen;
    const uint8_t* rel_end = dspmod_tce + sizeof(dspmod_tce);
    if (rel + 4 > rel_end || DM_RELOCATED_FLAG >= tlen + dlen) return false;
    const uint32_t base = (uint32_t)text;
    uint32_t first = (rel[0] << 24) | (rel[1] << 16) | (rel[2] << 8) | rel[3];
    rel += 4;
    if (first)
    {
        uint8_t* p = text + first;
        *(uint32_t*)p += base;
        while (rel < rel_end && *rel)
        {
            if (*rel == 1) p += 254;
            else { p += *rel; *(uint32_t*)p += base; }
            rel++;
        }
    }
    text[DM_RELOCATED_FLAG] = 0xff;
    flush_caches();
    return true;
}

// Relocates DSPMOD, saves the sound registers, runs its init (loads its DSP program, switches
// the matrix to DSP -> DAC) and starts Timer A.
bool DspMod::init_once()
{
    if (initialised) return true;
    if (!relocated)
    {
        if (!relocate()) return false;
        relocated = true;
    }
    for (int i = 0; i < 14; i++) saved_regs[i] = REG8(SOUND_REGS[i]);
    REG8(0xffff8901L) = 0;          // stop our own DMA playback
    call(DM_INIT);

    // Timer A: 2457600 / 64 / 192 = 200 Hz, divided by four in the interrupt stub.
    REG8(0xfffffa19L) = 0;
    old_timer_a = *(void (**)())0x134L;
    *(void (**)())0x134L = dspmod_isr;
    REG8(0xfffffa07L) |= 0x20;
    REG8(0xfffffa13L) |= 0x20;
    REG8(0xfffffa1fL) = 192;
    REG8(0xfffffa19L) = 5;
    initialised = true;
    return true;
}

bool DspMod::play(uint8_t* mod, uint32_t rate)
{
    stop_song();
    // DSPMOD knows "M.K.", "FLT4", "CD8 ", "CD81", "FA08". "M!K!" is the same 4-voice format
    // with more than 64 patterns: renamed so DSPMOD takes it.
    if (!memcmp(mod + 1080, "M!K!", 4)) memcpy(mod + 1080, "M.K.", 4);
    if (call(DM_MOD_TYPE, 0, 0, 0, 0, mod) != 4) return false;   // 4-voice modules only (2 fx voices needed)
    if (!init_once()) return false;

    call(DM_PLAYER_ON, 0, 0, 0, 0, mod, vol_tab);
    *(int16_t*)(dspmod_tce + DM_DSP_TRACKS) = 6;   // 4 module voices + 2 fx voices

    std::memset(ring_l, 0, sizeof(ring_l));
    std::memset(ring_r, 0, sizeof(ring_r));
    base_period = (int)((3546895 + rate / 2) / rate);   // Amiga PAL clock
    period = base_period;
    // fx(channel, period, volume 0..64, stereo position -63..63, start, end, repeat start, repeat length)
    call(DM_FX, 0, period, 64, (uint32_t)-63, ring_l, ring_l + RING, ring_l, RING);
    call(DM_FX, 1, period, 64, 63,            ring_r, ring_r + RING, ring_r, RING);
    rd_abs = rd_prev = 0;
    wr_abs = TARGET;
    playing = true;
    return true;
}

void DspMod::stop_song()
{
    if (!playing) return;
    playing = false;   // the interrupt stops calling PlayMusic first
    call(DM_PLAYER_OFF);
}

void DspMod::shutdown()
{
    stop_song();
    if (!initialised) return;
    REG8(0xfffffa19L) = 0;
    REG8(0xfffffa07L) &= ~0x20;
    REG8(0xfffffa13L) &= ~0x20;
    *(void (**)())0x134L = old_timer_a;
    call(DM_OFF);
    for (int i = 0; i < 14; i++) REG8(SOUND_REGS[i]) = saved_regs[i];
    REG8(0xffff8901L) = 0;
    initialised = false;
}

void DspMod::frame()
{
    if (!playing) return;
    call(DM_PLAY_MUSIC);
}

// Writes one game step into the two rings just ahead of where DSPMOD reads, clears what it has
// already played (so a late writer is heard as silence, not as an echo of old sound), and
// nudges the fx voices' period so the distance between reader and writer stays near TARGET.
void DspMod::push_fx(const int8_t* lr, uint32_t frames)
{
    if (!playing) return;
    DmVoice* v = voices();
    uint32_t rd = v[4].ptr - (uint32_t)ring_l;
    if (rd >= RING) rd = 0;
    uint32_t done = (rd - rd_prev) & (RING - 1);
    for (uint32_t i = rd_prev; i != rd; i = (i + 1) & (RING - 1)) { ring_l[i] = 0; ring_r[i] = 0; }
    rd_prev = rd;
    rd_abs += done;

    if (wr_abs < rd_abs || wr_abs - rd_abs > RING - frames - 2)
        wr_abs = rd_abs + TARGET;   // fell behind or too far ahead: start again at the target distance
    for (uint32_t f = 0; f < frames; f++)
    {
        const uint32_t i = (wr_abs + f) & (RING - 1);
        ring_l[i] = lr[f * 2];
        ring_r[i] = lr[f * 2 + 1];
    }
    wr_abs += frames;

    // Ahead by more than the target: play the fx a little faster (smaller period), and the
    // other way round. 1 period unit per 512 samples away, within +-6 %.
    const int32_t level = (int32_t)(wr_abs - rd_abs);
    int p = base_period - (level - (int32_t)TARGET) / 512;
    const int span = base_period * 6 / 100 + 1;
    if (p < base_period - span) p = base_period - span;
    if (p > base_period + span) p = base_period + span;
    if (p != period) { period = p; v[4].period = (int16_t)p; v[5].period = (int16_t)p; }
}

void DspMod::voice_state(int i, uint32_t out[8]) const
{
    const DmVoice& v = voices()[i];
    out[0] = v.ptr; out[1] = v.end; out[2] = v.rep_start; out[3] = v.rep_len;
    out[4] = (uint16_t)v.period; out[5] = (uint16_t)v.vol; out[6] = (uint16_t)v.main_vol; out[7] = (uint16_t)v.pos;
}

#else
void DspMod::voice_state(int, uint32_t out[8]) const { for (int i = 0; i < 8; i++) out[i] = 0; }
bool DspMod::play(uint8_t*, uint32_t) { return false; }
void DspMod::stop_song() {}
void DspMod::shutdown() {}
void DspMod::frame() {}
void DspMod::push_fx(const int8_t*, uint32_t) {}
#endif
