/***************************************************************************
    Atari STE DMA Sound Backend - Implementation.
    See audio.hpp for design notes and status.

    Tested in Hatari (Mega STE, Falcon 030/060 emulation); see README_ATARI.md.

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

#include <cstring>
#include <climits>
#include <cstdio>
#include <mint/osbind.h>
#include "atari/audio.hpp"
#include "atari/modplayer.hpp"
#include "atari/dsp_replay.hpp"
#include "atari/dspmod.hpp"
#include "atari/options.hpp"
#include "frontend/config.hpp"
#include "engine/audio/osoundint.hpp"

// --------------------------------------------------------------------------
// STE/Falcon DMA sound registers (offsets checked against EmuTOS's dmasound.c).
// --------------------------------------------------------------------------
#define DMA_CTRL       (*(volatile uint16_t*)0xFF8900) // bit0 on/off, bit1 loop
#define DMA_START_HI   (*(volatile uint8_t*)0xFF8903)
#define DMA_START_MID  (*(volatile uint8_t*)0xFF8905)
#define DMA_START_LO   (*(volatile uint8_t*)0xFF8907)
#define DMA_END_HI     (*(volatile uint8_t*)0xFF890F)
#define DMA_END_MID    (*(volatile uint8_t*)0xFF8911)
#define DMA_END_LO     (*(volatile uint8_t*)0xFF8913)
#define DMA_MODE       (*(volatile uint8_t*)0xFF8921) // bits0-1 rate, bit7 mono/stereo

static uint32_t dma_started = 0;   // buffers handed to the DMA (statistics)

// Nothing is allocated here: the buffers are created by start_audio(), once the options are known.
Audio::Audio()
{
    sound_enabled = false;
    for (int i = 0; i < NUM_DMA_BUFFERS; i++) dma_buffer[i] = nullptr;
    playing = -1;
    queue_len = 0;
    queued_bytes = 0;
    mix_buffer = nullptr;
    wavfile.loaded = false;
    wavfile.data = nullptr;
}

// Stops the DMA and gives the buffers back (the DMA buffers were taken from ST-RAM with Mxalloc).
Audio::~Audio()
{
    stop_audio();
    delete[] mix_buffer;
    for (int i = 0; i < NUM_DMA_BUFFERS; i++)
        if (dma_buffer[i]) Mfree(dma_buffer[i]);
}

// Called once at start-up (main_atari.cpp): starts the sound output unless sound is disabled.
void Audio::init()
{
    if (config.sound.enabled)
        start_audio();
}

// Allocates the mix buffer and the four DMA buffers, selects the DMA rate that matches the
// engine's mix rate, and starts the DMA on a short burst of silence. tick() feeds it from then on.
void Audio::start_audio()
{
    if (sound_enabled) return;

    mix_buffer = new int16_t[DMA_BUFFER_SAMPLES];
    // DMA sound can only read ST-RAM: allocate explicitly there (a plain new would land
    // in fast RAM when the program is flagged to use it).
    for (int i = 0; i < NUM_DMA_BUFFERS; i++)
        dma_buffer[i] = (int8_t*)Mxalloc(DMA_BUFFER_SAMPLES * 2, 0); // stereo, 1 byte/sample/channel
    clear_buffers();

    // Rate code 1 == 12517 Hz (see STE_RATE); bit7=0 selects stereo output
    // per the register layout assumed above.
    // DMA rate code from the engine's mix rate: 0=6258, 1=12517, 2=25033, 3=50066 Hz.
    DMA_MODE = (config.sound.rate < 9000) ? 0 : (config.sound.rate < 18000) ? 1 : (config.sound.rate < 37000) ? 2 : 3;

    sound_enabled = true;
    playing = 0;
    queue_len = 0;
    start_dma(dma_buffer[0], 64);   // a short burst of silence; real buffers follow via service()
}

// Stops the DMA playback. The buffers stay allocated until the destructor.
void Audio::stop_audio()
{
    if (!sound_enabled) return;
    DMA_CTRL = 0;
    sound_enabled = false;
}

// Fills the mix buffer and every DMA buffer with silence.
void Audio::clear_buffers()
{
    if (mix_buffer) std::memset(mix_buffer, 0, DMA_BUFFER_SAMPLES * sizeof(int16_t));
    for (int i = 0; i < NUM_DMA_BUFFERS; i++)
        if (dma_buffer[i]) std::memset(dma_buffer[i], 0, DMA_BUFFER_SAMPLES * 2);
}

// Programs the DMA start and end addresses for `bytes` bytes of 8-bit stereo at `buffer` and
// starts it, without looping: the DMA stops by itself at the end (see dma_busy()).
void Audio::start_dma(int8_t* buffer, uint32_t bytes)
{
    uint32_t start = (uint32_t)buffer;
    uint32_t end   = start + bytes;

    DMA_START_HI  = (uint8_t)(start >> 16);
    DMA_START_MID = (uint8_t)(start >> 8);
    DMA_START_LO  = (uint8_t)(start);
    DMA_END_HI    = (uint8_t)(end >> 16);
    DMA_END_MID   = (uint8_t)(end >> 8);
    DMA_END_LO    = (uint8_t)(end);
    DMA_CTRL = 1; // on, no loop - tick() re-arms each half-buffer manually
}

// Called every game tick (see main_atari.cpp's main_loop(), same call site
// as the SDL version's audio.tick()).
void Audio::tick()
{
    if (!sound_enabled) return;

    uint32_t a0 = PERF_NOW();
    osoundint.pcm->stream_update();
    uint32_t a1 = PERF_NOW();
    osoundint.ym->stream_update();
    uint32_t a2 = PERF_NOW();

    int16_t* pcm_buffer = osoundint.pcm->get_buffer();
    int16_t* ym_buffer  = osoundint.ym->get_buffer();
    // Both chips write interleaved stereo (L, R, L, R ...): frame_size sample pairs per tick.
    uint32_t frames = osoundint.pcm->buffer_size / 2;
    if (frames > DMA_BUFFER_SAMPLES) frames = DMA_BUFFER_SAMPLES;
    uint32_t samples = frames;

    // outrun.ini's fm_half: the FM chip produced half as many frames as the PCM chip. Each one
    // is used for an even output frame, and the odd frames in between are the average of their
    // two neighbours (the last one of a step repeats: the next step is not synthesised yet).
    static int16_t ym_full[DMA_BUFFER_SAMPLES * 2];
    if (atari_opt.fm_half)
    {
        const uint32_t half = osoundint.ym->buffer_size / 2;   // frames the chip produced
        for (uint32_t f = 0; f < frames; f++)
        {
            uint32_t a = f >> 1;
            if (a >= half) a = half - 1;
            uint32_t b = (f & 1) && a + 1 < half ? a + 1 : a;
            ym_full[f * 2]     = (int16_t)((ym_buffer[a * 2]     + ym_buffer[b * 2])     >> 1);
            ym_full[f * 2 + 1] = (int16_t)((ym_buffer[a * 2 + 1] + ym_buffer[b * 2 + 1]) >> 1);
        }
        ym_buffer = ym_full;
    }

    // DSP replay running (mod_dsp=1, see dsp_replay.hpp): the DAC listens to the DSP, not to
    // the DMA, so the FM + PCM mix is queued there and played as an extra pair of voices.
    if (dsp_replay.active() || dspmod.active())
    {
        static int8_t fx[DMA_BUFFER_SAMPLES * 2];
        for (uint32_t i = 0; i < frames * 2; i++)
        {
            int32_t mix = pcm_buffer[i] + ym_buffer[i];
            if (mix > SHRT_MAX) mix = SHRT_MAX;
            else if (mix < SHRT_MIN) mix = SHRT_MIN;
            fx[i] = (int8_t)(mix >> 8);
        }
        if (dspmod.active()) dspmod.push_fx(fx, frames);
        else                 dsp_replay.push_fx(fx, frames);
#ifdef DSPFX_LOG
        // Test aid: state of the effects queue every 50 steps, in DSPFX.TXT.
        {
            static uint32_t n = 0;
            if ((++n % 50) == 0)
            {
                FILE* lf = fopen("DSPFX.TXT", "a");
                if (lf)
                {
                    fprintf(lf, "DSPFX step=%lu level=%lu underruns=%lu late=%lu\r\n", (unsigned long)n, (unsigned long)dsp_replay.fx_level(),
                            (unsigned long)dsp_replay.stat_underruns, (unsigned long)dsp_replay.stat_late);
                    fclose(lf);
                }
            }
        }
#endif
        PERF_PRINTF("AUDIO samples=%lu pcm=%lu ym=%lu mix=%lu (5ms)\r\n", (unsigned long)samples, (unsigned long)(a1-a0), (unsigned long)(a2-a1), (unsigned long)(PERF_NOW()-a2));
        return;
    }

    // Render one tick's worth of the .mod player (modplayer.cpp) into a mono buffer here, added
    // to both channels below exactly like the wavfile mechanism already does (mod music and
    // wavfile music are alternatives, not meant to run together). A no-op whenever nothing is
    // loaded (mod=0 in outrun.ini, or mod=1 but no Music\TRACKn.MOD file was found - see
    // engine/omusic.cpp), so this costs nothing when the feature is off.
    static int16_t mod_buf[DMA_BUFFER_SAMPLES];
    if (modplayer.loaded())
    {
        memset(mod_buf, 0, frames * sizeof(int16_t));
        modplayer.mix(mod_buf, frames);
    }

    // Mix (identical clipping logic to sdl2/audio.cpp), then convert the
    // engine's 16-bit signed samples down to the STE DMA's 8-bit signed
    // stereo format and write into the buffer NOT currently playing.
    service();
    int fill_idx = 0;
    while (fill_idx == playing || (queue_len > 0 && fill_idx == queue[0]) || (queue_len > 1 && fill_idx == queue[1]))
        fill_idx++;                                                   // the buffer nobody is using
    int8_t* fill = dma_buffer[fill_idx];
    for (uint32_t i = 0; i < frames * 2; i++)
    {
        int32_t mix = pcm_buffer[i] + ym_buffer[i];
        if (wavfile.loaded && wavfile.data)
        {
            mix += wavfile.data[wavfile.pos];
            if ((i & 1) && ++wavfile.pos >= wavfile.length) wavfile.pos = 0;
        }
        if (modplayer.loaded())
            mix += mod_buf[i >> 1];
        if (mix > SHRT_MAX) mix = SHRT_MAX;
        else if (mix < SHRT_MIN) mix = SHRT_MIN;

        fill[i] = (int8_t)(mix >> 8); // 16-bit -> 8-bit
    }

#ifdef AUDIO_DUMP
    // Test aid: appends what this step hands to the DMA (signed 8-bit, L,R interleaved, at the
    // mixing rate) to AUDIO.RAW, so the sound can be listened to outside the emulator.
    { FILE* df = fopen("AUDIO.RAW", "ab"); if (df) { fwrite(fill, 1, frames * 2, df); fclose(df); } }
#endif
    uint32_t a3 = PERF_NOW();
    PERF_PRINTF("AUDIO samples=%lu pcm=%lu ym=%lu mix=%lu (5ms)\r\n", (unsigned long)samples, (unsigned long)(a1-a0), (unsigned long)(a2-a1), (unsigned long)(a3-a2));

    // Queue the finished buffer behind the one being played; when two are already waiting the oldest is dropped.
#ifdef PERF_PRINT
    static uint32_t n_ticks = 0, n_dropped = 0;
    n_ticks++;
    if (queue_len == 2) n_dropped++;
    if ((n_ticks % 50) == 0)
        PERF_PRINTF("AUDIOQ ticks=%lu dropped=%lu started=%lu%c%c", (unsigned long)n_ticks, (unsigned long)n_dropped, (unsigned long)dma_started, 13, 10);
#endif
    if (queue_len == 2)
    {
        queue[0] = queue[1];
        queue_len = 1;
    }
    queue[queue_len++] = fill_idx;
    queued_bytes = samples * 2;
    service();
}

// True while the DMA is still playing a buffer (its play bit clears by itself at the end).
bool Audio::dma_busy()
{
    return (DMA_CTRL & 1) != 0;
}

// Hand the next queued buffer to the DMA once it is idle.  Called after each tick and from the
// main loop's waiting loop, so the next buffer starts within microseconds of the last one ending.
void Audio::service()
{
    if (!sound_enabled || queue_len == 0 || dsp_replay.active() || dspmod.active() || dma_busy()) return;
    playing = queue[0];
    queue[0] = queue[1];
    queue_len--;
    dma_started++;
    start_dma(dma_buffer[playing], queued_bytes);
}

// One game step without sound output, used by the main loop when the machine has no time to
// synthesise: the PCM chip still runs (the game reads its end-of-sample flags) and the FM
// chip only advances its timers, which drive the game's sound sequencer.
void Audio::tick_muted()
{
    if (!sound_enabled) return;
    osoundint.pcm->stream_update();   // sample-end flags are read by the game
    osoundint.ym->skip_frame();
}

// Interface shared with the SDL backend; no speed correction is needed here.
double Audio::adjust_speed()
{
    return 1.0;
}

// Interface shared with the SDL backend. Not implemented on this port (see the note inside).
void Audio::load_wav(const char* filename)
{
    // Not implemented in this pass (WAV loading is a minor feature used
    // for a handful of sample overrides in the PC port); clear_wav() below
    // ensures the mixer path above just skips it.
    (void)filename;
}

// Interface shared with the SDL backend: no .wav data is ever loaded on this port (see load_wav()).
void Audio::clear_wav()
{
    wavfile.loaded = false;
}
