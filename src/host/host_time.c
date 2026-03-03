/*
 * host_time.c — Monotonic clock implementation.
 */

#include "croft/host_time.h"

#if defined(CROFT_OS_MACOS) || defined(CROFT_OS_LINUX)

#include <time.h>

uint64_t host_time_millis(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

#elif defined(CROFT_OS_WINDOWS)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

uint64_t host_time_millis(void)
{
    static LARGE_INTEGER freq = {0};
    LARGE_INTEGER now;

    if (freq.QuadPart == 0)
        QueryPerformanceFrequency(&freq);

    QueryPerformanceCounter(&now);
    return (uint64_t)(now.QuadPart * 1000 / freq.QuadPart);
}

#endif
