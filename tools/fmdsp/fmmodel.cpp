/***************************************************************************
    fmmodel - checks the FM-on-DSP split on the host before any DSP code.

    Replays a YM2151 register log (see ymref.cpp) twice:
      1. through the game's YM2151 emulation (ym2151.cpp), as the game does;
      2. "68k side": the same YM2151 object only decodes the registers; after each
         write, what changed (operator parameters, channel settings, key on/off) is
         sent as an event to fm::Engine (fm_engine.h), which renders the sound -
         the work the DSP program will do.
    and compares the two outputs sample by sample. Also writes the event stream to
    a file (EVENTS.BIN) in the format the DSP program will receive.

    Build: g++ -O2 -DPLATFORM_ATARI -Isrc/main -Itools/fmdsp tools/fmdsp/fmmodel.cpp \
               src/main/hwaudio/ym2151.cpp src/main/hwaudio/soundchip.cpp -o fmmodel
    Use:   fmmodel MAGICAL.LOG [model.raw [ticks]]   (model.raw: 16-bit little-endian L,R)

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

#include <cstdio>
#include <cstdlib>
#include <vector>
#include "hwaudio/ym2151.hpp"
#include "fm_engine.h"

extern YM2151Operator oper[32];
extern uint32_t pan[16];
extern uint8_t connects[8];
const signed int*   ym2151_tl_tab();
const unsigned int* ym2151_sin_tab();
const uint8_t*      ym2151_eg_inc();
uint32_t ym2151_eg_timer_add();
uint32_t ym2151_eg_timer_overflow();

// What the DSP has been told so far, to send only changes.
struct OpSnap { uint32_t freq; int32_t tl, d1l; uint8_t sh[4], sel[4]; };
struct ChSnap { int con, fb_shift, l, r; };

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
static bool same(const OpSnap& a, const OpSnap& b) { return !memcmp(&a, &b, sizeof(a)); }

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: fmmodel LOG [model.raw [ticks]]\n"); return 1; }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    std::vector<unsigned char> log;
    for (int c; (c = fgetc(f)) != EOF; ) log.push_back((unsigned char)c);
    fclose(f);
    const int RATE = 12517, FPS = 30;
    int ticks = ((log[log.size() - 4] << 8) | log[log.size() - 3]) + 1;
    if (argc > 3 && atoi(argv[3]) > 0 && atoi(argv[3]) < ticks) ticks = atoi(argv[3]);   // only the first N game steps

    // 1. reference
    YM2151 ref(0.5f, 4000000);
    ref.init(RATE, FPS);
    std::vector<int16_t> out_ref;
    {
        size_t pos = 0;
        for (int t = 0; t < ticks; t++)
        {
            while (pos + 4 <= log.size() && ((log[pos] << 8) | log[pos + 1]) <= t) { ref.write_reg(log[pos + 2], log[pos + 3]); pos += 4; }
            ref.stream_update();
            const int16_t* b = ref.get_buffer();
            out_ref.insert(out_ref.end(), b, b + ref.buffer_size);
        }
    }
    const uint32_t frames = ref.buffer_size / 2;

    // 2. split: a fresh YM2151 for the registers, fm::Engine for the sound.
    //    (oper/pan/connects are the emulation's globals: the reference is done with them.)
    YM2151 regs(0.5f, 4000000);
    regs.init(RATE, FPS);
    fm::Engine eng;
    eng.tl_tab = (const int32_t*)ym2151_tl_tab();
    eng.sin_tab = (const uint32_t*)ym2151_sin_tab();
    eng.eg_inc = ym2151_eg_inc();
    eng.eg_add = ym2151_eg_timer_add();
    eng.eg_ovf = ym2151_eg_timer_overflow();
    eng.reset();

    OpSnap sent_op[32]; ChSnap sent_ch[8];
    FILE* ev = fopen("EVENTS.BIN", "wb");
    uint32_t n_op = 0, n_ch = 0, n_key = 0;
    auto put = [&](uint32_t w) { fputc(w >> 16, ev); fputc(w >> 8, ev); fputc(w, ev); };   // 24-bit words, as the DSP host port takes them
    auto sync = [&](bool all) {
        for (int i = 0; i < 32; i++)
        {
            OpSnap s = op_snap(i);
            if (!all && same(s, sent_op[i])) continue;
            sent_op[i] = s;
            eng.op_param(i, s.freq, s.tl, s.d1l, s.sh, s.sel);
            n_op++;
            // event 1: op, freq high 16 / low 16, tl, d1l, (sh << 8 | sel) x 4
            put(0x010000 | i); put(s.freq >> 16); put(s.freq & 0xffff); put(s.tl); put(s.d1l);
            for (int r = 0; r < 4; r++) put((s.sh[r] << 8) | s.sel[r]);
        }
        for (int c = 0; c < 8; c++)
        {
            ChSnap s = { connects[c], (int)oper[c * 4].fb_shift, pan[c * 2] != 0, pan[c * 2 + 1] != 0 };
            if (!all && !memcmp(&s, &sent_ch[c], sizeof(s))) continue;
            sent_ch[c] = s;
            eng.ch_param(c, s.con, s.fb_shift, s.l, s.r);
            n_ch++;
            put(0x020000 | c); put((s.con << 8) | s.fb_shift); put((s.l ? 1 : 0) | (s.r ? 2 : 0));
        }
    };
    sync(true);

    std::vector<int16_t> out_eng;
    std::vector<int16_t> buf(frames * 2);
    size_t pos = 0;
    for (int t = 0; t < ticks; t++)
    {
        while (pos + 4 <= log.size() && ((log[pos] << 8) | log[pos + 1]) <= t)
        {
            const int r = log[pos + 2], v = log[pos + 3];
            regs.write_reg(r, v);
            sync(false);
            if (r == 8) { eng.key(v); n_key++; put(0x030000 | v); }
            pos += 4;
        }
#ifdef ACTSTAT
        {   // statistics: active channels / non-off operators per step
            static long sum_ch = 0, sum_op = 0, steps = 0, max_ch = 0;
            int ac = 0, ao = 0;
            for (int c = 0; c < 8; c++)
            {
                bool on = false;
                for (int k = 0; k < 4; k++)
                {
                    const fm::Op& o = eng.op[c * 4 + k];
                    if (o.state != fm::EG_OFF) ao++;
                    if (o.state == fm::EG_OFF) continue;
                    if (o.state == fm::EG_REL && (uint32_t)(o.tl + o.volume) >= fm::ENV_QUIET) continue;
                    on = true;
                }
                ac += on;
            }
            sum_ch += ac; sum_op += ao; steps++; if (ac > max_ch) max_ch = ac;
            if (t == ticks - 1) printf("active channels avg %.2f max %ld, non-off operators avg %.2f\n", sum_ch / (double)steps, max_ch, sum_op / (double)steps);
        }
#endif
        put(0x040000 | frames);   // render one game step
        eng.render(buf.data(), frames, 128);
        out_eng.insert(out_eng.end(), buf.begin(), buf.end());
    }
    fclose(ev);

    // compare
    size_t diff = 0, first = (size_t)-1; int maxd = 0;
    for (size_t i = 0; i < out_ref.size() && i < out_eng.size(); i++)
    {
        int d = abs(out_ref[i] - out_eng[i]);
        if (d) { diff++; if (first == (size_t)-1) first = i; if (d > maxd) maxd = d; }
    }
    printf("%s: %d ticks, %zu samples, events: %u op, %u ch, %u key; differing samples %zu (max %d, first at %zd)\n",
           argv[1], ticks, out_ref.size(), n_op, n_ch, n_key, diff, maxd, first == (size_t)-1 ? -1 : (ssize_t)first);
    if (argc > 2)
    {
        FILE* w = fopen(argv[2], "wb");
        fwrite(out_eng.data(), 2, out_eng.size(), w);
        fclose(w);
    }
    return diff ? 1 : 0;
}
