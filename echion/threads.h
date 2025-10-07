// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#include <Python.h>
#define Py_BUILD_CORE

#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <unordered_map>


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

    class Error : public std::exception
    {
    public:
        const char* what() const noexcept override;
    };

    class CpuTimeError : public Error
    {
    public:
        const char* what() const noexcept override;
    };

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

    void update_cpu_time();
    bool is_running();

    void sample(int64_t, PyThreadState*, microsecond_t);
    void unwind(PyThreadState*);

    // ------------------------------------------------------------------------
    ThreadInfo(uintptr_t thread_id, unsigned long native_id, const char* name);

private:
    void unwind_tasks();
    void unwind_greenlets(PyThreadState*, unsigned long);
};

// ----------------------------------------------------------------------------

// We make this a reference to a heap-allocated object so that we can avoid
// the destruction on exit. We are in charge of cleaning up the object. Note
// that the object will leak, but this is not a problem.
inline std::unordered_map<uintptr_t, ThreadInfo::Ptr>& thread_info_map =
    *(new std::unordered_map<uintptr_t, ThreadInfo::Ptr>());  // indexed by thread_id

inline std::mutex thread_info_map_lock;

void for_each_thread(InterpreterInfo& interp, std::function<void(PyThreadState*, ThreadInfo&)> callback);