/***************************************************************************
    DMTEST.TOS - test of the DSPMOD integration (src/main/atari/dspmod.cpp).

    Plays TEST.MOD with DSPMOD and, like the game, streams a "game sound" into
    the two fx voices through DspMod::push_fx(): a 1000 Hz tone on the left and
    a 1500 Hz tone on the right, 417 frames at 12517 Hz every 1/30 s. Runs for
    about 12 s, writes what happened to DMTEST.TXT, then shuts DSPMOD down.
    Used by the Hatari test on GitHub (.github/workflows/hatari-dspmod.yml),
    which records the sound and checks the three frequencies are present.

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

#include <cstdio>
#include <cmath>
#include <mint/osbind.h>
#include "atari/dspmod.hpp"

static const uint32_t RATE = 12517;
static const uint32_t STEP_FRAMES = 417;   // one game step (1/30 s)

int main()
{
    long ssp = Super(0L);
    volatile uint32_t* hz200 = (volatile uint32_t*)0x4BA;
    FILE* log = fopen("DMTEST.TXT", "w");

    FILE* f = fopen("TEST.MOD", "rb");
    uint8_t* mod = 0;
    long size = 0;
    if (f)
    {
        fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
        mod = new uint8_t[size];
        fread(mod, 1, size, f);
        fclose(f);
    }
    if (log) fprintf(log, "TEST.MOD size=%ld\r\n", size);

    bool ok = mod && dspmod.play(mod, RATE);
    if (log) fprintf(log, "play=%d active=%d\r\n", (int)ok, (int)dspmod.active());

    static int8_t lr[STEP_FRAMES * 2];
    static int8_t sine[RATE];   // one second of a 1 Hz sine: index = phase in 1/RATE turns
    for (uint32_t i = 0; i < RATE; i++) sine[i] = (int8_t)(60.0 * sin(2 * M_PI * i / RATE));
    uint32_t phase_l = 0, phase_r = 0, steps = 0;
    const uint32_t t0 = *hz200;
    uint32_t next = t0 * 3;   // in 1/600 s, like the game's main loop: 20 units = 1/30 s
    while (ok && *hz200 - t0 < 200 * 12)
    {
        if ((int32_t)(*hz200 * 3 - next) < 0) continue;
        next += 20;
        for (uint32_t i = 0; i < STEP_FRAMES; i++)
        {
            lr[i * 2]     = sine[phase_l];
            lr[i * 2 + 1] = sine[phase_r];
            phase_l = (phase_l + 1000) % RATE;
            phase_r = (phase_r + 1500) % RATE;
        }
        dspmod.push_fx(lr, STEP_FRAMES);
        steps++;
    }
    if (log) fprintf(log, "steps=%lu\r\n", (unsigned long)steps);
    dspmod.shutdown();
    if (log) { fprintf(log, "shutdown ok\r\n"); fclose(log); }
    SuperToUser((void*)ssp);
    return 0;
}
