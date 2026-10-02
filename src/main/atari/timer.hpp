#pragma once

/***************************************************************************
    Atari Timer / Frame Pacing.

    Same public interface as src/main/sdl2/timer.hpp (Timer::start/stop/
    pause/unpause/get_ticks/is_started/is_paused) so src/main/main_atari.cpp
    (the port's replacement for main.cpp) can use it as a drop-in.

    Implementation note: uses the TOS/MiNT low-memory system variable
    commonly called "_hz_200" (address 0x4BA), a 32-bit counter incremented
    by the OS at a fixed 200Hz regardless of PAL/NTSC video timing or VBL
    rate. This is a long-standing, widely-used technique in ST/STE
    homebrew for a monotonic ~5ms-resolution clock without installing any
    interrupt handlers of our own. get_ticks() below converts to
    milliseconds (*5) to match the SDL_GetTicks()-based original exactly.

    A separate VBL-synced wait (used for frame-pacing/tear prevention, see
    video.cpp's finalize_frame()) is provided by vbl_wait() below via the
    XBIOS Vsync() call, which is the standard MiNT/TOS way to block until
    the next vertical blank.

    Tested in Hatari (Mega STE, Falcon 030/060 emulation); see README_ATARI.md.

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

#include "../stdint.hpp"

class Timer
{
public:
    Timer();

    void start();
    void stop();
    void pause();
    void unpause();

    int get_ticks();

    bool is_started();
    bool is_paused();

private:
    int startTicks;
    int pausedTicks;
    bool paused;
    bool started;

    static uint32_t hz200_now();
};

// Block until the next vertical blank (XBIOS Vsync()). Used by the main
// loop for tear-free double buffering instead of SDL_Delay()-based pacing.
void vbl_wait();
void frame_pace();   // 60 fps cap without waiting for the retrace (Falcon, triple buffered)
