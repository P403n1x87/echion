// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <deque>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#define UNW_LOCAL_ONLY
#include <libunwind.h>

#include <echion/frame.h>
#include <echion/mojo.h>

#define MAX_FRAMES 2048

class FrameStack : public std::deque<Frame::Ref>
{
public:
    using Ptr = std::unique_ptr<FrameStack>;
    using Key = Frame::Key;

    // ------------------------------------------------------------------------
    Key key()
    {
        Key h = 0;

        for (auto it = this->begin(); it != this->end(); ++it)
            h = rotl(h) ^ (*it).get().cache_key;

        return h;
    }

    // ------------------------------------------------------------------------
    void render()
    {
        for (auto it = this->rbegin(); it != this->rend(); ++it)
        {
#if PY_VERSION_HEX >= 0x030c0000
            if ((*it).get().is_entry)
                // This is a shim frame so we skip it.
                continue;
#endif
            mojo.frame_ref((*it).get().cache_key);
        }
    }

private:
    // ------------------------------------------------------------------------
    static inline Frame::Key rotl(Key key)
    {
        return (key << 1) | (key >> (CHAR_BIT * sizeof(key) - 1));
    }
};

// ----------------------------------------------------------------------------

static FrameStack python_stack;
static FrameStack native_stack;
static FrameStack interleaved_stack;

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
static size_t
unwind_frame_unsafe(PyObject *frame, FrameStack &stack)
{
    std::unordered_set<PyObject *> seen_frames; // Used to detect cycles in the stack
    int count = 0;

    PyObject *current_frame = frame;
    while (current_frame != NULL && stack.size() < MAX_FRAMES)
    {

        if (seen_frames.find(current_frame) != seen_frames.end())
            break;

        count++;

        seen_frames.insert(current_frame);

        stack.push_back(Frame::get(current_frame));

#if PY_VERSION_HEX >= 0x030b0000
        current_frame = (PyObject *)((_PyInterpreterFrame *)current_frame)->previous;
#else
        current_frame = (PyObject *)((PyFrameObject *)current_frame)->f_back;
#endif
    }

    return count;
}

// ----------------------------------------------------------------------------
static void
unwind_python_stack(PyThreadState *tstate, FrameStack &stack)
{
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
unwind_python_stack_unsafe(PyThreadState *tstate, FrameStack &stack)
{
    stack.clear();

#if PY_VERSION_HEX >= 0x030b0000
    PyObject *frame_addr = (PyObject *)tstate->cframe->current_frame;
#else // Python < 3.11
    PyObject *frame_addr = (PyObject *)tstate->frame;
#endif
    unwind_frame_unsafe(frame_addr, stack);
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

// ----------------------------------------------------------------------------
// This table is used to store entire stacks and index them by key. This is
// used when profiling memory events to account for deallocations.
class StackTable
{
public:
    // ------------------------------------------------------------------------
    FrameStack::Key inline store(FrameStack::Ptr stack)
    {
        std::lock_guard<std::mutex> lock(this->lock);

        auto stack_key = stack->key();

        auto stack_entry = table.find(stack_key);
        if (stack_entry == table.end())
        {
            table.emplace(stack_key, std::move(stack));
        }
        else
        {
            // TODO: Check for collisions.
        }

        return stack_key;
    }

    // ------------------------------------------------------------------------
    FrameStack &retrieve(FrameStack::Key stack_key)
    {
        std::lock_guard<std::mutex> lock(this->lock);

        return *table.find(stack_key)->second;
    }

    // ------------------------------------------------------------------------
    void clear()
    {
        std::lock_guard<std::mutex> lock(this->lock);

        table.clear();
    }

private:
    std::unordered_map<FrameStack::Key, std::unique_ptr<FrameStack>> table;
    std::mutex lock;
};

// ----------------------------------------------------------------------------
// We make this a reference to a heap-allocated object so that we can avoid
// the destruction on exit. We are in charge of cleaning up the object. Note
// that the object will leak, but this is not a problem.
static auto &stack_table = *(new StackTable());
