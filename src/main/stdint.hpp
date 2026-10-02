/***************************************************************************
    Data Types.

    Originally this enforced data type size at compile time using Boost's
    BOOST_STATIC_ASSERT_MSG. Boost is not available on the m68k-atari-mint
    cross toolchain used by the Atari STE port, and the check itself is
    pure C++11 (no library dependency needed), so it has been switched to
    the standard `static_assert` keyword. Behaviour is identical on every
    platform (MSVC/GCC/Clang all support static_assert), this only removes
    an unnecessary Boost dependency.

    Copyright Chris White.
    See license.txt for more details.
***************************************************************************/

#pragma once

/** C99 Standard Naming */
#if defined(_MSC_VER)
    typedef signed char int8_t;
    typedef signed short int16_t;
    typedef signed int int32_t;
    typedef signed long long int64_t;

    typedef unsigned char uint8_t;
    typedef unsigned short uint16_t;
    typedef unsigned int uint32_t;
    typedef unsigned long long uint64_t;
#else
    #include <stdint.h>
#endif

/* Report typedef errors */
static_assert(sizeof(int8_t)   == 1, "int8_t is not of the correct size" );
static_assert(sizeof(int16_t)  == 2, "int16_t is not of the correct size");
static_assert(sizeof(int32_t)  == 4, "int32_t is not of the correct size");
static_assert(sizeof(int64_t)  == 8, "int64_t is not of the correct size");

static_assert(sizeof(uint8_t)  == 1, "int8_t is not of the correct size" );
static_assert(sizeof(uint16_t) == 2, "int16_t is not of the correct size");
static_assert(sizeof(uint32_t) == 4, "int32_t is not of the correct size");
static_assert(sizeof(uint64_t) == 8, "int64_t is not of the correct size");

// Timing traces: compiled in only with -DPERF_PRINT. Written to PERFLOG.TXT (append) rather
// than drawn on screen - lets a run be captured headless (e.g. through Hatari's --harddrive)
// without needing a working display or console redirection.
#ifdef PERF_PRINT
#include <cstdio>
#include <cstdarg>
static inline void perf_printf_file(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    FILE* f = fopen("PERFLOG.TXT", "a");
    if (f) { vfprintf(f, fmt, ap); fclose(f); }
    va_end(ap);
}
#define PERF_PRINTF perf_printf_file
#define PERF_NOW() (*(volatile uint32_t*)0x4BA)   // Atari 200Hz system counter
#else
#define PERF_PRINTF(...) ((void)0)
#define PERF_NOW() 0u
#endif
