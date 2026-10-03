/***************************************************************************
    FM engine model - the part of the YM2151 that the DSP will compute.

    The 68k keeps the real YM2151 emulation (src/main/hwaudio/ym2151.cpp) for the
    register decoding and the timers, and sends this engine only what it needs:
      - per operator: phase increment, total level, D1L and the four envelope
        rates as (shift, select) pairs - see op_param();
      - per channel: algorithm, feedback shift, left/right enable - ch_param();
      - key on/off writes (register 8) - key().
    The engine then renders a game step: envelope generator, phases, the four
    operators of each channel, the 8 algorithms, feedback and the output mix.

    It follows ym2151.cpp's PLATFORM_ATARI paths exactly (including the "silent
    channel" rule of stream_update()), with the LFO, noise and CSM left out: the
    three OutRun tracks never use them (checked on the register logs). The DSP
    program is written to compute exactly this, so that its output can be
    compared sample for sample with this model, and this model with ym2151.cpp.

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

#pragma once
#include <stdint.h>
#include <string.h>

namespace fm {

enum { EG_OFF = 0, EG_REL = 1, EG_SUS = 2, EG_DEC = 3, EG_ATT = 4 };
enum { R_AR = 0, R_D1R = 1, R_D2R = 2, R_RR = 3 };
static const int32_t MAX_ATT = 1023;
static const uint32_t TL_TAB_LEN = 13 * 2 * 256;
static const uint32_t ENV_QUIET = TL_TAB_LEN >> 3;

struct Op
{
    uint32_t phase, freq;
    int32_t  tl, d1l;
    uint8_t  sh[4], sel[4];
    int32_t  volume;
    int      state, key;
};

struct Ch
{
    int     con, fb_shift;
    bool    pan_l, pan_r;
    int32_t fb_prev, fb_curr, mem_value;
};

class Engine
{
public:
    const int32_t*  tl_tab;
    const uint32_t* sin_tab;
    const uint8_t*  eg_inc;
    uint32_t eg_add, eg_ovf;

    Op op[32];
    Ch ch[8];
    uint32_t eg_cnt, eg_timer;

    void reset()
    {
        memset(op, 0, sizeof(op));
        memset(ch, 0, sizeof(ch));
        for (int i = 0; i < 32; i++) op[i].volume = MAX_ATT;
        eg_cnt = eg_timer = 0;
    }

    void op_param(int i, uint32_t freq, int32_t tl, int32_t d1l, const uint8_t sh[4], const uint8_t sel[4])
    {
        Op& o = op[i];
        o.freq = freq; o.tl = tl; o.d1l = d1l;
        for (int r = 0; r < 4; r++) { o.sh[r] = sh[r]; o.sel[r] = sel[r]; }
    }

    void ch_param(int c, int con, int fb_shift, bool l, bool r)
    {
        ch[c].con = con; ch[c].fb_shift = fb_shift; ch[c].pan_l = l; ch[c].pan_r = r;
    }

    // Register 8: key on/off of the four operators of channel v & 7 (KEY_ON / KEY_OFF macros).
    void key(int v)
    {
        Op* o = &op[(v & 7) * 4];
        static const int BIT[4] = { 0x08, 0x20, 0x10, 0x40 };   // M1, M2, C1, C2
        for (int k = 0; k < 4; k++)
        {
            Op& p = o[k];
            if (v & BIT[k])
            {
                if (!p.key)
                {
                    p.phase = 0;
                    p.state = EG_ATT;
                    p.volume += (~p.volume * (int32_t)eg_inc[p.sel[R_AR] + ((eg_cnt >> p.sh[R_AR]) & 7)]) >> 4;
                    if (p.volume <= 0) { p.volume = 0; p.state = EG_DEC; }
                }
                p.key |= 1;
            }
            else if (p.key)
            {
                p.key &= ~1;
                if (!p.key && p.state > EG_REL) p.state = EG_REL;
            }
        }
    }

    // One game step: `frames` stereo samples, interleaved L, R, scaled by gain/256.
    void render(int16_t* out, uint32_t frames, int32_t gain)
    {
        bool on[8];
        int active = 0;
        for (int c = 0; c < 8; c++)
        {
            on[c] = false;
            for (int k = 0; k < 4 && !on[c]; k++)
            {
                const Op& o = op[c * 4 + k];
                if (o.state == EG_OFF) continue;
                if (o.state == EG_REL && (uint32_t)(o.tl + o.volume) >= ENV_QUIET) continue;
                on[c] = true;
            }
            if (on[c]) active++;
            else ch[c].fb_prev = ch[c].fb_curr = ch[c].mem_value = 0;
        }
        if (!active) { memset(out, 0, frames * 4); return; }

        for (uint32_t s = 0; s < frames; s++)
        {
            advance_eg();
            int32_t outl = 0, outr = 0;
            for (int c = 0; c < 8; c++)
                if (on[c])
                {
                    const int32_t v = chan_calc(c);
                    if (ch[c].pan_l) outl += v;
                    if (ch[c].pan_r) outr += v;
                }
            if (outl > 32767) outl = 32767; else if (outl < -32768) outl = -32768;
            if (outr > 32767) outr = 32767; else if (outr < -32768) outr = -32768;
            *out++ = (int16_t)((outl * gain) >> 8);
            *out++ = (int16_t)((outr * gain) >> 8);
            for (int c = 0; c < 8; c++)
                if (on[c])
                    for (int k = 0; k < 4; k++) op[c * 4 + k].phase += op[c * 4 + k].freq;
        }
    }

private:
    void advance_eg()
    {
        eg_timer += eg_add;
        while (eg_timer >= eg_ovf)
        {
            eg_timer -= eg_ovf;
            eg_cnt++;
            for (int i = 0; i < 32; i++)
            {
                Op& o = op[i];
                int r;
                switch (o.state)
                {
                case EG_ATT: r = R_AR; break;
                case EG_DEC: r = R_D1R; break;
                case EG_SUS: r = R_D2R; break;
                case EG_REL: r = R_RR; break;
                default: continue;
                }
                if (eg_cnt & ((1u << o.sh[r]) - 1)) continue;
                const int32_t inc = eg_inc[o.sel[r] + ((eg_cnt >> o.sh[r]) & 7)];
                switch (o.state)
                {
                case EG_ATT:
                    o.volume += (~o.volume * inc) >> 4;
                    if (o.volume <= 0) { o.volume = 0; o.state = EG_DEC; }
                    break;
                case EG_DEC:
                    o.volume += inc;
                    if (o.volume >= o.d1l) o.state = EG_SUS;
                    break;
                default:   // SUS, REL
                    o.volume += inc;
                    if (o.volume >= MAX_ATT) { o.volume = MAX_ATT; o.state = EG_OFF; }
                    break;
                }
            }
        }
    }

    int32_t op_calc(const Op& o, uint32_t env, int32_t pm) const
    {
        const uint32_t p = (env << 3) + sin_tab[(((int32_t)((o.phase & ~0xffffu) + ((uint32_t)pm << 15))) >> 16) & 1023];
        return p >= TL_TAB_LEN ? 0 : tl_tab[p];
    }

    int32_t op_calc1(const Op& o, uint32_t env, int32_t pm) const
    {
        const int32_t i = (int32_t)((o.phase & ~0xffffu) + (uint32_t)pm);
        const uint32_t p = (env << 3) + sin_tab[(i >> 16) & 1023];
        return p >= TL_TAB_LEN ? 0 : tl_tab[p];
    }

    // ym2151.cpp's chan_calc(), with set_connect()'s routing written out per algorithm.
    int32_t chan_calc(int c)
    {
        Ch& h = ch[c];
        Op* o = &op[c * 4];
        int32_t m2 = 0, c1 = 0, c2 = 0, mem = 0, cout = 0;

        // MEM restore: algorithm 3 restores into C2, all others except 4/6/7 into M2
        // (4, 6, 7 point at "mem", which is cleared again right after).
        switch (h.con)
        {
        case 3: c2 = h.mem_value; break;
        case 4: case 6: case 7: mem = h.mem_value; break;
        default: m2 = h.mem_value; break;
        }

        // M1 with feedback
        uint32_t env = o[0].tl + (uint32_t)o[0].volume;
        {
            int32_t fb = h.fb_prev + h.fb_curr;
            h.fb_prev = h.fb_curr;
            switch (h.con)
            {
            case 0: case 3: case 4: case 6: c1 = h.fb_prev; break;
            case 1: mem = h.fb_prev; break;
            case 2: c2 = h.fb_prev; break;
            case 5: mem = c1 = c2 = h.fb_prev; break;
            case 7: cout = h.fb_prev; break;
            }
            h.fb_curr = 0;
            if (env < ENV_QUIET)
            {
                if (!h.fb_shift) fb = 0;
                h.fb_curr = op_calc1(o[0], env, fb << h.fb_shift);
            }
        }

        // M2 -> C2 (algorithms 0-4), OUT (5-7)
        env = o[1].tl + (uint32_t)o[1].volume;
        if (env < ENV_QUIET)
        {
            const int32_t v = op_calc(o[1], env, m2);
            if (h.con <= 4) c2 += v; else cout += v;
        }

        // C1 -> MEM (0-3), OUT (4-7)
        env = o[2].tl + (uint32_t)o[2].volume;
        if (env < ENV_QUIET)
        {
            const int32_t v = op_calc(o[2], env, c1);
            if (h.con <= 3) mem += v; else cout += v;
        }

        // C2 -> OUT
        env = o[3].tl + (uint32_t)o[3].volume;
        if (env < ENV_QUIET) cout += op_calc(o[3], env, c2);

        h.mem_value = mem;
        return cout;
    }
};

} // namespace fm
