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
#include <mint/cookie.h>
#include "atari/audio.hpp"
#include "atari/modplayer.hpp"
#include "atari/dsp_replay.hpp"
#include "atari/dspmod.hpp"
#include "atari/fmdsp.hpp"
#include "atari/options.hpp"
#include "frontend/config.hpp"
#include "engine/audio/osoundint.hpp"
#include "main.hpp"

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

#ifdef AUDIO_TIMING
static uint32_t fine_time();
uint32_t g_t_drv, g_t_pcm, g_t_fm, g_t_mix, g_t_replay, g_pictures, g_logic, g_t_prep, g_t_draw, g_rate_changes, g_rate;   // per 300 steps, 1/38400 s
uint32_t atari_fine_time() { return fine_time(); }
#define T_MARK(v) const uint32_t v = fine_time()
#else
#define T_MARK(v)
#endif

static uint32_t dma_started = 0;   // buffers handed to the DMA (statistics)
#ifdef UNDERRUN_LOG
// Test aid: each time the DMA ran out of sound (no buffer queued when it finished), the step
// number, in UNDR.TXT.
static uint32_t g_steps_mixed = 0;
static bool g_starved = false;
#endif

// Nothing is allocated here: the buffers are created by start_audio(), once the options are known.
Audio::Audio()
{
    sound_enabled = false;
    irq_on = false;
    irq_next = 0;
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
    start_irq();
}

// Stops the DMA playback. The buffers stay allocated until the destructor.
void Audio::stop_audio()
{
    stop_irq();
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
// Called every game step by the main loop: the sound of that step - unless the sound comes
// from the Timer B interrupt (irq_step()), which then does all of it.
void Audio::tick()
{
    if (!irq_on) tick_now();
}

// --------------------------------------------------------------------------
// Sound from an interrupt (outrun.ini's sound_irq, on by default).
//
// Each game step makes one step of sound (1/30 s). When the machine cannot keep up with the
// game - a Falcon 030 runs it at about a quarter of real time - steps are skipped and the
// sound comes out in pieces, whoever computes it. So the sound driver (osoundint.tick(), the
// game's Z80 program) and the sound steps are run from MFP Timer B instead, about 60 times a
// second: a step is made whenever fewer than two buffers wait for the DMA (or, while a .mod
// plays on the DSP, every 1/30 s of real time). The music then keeps its tempo and has no
// holes whatever the picture rate; the game only queues its sound commands, and the few
// things shared with it are guarded by atari_sound_lock() / atari_sound_unlock().
// --------------------------------------------------------------------------
extern "C" void sound_isr();            // sound_asm.S: Timer B, calls sound_isr_step()
extern "C" void sound_isr_step() { cannonball::audio.irq_step(); }

static void (*old_timer_b)() = 0;
static int lock_depth = 0;

#define MFP8(a) (*(volatile uint8_t*)(a))

// Keeps the sound interrupt out (nests). Called by the main program only; touches the MFP
// only while the interrupt is installed (supervisor mode then).
void atari_sound_lock()
{
    if (lock_depth++ == 0 && cannonball::audio.irq_mode()) MFP8(0xfffffa13L) &= ~0x01;   // Timer B masked (a pending one waits)
}

void atari_sound_unlock()
{
    if (lock_depth > 0 && --lock_depth == 0 && cannonball::audio.irq_mode()) MFP8(0xfffffa13L) |= 0x01;
}

void Audio::start_irq()
{
    if (irq_on || !atari_opt.sound_irq) return;
    if (atari_opt.sound_irq == 2)
    {
        long cpu = 0;
        if (Getcookie(C__CPU, &cpu) != C_FOUND || cpu < 40) return;   // automatic: 68040/68060 only
    }
    irq_next = *(volatile uint32_t*)0x4BAL * 3;
    MFP8(0xfffffa1bL) = 0;                       // Timer B stopped
    old_timer_b = *(void (**)())0x120L;
    *(void (**)())0x120L = sound_isr;
    MFP8(0xfffffa07L) |= 0x01;                   // enabled
    if (lock_depth == 0) MFP8(0xfffffa13L) |= 0x01;   // unmasked
    MFP8(0xfffffa21L) = 205;                     // 2457600 / 200 / 205 = 59.9 Hz
    MFP8(0xfffffa1bL) = 7;                       // delay mode, divider 200
    irq_on = true;
}

void Audio::stop_irq()
{
    if (!irq_on) return;
    MFP8(0xfffffa1bL) = 0;
    MFP8(0xfffffa07L) &= ~0x01;
    MFP8(0xfffffa13L) &= ~0x01;
    MFP8(0xfffffa0fL) &= ~0x01;                  // nothing left in service
    *(void (**)())0x120L = old_timer_b;
    irq_on = false;
}

// From the Timer B interrupt (with the interrupt level lowered, never re-entered).
void Audio::irq_step()
{
    if (!sound_enabled || !osoundint.ym || !osoundint.pcm) return;
    const uint32_t now = *(volatile uint32_t*)0x4BAL * 3;    // 1/600 s
    if (dsp_replay.active() || dspmod.active())
    {
        // the DAC listens to the DSP: one step per 1/30 s of real time
        if ((int32_t)(now - irq_next) < 0) return;
        if ((int32_t)(now - irq_next) > 6 * 20) irq_next = now;   // far behind: start again
        irq_next += 20;
    }
    else
    {
        service_now();
        if (queue_len >= 2) return;                // enough sound waiting for the DMA
        irq_next = now;
    }
    T_MARK(d0);
    osoundint.tick();
#ifdef AUDIO_TIMING
    g_t_drv += fine_time() - d0;
#endif
    tick_now();
}

#ifdef AUDIO_TIMING
// Test aid: 68k time spent in tick(), in 1/38400 s (200 Hz counter + MFP Timer C), averaged
// over 300 steps into AUDT.TXT.
static uint32_t fine_time()
{
    volatile uint32_t* hz = (volatile uint32_t*)0x4BAL;
    volatile uint8_t* tcdr = (volatile uint8_t*)0xfffffa23L;
    uint32_t a, b; uint8_t c;
    do { a = *hz; c = *tcdr; b = *hz; } while (a != b);
    return a * 192 + (192 - c);
}
void Audio::tick_now()
{
    const uint32_t t0 = fine_time();
    tick_body();
    static uint32_t sum = 0, n = 0, mx = 0;
    const uint32_t dt = fine_time() - t0;
    sum += dt; if (dt > mx) mx = dt;
    if (++n == 300)
    {
        FILE* f = fopen("AUDT.TXT", "a");
        static uint32_t last = 0;
        const uint32_t now = fine_time();
        if (f) { fprintf(f, "fm_dsp=%d tick avg %lu us max %lu us; 300 steps took %lu ms (10000 = real time); per step: driver %lu pcm %lu fm %lu us; .mod replay %lu us; pictures %lu game steps %lu; per picture: prepare %lu us, to screen %lu us; auto rate %lu/2 fps, %lu changes\r\n", fmdsp.active() ? 1 : 0,
                         (unsigned long)(sum / n * 26), (unsigned long)(mx * 26), (unsigned long)(last ? (now - last) * 26 / 1000 : 0),
                         (unsigned long)(g_t_drv / n * 26), (unsigned long)(g_t_pcm / n * 26), (unsigned long)(g_t_fm / n * 26),
                         (unsigned long)(g_t_replay / n * 26), (unsigned long)g_pictures, (unsigned long)g_logic,
                         (unsigned long)(g_pictures ? g_t_prep / g_pictures * 26 : 0), (unsigned long)(g_pictures ? g_t_draw / g_pictures * 26 : 0),
                         (unsigned long)g_rate, (unsigned long)g_rate_changes); fclose(f); }
        g_t_drv = g_t_pcm = g_t_fm = g_t_replay = g_pictures = g_logic = g_t_prep = g_t_draw = g_rate_changes = 0;
        last = now;
        sum = n = mx = 0;
    }
}
void Audio::tick_body()
#else
void Audio::tick_now()
#endif
{
    if (!sound_enabled) return;

    uint32_t a0 = PERF_NOW();
    T_MARK(m0);
    osoundint.pcm->stream_update();
#ifdef AUDIO_TIMING
    g_t_pcm += fine_time() - m0;
#endif
    uint32_t a1 = PERF_NOW();
    const bool fm_on_dsp = use_fmdsp();
#ifdef FMDSP_VERIFY
    // Test aid: the 68k synthesises the FM as well, and the DSP's output is compared with it.
    osoundint.ym->stream_update();
#else
    if (fm_on_dsp) osoundint.ym->skip_frame();   // timers only: the DSP makes the sound
    else           osoundint.ym->stream_update();
#endif
    uint32_t a2 = PERF_NOW();

    int16_t* pcm_buffer = osoundint.pcm->get_buffer();
    int16_t* ym_buffer  = osoundint.ym->get_buffer();
    // Both chips write interleaved stereo (L, R, L, R ...): frame_size sample pairs per tick.
    uint32_t frames = osoundint.pcm->buffer_size / 2;
    if (frames > DMA_BUFFER_SAMPLES) frames = DMA_BUFFER_SAMPLES;
    uint32_t samples = frames;

    // FM on the DSP (outrun.ini's fm_dsp, fmdsp.hpp): it gives back the previous step, so the
    // PCM chip's output is delayed by one step too (the music's drums are on the PCM chip).
    static int16_t pcm_late[DMA_BUFFER_SAMPLES * 2];
    static int16_t fm_silence[DMA_BUFFER_SAMPLES * 2];
    if (fm_on_dsp)
    {
        T_MARK(f0);
        const int16_t* fm = fmdsp.step(frames);
#ifdef AUDIO_TIMING
        g_t_fm += fine_time() - f0;
#endif
#ifdef FMDSP_VERIFY
        {
            static int16_t ref[DMA_BUFFER_SAMPLES * 2];
            static bool have_ref = false;
            static uint32_t steps = 0, bad = 0, exact = 0;
            if (have_ref)
            {
                uint32_t d = 0;
                for (uint32_t i = 0; i < frames * 2; i++) if ((fm ? fm[i] : 0) != ref[i]) d++;
                if (d) bad++; else exact++;
                if (d && bad <= 200)
                {
                    FILE* f = fopen("VERIFY.TXT", "a");
                    if (f) { fprintf(f, "step %lu: %lu samples differ\r\n", (unsigned long)steps, (unsigned long)d); fclose(f); }
                }
            }
            if (++steps % 300 == 0)
            {
                FILE* f = fopen("VERIFY.TXT", "a");
                if (f) { fprintf(f, "%lu steps: %lu exact, %lu differ\r\n", (unsigned long)steps, (unsigned long)exact, (unsigned long)bad); fclose(f); }
            }
            std::memcpy(ref, osoundint.ym->get_buffer(), frames * 4);
            have_ref = true;
        }
#endif
        static int16_t pcm_now[DMA_BUFFER_SAMPLES * 2];
        std::memcpy(pcm_now, pcm_late, frames * 4);
        std::memcpy(pcm_late, pcm_buffer, frames * 4);
        pcm_buffer = pcm_now;
        ym_buffer = fm ? (int16_t*)fm : fm_silence;
    }

    // outrun.ini's fm_half: the FM chip produced half as many frames as the PCM chip. Each one
    // is used for an even output frame, and the odd frames in between are the average of their
    // two neighbours (the last one of a step repeats: the next step is not synthesised yet).
    static int16_t ym_full[DMA_BUFFER_SAMPLES * 2];
    if (atari_opt.fm_half && !fm_on_dsp && osoundint.ym->buffer_size < osoundint.pcm->buffer_size)
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
    service_now();
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
    { FILE* df = fopen("FM.RAW", "ab"); if (df) { fwrite(ym_buffer, 2, frames * 2, df); fclose(df); } }   // the FM part alone (16-bit)
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
#ifdef UNDERRUN_LOG
    g_steps_mixed++;
#endif
    service_now();
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
    if (!irq_on) service_now();
}

void Audio::service_now()
{
#ifdef UNDERRUN_LOG
    if (sound_enabled && dma_started && queue_len == 0 && !dma_busy() && !g_starved)
    {
        g_starved = true;
        FILE* f = fopen("UNDR.TXT", "a");
        if (f) { fprintf(f, "underrun after step %lu\r\n", (unsigned long)g_steps_mixed); fclose(f); }
    }
    if (queue_len) g_starved = false;
#endif
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
    if (!sound_enabled || irq_on) return;
    osoundint.pcm->stream_update();   // sample-end flags are read by the game
    osoundint.ym->skip_frame();
    if (fmdsp.active()) fmdsp.step(osoundint.pcm->buffer_size / 2);   // the DSP's FM chip keeps up
}

// From OSoundInt::init(), right after the FM chip was reset: the DSP program is started again
// from the same reset state (fmdsp.hpp), unless the DSP is busy with a .mod.
void atari_fm_restart()
{
    if (!atari_opt.fm_dsp || dsp_replay.active() || dspmod.holds_dsp()) return;
    fmdsp.stop();
    fmdsp.start();
}

// True when the FM sound of this step is to be computed by the DSP (fmdsp.hpp): asked for in
// outrun.ini, the DSP not taken by a .mod replay, and the FM chip at the mixing rate. Starts the
// DSP program if needed (at start-up, or once a DSP .mod replay has given the DSP back).
bool Audio::use_fmdsp()
{
    // (DSPMOD keeps its program in the DSP between two songs: the DSP is not free then)
    if (!atari_opt.fm_dsp || dsp_replay.active() || dspmod.holds_dsp()) return false;
    if (osoundint.ym->buffer_size != osoundint.pcm->buffer_size || osoundint.pcm->buffer_size / 2 > FmDsp::MAX_FRAMES)
        return false;
    return fmdsp.start();
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
