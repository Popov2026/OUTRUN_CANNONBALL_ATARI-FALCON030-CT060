#pragma once

/***************************************************************************
    Minimal 4-channel Amiga ProTracker (.mod) replay engine.

    Plays the game's music from .mod files instead of the FM chip when
    outrun.ini's mod=1: Music\TRACK1.MOD..TRACK3.MOD replace the three
    selectable tracks (engine/omusic.cpp) and TRACK4.MOD the Last Wave
    high-score tune (engine/ohiscore.cpp). The files are supplied by the
    user; a missing one falls back to the FM track. Much cheaper than the FM
    music: the FM chip then has almost nothing to synthesise.

    Two ways of producing the sound, chosen when a song is loaded:
      - CPU mixing (default): mix() adds the song to the buffer that
        atari/audio.cpp sends to the DMA, at the engine's mix rate.
      - DSP replay (outrun.ini's mod_dsp=1, 4-channel modules): the song is
        advanced by the DSP replay's timer interrupt, which reads the
        channels through dsp_params() / dsp_fetch() / dsp_tick() and sends
        the sample data to the DSP (see atari/dsp_replay.hpp).

    Format support:
      - Both MOD header variants: 15-sample (no magic id at offset 1080,
        the original Ultimate Soundtracker/NoiseTracker layout) and 31-sample
        (modern ProTracker, magic id "M.K."/"M!K!"/"4CHN"/"6CHN"/"8CHN"/
        "FLT4"/"FLT8" at offset 1080). Channel count is read from the magic
        id when present (default 4 otherwise).
      - Effects: 0 arpeggio, 1/2 portamento up/down, 3 tone portamento,
        4 vibrato (sine only), 9 sample offset, A volume slide, B position
        jump, C set volume, D pattern break, F set speed/tempo.
      - NOT implemented (silently ignored - most modules still play
        recognisably without them): E-commands (fine slides, retrigger,
        note cut/delay, loop), 5/6 (volume slide + portamento/vibrato
        combined - treated as plain volume slide), 7 tremolo, offset
        commands beyond a single byte (no "invert loop" H, no 32-sample
        XM-style extensions - this is a classic .mod player only).

    CPU mixing: nearest-neighbour (no interpolation, the same trade-off
    atari/pcm_asm.S makes for the PCM chip), all channels summed to mono and
    added onto what atari/audio.cpp's Audio::tick() is already mixing. The
    DSP replay keeps the Amiga's stereo panning (channels 0 and 3 left, 1
    and 2 right).

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

#include "../stdint.hpp"

class ModPlayer
{
public:
    ModPlayer();
    ~ModPlayer();

    // Loads a .mod file (title + sample headers + pattern data + raw sample
    // PCM) from the program's folder. Returns false (and leaves the player
    // silent) if the file can't be opened or doesn't look like a .mod.
    bool load(const char* filename, uint32_t mix_rate);
    void unload();
    bool loaded() const { return song_loaded; }

    // Renders `frames` mono samples at the mix_rate passed to load(), ADDING
    // them (not overwriting) into `dst[frames]` - same convention as the
    // wavfile mixing already in Audio::tick(). Safe to call with frames==0
    // (does nothing) and with no song loaded (does nothing).
    void mix(int16_t* dst, uint32_t frames);

    // Restarts playback from the beginning of the song (order 0, row 0).
    void restart();

    // --- DSP replay (atari/dsp_replay.cpp), all three called from its timer interrupt ---
    // Volume (0..64, 0 when the channel is silent) and pitch (24-bit fraction of the DSP's
    // 49170 Hz output rate) of one channel for the coming 1/50 s frame.
    void dsp_params(int channel, uint32_t* vol, uint32_t* freq);
    // The next `n` source bytes of that channel (readable up to n + 6), then advances it.
    const int8_t* dsp_fetch(int channel, uint32_t n);
    // Advances the song by one 1/50 s frame (one tracker tick at the default tempo).
    void dsp_tick();

private:
    bool load_now(const char* filename, uint32_t mix_rate);
    void unload_now();
    static const int MAX_SAMPLES = 31;
    static const int MAX_PATTERNS = 128;
    static const int MAX_ORDERS = 128;
    static const int MAX_CHANNELS = 8;   // headroom for 6CHN/8CHN; classic .mod is 4

    struct Sample
    {
        int16_t* data;         // signed 8-bit source, sign-extended to 16 on load (see .cpp)
        int8_t*  data8;        // DSP replay only: the original bytes, followed by DSP_PAD bytes of
                               // what comes next (the loop again, or silence) so a frame can be
                               // read in one piece
        uint32_t length;       // in sample frames (bytes in the original file)
        uint32_t loop_start;
        uint32_t loop_length;  // 0/1 = no loop
        int8_t   finetune;     // -8..7, as stored (already sign-extended)
        uint8_t  volume;       // 0..64
    };

    struct Channel
    {
        int     sample;         // index into samples[], -1 = none yet
        uint32_t pos;            // Q16.16 fixed-point position into the sample
        uint32_t step;           // Q16.16 per-output-sample advance, from the current period
        uint16_t period;         // current Amiga period (pitch)
        uint16_t period_target;  // tone portamento target
        uint8_t  volume;         // 0..64, current
        uint16_t out_period;     // period actually heard (vibrato/arpeggio included) - DSP replay
        uint32_t dsp_pos;        // position in data8, in bytes - DSP replay
        uint8_t  vibrato_pos;
        uint8_t  vibrato_speed, vibrato_depth;
        uint8_t  last_param[16]; // per-effect-letter remembered param (some effects reuse the last nonzero one)
    };

    Sample  samples[MAX_SAMPLES];
    int     num_samples;
    uint8_t order[MAX_ORDERS];
    int     song_length;
    int     num_channels;
    int     num_patterns;
    // pattern data: [pattern][row][channel] -> 4 raw bytes, kept exactly as stored
    // in the file (period hi/lo, sample hi/lo, effect, param - see .cpp for decode).
    uint8_t (*patterns)[64][MAX_CHANNELS][4];

    Channel channels[MAX_CHANNELS];
    bool    song_loaded;
    uint8_t* dspmod_raw;   // mod_dsp = 2: the whole file, played by DSPMOD (atari/dspmod.hpp)
    bool    dsp_mode;   // the song is played by the DSP replay (atari_opt.mod_dsp): its timer
                          // interrupt drives the tracker, mix() does nothing
    volatile bool dsp_ready;   // false while the song is being loaded or freed: the interrupt
                               // then sends silence instead of touching the sample data
    uint32_t dsp_tempo_acc;
    static const uint32_t DSP_PAD = 1536;   // more than the 1400 bytes a voice can ask for in one go

    void set_period(Channel& ch, int period);   // pitch of a channel from an Amiga period

    uint32_t out_rate;         // mix() output rate, from load()
    uint32_t samples_per_tick; // Q16.16: output samples between two tracker ticks, from bpm
    uint32_t tick_frac;        // Q16.16: fractional position within the current tick

    int order_pos, row_pos;
    int speed;   // ticks per row
    int bpm;
    int tick_in_row;
    bool pattern_delay_active; int pattern_delay;
    int pattern_break_row;   bool do_pattern_break;
    int position_jump_order; bool do_position_jump;

    void process_tick();          // one tracker tick: on tick 0 of a row, reads the new row; every tick runs continuous effects
    void process_row();
    void trigger_note(Channel& ch, uint8_t sample_num, uint16_t period, uint8_t effect, uint8_t param);
    void do_effect_tick(Channel& ch, uint8_t effect, uint8_t param, bool first_tick);
    void update_step(Channel& ch);
    static uint16_t period_table_lookup(int semitone_index, int finetune);
};

extern ModPlayer modplayer;
