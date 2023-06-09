// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <stack>
#include <unordered_set>
#include <vector>

#include <libunwind.h>

#include <echion/frame.h>

static std::stack<Frame *> python_stack;
static std::stack<Frame *> native_stack;
static std::vector<Frame *> interleaved_stack;
static PyThreadState *current_tstate = NULL;

// ----------------------------------------------------------------------------
void unwind_native_stack()
{
    unw_cursor_t cursor;
    unw_context_t context;

    unw_getcontext(&context);
    unw_init_local(&cursor, &context);

    while (unw_step(&cursor) > 0)
    {
        unw_word_t offset, pc;
        unw_get_reg(&cursor, UNW_REG_IP, &pc);
        if (pc == 0)
        {
            // TODO: Invalid stack
            break;
        }

        char sym[256];
        native_stack.push(
            unw_get_proc_name(&cursor, sym, sizeof(sym), &offset)
                ? new Frame("<unknown>")
                : new Frame(pc, sym, offset));
    }
}

// ----------------------------------------------------------------------------
static void
unwind_python_stack(PyThreadState *tstate)
{
    std::unordered_set<void *> seen_frames; // Used to detect cycles in the stack

#if PY_VERSION_HEX >= 0x030b0000
    _PyCFrame cframe;
    _PyCFrame *cframe_addr = tstate->cframe;
    if (copy_type(cframe_addr, cframe))
        // TODO: Invalid frame
        return;
    _PyInterpreterFrame *iframe_addr = cframe.current_frame;

    while (iframe_addr != NULL)
    {
        _PyInterpreterFrame iframe;
        PyCodeObject code;

        if (seen_frames.find((void *)iframe_addr) != seen_frames.end() || copy_type(iframe_addr, iframe) || copy_type(iframe.f_code, code))
        {
            python_stack.push(new Frame("INVALID"));
            break;
        }

        // We cannot use _PyInterpreterFrame_LASTI because _PyCode_CODE reads
        // from the code object.
        const int lasti = ((int)(iframe.prev_instr - (_Py_CODEUNIT *)(iframe.f_code))) - offsetof(PyCodeObject, co_code_adaptive) / sizeof(_Py_CODEUNIT);
        Frame *frame = new Frame(code, lasti);
        if (!frame->is_valid())
        {
            python_stack.push(new Frame("INVALID"));
            break;
        }
        frame->is_entry = iframe.is_entry;
        python_stack.push(frame);

        seen_frames.insert((void *)iframe_addr);
        iframe_addr = iframe.previous;
    }

#else // Python < 3.11
    PyFrameObject *frame_addr = tstate->frame;

    // Unwind the stack from leaf to root and store it in a stack. This way we
    // can print it from root to leaf.
    while (frame_addr != NULL)
    {
        PyFrameObject py_frame;
        PyCodeObject code;

        if (seen_frames.find((void *)frame_addr) != seen_frames.end() || copy_type(frame_addr, py_frame) || copy_type(py_frame.f_code, code))
        {
            python_stack.push(new Frame("INVALID"));
            break;
        }

        Frame *frame = new Frame(code, py_frame.f_lasti);
        if (!frame->is_valid())
        {
            python_stack.push(new Frame("INVALID"));
            break;
        }
        python_stack.push(frame);

        seen_frames.insert((void *)frame_addr);
        frame_addr = py_frame.f_back;
    }

#endif
}

// ----------------------------------------------------------------------------
static void
interleave_stacks()
{
    while (!native_stack.empty())
    {
        Frame *native_frame = native_stack.top();
        native_stack.pop();

        if (strstr(native_frame->name, "PyEval_EvalFrameDefault") != NULL)
        {
            if (python_stack.empty())
            {
                // We expected a Python frame but we found none, so we report
                // the native frame instead.
                std::cerr << "Expected Python frame(s), found none!" << std::endl;
                interleaved_stack.push_back(native_frame);
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
                    Frame *python_frame = python_stack.top();
                    cframe_count += python_frame->is_entry;
                    if (cframe_count >= 2)
                        break;

                    python_stack.pop();

                    interleaved_stack.push_back(python_frame);
                }
#else
                Frame *python_frame = python_stack.top();
                python_stack.pop();
                interleaved_stack.push_back(python_frame);
#endif
            }
        }
        else
            interleaved_stack.push_back(native_frame);
    }

    if (!python_stack.empty())
    {
        std::cerr << "Python stack not empty after interleaving!" << std::endl;
        Frame *python_frame = python_stack.top();
        python_stack.pop();

        interleaved_stack.push_back(python_frame);
    }
}
