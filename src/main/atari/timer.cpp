#include "atari/timer.hpp"

#ifdef __MINT__
#include <mint/osbind.h>   // Vsync()
#endif

// Standard ST/STE low-memory system variable: 200Hz counter maintained by
// the OS. See header comment for rationale. Declared volatile and accessed
// through a pointer so the compiler never caches it across calls.
#define HZ_200 (*(volatile uint32_t*)0x4baL)

uint32_t Timer::hz200_now()
{
    return HZ_200;
}

// Stopped, at zero.
Timer::Timer()
{
    startTicks  = 0;
    pausedTicks = 0;
    paused  = false;
    started = false;
}

// Starts (or restarts) counting from now.
void Timer::start()
{
    started = true;
    paused  = false;
    // *5: convert 200Hz ticks to milliseconds (1 tick = 5 ms), matching the
    // millisecond-based get_ticks() contract the sdl2 Timer exposes.
    startTicks = (int)(hz200_now() * 5);
}

// Stops the timer; get_ticks() then returns 0.
void Timer::stop()
{
    started = false;
    paused  = false;
}

// Freezes the count at its current value.
void Timer::pause()
{
    if (started && !paused)
    {
        paused = true;
        pausedTicks = (int)(hz200_now() * 5) - startTicks;
    }
}

// Resumes counting from the value frozen by pause().
void Timer::unpause()
{
    if (paused)
    {
        paused = false;
        startTicks = (int)(hz200_now() * 5) - pausedTicks;
        pausedTicks = 0;
    }
}

// Milliseconds since start(), not counting paused time; 0 if stopped. 5 ms resolution.
int Timer::get_ticks()
{
    if (started)
        return paused ? pausedTicks : (int)(hz200_now() * 5) - startTicks;
    return 0;
}

bool Timer::is_started() { return started; }
bool Timer::is_paused()  { return paused; }

// Waits for the next vertical blank.
void vbl_wait()
{
#ifdef __MINT__
    Vsync();
#endif
}

// Frame-rate cap for builds that do not block on the vertical retrace: at most
// 60 frames per second, measured with the 200Hz counter (10 units of 1/600 s
// per frame).  A frame that is already late is never "made up" with a burst.
void frame_pace()
{
    static uint32_t next = 0;
    uint32_t now = HZ_200 * 3;
    next += 10;                              // when this frame was due
    if ((int32_t)(now - next) > 30)
        next = now;                          // far behind (or first call): forget the backlog, do not wait
    while ((int32_t)(HZ_200 * 3 - next) < 0) { }
}