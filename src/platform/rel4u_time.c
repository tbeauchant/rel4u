#define _POSIX_C_SOURCE 199309L
#include "rel4u_time.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static LARGE_INTEGER g_perf_frequency;
static BOOL g_perf_init = FALSE;

static void init_perf_counter(void) {
    if (!g_perf_init) {
        QueryPerformanceFrequency(&g_perf_frequency);
        g_perf_init = TRUE;
    }
}

uint64_t rel4u_time_now_ns(void) {
    init_perf_counter();
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (uint64_t)((counter.QuadPart * 1000000000ULL) / g_perf_frequency.QuadPart);
}

uint64_t rel4u_time_now_ms(void) {
    return rel4u_time_now_ns() / 1000000ULL;
}

void rel4u_time_sleep_ms(uint32_t ms) {
    Sleep(ms);
}

#else
#include <time.h>
#include <unistd.h>

uint64_t rel4u_time_now_ns(void) {
    struct timespec ts;
#if defined(CLOCK_MONOTONIC_RAW)
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

uint64_t rel4u_time_now_ms(void) {
    return rel4u_time_now_ns() / 1000000ULL;
}

void rel4u_time_sleep_ms(uint32_t ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

#endif
