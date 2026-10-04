/***************************************************************************
    Minimal 4-channel Amiga ProTracker (.mod) replay engine - see
    modplayer.hpp for scope, format support and what is deliberately not
    implemented.

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

#include <cstdio>
#include <cstring>
#include "atari/modplayer.hpp"
#include "atari/options.hpp"
#include "atari/dsp_replay.hpp"
#include "atari/dspmod.hpp"
#include "atari/fmdsp.hpp"
#include "atari/audio.hpp"

ModPlayer modplayer;

// Standard Amiga period table, finetune 0, three octaves (C-1..B-3 - the
// range .mod pattern data actually uses). Finetune is not applied to note
// periods (deliberate simplification, see header): most modules are
// composed with sample finetune 0 anyway.
static const uint16_t PERIOD_TABLE[36] =
{
    856,808,762,720,678,640,604,570,538,508,480,453,
    428,404,381,360,339,320,302,285,269,254,240,226,
    214,202,190,180,170,160,151,143,135,127,120,113
};

// PAL Amiga: source sample rate for a channel at a given period.
static const double AMIGA_CLOCK = 7093789.2;

// No song loaded; every sample slot empty.
ModPlayer::ModPlayer()
{
    patterns = 0;
    song_loaded = false;
    dsp_mode = false;
    dsp_ready = false;
    dsp_tempo_acc = 0;
    dspmod_raw = 0;
    num_samples = 0;
    for (int i = 0; i < MAX_SAMPLES; i++) { samples[i].data = 0; samples[i].data8 = 0; samples[i].length = 0; }
}

// Frees the song, if any.
ModPlayer::~ModPlayer()
{
    unload();
}

// Frees the current song. The DSP replay is told first (dsp_ready) so that its interrupt stops
// reading the sample data before it is freed.
// The sound interrupt (atari/audio.cpp) mixes and plays the module: kept out meanwhile.
void ModPlayer::unload()
{
    atari_sound_lock();
    unload_now();
    atari_sound_unlock();
}

void ModPlayer::unload_now()
{
    if (dspmod_raw)
    {
        dspmod.stop_song();   // DSPMOD stops reading the module before it is freed
        delete[] dspmod_raw;
        dspmod_raw = 0;
    }
    dsp_ready = false;   // from here on the DSP replay's interrupt sends silence for the 4 channels
    for (int i = 0; i < MAX_SAMPLES; i++)
    {
        delete[] samples[i].data;
        samples[i].data = 0;
        delete[] samples[i].data8;
        samples[i].data8 = 0;
    }
    delete[] patterns;
    patterns = 0;
    song_loaded = false;
    dsp_mode = false;
}

// Reads a big-endian 16-bit field from a raw byte pointer (all multi-byte
// fields in a .mod file are big-endian: it is an Amiga format).
static inline uint16_t rd16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }

// Loads a .mod file and starts it from the beginning. See modplayer.hpp for the formats
// accepted. False (nothing loaded, nothing playing) if the file is missing or not a module.
bool ModPlayer::load(const char* filename, uint32_t mix_rate)
{
    atari_sound_lock();
    const bool ok = load_now(filename, mix_rate);
    atari_sound_unlock();
    return ok;
}

bool ModPlayer::load_now(const char* filename, uint32_t mix_rate)
{
    unload_now();

    FILE* f = fopen(filename, "rb");
    if (!f)
    {
        // No .mod for this track: FM music through the normal DMA path, so DSPMOD (mod_dsp = 2)
        // must give the DAC back.
        if (atari_opt.mod_dsp == 2) dspmod.shutdown();
        return false;
    }

    // Read the whole file into one buffer: .mod files used as test content
    // here are a few hundred KB at most, and this keeps the offset maths
    // below simple (no seeking back and forth while parsing samples vs
    // patterns, which are interleaved in the header but not in the body).
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize < 600) { fclose(f); return false; }   // smaller than even the old 15-sample header
    uint8_t* raw = new uint8_t[fsize];
    size_t got = fread(raw, 1, fsize, f);
    fclose(f);
    if ((long)got != fsize) { delete[] raw; return false; }

    // mod_dsp = 2: DSPMOD plays the file as it is (it interprets the patterns itself and streams
    // the samples to its DSP program). Modules it does not take fall back to the parser below.
    if (atari_opt.mod_dsp == 2 && fsize >= 1084)
    {
        fmdsp.stop();   // the DSP goes to DSPMOD (the FM sound back to the 68k meanwhile)
        if (dspmod.play(raw, mix_rate))
        {
            dspmod_raw = raw;
            song_loaded = true;
            return true;
        }
        dspmod.shutdown();
    }

    // Detect the header variant via the magic id at offset 1080 (present
    // only in the newer 31-sample layout).
    bool has_magic = false;
    num_channels = 4;
    if (fsize >= 1084)
    {
        const uint8_t* tag = raw + 1080;
        struct { const char* id; int ch; } tags[] = {
            {"M.K.",4},{"M!K!",4},{"FLT4",4},{"4CHN",4},{"6CHN",6},{"8CHN",8},{"FLT8",8}
        };
        for (size_t i = 0; i < sizeof(tags)/sizeof(tags[0]); i++)
            if (memcmp(tag, tags[i].id, 4) == 0) { has_magic = true; num_channels = tags[i].ch; break; }
    }
    if (num_channels > MAX_CHANNELS) num_channels = MAX_CHANNELS;

    num_samples = has_magic ? 31 : 15;

    // Sample headers: 20-byte title, then num_samples * 30-byte entries.
    long off = 20;
    for (int i = 0; i < num_samples; i++)
    {
        const uint8_t* s = raw + off;
        uint32_t len_words    = rd16(s + 22);
        int8_t   finetune_raw = (int8_t)(s[24] & 0x0F);
        if (finetune_raw > 7) finetune_raw -= 16;   // 4-bit signed, stored 0..15
        uint8_t  volume        = s[25];
        uint32_t loop_start_w  = rd16(s + 26);
        uint32_t loop_len_w    = rd16(s + 28);

        samples[i].length      = len_words * 2;
        samples[i].finetune    = finetune_raw;
        samples[i].volume      = volume > 64 ? 64 : volume;
        samples[i].loop_start  = loop_start_w * 2;
        samples[i].loop_length = loop_len_w * 2;
        samples[i].data        = 0;   // filled in below, once the pattern block's size is known
        samples[i].data8       = 0;
        off += 30;
    }
    for (int i = num_samples; i < MAX_SAMPLES; i++) { samples[i].length = 0; samples[i].loop_length = 0; }

    song_length = raw[off]; off++;
    off++;   // restart position byte (NoiseTracker-era field) - not used
    for (int i = 0; i < MAX_ORDERS; i++) order[i] = raw[off + i];
    off += MAX_ORDERS;
    if (has_magic) off += 4;   // magic id already read above

    num_patterns = 0;
    for (int i = 0; i < song_length && i < MAX_ORDERS; i++)
        if (order[i] + 1 > num_patterns) num_patterns = order[i] + 1;
    if (num_patterns > MAX_PATTERNS) num_patterns = MAX_PATTERNS;
    if (num_patterns < 1) num_patterns = 1;

    long pattern_bytes = (long)num_patterns * 64 * num_channels * 4;
    if (off + pattern_bytes > fsize) { delete[] raw; return false; }   // truncated/unrecognised file

    patterns = new uint8_t[num_patterns][64][MAX_CHANNELS][4];
    memset(patterns, 0, (size_t)num_patterns * 64 * MAX_CHANNELS * 4);
    for (int p = 0; p < num_patterns; p++)
        for (int row = 0; row < 64; row++)
            for (int ch = 0; ch < num_channels; ch++)
            {
                const uint8_t* e = raw + off;
                patterns[p][row][ch][0] = e[0]; patterns[p][row][ch][1] = e[1];
                patterns[p][row][ch][2] = e[2]; patterns[p][row][ch][3] = e[3];
                off += 4;
            }

    // Sample PCM data follows the pattern block, one sample's worth after another,
    // in the same order as the sample headers. Stored signed 8-bit; sign-extend to
    // 16-bit once here so mix() is a plain lookup with no per-sample conversion.
    for (int i = 0; i < num_samples; i++)
    {
        uint32_t len = samples[i].length;
        if (len == 0) continue;
        if (off + (long)len > fsize) len = (fsize > off) ? (uint32_t)(fsize - off) : 0;
        samples[i].data = new int16_t[len ? len : 1];
        for (uint32_t j = 0; j < len; j++)
            samples[i].data[j] = (int16_t)((int8_t)raw[off + j]) << 6;   // scale 8-bit source up, headroom for volume/mixing
        off += samples[i].length;
        samples[i].length = len;   // clamp to what was actually present
    }

    delete[] raw;

    out_rate = mix_rate;
    for (int c = 0; c < MAX_CHANNELS; c++)
    {
        channels[c].sample = -1;
        channels[c].pos = 0; channels[c].step = 0;
        channels[c].period = 0; channels[c].period_target = 0;
        channels[c].volume = 0;
        channels[c].out_period = 0; channels[c].dsp_pos = 0;
        channels[c].vibrato_pos = 0; channels[c].vibrato_speed = 0; channels[c].vibrato_depth = 0;
        memset(channels[c].last_param, 0, sizeof(channels[c].last_param));
    }

    song_loaded = true;

    // DSP replay (atari_opt.mod_dsp, see dsp_replay.hpp): 4-channel modules only. Each sample
    // gets a byte copy followed by DSP_PAD bytes of whatever plays next (the loop again, or
    // silence), so the interrupt can hand the DSP one frame's worth in a single run.
    dsp_mode = false;
    dsp_tempo_acc = 0;
    if (atari_opt.mod_dsp == 1 && num_channels <= 4)
        fmdsp.stop();   // the DSP goes to the replay (the FM sound back to the 68k meanwhile)
    if (atari_opt.mod_dsp == 1 && num_channels <= 4 && dsp_replay.start())
    {
        for (int i = 0; i < num_samples; i++)
        {
            Sample& sm = samples[i];
            if (sm.length == 0 || !sm.data) continue;
            sm.data8 = new int8_t[sm.length + DSP_PAD];
            for (uint32_t j = 0; j < sm.length; j++)
                sm.data8[j] = (int8_t)(sm.data[j] >> 6);
            const bool looped = sm.loop_length > 1 && sm.loop_start < sm.length;
            for (uint32_t j = 0; j < DSP_PAD; j++)
            {
                uint32_t src = sm.loop_start + (sm.length - sm.loop_start + j) % (looped ? sm.loop_length : 1);
                sm.data8[sm.length + j] = (looped && src < sm.length) ? sm.data8[src] : 0;
            }
        }
        dsp_mode = true;
    }

    restart();
    dsp_ready = dsp_mode;
#ifdef MOD_MUSIC_LOG
    // Test aid: confirms the parse succeeded and dumps the header fields, written to a plain
    // file (see hwaudio/segapcm.cpp's PCM_MEASURE_FILE comment for why - console output was not
    // reliably captured this session).
    {
        FILE* lf = fopen("MODLOG.TXT", "a");
        if (lf)
        {
            fprintf(lf, "MOD load OK: %s channels=%d samples=%d song_len=%d patterns=%d rate=%lu\r\n",
                    filename, num_channels, num_samples, song_length, num_patterns, (unsigned long)mix_rate);
            fclose(lf);
        }
    }
#endif
    return true;
}

// Back to the first row of the song, default speed (6 ticks per row) and tempo (125).
void ModPlayer::restart()
{
    order_pos = 0; row_pos = 0;
    speed = 6; bpm = 125;
    tick_in_row = 0;
    pattern_delay_active = false; pattern_delay = 0;
    do_pattern_break = false; pattern_break_row = 0;
    do_position_jump = false; position_jump_order = 0;
    tick_frac = 0;
    // samples_per_tick, Q16.16: (out_rate * 2.5) / bpm samples between ticks (standard Amiga tempo formula).
    samples_per_tick = (uint32_t)(((double)out_rate * 2.5 / bpm) * 65536.0);
    if (song_loaded) process_row();
}

// Amiga period of a note, by index in PERIOD_TABLE (0 = C-1 .. 35 = B-3), clamped to the table.
uint16_t ModPlayer::period_table_lookup(int semitone_index, int /*finetune*/)
{
    if (semitone_index < 0) semitone_index = 0;
    if (semitone_index > 35) semitone_index = 35;
    return PERIOD_TABLE[semitone_index];
}

// Index in PERIOD_TABLE of the note closest to `period` (used by the arpeggio effect).
static int nearest_semitone(uint16_t period)
{
    int best = 0; int best_diff = 0x7fffffff;
    for (int i = 0; i < 36; i++)
    {
        int diff = period - PERIOD_TABLE[i];
        if (diff < 0) diff = -diff;
        if (diff < best_diff) { best_diff = diff; best = i; }
    }
    return best;
}

// The DSP replay works from the period itself; the step (floating point, far too slow for an
// interrupt on a machine without an FPU) is only needed by the CPU mixer.
void ModPlayer::set_period(Channel& ch, int period)
{
    ch.out_period = (uint16_t)period;
    if (dsp_mode) return;
    if (period == 0) { ch.step = 0; return; }
    double src_hz = AMIGA_CLOCK / (period * 2.0);
    ch.step = (uint32_t)((src_hz / (double)out_rate) * 65536.0);
}

// Recomputes the channel's pitch from its current period.
void ModPlayer::update_step(Channel& ch)
{
    set_period(ch, ch.period);
}

// A row's note for one channel: selects the sample and its default volume, then either starts
// the note from the beginning of the sample or, for the tone portamento effects, only sets
// the pitch to slide towards.
void ModPlayer::trigger_note(Channel& ch, uint8_t sample_num, uint16_t period, uint8_t effect, uint8_t param)
{
    if (sample_num >= 1 && sample_num <= num_samples)
    {
        int idx = sample_num - 1;
        ch.sample = idx;
        ch.volume = samples[idx].volume;
    }
    // Effect 3 (tone portamento) re-uses the note as a *target* and does not retrigger
    // the sample or reset position; effect 5 (volume slide + tone portamento) likewise.
    if (period != 0)
    {
        if (effect == 0x3 || effect == 0x5)
        {
            ch.period_target = period;
        }
        else
        {
            ch.period = period;
            update_step(ch);
            if (ch.sample >= 0)
            {
                ch.pos = 0;
                ch.dsp_pos = 0;
            }
        }
    }
}

// Applies one channel's effect for one tick. first_tick is true on the tick the row is read:
// some effects act only then (set volume, sample offset), most only on the following ticks.
void ModPlayer::do_effect_tick(Channel& ch, uint8_t effect, uint8_t param, bool first_tick)
{
    switch (effect)
    {
        case 0x0:   // arpeggio: base / base+x / base+y, cycling every tick (no-op on tick 0 or if param 0)
            if (param != 0 && ch.period != 0)
            {
                int step3 = tick_in_row % 3;
                int base = nearest_semitone(ch.period);
                int add = (step3 == 1) ? (param >> 4) : (step3 == 2) ? (param & 0x0F) : 0;
                uint16_t p = period_table_lookup(base + add, 0);
                set_period(ch, p);
            }
            break;

        case 0x1:   // portamento up (toward lower period = higher pitch)
            if (!first_tick)
            {
                if (param) ch.last_param[1] = param; else param = ch.last_param[1];
                int p = (int)ch.period - param;
                ch.period = (uint16_t)(p < 113 ? 113 : p);
                update_step(ch);
            }
            break;

        case 0x2:   // portamento down
            if (!first_tick)
            {
                if (param) ch.last_param[2] = param; else param = ch.last_param[2];
                int p = (int)ch.period + param;
                ch.period = (uint16_t)(p > 856 ? 856 : p);
                update_step(ch);
            }
            break;

        case 0x3:   // tone portamento: slide current period toward period_target
        case 0x5:   // same slide, but `param` here is a volume-slide amount, not a porta speed -
                    // always reuse the last real 0x3 speed instead (see last_param[3] below).
        {
            uint8_t porta_speed = (effect == 0x3 && param) ? param : ch.last_param[3];
            if (effect == 0x3 && param) ch.last_param[3] = param;
            if (!first_tick && ch.period_target != 0)
            {
                if (ch.period < ch.period_target)
                {
                    ch.period = (uint16_t)((ch.period + porta_speed > ch.period_target) ? ch.period_target : ch.period + porta_speed);
                }
                else if (ch.period > ch.period_target)
                {
                    ch.period = (uint16_t)((ch.period < ch.period_target + porta_speed) ? ch.period_target : ch.period - porta_speed);
                }
                update_step(ch);
            }
            if (effect == 0x5 && !first_tick)
                goto do_volslide;
            break;
        }

        case 0x4:   // vibrato: sine wobble around the current period
            if (!first_tick || param)
            {
                if (param & 0xF0) ch.vibrato_speed = param >> 4;
                if (param & 0x0F) ch.vibrato_depth = param & 0x0F;
            }
            if (!first_tick)
            {
                static const uint8_t SINE[32] = {
                    0,24,49,74,97,120,141,161,180,197,212,224,235,244,250,253,
                    255,253,250,244,235,224,212,197,180,161,141,120,97,74,49,24
                };
                ch.vibrato_pos = (uint8_t)((ch.vibrato_pos + ch.vibrato_speed) & 63);
                int idx = ch.vibrato_pos & 31;
                int s = (ch.vibrato_pos & 32) ? -SINE[idx] : SINE[idx];
                int delta = (s * ch.vibrato_depth) / 128;
                int p = (int)ch.period + delta;
                if (p < 56) p = 56; if (p > 1712) p = 1712;
                set_period(ch, p);
            }
            break;

        case 0x9:   // sample offset (tick 0 only): start the sample at param*256 instead of 0
            if (first_tick && ch.sample >= 0)
            {
                uint32_t o = (uint32_t)param << 8;
                if (o < samples[ch.sample].length) { ch.pos = o << 16; ch.dsp_pos = o; }
            }
            break;

        case 0xA:   // volume slide (both first and later ticks contribute on real hardware; kept simple: every tick but 0)
        do_volslide:
            if (!first_tick)
            {
                if (param) ch.last_param[0xA] = param; else param = ch.last_param[0xA];
                int up = param >> 4, down = param & 0x0F;
                int v = (int)ch.volume + up - down;
                if (v < 0) v = 0; if (v > 64) v = 64;
                ch.volume = (uint8_t)v;
            }
            break;

        case 0xC:   // set volume (tick 0 only)
            if (first_tick)
            {
                ch.volume = param > 64 ? 64 : param;
            }
            break;

        default:
            break;   // B/D/F handled per-row in process_row(); everything else is a documented no-op
    }
}

// Reads the current row: notes and effects of every channel, plus the effects that act on the
// song as a whole (position jump, pattern break, speed/tempo).
void ModPlayer::process_row()
{
    int p = order[order_pos];
    if (p >= num_patterns) p = 0;
    tick_in_row = 0;   // set before the effects below run: tick 0 of this row, needed by e.g. arpeggio

    for (int c = 0; c < num_channels; c++)
    {
        const uint8_t* e = patterns[p][row_pos][c];
        uint16_t period = (uint16_t)(((e[0] & 0x0F) << 8) | e[1]);
        uint8_t  sample_num = (uint8_t)((e[0] & 0xF0) | (e[2] >> 4));
        uint8_t  effect = (uint8_t)(e[2] & 0x0F);
        uint8_t  param = e[3];

        trigger_note(channels[c], sample_num, period, effect, param);
        do_effect_tick(channels[c], effect, param, true);

        switch (effect)
        {
            case 0xB: do_position_jump = true; position_jump_order = param; break;
            case 0xD: do_pattern_break = true; pattern_break_row = ((param >> 4) * 10) + (param & 0x0F); break;
            case 0xF:
                if (param == 0) break;
                if (param < 0x20) speed = param;
                else
                {
                    bpm = param;
                    if (!dsp_mode) samples_per_tick = (uint32_t)(((double)out_rate * 2.5 / bpm) * 65536.0);
                }
                break;
            default: break;
        }
    }
}

// One tracker tick: moves to the next row when the current one has lasted `speed` ticks,
// otherwise runs the continuous effects of the current row.
void ModPlayer::process_tick()
{
    tick_in_row++;
    if (tick_in_row >= speed)
    {
        // Advance to the next row/pattern before the row 0 processing above runs again.
        int next_row = row_pos + 1;
        int next_order = order_pos;

        if (do_pattern_break) { next_row = pattern_break_row; next_order = order_pos + 1; do_pattern_break = false; }
        if (do_position_jump) { next_order = position_jump_order; next_row = do_pattern_break ? next_row : 0; do_position_jump = false; }
        if (next_row >= 64) { next_row = 0; next_order = order_pos + 1; }
        if (next_order >= song_length || next_order >= MAX_ORDERS) next_order = 0;

        order_pos = next_order; row_pos = next_row;
        process_row();
    }
    else
    {
        for (int c = 0; c < num_channels; c++)
        {
            const uint8_t* e = patterns[order[order_pos] < num_patterns ? order[order_pos] : 0][row_pos][c];
            uint8_t effect = (uint8_t)(e[2] & 0x0F);
            uint8_t param = e[3];
            do_effect_tick(channels[c], effect, param, false);
        }
    }
}

// CPU mixer: adds `frames` samples of the song to dst[], advancing the song as it goes.
// Does nothing when the song is played by the DSP replay.
void ModPlayer::mix(int16_t* dst, uint32_t frames)
{
    if (!song_loaded || frames == 0) return;
    if (dsp_mode || dspmod_raw) return;   // played by the DSP replay, driven from its own timer interrupt

    for (uint32_t i = 0; i < frames; i++)
    {
        // Advance the tracker by whole ticks until less than one tick remains in this sample.
        while (tick_frac >= samples_per_tick)
        {
            tick_frac -= samples_per_tick;
            process_tick();
        }

        if (!dsp_mode)
        {
            int32_t acc = 0;
            for (int c = 0; c < num_channels; c++)
            {
                Channel& ch = channels[c];
                if (ch.sample < 0 || ch.step == 0 || ch.volume == 0) continue;
                Sample& s = samples[ch.sample];
                if (!s.data || s.length == 0) continue;

                uint32_t idx = ch.pos >> 16;
                if (idx >= s.length)
                {
                    if (s.loop_length > 1)
                    {
                        uint32_t rel = idx - s.loop_start;
                        idx = s.loop_start + (rel % s.loop_length);
                        ch.pos = (idx << 16) | (ch.pos & 0xFFFF);
                    }
                    else
                    {
                        ch.sample = -1;
                        continue;
                    }
                }
                acc += (int32_t)(s.data[idx] * ch.volume) >> 6;   // volume 0..64 -> /64, done as >>6

                ch.pos += ch.step;
                uint32_t new_idx = ch.pos >> 16;
                if (new_idx >= s.length && s.loop_length > 1)
                {
                    uint32_t rel = new_idx - s.loop_start;
                    new_idx = s.loop_start + (rel % s.loop_length);
                    ch.pos = (new_idx << 16) | (ch.pos & 0xFFFF);
                }
            }
            // num_channels-way sum can clip; scale down by channel count headroom (2 bits covers up to 4 channels
            // without audible loss on typical material, matches the /64 volume scale chosen above).
            if (acc > 32767) acc = 32767; else if (acc < -32768) acc = -32768;
            dst[i] += (int16_t)acc;
        }

        tick_frac += 65536;
    }

#ifdef MOD_MUSIC_LOG
    // Test aid: peak amplitude and active-channel count over ~5s of ticks, written to file -
    // confirms the mixer is actually producing sound (not silently doing nothing) without
    // needing to listen to it.
    {
        static int16_t peak = 0;
        static uint32_t calls = 0;
        for (uint32_t i = 0; i < frames; i++)
        {
            int16_t v = dst[i]; if (v < 0) v = (int16_t)(-v);
            if (v > peak) peak = v;
        }
        if (++calls == 150)
        {
            int active = 0;
            for (int c = 0; c < num_channels; c++) if (channels[c].sample >= 0) active++;
            FILE* lf = fopen("MODLOG.TXT", "a");
            if (lf) { fprintf(lf, "MOD mix: peak=%d active_ch=%d order=%d row=%d bpm=%d speed=%d\r\n",
                               peak, active, order_pos, row_pos, bpm, speed); fclose(lf); }
            peak = 0; calls = 0;
        }
    }
#endif
}

// ---------------------------------------------------------------------------
// DSP replay interface (called from dsp_replay.cpp's Timer A interrupt)
// ---------------------------------------------------------------------------

// PAL Amiga clock / 2 / 49170 Hz, as a 24-bit fraction: divided by the period it gives the
// channel's sample rate relative to the DSP's output rate.
static const uint32_t DSP_PITCH = 605127385UL;

// See modplayer.hpp. A silent channel is reported as volume 0 with the smallest pitch, so
// the DSP asks for no data for it.
void ModPlayer::dsp_params(int channel, uint32_t* vol, uint32_t* freq)
{
    *vol = 0;
    *freq = 1;
    if (!dsp_ready) return;
    Channel& ch = channels[channel];
    if (ch.sample < 0 || ch.out_period == 0 || !samples[ch.sample].data8) return;
    uint32_t f = DSP_PITCH / ch.out_period;
    if (f > 0x7fffff) f = 0x7fffff;   // the DSP steps at most one source byte per output sample
    *freq = f;
    *vol = ch.volume;
}

// See modplayer.hpp. The data returned runs past the end of the sample into its padding,
// which holds what plays next (the loop again, or silence); the position is then wrapped.
const int8_t* ModPlayer::dsp_fetch(int channel, uint32_t n)
{
    static const int8_t silence[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    if (!dsp_ready) return silence;
    Channel& ch = channels[channel];
    if (ch.sample < 0 || !samples[ch.sample].data8) return silence;
    Sample& sm = samples[ch.sample];
    if (ch.dsp_pos >= sm.length) ch.dsp_pos = sm.length;   // can only happen on a sample change
    const int8_t* p = sm.data8 + ch.dsp_pos;               // n + 6 <= DSP_PAD bytes are readable
    uint32_t pos = ch.dsp_pos + n;
    if (pos >= sm.length)
    {
        if (sm.loop_length > 1 && sm.loop_start < sm.length)
            pos = sm.loop_start + (pos - sm.loop_start) % sm.loop_length;   // same wrap as mix()
        else
        {
            pos = sm.length;
            ch.sample = -1;    // one-shot sample finished: silent from the next frame on
        }
    }
    ch.dsp_pos = pos;
    return p;
}

// See modplayer.hpp.
void ModPlayer::dsp_tick()
{
    if (!dsp_ready) return;
    // Tracker ticks last 2.5 / bpm seconds: exactly one per 1/50 s frame at the default 125.
    dsp_tempo_acc += bpm;
    while (dsp_tempo_acc >= 125)
    {
        dsp_tempo_acc -= 125;
        process_tick();
    }
}
