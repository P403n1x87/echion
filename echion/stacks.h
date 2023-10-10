// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <deque>
#include <unordered_set>

#define UNW_LOCAL_ONLY
#include <libunwind.h>

#include <echion/frame.h>
#include "interface.hpp"

#define MAX_FRAMES 2048

typedef std::deque<Frame::Ref> FrameStack;

static FrameStack python_stack;
static FrameStack native_stack;
static FrameStack interleaved_stack;

static PyThreadState *current_tstate = NULL;

// ----------------------------------------------------------------------------
void unwind_native_stack()
{
    unw_cursor_t cursor;
    unw_context_t context;

    unw_getcontext(&context);
    unw_init_local(&cursor, &context);

    native_stack.clear();

    while (unw_step(&cursor) > 0 && native_stack.size() < MAX_FRAMES)
    {
        unw_word_t offset, pc;
        unw_get_reg(&cursor, UNW_REG_IP, &pc);
        if (pc == 0)
        {
            // TODO: Invalid stack
            break;
        }

        char sym[256];
        native_stack.push_back(
            unw_get_proc_name(&cursor, sym, sizeof(sym), &offset)
                ? UNKNOWN_FRAME
                : Frame::get(pc, sym, offset));
    }
}

// ----------------------------------------------------------------------------
static inline const char *get_or_default(const std::optional<std::string> &str, const char *base) {
  return str.has_value() ? str->c_str() : base;
}

static size_t
unwind_frame(PyObject *frame_addr, FrameStack &stack)
{
    (void)stack;

    std::unordered_set<PyObject *> seen_frames; // Used to detect cycles in the stack
    int count = 0;

    PyObject *current_frame_addr = frame_addr;

    // DAS: change the loop criteria since we don't populate the stack
    while (current_frame_addr != NULL && count < MAX_FRAMES)
    {
        count++;
        if (seen_frames.find(current_frame_addr) != seen_frames.end())
        {
            ddup_push_frame(
                "[BROKEN]",
                "lol_nofile.py",
                0,
                0
            );
            break;
        }

        seen_frames.insert(current_frame_addr);

        try
        {
            Frame &frame = Frame::read(current_frame_addr, &current_frame_addr);
//            stack.push_back(frame);
            ddup_push_frame(
                get_or_default(frame.name, "unknown_func"),
                get_or_default(frame.filename, "unknown_file"),
                0,
                frame.location.line
            );
        }
        catch (Frame::Error &e)
        {
            ddup_push_frame(
                "[BROKEN]",
                "lol_nofile.py",
                0,
                0
            );
            break;
        }
    }

    return count;
}

// ----------------------------------------------------------------------------
const char *get_env_or_default(const char *name, const char *default_value) {
  char *value = getenv(name);
  if (value == NULL || strlen(value) == 0)
    return default_value;
  return value;
}
static void
unwind_python_stack(PyThreadState *tstate, FrameStack &stack)
{
    static bool ddup_initialized = false;
    if (!ddup_initialized) {
        std::cout << "Initializing ddup" << std::endl;
        ddup_initialized = true;
        ddup_config_env(get_env_or_default("DD_ENV", "prod"));
        ddup_config_version(get_env_or_default("DD_VERSION", ""));
        ddup_config_service(get_env_or_default("DD_SERVICE", "echion_service"));
        ddup_config_url(get_env_or_default("DD_TRACE_AGENT_URL", "http://localhost:8126"));
        ddup_config_runtime("python");
        ddup_config_runtime_version(Py_GetVersion());
        ddup_config_profiler_version("echion_v0.1");
        ddup_config_max_nframes(MAX_FRAMES);
        ddup_init();
    }

    std::unordered_set<void *> seen_frames; // Used to detect cycles in the stack

//    stack.clear();

#if PY_VERSION_HEX >= 0x030b0000
    _PyCFrame cframe;
    _PyCFrame *cframe_addr = tstate->cframe;
    if (copy_type(cframe_addr, cframe)) {
        ddup_start_sample(2);
        ddup_push_walltime(10000, 1);
        ddup_push_cputime(10000, 1);
        ddup_push_frame(
            "[DROPPED]",
            "nofile.py",
            0,
            0
        );
        ddup_flush_sample();
        return;
    }

    PyObject *frame_addr = (PyObject *)cframe.current_frame;
#else // Python < 3.11
    PyObject *frame_addr = (PyObject *)tstate->frame;
#endif
    // Lies, damned lies, and statistics
    ddup_start_sample(MAX_FRAMES);
    ddup_push_walltime(100, 1);
    ddup_push_cputime(100, 1);
    unwind_frame(frame_addr, stack);

    ddup_flush_sample();

    // Use a static timer for now
    static auto lastTime = std::chrono::high_resolution_clock::now();

    auto currentTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastTime);

    if (duration.count() >= 60) {
        std::cout << "Uploading!" << std::endl;
        ddup_upload();
        lastTime = currentTime;
    }
}

// ----------------------------------------------------------------------------
static void
unwind_python_stack(PyThreadState *tstate)
{
    unwind_python_stack(tstate, python_stack);
}

// ----------------------------------------------------------------------------
static void
interleave_stacks(FrameStack &python_stack)
{
    interleaved_stack.clear();

    auto p = python_stack.rbegin();
    // The last two frames are usually the signal trampoline and the signal
    // handler. We skip them.
    for (auto n = native_stack.rbegin(); n != native_stack.rend() - 2; ++n)
    {
        auto native_frame = *n;

        if (native_frame.get().name->find("PyEval_EvalFrameDefault") != std::string::npos)
        {
            if (p == python_stack.rend())
            {
                // We expected a Python frame but we found none, so we report
                // the native frame instead.
                std::cerr << "Expected Python frame(s), found none!" << std::endl;
                interleaved_stack.push_front(native_frame);
            }
            else
            {

                // We skip the PyEval_EvalFrameDefault frame because it is the
                // function that calls the Python code.
#if PY_VERSION_HEX >= 0x030b0000
                int cframe_count = 0;
                while (p != python_stack.rend())
                {
                    // The Python stack will start with an entry frame at the top.
                    // We stop popping at the next entry frame.
                    cframe_count += (*p).get().is_entry;
                    if (cframe_count >= 2)
                        break;

                    interleaved_stack.push_front(*p++);
                }
#else
                interleaved_stack.push_front(*p++);
#endif
            }
        }
        else
            interleaved_stack.push_front(native_frame);
    }

    if (p != python_stack.rend())
    {
        std::cerr << "Python stack not empty after interleaving!" << std::endl;
        while (p != python_stack.rend())
            interleaved_stack.push_front(*p++);
    }
}

// ----------------------------------------------------------------------------
static void
interleave_stacks()
{
    interleave_stacks(python_stack);
}
