/***************************************************************************
    Cannonball Atari Mega STE Entry Point.

    Replaces main.cpp for this target. main.cpp is SDL2-specific (SDL_Init,
    SDL event loop, SDL_Delay-based pacing, game controller mapping file,
    force-feedback init) and is not compiled for PLATFORM_ATARI - this file
    reimplements only the platform glue, calling into exactly the same
    engine entry points (Roms::load_revb_roms, Outrun::init/tick,
    OSoundInt::tick, Video::init/prepare_frame/render_frame) that main.cpp
    does. No engine/hwvideo/hwaudio file is touched to make this work.

    v1 scope cuts, deliberately made to keep this a "transpose" rather than
    a rewrite of the frontend layer (see frontend/config.cpp's PLATFORM_ATARI
    branch for the matching config-side cuts):
      - No frontend menu (Menu class / menu.cpp) is linked in. The game
        boots straight to STATE_INIT_GAME every time.
      - No command-line parsing, no config.xml, no hiscore persistence.
      - No widescreen/hi-res video modes (atari/video.hpp only supports the
        native 320x224->320x200 cropped path).

    Builds for the Mega STE and the Falcon 030/060 (see README_ATARI.md).

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

#include <cstdlib>
#include <cstdio>
#include <new>
#include <cstring>
#include <mint/osbind.h>
#include <mint/basepage.h>
#include <mint/cookie.h>

// Stack size requested from the MiNTLib startup code (__stksize).
extern "C" long __stksize = 262144L;
#include "video.hpp"
#include "romloader.hpp"
#include "trackloader.hpp"
#include "stdint.hpp"
#include "main.hpp"
#include "engine/outrun.hpp"
#include "engine/oroad.hpp"
#include "engine/oinitengine.hpp"
#include "frontend/config.hpp"

#include "engine/oinputs.hpp"
#include "engine/ooutputs.hpp"

#include "atari/timer.hpp"
#include "atari/input.hpp"
#include "atari/options.hpp"
#include "atari/dsp_replay.hpp"
#include "atari/dspmod.hpp"
#include "atari/fmdsp.hpp"

void atari_redraw_desktop();   // atari/gemredraw.cpp

using namespace cannonball;

// On a PC the OS hands out zeroed pages, so engine code that relies on
// freshly `new`ed memory being zero happens to work. TOS's Malloc does not
// zero anything, so replicate that behaviour explicitly.
void* operator new(std::size_t n)
{
    void* p = malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    memset(p, 0, n);
    return p;
}
void* operator new[](std::size_t n) { return operator new(n); }
void operator delete(void* p) throw()   { free(p); }
void operator delete[](void* p) throw() { free(p); }

int    cannonball::state       = STATE_BOOT;
double cannonball::frame_ms    = 0;
int    cannonball::frame       = 0;
bool   cannonball::tick_frame  = true;
int    cannonball::fps_counter = 0;

Audio cannonball::audio;
bool pause_engine;
bool g_take_screenshot;
bool g_quit_confirm;

// The original (user-mode) stack pointer Super(0L) hands back when switching to supervisor mode
// at the very start of main() - saved so quit_func() can switch back to user mode before exit().
static long g_user_ssp = 0;

// Everything the game needs to give back before it goes away, but NOT the actual process
// termination call - see quit_func() (still used for the early ROM/video-init failure paths)
// and the STATE_QUIT handling in main_loop()/main() (the normal, in-game Escape/F10 path) for
// the two different ways this program can end.
static void quit_cleanup()
{
    audio.stop_audio();
    dsp_replay.stop();      // hands Timer A, the sound matrix and the DSP back (no-op unless mod_dsp started it)
    dspmod.shutdown();      // same for DSPMOD (mod_dsp = 2)
    fmdsp.stop();           // and the FM program (fm_dsp = 1)
    input.shutdown();   // give the keyboard interrupt vector back (TOS does not reclaim vectors)
    // Put the display back the way it was before the game took over (see atari/video_falcon.cpp's
    // Render::disable()), so EmuTOS's own desktop is actually visible again afterwards - called
    // while still in supervisor mode, same as every other VsetScreen call in this codebase.
    video.disable();
}

// Ends the program from a point where returning from main() is not possible (start-up
// failures): cleans up, leaves supervisor mode and terminates.
static void quit_func(int code)
{
    quit_cleanup();
    // Back to user mode before terminating: this program spends its whole life in supervisor mode
    // (Super(0L) in main(), for direct hardware access). SuperToUser(), not a plain Super(ptr):
    // osbind.h documents a real EmuTOS bug where a plain Super(oldssp) gives the wrong user stack
    // pointer if the supervisor stack moved since the original Super(0L) - which it certainly has
    // by the time this runs - SuperToUser() backs up/restores around the trap to avoid it.
    SuperToUser((void*)g_user_ssp);
    // This whole quit_func()/Pterm() path was extensively tested this session (every combination
    // of exit()/Pterm(), with/without SuperToUser(), with/without any cleanup at all) and never
    // once returned to a usable desktop when triggered from inside the running game - it always
    // freezes the machine instead, GEMDOS's own doing, not this program's, since even a Pterm()
    // called with literally nothing else run first behaves identically. Kept only for the ROM
    // load / video init failure paths below (main(), before the game ever starts running - those
    // still need an immediate, unconditional way out). The Escape/F10 in-game quit path no longer
    // uses this function at all: it sets state = STATE_QUIT and lets main_loop()/main() unwind
    // and return from main() normally instead, which is what a well-behaved program launched from
    // the AUTO folder (as this one always is here, launched before the desktop/AES exists yet -
    // see README_ATARI.md) is expected to do, rather than force-terminating via a GEMDOS trap.
    Pterm(code);
}

// No event queue on this target: input.frame_done() polls the keyboard
// state maintained by the IKBD vector directly (see atari/input.cpp)
// instead of draining an SDL event queue.
static void tick()
{
    // Escape or F10: raise the "QUIT" confirmation overlay (drawn in atari/video_falcon.cpp's
    // Render::draw_frame(), see g_quit_confirm there) instead of quitting immediately - resolved
    // below by UP/DOWN once state==STATE_GAME. Reads the interrupt-maintained scancode table
    // directly (Input::escape_down()/f10_down(), see atari/input.hpp) - one byte-array read each.
    if (input.escape_down() || input.f10_down())
        g_quit_confirm = true;

    frame++;

    if (config.fps == 60)
        tick_frame = frame & 1;
    else if (config.fps == 120)
        tick_frame = (frame & 3) == 1;

    if (tick_frame)
    {
        oinputs.tick();
        oinputs.do_gear();
    }

    switch (state)
    {
        case STATE_GAME:
        {
            if (tick_frame)
            {
                if (input.has_pressed(Input::TIMER)) outrun.freeze_timer = !outrun.freeze_timer;
                // The "PAUSE" caption itself is drawn straight into the RGB565 output in
                // atari/video_falcon.cpp's Render::draw_frame() (see cannonball::pause_engine
                // there) rather than through the engine's own HUD tile layer: the HUD's debug
                // font (engine/ohud.cpp's blit_text_new(), used by the FPS counter/debug overlay)
                // turned out not to render anything visible with this ROM set's tile data when
                // tried here - not worth the ROM-reverse-engineering to fix, when a handful of
                // hardcoded glyph bitmaps blitted after palette conversion sidesteps the whole
                // ROM-tile-encoding question and is guaranteed the same regardless of game state.
                if (input.has_pressed(Input::PAUSE)) pause_engine = !pause_engine;
                // F9: screenshot (not C - suspected host-level interception, see input.hpp's
                // SC_F9 comment). The actual capture happens in atari/video_falcon.cpp's
                // Render::draw_frame() (see g_take_screenshot there), which is where the final,
                // already-composited RGB565 picture is available; this just raises the request.
                if (input.has_pressed(Input::SCREENSHOT)) g_take_screenshot = true;
                // Escape/F10 are read directly at the top of tick(); this resolves the "QUIT"
                // overlay they raise (see g_quit_confirm there and in video_falcon.cpp). UP/DOWN
                // confirm/cancel it - not Y/N: both the has_pressed(QUIT_YES/QUIT_NO) path and a
                // direct scan_state read of Y/N (0x15/0x31) produced a system beep with no other
                // effect, for reasons not understood, while UP/DOWN are proven reliable all
                // session (steering) - has_pressed() itself is not the problem, only those two
                // particular scancodes are, so this stays on the ordinary has_pressed() path.
                if (g_quit_confirm)
                {
                    // state = STATE_QUIT, not quit_func(0): see quit_func()'s comment - letting
                    // main_loop() notice state==STATE_QUIT, do its own quit_cleanup() and return
                    // (main() then returns normally too, see there) is what actually gets back to
                    // a working desktop; explicitly terminating via Pterm() never did.
                    if (input.has_pressed(Input::UP)) state = STATE_QUIT;
                    if (input.has_pressed(Input::DOWN)) g_quit_confirm = false;
                }
            }

            if ((!pause_engine && !g_quit_confirm) || input.has_pressed(Input::STEP))
            {
#ifdef PERF_PRINT
                uint32_t q0 = PERF_NOW();
#endif
                outrun.tick(tick_frame);
#ifdef PERF_PRINT
                uint32_t q1 = PERF_NOW();
#endif
                if (tick_frame) input.frame_done();
#ifndef NO_SOUND   // -DNO_SOUND also skips the engine's own sound command processing
                if (!audio.irq_mode()) osoundint.tick();   // else the sound interrupt runs it (atari/audio.cpp)
#endif
#ifdef PERF_PRINT
                {
                    static uint32_t a_out = 0, a_snd = 0, a_n = 0;
                    a_out += q1 - q0;
                    a_snd += PERF_NOW() - q1;
                    if (++a_n == 50)
                    {
                        PERF_PRINTF("STEP50 outrun=%lu rest=%lu (5ms)\r\n", (unsigned long)a_out, (unsigned long)a_snd);
                        a_out = a_snd = a_n = 0;
                    }
                }
#endif
            }
            else if (tick_frame)
            {
                input.frame_done();
            }
        }
        break;

        case STATE_INIT_GAME:
            if (config.engine.jap && !roms.load_japanese_roms())
            {
                state = STATE_QUIT;
            }
            else
            {
                tick_frame = true;
                pause_engine = false;
                outrun.init();
                state = STATE_GAME;
            }
            break;

        // STATE_MENU / STATE_INIT_MENU intentionally absent - no frontend
        // menu is linked in this pass (see file header).
        default:
            break;
    }

    outrun.outputs->writeDigitalToConsole();
}

#ifdef PLATFORM_FALCON
// Fixed-timestep loop (Falcon).  Game logic is decoupled from drawing: it always
// advances at the original 30 steps per second of real time, however slowly the
// frames can be drawn.  When drawing is slower than one step, several steps run
// back to back before the next picture (the picture is just the latest state);
// when the machine is faster, it waits for the next step.
#ifdef BENCH_N
#ifndef BENCH_START
#define BENCH_START 100      // pictures skipped (ROM loading, menus) before timing starts
#endif
#endif
#ifdef OLD_PACING
static void main_loop()
{
    volatile uint32_t* hz200 = (volatile uint32_t*)0x4BA;
    const int32_t TICK_UNITS = 20;          // 1/30 s, in units of 1/600 s (hz200 * 3)
    const int     MAX_TICKS  = 60;          // safety cap on steps run between two pictures
    const int32_t MAX_LOOP_UNITS = 600;     // never keep a picture waiting more than ~1 s catching up
    uint32_t next_tick  = *hz200 * 3;
    int      last_ticks = 1;
    int      renders    = 0;

    while (state != STATE_QUIT)
    {
        int ticks = 0;
        bool behind = false;
        const uint32_t loop_start = *hz200 * 3;
        while (ticks < MAX_TICKS)
        {
            uint32_t now = *hz200 * 3;
            if ((int32_t)(now - next_tick) < 0)
            {
                if (ticks) break;           // caught up: go and draw
                audio.service();            // ahead of real time: wait for the next step, keeping the sound DMA fed
                continue;
            }
            if (ticks && (int32_t)(now - loop_start) > MAX_LOOP_UNITS)
            {
                behind = true;              // catching up is taking too long: draw now
                break;
            }
            tick();
            // Real-time sound only when one step per picture is enough; otherwise
            // just keep the chips' timers running so the game's sound logic carries on.
#ifdef FORCE_SOUND
            audio.tick();                   // test option: always synthesise, however slow
#else
            if (ticks == 0 && last_ticks <= 1) audio.tick(); else audio.tick_muted();
#endif
            next_tick += TICK_UNITS;
            ticks++;
        }
        if (ticks == MAX_TICKS || behind)
            next_tick = *hz200 * 3;         // hopelessly behind: forget the backlog
        last_ticks = ticks;

        video.prepare_frame();
        video.render_frame();

        renders++;
#ifdef BENCH_N
        // Test aid (-DBENCH_N=400): time N pictures without printing anything in between
        // (console output scrolls the screen and would distort the measurement).
        {
            static uint32_t b_hz = 0, last_p = 0;
            static int b_frame = 0, cnt = 0;
            static double sum = 0, sumsq = 0, ssum = 0, ssumsq = 0; static int maxms = 0;
            static int last_f = 0;
            const uint32_t p1 = *hz200 * 3;
            if (renders == BENCH_START) { b_hz = *hz200; b_frame = cannonball::frame; last_p = p1; }
            else if (renders > BENCH_START && renders <= BENCH_START + BENCH_N)
            {
                double d = (double)(p1 - last_p) * 1000.0 / 600.0;
                last_p = p1; sum += d; sumsq += d * d; cnt++;
                if ((int)d > maxms) maxms = (int)d;
                { double sp = (double)(cannonball::frame - last_f); ssum += sp; ssumsq += sp * sp; }
            }
            if (renders >= BENCH_START && renders <= BENCH_START + BENCH_N) last_f = cannonball::frame;
            if (0)
            {
            }
            if (renders == BENCH_START + BENCH_N)
            {
                uint32_t dt = *hz200 - b_hz;
                double mean = sum / cnt, var = sumsq / cnt - mean * mean;
                double smean = ssum / cnt, svar = ssumsq / cnt - smean * smean;
                printf("BENCH pictures=%d hz200=%lu logic=%d K=0 interval_ms=%d max_ms=%d std_ms=%d steps_per_pic_x100=%d std_x100=%d%c%c", BENCH_N, (unsigned long)dt,
                       cannonball::frame - b_frame, (int)mean, maxms, (int)(var > 0 ? __builtin_sqrt(var) : 0), (int)(smean * 100), (int)(100 * (svar > 0 ? __builtin_sqrt(svar) : 0)), 13, 10);
            }
        }
#endif
        if (renders == 10 || renders == 40 || (renders % 25) == 0)
            PERF_PRINTF("MARK frame=%d hz200=%lu state=%d pos=%ld speed=%ld logic=%d\r\n", renders, (unsigned long)*hz200, (int)outrun.game_state, (long)(oroad.road_pos >> 16), (long)(oinitengine.car_increment >> 16), cannonball::frame);
    }

    quit_func(0);
}
#else
// Fixed-cadence loop (Falcon).  Steps run at their scheduled times, 30 per second; a picture is
// drawn after every K-th step, K being the smallest number that lets a picture fit in the time
// of K steps (1 = 30 pictures/s, 2 = 15, ...), so the picture rate is steady and every picture
// advances the game by the same number of steps.  Sound is synthesised only when the machine
// was seen to be idle (waiting for the next step) for long enough.
static void main_loop()
{
    volatile uint32_t* hz200 = (volatile uint32_t*)0x4BA;
    const int32_t TICK_UNITS = 20;          // 1/30 s, in units of 1/600 s (hz200 * 3)
    const int     K_MAX      = 4;
    const uint32_t SOUND_UNITS = 10;        // about what one step of synthesis costs on a fast 060 (1/600 s)
    uint32_t next_tick = *hz200 * 3;
    int      K = atari_opt.cadence ? atari_opt.cadence : 2, steps = 0, votes = 0;
    int      pic_acc = 0;                   // pictures owed, in 1/60 units (fps and automatic mode)
    // Automatic mode (cadence = 0, fps = 0): pictures per second in 1/2 units, from 30 down
    // to 7.5. It goes down one level as soon as the current one has not fitted for a few
    // pictures, and up one level only after ~3 s in which the slowest recent pictures would
    // have fitted the faster level with 20 % to spare: no back and forth at a threshold.
    static const int LEVEL2[] = { 60, 50, 40, 30, 20, 15 };
    const int NLEVELS = 6;
    int      lvl = 3, up_votes = 0, down_votes = 0;
    uint32_t P_peak = 0;                    // slowest recent picture, decaying (1/600 s)
    uint32_t P = 0, L = 0;                  // smoothed picture / logic-step time, 1/600 s
    uint32_t idle = 0, wait_start = 0;
    bool     waiting = false, audio_on = false;
    int      renders = 0;

    while (state != STATE_QUIT)
    {
        uint32_t now = *hz200 * 3;
        if ((int32_t)(now - next_tick) < 0)
        {
            if (!waiting) { waiting = true; wait_start = now; }
            audio.service();                // ahead of schedule: keep the sound DMA fed
            continue;
        }
        if (waiting) { idle += now - wait_start; waiting = false; }
#ifndef CATCHUP_STEPS
#define CATCHUP_STEPS 6
#endif
        if ((int32_t)(now - next_tick) > CATCHUP_STEPS * TICK_UNITS)
            next_tick = now;                // hopelessly behind: forget the backlog

        tick();
#ifdef AUDIO_TIMING
        { extern uint32_t g_logic; g_logic++; }
#endif
        const uint32_t t1 = *hz200 * 3;
        L = (3 * L + (t1 - now)) / 4;
#ifdef FORCE_SOUND
        audio.tick();
#else
        if (audio_on) audio.tick(); else audio.tick_muted();
#endif
        next_tick += TICK_UNITS;

        if (atari_opt.cadence == 0)
        {
            // fps = N (or the automatic level): a picture after the steps that bring N/30 of a
            // picture each, e.g. 25 = five pictures out of six steps
            pic_acc += atari_opt.fps ? 2 * atari_opt.fps : LEVEL2[lvl];
            if (pic_acc < 60)
                continue;
            pic_acc -= 60;
        }
        else if (++steps < K)
            continue;
        steps = 0;

        const uint32_t p0 = *hz200 * 3;
#ifdef AUDIO_TIMING
        extern uint32_t g_t_prep, g_t_draw; extern uint32_t atari_fine_time();
        const uint32_t f0 = atari_fine_time();
        video.prepare_frame();
        const uint32_t f1 = atari_fine_time();
        video.render_frame();
        g_t_prep += f1 - f0; g_t_draw += atari_fine_time() - f1;
#else
        video.prepare_frame();
        video.render_frame();
#endif
#ifdef AUDIO_TIMING
        { extern uint32_t g_pictures; g_pictures++; }
#endif
        const uint32_t p1 = *hz200 * 3;
        P = (3 * P + (p1 - p0)) / 4;
        P_peak -= P_peak / 16;
        if (p1 - p0 > P_peak) P_peak = p1 - p0;
        renders++;
#ifdef AUTOSHOT
        // Test aid (-DAUTOSHOT, with -DAUTOPLAY): saves two pictures of the drive as SHOTnnnn.PNG
        // without anyone at the keyboard, so the display can be looked at outside the emulator.
#ifdef AUTOPLAY_GRASS
        if (renders >= 300 && renders % 20 == 0 && renders <= 700) g_take_screenshot = true;
#else
        if (renders == 170 || renders == 260) g_take_screenshot = true;
#endif
#endif

        // K for the next pictures: P + K*L must fit in K*TICK_UNITS (10 % margin).
        {
            const int32_t room = TICK_UNITS - (int32_t)L;
            int target = K_MAX;
            if (room > 0)
            {
                target = (int)(((P * 11 / 10) + room - 1) / room);
                if (target < 1) target = 1;
                if (target > K_MAX) target = K_MAX;
            }
            (void)target; (void)votes;
            if (atari_opt.cadence)
                K = atari_opt.cadence;      // fixed by the option
            else
            {
                // a level fits when a picture plus the steps it covers (60 / LEVEL2) fit their time
                #define FITS(l, pic, margin) (room > 0 && (int32_t)(pic) * (100 + (margin)) * LEVEL2[l] <= 60 * 100 * room)
                if (!atari_opt.fps)
                {
#ifdef AUDIO_TIMING
                    extern uint32_t g_rate_changes, g_rate; const int lvl_before = lvl;
#endif
                    if (!FITS(lvl, P, 0)) { if (++down_votes >= 6 && lvl < NLEVELS - 1) { lvl++; down_votes = up_votes = 0; } }
                    else down_votes = 0;
                    if (lvl > 0 && FITS(lvl - 1, P_peak, 20))
                    {
                        if (++up_votes >= 3 * LEVEL2[lvl] / 2) { lvl--; up_votes = down_votes = 0; }
                    }
                    else up_votes = 0;
#ifdef AUDIO_TIMING
                    if (lvl != lvl_before) g_rate_changes++;
                    g_rate = LEVEL2[lvl];
#endif
                }
                #undef FITS
                // steps per picture, for the sound's idle-time rule below
                const int r2 = atari_opt.fps ? 2 * atari_opt.fps : LEVEL2[lvl];
                K = (60 + r2 - 1) / r2;
            }
        }
        // Sound on when the last group left enough idle time for K steps of synthesis
        // (switch on with a margin, off only when it has clearly run out).
        if (atari_opt.sound == 0)      audio_on = false;
        else if (atari_opt.sound == 1) audio_on = true;
        else if (audio_on)             audio_on = idle >= (uint32_t)K * 2;
        else                           audio_on = idle >= (uint32_t)K * SOUND_UNITS;
        idle = 0;

#ifdef BENCH_N
        // Test aid (-DBENCH_N=400): time N pictures and report their rhythm, printing nothing in between.
        {
            static uint32_t b_hz = 0, last_p = 0;
            static int b_frame = 0, cnt = 0;
            static double sum = 0, sumsq = 0, ssum = 0, ssumsq = 0; static int maxms = 0;
            static int last_f = 0;
            if (renders == BENCH_START) { b_hz = *hz200; b_frame = cannonball::frame; last_p = p1; }
            else if (renders > BENCH_START && renders <= BENCH_START + BENCH_N)
            {
                double d = (double)(p1 - last_p) * 1000.0 / 600.0;      // ms between two pictures
                last_p = p1; sum += d; sumsq += d * d; cnt++;
                if ((int)d > maxms) maxms = (int)d;
                { double sp = (double)(cannonball::frame - last_f); ssum += sp; ssumsq += sp * sp; }
            }
            if (renders >= BENCH_START && renders <= BENCH_START + BENCH_N) last_f = cannonball::frame;
            if (0)
            {
            }
            if (renders == BENCH_START + BENCH_N)
            {
                uint32_t dt = *hz200 - b_hz;
                double mean = sum / cnt, var = sumsq / cnt - mean * mean;
                double smean = ssum / cnt, svar = ssumsq / cnt - smean * smean;
                printf("BENCH pictures=%d hz200=%lu logic=%d K=%d interval_ms=%d max_ms=%d std_ms=%d steps_per_pic_x100=%d std_x100=%d audio=%d%c%c", BENCH_N, (unsigned long)dt,
                       cannonball::frame - b_frame, K, (int)mean, maxms, (int)(var > 0 ? __builtin_sqrt(var) : 0), (int)(smean * 100), (int)(100 * (svar > 0 ? __builtin_sqrt(svar) : 0)), (int)audio_on, 13, 10);
                // Exit cleanly right after printing: stdout to a redirected file is fully buffered
                // (not line-buffered) while the guest process is alive, so without an explicit exit
                // here the BENCH line just sits in the C library's buffer and is never seen - only
                // process termination (normal exit(), which flushes stdio) writes it out.
                fflush(stdout);
                quit_func(0);
            }
        }
#endif
        if (renders == 10 || renders == 40 || (renders % 25) == 0)
        {
            PERF_PRINTF("MARK frame=%d hz200=%lu K=%d P=%lu L=%lu audio=%d logic=%d\r\n", renders, (unsigned long)*hz200, K, (unsigned long)P, (unsigned long)L, (int)audio_on, cannonball::frame);
#ifdef MARK_FILE
            // Test aid: overwrites (not appends) a single-line file with the latest MARK snapshot
            // plus `state`, at the same (already infrequent) cadence as the line above - so whatever
            // is on disk when the process is killed is the LAST picture that actually completed,
            // letting a freeze be located precisely (e.g. state already STATE_QUIT but stuck before
            // quit_func, vs stuck earlier still in STATE_GAME).
            {
                FILE* lf = fopen("MARK.TXT", "w");
                if (lf) { fprintf(lf, "renders=%d frame=%d hz200=%lu K=%d state=%d game_state=%d steps=%d\r\n",
                                   renders, cannonball::frame, (unsigned long)*hz200, K, (int)state, (int)outrun.game_state, steps); fclose(lf); }
            }
#endif
        }
    }

    quit_cleanup();
}
#endif
#else
static void main_loop()
{
    Timer frame_time;
    volatile uint32_t* hz200 = (volatile uint32_t*)0x4BA;
    uint32_t t_start = *hz200, t_tick = 0, t_video = 0, t_audio = 0;
    int nframes = 0;

    while (state != STATE_QUIT)
    {
        frame_time.start();

        uint32_t a = *hz200;
        tick();
        uint32_t b = *hz200;

        video.prepare_frame();
        video.render_frame();
        uint32_t c = *hz200;

        audio.tick();
        uint32_t d = *hz200;
        static int frames_total = 0;
        frames_total++;
        if (frames_total == 10 || frames_total == 40 || (frames_total % 25) == 0)
            PERF_PRINTF("MARK frame=%d hz200=%lu state=%d pos=%ld speed=%ld\r\n", frames_total, (unsigned long)d, (int)outrun.game_state, (long)(oroad.road_pos >> 16), (long)(oinitengine.car_increment >> 16));
        t_tick += b - a; t_video += c - b; t_audio += d - c;
        if (++nframes == 5)
        {
            PERF_PRINTF("PERF 5 frames: total=%lu tick=%lu video=%lu audio=%lu (units 5ms)\r\n",
                   (unsigned long)(d - t_start), (unsigned long)t_tick,
                   (unsigned long)t_video, (unsigned long)t_audio);
            nframes = 0; t_start = *hz200; t_tick = t_video = t_audio = 0;
        }

        // Frame pacing: wait for the next VBL rather than an SDL_Delay()
        // ms estimate. On a PAL Mega STE this is ~50Hz; the engine's own
        // config.set_fps()/tick_frame halving logic (above) already
        // divides that down to the original arcade's 30/60fps split, the
        // same way the SDL build divides its own display's refresh rate.
        // NOTE: this ties frame rate to the display refresh rate with no
        // slack for a slow frame - if hwroad/hwtiles/hwsprites + the
        // atari/video.cpp reduction pipeline take longer than one VBL,
        // this will simply run at a fraction of 50Hz rather than dropping
        // frames gracefully. See top-level report's performance risk note.
#ifdef PLATFORM_FALCON
        frame_pace();
#else
        vbl_wait();
#endif
    }

    quit_func(0);
}
#endif // PLATFORM_FALCON

int main(int argc, char* argv[])
{
    (void)argc; (void)argv; // no command-line parsing

    // This "mint" target binary starts in user mode; direct hardware register access
    // (video shifter, DMA sound, ...) needs supervisor mode. The returned value is the user-mode
    // stack pointer, saved so quit_func() can switch back before calling exit() - see there.
    g_user_ssp = Super(0L);
    // Test aid (-DSTARTUP_DEBUG): print a checkpoint after each startup step below,
    // so a crash during boot (e.g. a linker/multilib mismatch - see README_ATARI.md,
    // LINKCPU) can be narrowed down to the step that never printed its checkpoint.
#ifdef STARTUP_DEBUG
    printf("DEBUG A: after Super()%c%c", 13, 10);
#endif
    // Test aid (-DEARLY_RETURN_TEST): return immediately, before any engine code
    // runs, to prove whether a crash happens in this program's own code at all or
    // earlier, in the C-runtime startup (crt0) that calls main() - see README_ATARI.md.
#ifdef EARLY_RETURN_TEST
    printf("DEBUG EARLY: reached main, returning now%c%c", 13, 10);
    Cconin();
    return 0;
#endif

    // Mega STE only: CPU speed/cache control register (bit0 = 16MHz, bit1 = cache).
    // The register does not exist on other machines, so check the machine cookie first.
    long mch = 0;
    Getcookie(C__MCH, &mch);
    if (mch == 0x00010010L)
        *(volatile uint8_t*)0xFF8E21 = 3;
#ifdef STARTUP_DEBUG
    printf("DEBUG B: after Getcookie, mch=%lx%c%c", (unsigned long)mch, 13, 10);
#endif

    config.load(); // hardcoded defaults, no XML (see frontend/config.cpp)
#ifdef STARTUP_DEBUG
    printf("DEBUG C: after config.load()%c%c", 13, 10);
#endif
    atari_load_options("outrun.ini");   // optional: shadows, scenery, cadence, sound (see atari/options.hpp)
#ifdef STARTUP_DEBUG
    printf("DEBUG D: after atari_load_options%c%c", 13, 10);
#endif

    if (!roms.load_revb_roms(config.sound.fix_samples))
    {
        printf("Could not load the OutRun ROM set from roms\\ (see README_ATARI.md).\r\nPress a key.\r\n");
        Cconin();
        quit_func(1);
        return 0;
    }

    config.set_fps(config.video.fps);
    if (!video.init(&roms, &config.video))
        quit_func(1);

    audio.init();

    input.init(config.controls.pad_id,
               config.controls.keyconfig, config.controls.padconfig,
               config.controls.analog,    config.controls.axis,
               config.controls.invert,    config.controls.asettings);

#ifdef MUSIC_RENDER
    // Offline tool (-DMUSIC_RENDER -DMUSIC_ID=0x85 -DMUSIC_SECONDS=100): plays one sound command
    // through the sound driver without the game.
    //   default:            writes what the FM and PCM chips produce to music_ym.raw / music_pcm.raw
    //                       (mono, signed 8 bit, at the mixing rate)
    //   -DMUSIC_LOGONLY:    no synthesis (fast); only the FM register writes are logged to music_log.bin
    extern FILE* g_ym_log;
    extern int g_render_tick;
    {
        outrun.init();                          // boots the sound driver
        osoundint.has_booted = true;            // (normally set when the attract mode starts)
#ifdef MUSIC_LOGONLY
#ifdef MUSIC_LOGNAME   // -DMUSIC_LOGNAME=MAGICAL.LOG (no quotes: they do not survive build_atari.sh)
#define MUSIC_STR2(x) #x
#define MUSIC_STR(x) MUSIC_STR2(x)
        g_ym_log = fopen(MUSIC_STR(MUSIC_LOGNAME), "wb");
#else
        g_ym_log = fopen("music_log.bin", "wb");
#endif
        FILE* fy = 0; FILE* fp = 0;
#else
        FILE* fy = fopen("music_ym.raw", "wb");
        FILE* fp = fopen("music_pcm.raw", "wb");
#endif
        const int TICKS = MUSIC_SECONDS * config.fps;
        double sum_y = 0, sum_p = 0;
        uint32_t n = 0;
        for (int t = 0; t < TICKS; t++)
        {
            g_render_tick = t;
            if (t == 30) osoundint.queue_sound_service(MUSIC_ID);
            osoundint.tick();
#ifdef MUSIC_LOGONLY
            osoundint.pcm->stream_update();     // sample-end flags are read by the driver
            osoundint.ym->skip_frame();
#else
            osoundint.pcm->stream_update();
            osoundint.ym->stream_update();
            const int16_t* yb = osoundint.ym->get_buffer();
            const int16_t* pb = osoundint.pcm->get_buffer();
            const uint32_t frames = osoundint.ym->buffer_size / 2;
            for (uint32_t i = 0; i < frames; i++)
            {
                int32_t y = (yb[2 * i] + yb[2 * i + 1]) / 2;
                int32_t p = (pb[2 * i] + pb[2 * i + 1]) / 2;
                sum_y += (double)y * y;  sum_p += (double)p * p;  n++;
                fputc((int8_t)(y >> 8), fy);
                fputc((int8_t)(p >> 8), fp);
            }
#endif
            if ((t % 300) == 0) { printf("tick %d%c%c", t, 13, 10); }
        }
        if (fy) fclose(fy);
        if (fp) fclose(fp);
        if (g_ym_log) fclose(g_ym_log);
        printf("RENDERED frames=%lu rms_ym=%ld rms_pcm=%ld%c%c", (unsigned long)n,
               (long)(n ? __builtin_sqrt(sum_y / n) : 0), (long)(n ? __builtin_sqrt(sum_p / n) : 0), 13, 10);
        quit_func(0);
        return 0;
    }
#endif

    state = STATE_INIT_GAME; // no frontend menu: boot straight into the game

    main_loop();
    // main_loop() already ran quit_cleanup() (stops the sound DMA, restores the keyboard vector,
    // puts the video mode back) right before returning here - see the STATE_QUIT handling in
    // tick()/main_loop(). A plain return, not quit_func()/Pterm(): see quit_func()'s own comment
    // for why - this is what actually gets back to a working desktop when launched, as this
    // program always is, from the AUTO folder, before EmuTOS's own desktop/AES exists yet.
    //
    // With MiNT running the program was started from a multitasking AES (XaAES, N.AES), which
    // does not redraw the desktop by itself when a program that took the screen over returns:
    // ask it to (see atari/gemredraw.cpp). AES calls belong in user mode, so supervisor mode is
    // left first; this is only done under MiNT, where leaving it is safe - the plain TOS/EmuTOS
    // path above stays exactly as it was.
    {
        long mint = 0;
        if (Getcookie(C_MiNT, &mint) == 0)
        {
            SuperToUser((void*)g_user_ssp);
            atari_redraw_desktop();
        }
    }
    return 0;
}