// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#if defined PL_LINUX
#include <time.h>
#include <sched.h>

#elif defined PL_DARWIN
#include <mach/clock.h>
#include <mach/mach.h>

static clock_serv_t cclock;

#elif defined PL_WIN32
#include <processthreadsapi.h>
#include <profileapi.h>

// On Windows we have to use the QueryPerformance API to get the right time
// resolution. We use this variable to cache the inverse frequency (counts per
// second), that is the period of each count, in units of us.
static double _period;

#endif

static void setup_timing()
{
#if defined PL_DARWIN
    // Get the wall time clock resource.
    host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);

#elif defined PL_WIN32
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    _period = ((double)1e6) / ((double)freq.QuadPart);

#endif
}

static void teardown_timing()
{
#if defined PL_DARWIN
    mach_port_deallocate(mach_task_self(), cclock);
#endif
}

typedef unsigned long microsecond_t;

static microsecond_t last_time = 0;

#define TS_TO_MICROSECOND(ts) ((ts).tv_sec * 1e6 + (ts).tv_nsec / 1e3)
#define TV_TO_MICROSECOND(tv) ((tv).seconds * 1e6 + (tv).microseconds)

// ----------------------------------------------------------------------------
static microsecond_t
gettime()
{
#if defined PL_LINUX
    struct timespec ts;
    if (clock_gettime(CLOCK_BOOTTIME, &ts))
        return 0;
    return TS_TO_MICROSECOND(ts);

#elif defined PL_DARWIN
    mach_timespec_t ts;
    clock_get_time(cclock, &ts);
    return TS_TO_MICROSECOND(ts);

#elif defined PL_WIN32
    LARGE_INTEGER count;
    QueryPerformanceCounter(&count);
    return count.QuadPart * _period;

#endif
}

// ----------------------------------------------------------------------------
static void
yield()
{
#if defined PL_LINUX || defined PL_DARWIN
    sched_yield();
#elif defined PL_WIN32
    SwitchToThread();
#endif
}
