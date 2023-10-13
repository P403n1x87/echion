// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <mutex>

#include <echion/stacks.h>
#include <echion/state.h>

// ----------------------------------------------------------------------------

static std::mutex sigprof_handler_lock;

// ----------------------------------------------------------------------------
void sigprof_handler([[maybe_unused]] int signum)
{
    unwind_native_stack();
    unwind_python_stack(current_tstate);
    // NOTE: Native stacks for tasks is non-trivial, so we skip it for now.

    sigprof_handler_lock.unlock();
}

// ----------------------------------------------------------------------------
void sigquit_handler([[maybe_unused]] int signum)
{
    // Wake up the where thread
    std::lock_guard<std::mutex> lock(where_lock);
    where_cv.notify_one();
}

// ----------------------------------------------------------------------------
void install_signals()
{
    signal(SIGQUIT, sigquit_handler);

    if (native)
        signal(SIGPROF, sigprof_handler);
}

// ----------------------------------------------------------------------------
void restore_signals()
{
    signal(SIGQUIT, SIG_DFL);
    signal(SIGPROF, SIG_DFL);
}
