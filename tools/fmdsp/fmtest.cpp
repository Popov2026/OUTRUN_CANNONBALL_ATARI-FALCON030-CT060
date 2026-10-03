/***************************************************************************
    FMTEST.TOS - runs the DSP FM program (src/main/atari/fm_dsp_p56.h) on a
    Falcon (or Hatari with --dsp emu) and records what it computes.

    Loads the program, sends it the YM2151 tables, then replays EVENTS.BIN (the
    event stream written by tools/fmdsp/fmmodel on the host) exactly as the game
    will: each render command is answered with the previous game step's samples,
    which are appended to FMOUT.RAW (16-bit L,R, big-endian). The result is
    compared with fmmodel's output on the host. Timing goes to FMTEST.TXT.

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

#include <cstdio>
#include <mint/osbind.h>
#include <mint/falcon.h>
#include "hwaudio/ym2151.hpp"
#include "atari/fm_dsp_p56.h"

const signed int*   ym2151_tl_tab();
const unsigned int* ym2151_sin_tab();
const uint8_t*      ym2151_eg_inc();
uint32_t ym2151_eg_timer_add();
uint32_t ym2151_eg_timer_overflow();

struct dsp_host { volatile unsigned char icr, cvr, isr, ivr, unused, high, mid, low; };
#define HOST ((dsp_host*)0xffffa200L)

static bool send(uint32_t v)
{
    for (long i = 0; i < 2000000; i++)
        if (HOST->isr & 2) { HOST->high = v >> 16; HOST->mid = v >> 8; HOST->low = v; return true; }
    return false;
}
// Time in 1/38400 s: the 200 Hz counter and MFP Timer C's count (192 down to 1).
static uint32_t fine_time()
{
    volatile uint32_t* hz200 = (volatile uint32_t*)0x4BA;
    volatile unsigned char* tcdr = (volatile unsigned char*)0xfffffa23L;
    uint32_t a, b; unsigned char c;
    do { a = *hz200; c = *tcdr; b = *hz200; } while (a != b);
    return a * 192 + (192 - c);
}
static bool recv(uint32_t* v)
{
    for (long i = 0; i < 2000000; i++)
        if (HOST->isr & 1) { *v = ((uint32_t)HOST->high << 16) | ((uint32_t)HOST->mid << 8) | HOST->low; return true; }
    return false;
}

int main()
{
    long ssp = Super(0L);
    volatile uint32_t* hz200 = (volatile uint32_t*)0x4BA;
    FILE* log = fopen("FMTEST.TXT", "w");

    YM2151 ym(0.5f, 4000000);
    ym.init(12517, 30);   // builds the tables

    long lock = Dsp_Lock();
    Dsp_ExecProg((char*)fm_dsp_p56, (long)FM_DSP_P56_WORDS, Dsp_RequestUniqueAbility());
    bool ok = send(0x050000);
    const unsigned int* sin_tab = ym2151_sin_tab();
    const signed int* tl_tab = ym2151_tl_tab();
    const uint8_t* eg_inc = ym2151_eg_inc();
    for (int i = 0; ok && i < 1024; i++) ok = send(sin_tab[i]);
    for (int i = 0; ok && i < 6656; i++) ok = send((uint32_t)tl_tab[i] & 0xffffff);
    for (int i = 0; ok && i < 152; i++) ok = send(eg_inc[i]);
    if (ok) ok = send(ym2151_eg_timer_add());
    if (ok) ok = send(ym2151_eg_timer_overflow());
    uint32_t ack = 0;
    if (ok) ok = recv(&ack);
    if (log) { fprintf(log, "lock=%ld init sent=%d ack=%06lx\r\n", lock, (int)ok, (unsigned long)ack); fclose(log); log = fopen("FMTEST.TXT", "a"); }

    FILE* ev = fopen("EVENTS.BIN", "rb");
    FILE* out = fopen("FMOUT.RAW", "wb");
    FILE* tim = fopen("FMTIME.BIN", "wb");   // DSP time of each step, 1/38400 s, 16-bit big-endian
    uint32_t prev = 0, renders = 0, words = 0, last = 0;
    uint32_t dsp_sum = 0, dsp_max = 0, dsp_over = 0;   // DSP render time, 1/38400 s
    const uint32_t t0 = *hz200;
    static unsigned char samples[2 * 2 * 1024];
    while (ok && ev)
    {
        int b0 = fgetc(ev), b1 = fgetc(ev), b2 = fgetc(ev);
        if (b2 == EOF) break;
        const uint32_t w = ((uint32_t)b0 << 16) | (b1 << 8) | b2;
        ok = send(w);
        words++;
        if ((w >> 16) == 4)
        {
            for (uint32_t i = 0; ok && i < prev * 2; i++)
            {
                uint32_t v = 0;
                ok = recv(&v);
                samples[i * 2] = v >> 8; samples[i * 2 + 1] = v;
            }
            // how long the DSP takes over this step: ping it, it answers once done
            const uint32_t ta = fine_time();
            uint32_t pong = 0;
            if (ok) ok = send(0x060000);
            if (ok) ok = recv(&pong);
            const uint32_t dt = fine_time() - ta;
            dsp_sum += dt; if (dt > dsp_max) dsp_max = dt;
            if (tim) { fputc(dt >> 8, tim); fputc(dt, tim); }
            if (dt * 30 > 38400) dsp_over++;   // longer than a game step
            if (prev) fwrite(samples, 2, prev * 2, out);
            prev = w & 0xffff;
            last = w;
            renders++;
        }
    }
    // the last step: one more render to get it back
    if (ok && prev)
    {
        ok = send(last);
        for (uint32_t i = 0; ok && i < prev * 2; i++)
        {
            uint32_t v = 0;
            ok = recv(&v);
            samples[i * 2] = v >> 8; samples[i * 2 + 1] = v;
        }
        fwrite(samples, 2, prev * 2, out);
    }
    const uint32_t t1 = *hz200;
    if (out) fclose(out);
    if (tim) fclose(tim);
    if (ev) fclose(ev);
    if (log) { fprintf(log, "events=%s words=%lu renders=%lu ok=%d hz200=%lu\r\n", ev ? "yes" : "NO", (unsigned long)words,
                        (unsigned long)renders, (int)ok, (unsigned long)(t1 - t0));
              fprintf(log, "dsp per step: avg %lu us, max %lu us, steps over 33.3 ms %lu\r\n",
                      (unsigned long)(renders ? dsp_sum / renders * 26 : 0), (unsigned long)(dsp_max * 26), (unsigned long)dsp_over);
              fclose(log); }
    Dsp_Unlock();
    SuperToUser((void*)ssp);
    return 0;
}
