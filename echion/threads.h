// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#include <Python.h>
#define Py_BUILD_CORE

#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>

#if defined PL_LINUX
#include <time.h>
#elif defined PL_DARWIN
#include <mach/clock.h>
#include <mach/mach.h>
#endif

#include <echion/errors.h>
#include <echion/greenlets.h>
#include <echion/interp.h>
#include <echion/render.h>
#include <echion/signals.h>
#include <echion/stacks.h>
#include <echion/tasks.h>
#include <echion/timing.h>

class ThreadInfo
{
public:
    using Ptr = std::unique_ptr<ThreadInfo>;

    uintptr_t thread_id;
    unsigned long native_id;

    std::string name;

#if defined PL_LINUX
    clockid_t cpu_clock_id;
#elif defined PL_DARWIN
    mach_port_t mach_port;
#endif
    microsecond_t cpu_time;

    uintptr_t asyncio_loop = 0;

    [[nodiscard]] Result<void> update_cpu_time();
    bool is_running();

    [[nodiscard]] Result<void> sample(int64_t, PyThreadState*, microsecond_t);
    void unwind(PyThreadState*);

    // ------------------------------------------------------------------------
#if defined PL_LINUX
    ThreadInfo(uintptr_t thread_id, unsigned long native_id, const char* name,
               clockid_t cpu_clock_id);
#elif defined PL_DARWIN
    ThreadInfo(uintptr_t thread_id, unsigned long native_id, const char* name,
               mach_port_t mach_port);
#endif

    [[nodiscard]] static Result<std::unique_ptr<ThreadInfo>> create(uintptr_t thread_id,
                                                                    unsigned long native_id,
                                                                    const char* name);

private:
    [[nodiscard]] Result<void> unwind_tasks();
    void unwind_greenlets(PyThreadState*, unsigned long);
};

// ----------------------------------------------------------------------------

// We make this a reference to a heap-allocated object so that we can avoid
// the destruction on exit. We are in charge of cleaning up the object. Note
// that the object will leak, but this is not a problem.
inline std::unordered_map<uintptr_t, ThreadInfo::Ptr>& thread_info_map =
    *(new std::unordered_map<uintptr_t, ThreadInfo::Ptr>());  // indexed by thread_id

inline std::mutex thread_info_map_lock;

void for_each_thread(InterpreterInfo& interp,
                     std::function<void(PyThreadState*, ThreadInfo&)> callback);