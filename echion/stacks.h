// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <deque>
#include <unordered_set>

#if defined PL_LINUX || defined PL_DARWIN
#define UNW_LOCAL_ONLY
#include <libunwind.h>
#elif defined PL_WIN32
#include <windows.h>
#include <dbghelp.h>
#endif

#include <echion/frame.h>

#define MAX_FRAMES 2048

class FrameStack : public std::deque<Frame::Ref>
{
public:
    void render(std::ostream &output)
    {
        for (auto it = this->rbegin(); it != this->rend(); ++it)
        {
#if PY_VERSION_HEX >= 0x030c0000
            if ((*it).get().is_entry)
                // This is a shim frame so we skip it.
                continue;
#endif
            (*it).get().render(output);
        }
    }
};

static FrameStack python_stack;
static FrameStack native_stack;
static FrameStack interleaved_stack;

// ----------------------------------------------------------------------------
#if defined PL_LINUX || defined PL_DARWIN
void unwind_native_stack()
{
    native_stack.clear();

    unw_cursor_t cursor;
    unw_context_t context;

    unw_getcontext(&context);
    unw_init_local(&cursor, &context);

    while (unw_step(&cursor) > 0 && native_stack.size() < MAX_FRAMES)
    {
        try
        {
            native_stack.push_back(Frame::get(cursor));
        }
        catch (Frame::Error &)
        {
            break;
        }
    }
}

#elif defined PL_WIN32
void unwind_native_stack(HANDLE thread)
{
    native_stack.clear();

    // Use StackWalk64 to unwind the stack
    HANDLE process = GetCurrentProcess();
    if (!SymInitialize(process, NULL, TRUE))
        return;

    SymSetOptions(SYMOPT_LOAD_LINES);

    CONTEXT context = {};
    context.ContextFlags = CONTEXT_FULL;
    if (thread == GetCurrentThread()) {
        // From the Win32 API documentation:
        // "If you call GetThreadContext for the current thread, the function
        // returns successfully; however, the context returned is not valid."
        // So we use RtlCaptureContext instead.
        RtlCaptureContext(&context);
    } else {
        // NOTE: From the Win32 API documentation:
        // "You cannot get a valid context for a running thread. Use the
        // SuspendThread function to suspend the thread before calling
        // GetThreadContext."
        if (!GetThreadContext(thread, &context))
            return;
    }

    // TODO: Add support for other architectures
    STACKFRAME64 stackframe = {};
    stackframe.AddrPC.Mode = AddrModeFlat;
    stackframe.AddrStack.Mode = AddrModeFlat;
    stackframe.AddrFrame.Mode = AddrModeFlat;
    stackframe.AddrPC.Offset = context.Rip;
    stackframe.AddrStack.Offset = context.Rsp;
    stackframe.AddrFrame.Offset = context.Rbp;

    while (StackWalk64(
        IMAGE_FILE_MACHINE_AMD64,
        process,
        thread,
        &stackframe,
        &context,
        NULL,
        SymFunctionTableAccess64,
        SymGetModuleBase64,
        NULL))
    {
        try
        {
            native_stack.push_back(Frame::get(stackframe));
        }
        catch (Frame::Error &)
        {
            break;
        }
    }

    SymCleanup(process);
}
#endif

// ----------------------------------------------------------------------------
static size_t
unwind_frame(PyObject *frame_addr, FrameStack &stack)
{
    std::unordered_set<PyObject *> seen_frames; // Used to detect cycles in the stack
    int count = 0;

    PyObject *current_frame_addr = frame_addr;
    while (current_frame_addr != NULL && stack.size() < MAX_FRAMES)
    {

        if (seen_frames.find(current_frame_addr) != seen_frames.end())
            break;

        count++;

        seen_frames.insert(current_frame_addr);

        try
        {
            Frame &frame = Frame::read(current_frame_addr, &current_frame_addr);

            stack.push_back(frame);
        }
        catch (Frame::Error &e)
        {
            break;
        }
    }

    return count;
}

// ----------------------------------------------------------------------------
static void
unwind_python_stack(PyThreadState *tstate, FrameStack &stack)
{
    std::unordered_set<void *> seen_frames; // Used to detect cycles in the stack

    stack.clear();

#if PY_VERSION_HEX >= 0x030b0000
    _PyCFrame cframe;
    _PyCFrame *cframe_addr = tstate->cframe;
    if (copy_type(cframe_addr, cframe))
        // TODO: Invalid frame
        return;

    PyObject *frame_addr = (PyObject *)cframe.current_frame;
#else // Python < 3.11
    PyObject *frame_addr = (PyObject *)tstate->frame;
#endif
    unwind_frame(frame_addr, stack);
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

#if defined PL_LINUX || defined PL_DARWIN
    // The last two frames are usually the signal trampoline and the signal
    // handler. We skip them.
    #define NATIVE_FRAME_SKIP 2
#elif defined PL_WIN32
    // On Windows we don't have signals so we don't have frames to skip.
    #define NATIVE_FRAME_SKIP 0
#endif

    for (auto n = native_stack.rbegin(); n != native_stack.rend() - NATIVE_FRAME_SKIP; ++n)
    {
        auto native_frame = *n;

        if (string_table.lookup(native_frame.get().name).find("PyEval_EvalFrameDefault") != std::string::npos)
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
