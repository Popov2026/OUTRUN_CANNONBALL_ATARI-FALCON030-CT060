/***************************************************************************
    FM synthesis on the DSP56001 - see fmdsp.hpp.

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

#include <cstring>
#include "atari/fmdsp.hpp"

FmDsp fmdsp;

#ifdef __MINT__
#include <mint/osbind.h>
#include <mint/falcon.h>
#include <mint/cookie.h>
#include "hwaudio/ym2151.hpp"
#include "atari/fm_dsp_p56.h"
#include <cstdio>

// The emulation's state (hwaudio/ym2151.cpp) and the tables the DSP program needs.
extern YM2151Operator oper[32];
extern uint32_t pan[16];
extern uint8_t connects[8];
extern void (*ym2151_write_hook)(int r, int v);
const signed int*   ym2151_tl_tab();
const unsigned int* ym2151_sin_tab();
const uint8_t*      ym2151_eg_inc();
uint32_t ym2151_eg_timer_add();
uint32_t ym2151_eg_timer_overflow();

// DSP host port: status (bit 0 a word to read, bit 1 room to write); a long written at
// $FFA204 sends its low 24 bits; a word read at $FFA206 takes the low 16 bits of a word.
#define HOST_ISR  (*(volatile uint8_t*)0xffffa202L)
#define HOST_TX   (*(volatile uint32_t*)0xffffa204L)
#define HOST_RX16 (*(volatile int16_t*)0xffffa206L)

static inline uint32_t hz200() { return *(volatile uint32_t*)0x4BAL; }   // supervisor mode

// Waits for a host port status bit, at most 200 ms.
static bool wait_isr(uint8_t bit)
{
    if (HOST_ISR & bit) return true;
    const uint32_t t0 = hz200();
    while (!(HOST_ISR & bit))
        if (hz200() - t0 > 40) return false;
    return true;
}

// What the DSP has been told so far about each operator and channel: only changes are sent.
struct OpSnap { uint32_t freq, tl, d1l; uint8_t sh[4], sel[4]; };
struct ChSnap { uint32_t con, fb_shift, pan; };
static OpSnap sent_op[32];
static ChSnap sent_ch[8];

static OpSnap op_snap(int i)
{
    const YM2151Operator& o = oper[i];
    OpSnap s;
    s.freq = o.freq; s.tl = o.tl; s.d1l = o.d1l;
    s.sh[0] = o.eg_sh_ar;  s.sel[0] = o.eg_sel_ar;
    s.sh[1] = o.eg_sh_d1r; s.sel[1] = o.eg_sel_d1r;
    s.sh[2] = o.eg_sh_d2r; s.sel[2] = o.eg_sel_d2r;
    s.sh[3] = o.eg_sh_rr;  s.sel[3] = o.eg_sel_rr;
    return s;
}

static void write_hook(int r, int v) { fmdsp.reg_written(r, v); }

bool FmDsp::start()
{
    if (running) return true;
    if (broken) return false;
    long snd = 0;
    if (Getcookie(C__SND, &snd) != C_FOUND || !(snd & SND_DSP) || Dsp_Lock() != 0)
    {
        broken = true;
        return false;
    }
    Dsp_ExecProg((char*)fm_dsp_p56, (long)FM_DSP_P56_WORDS, Dsp_RequestUniqueAbility());

    // set-up event: the tables, then the envelope timing; the program answers $FA5E00
    queued = 0;
    put(0x050000);
    const unsigned int* sin_tab = ym2151_sin_tab();
    const signed int* tl_tab = ym2151_tl_tab();
    const uint8_t* eg_inc = ym2151_eg_inc();
    bool ok = send_queue();
    for (int i = 0; ok && i < 1024; i++) { ok = wait_isr(2); HOST_TX = sin_tab[i]; }
    for (int i = 0; ok && i < 6656; i++) { ok = wait_isr(2); HOST_TX = (uint32_t)tl_tab[i] & 0xffffff; }
    for (int i = 0; ok && i < 152; i++)  { ok = wait_isr(2); HOST_TX = eg_inc[i]; }
    if (ok) { ok = wait_isr(2); HOST_TX = ym2151_eg_timer_add(); }
    if (ok) { ok = wait_isr(2); HOST_TX = ym2151_eg_timer_overflow(); }
    if (ok && wait_isr(1))
    {
        const uint32_t ack = ((uint32_t)*(volatile uint8_t*)0xffffa205L << 16) | (uint16_t)HOST_RX16;
        ok = ack == 0xfa5e00;
    }
    else ok = false;
    if (!ok)
    {
        Dsp_Unlock();
        broken = true;
        return false;
    }
    prev_frames = 0;
    full_sync = true;
    dirty_ops = 0xffffffff;
    dirty_ch = 0xff;
    running = true;
    ym2151_write_hook = write_hook;
    return true;
}

void FmDsp::stop()
{
    if (!running) return;
    ym2151_write_hook = 0;
    running = false;
    queued = 0;
    Dsp_Unlock();
}

void FmDsp::fail()
{
    stop();
    broken = true;
}

void FmDsp::put(uint32_t w)
{
    if (queued == QUEUE && !send_queue()) return;
    queue[queued++] = w;
}

// Sends the queued events. Waits while the DSP is still computing the previous step.
bool FmDsp::send_queue()
{
    for (int i = 0; i < queued; i++)
    {
        if (!wait_isr(2)) { fail(); return false; }
        HOST_TX = queue[i];
    }
    queued = 0;
    return true;
}

#ifdef FMDSP_LOG
#include <cstdio>
static uint32_t g_steps = 0;
#endif
void FmDsp::reg_written(int r, int v)
{
    if (!running) return;
#ifdef FMDSP_LOG
    // Test aid: the writes to what the DSP program leaves out (LFO, noise, PMS/AMS, CSM)
    if (r == 0x01 || r == 0x0f || r == 0x14 || r == 0x18 || r == 0x19 || r == 0x1b || (r >= 0x38 && r < 0x40) || ((r & 0xe0) == 0xa0 && (v & 0x80)))
    {
        FILE* f = fopen("FMDSP.TXT", "a");
        if (f) { fprintf(f, "step %lu reg %02x = %02x\r\n", (unsigned long)g_steps, r, v); fclose(f); }
    }
#endif
    if (r == 8)
    {
        // key on/off: whatever changed before it must reach the DSP first
        flush();
        put(0x030000 | (v & 0xff));
    }
    else if (r >= 0x40)
        dirty_ops |= 1u << ((r & 7) * 4 + ((r & 0x18) >> 3));   // operator registers
    else if (r >= 0x28)
        dirty_ops |= 0xfu << ((r & 7) * 4);                       // key code / fraction: the channel's 4
    else if (r >= 0x20)
        dirty_ch |= 1u << (r & 7);                                // pan, feedback, algorithm
}

// Queues the parameters of the operators and channels that changed (events $01 and $02,
// see tools/fmdsp/fm_dsp.asm).
void FmDsp::flush()
{
    for (int i = 0; dirty_ops; i++, dirty_ops >>= 1)
    {
        if (!(dirty_ops & 1)) continue;
        const OpSnap s = op_snap(i);
        if (!full_sync && !memcmp(&s, &sent_op[i], sizeof(s))) continue;
        sent_op[i] = s;
        put(0x010000 | i);
        put(s.freq >> 16);
        put(s.freq & 0xffff);
        put(s.tl & 0xffffff);
        put(s.d1l & 0xffffff);
        for (int r = 0; r < 4; r++) put((s.sh[r] << 8) | s.sel[r]);
    }
    for (int c = 0; dirty_ch; c++, dirty_ch >>= 1)
    {
        if (!(dirty_ch & 1)) continue;
        const ChSnap s = { connects[c], oper[c * 4].fb_shift, (pan[c * 2] ? 1u : 0u) | (pan[c * 2 + 1] ? 2u : 0u) };
        if (!full_sync && !memcmp(&s, &sent_ch[c], sizeof(s))) continue;
        sent_ch[c] = s;
        put(0x020000 | c);
        put((s.con << 8) | s.fb_shift);
        put(s.pan);
    }
    full_sync = false;
}

const int16_t* FmDsp::step(uint32_t frames)
{
    if (!running) return 0;
#ifdef FMDSP_LOG
    g_steps++;
#endif
    if (frames > MAX_FRAMES) { fail(); return 0; }
    flush();
#ifdef UNDERRUN_LOG
    {
        // Test aid: steps with many register changes (a tune starting), in UNDR.TXT.
        static uint32_t n = 0;
        n++;
        if (queued > 60)
        {
            FILE* f = fopen("UNDR.TXT", "a");
            if (f) { fprintf(f, "fm step %lu: %d words of events\r\n", (unsigned long)n, queued); fclose(f); }
        }
    }
#endif
    put(0x040000 | frames);
    if (!send_queue()) return 0;
    // The DSP sends the previous step back straight away, then computes this one: first how
    // many words follow (none when that step was silent), then the samples.
    if (!prev_frames) { prev_frames = frames; return 0; }
    const uint32_t expect = prev_frames * 2;
    prev_frames = frames;
    if (!wait_isr(1)) { fail(); return 0; }
    const uint32_t n = (uint16_t)HOST_RX16;
    if (n == 0) return 0;
    if (n != expect) { fail(); return 0; }
    for (uint32_t i = 0; i < n; i++)
    {
        if (!(HOST_ISR & 1) && !wait_isr(1)) { fail(); return 0; }
        out[i] = HOST_RX16;
    }
    return out;
}

#else
// Host builds (tools, tests): no DSP.
bool FmDsp::start() { return false; }
void FmDsp::stop() {}
void FmDsp::reg_written(int, int) {}
const int16_t* FmDsp::step(uint32_t) { return 0; }
#endif
