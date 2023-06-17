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

#define MAX_FRAMES 2048

typedef std::deque<Frame *> FrameStack;

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
static void
unwind_python_stack(PyThreadState *tstate)
{
    std::unordered_set<void *> seen_frames; // Used to detect cycles in the stack

    python_stack.clear();

#if PY_VERSION_HEX >= 0x030b0000
    _PyCFrame cframe;
    _PyCFrame *cframe_addr = tstate->cframe;
    if (copy_type(cframe_addr, cframe))
        // TODO: Invalid frame
        return;
    _PyInterpreterFrame *iframe_addr = cframe.current_frame;

    while (iframe_addr != NULL && python_stack.size() < MAX_FRAMES)
    {
        _PyInterpreterFrame iframe;

        if (seen_frames.find((void *)iframe_addr) != seen_frames.end() || copy_type(iframe_addr, iframe))
        {
            python_stack.push_back(INVALID_FRAME);
            break;
        }
        seen_frames.insert((void *)iframe_addr);

        // We cannot use _PyInterpreterFrame_LASTI because _PyCode_CODE reads
        // from the code object.
        const int lasti = ((int)(iframe.prev_instr - (_Py_CODEUNIT *)(iframe.f_code))) - offsetof(PyCodeObject, co_code_adaptive) / sizeof(_Py_CODEUNIT);
        auto frame = Frame::get(iframe.f_code, lasti);

        python_stack.push_back(frame);

        if (frame == INVALID_FRAME)
            break;

        frame->is_entry = iframe.is_entry;

        iframe_addr = iframe.previous;
    }

#else // Python < 3.11
    PyFrameObject *frame_addr = tstate->frame;

    // Unwind the stack from leaf to root and store it in a stack. This way we
    // can print it from root to leaf.
    while (frame_addr != NULL && python_stack.size() < MAX_FRAMES)
    {
        PyFrameObject py_frame;

        if (seen_frames.find((void *)frame_addr) != seen_frames.end() || copy_type(frame_addr, py_frame))
        {
            python_stack.push_back(INVALID_FRAME);
            break;
        }
        seen_frames.insert((void *)frame_addr);

        Frame *frame = Frame::get(py_frame.f_code, py_frame.f_lasti);
        python_stack.push_back(frame);
        if (frame == INVALID_FRAME)
            break;

        frame_addr = py_frame.f_back;
    }

#endif
}

// ----------------------------------------------------------------------------
static void
interleave_stacks()
{
    interleaved_stack.clear();

    while (!native_stack.empty())
    {
        Frame *native_frame = native_stack.back();
        native_stack.pop_back();

        if (native_stack.size() < 2)
        {
            // The last two frames are usually the signal trampoline and the
            // signal handler. We skip them.
            continue;
        }

        if (strstr(native_frame->name, "PyEval_EvalFrameDefault") != NULL)
        {
            if (python_stack.empty())
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
                while (!python_stack.empty())
                {
                    // The Python stack will start with an entry frame at the top.
                    // We stop popping at the next entry frame.
                    Frame *python_frame = python_stack.back();
                    cframe_count += python_frame->is_entry;
                    if (cframe_count >= 2)
                        break;

                    python_stack.pop_back();

                    interleaved_stack.push_front(python_frame);
                }
#else
                Frame *python_frame = python_stack.back();
                python_stack.pop_back();
                interleaved_stack.push_front(python_frame);
#endif
            }
        }
        else
            interleaved_stack.push_front(native_frame);
    }

    if (!python_stack.empty())
    {
        std::cerr << "Python stack not empty after interleaving!" << std::endl;
        Frame *python_frame = python_stack.back();
        python_stack.pop_back();

        interleaved_stack.push_front(python_frame);
    }
}
