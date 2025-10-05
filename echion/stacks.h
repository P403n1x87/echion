// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <deque>
#include <mutex>
#include <unordered_map>

#ifndef UNWIND_NATIVE_DISABLE
#define UNW_LOCAL_ONLY
#include <libunwind.h>
#endif  // UNWIND_NATIVE_DISABLE

#include <echion/config.h>
#include <echion/frame.h>
#include <echion/mojo.h>

// ----------------------------------------------------------------------------

class FrameStack : public std::deque<Frame::Ref>
{
public:
    using Ptr = std::unique_ptr<FrameStack>;
    using Key = Frame::Key;

    // ------------------------------------------------------------------------
    Key key();

    // ------------------------------------------------------------------------
    void render();

    // ------------------------------------------------------------------------
    void render_where();

private:
    // ------------------------------------------------------------------------
    static Frame::Key rotl(Key key);
};

// ----------------------------------------------------------------------------

extern FrameStack python_stack;
extern FrameStack native_stack;
extern FrameStack interleaved_stack;

// ----------------------------------------------------------------------------
#ifndef UNWIND_NATIVE_DISABLE
void unwind_native_stack();
#endif  // UNWIND_NATIVE_DISABLE

// ----------------------------------------------------------------------------
size_t unwind_frame(PyObject* frame_addr, FrameStack& stack);

// ----------------------------------------------------------------------------
size_t unwind_frame_unsafe(PyObject* frame, FrameStack& stack);

// ----------------------------------------------------------------------------
void unwind_python_stack(PyThreadState* tstate, FrameStack& stack);

// ----------------------------------------------------------------------------
void unwind_python_stack_unsafe(PyThreadState* tstate, FrameStack& stack);

// ----------------------------------------------------------------------------
void unwind_python_stack(PyThreadState* tstate);

// ----------------------------------------------------------------------------
void interleave_stacks(FrameStack& python_stack);

// ----------------------------------------------------------------------------
void interleave_stacks();

// ----------------------------------------------------------------------------
class StackInfo
{
public:
    StringTable::Key task_name;
    bool on_cpu;
    FrameStack stack;

    StackInfo(StringTable::Key task_name, bool on_cpu);
};

// ----------------------------------------------------------------------------
// This table is used to store entire stacks and index them by key. This is
// used when profiling memory events to account for deallocations.
class StackTable
{
public:
    // ------------------------------------------------------------------------
    FrameStack::Key store(FrameStack::Ptr stack);

    // ------------------------------------------------------------------------
    FrameStack& retrieve(FrameStack::Key stack_key);

    // ------------------------------------------------------------------------
    void clear();

private:
    std::unordered_map<FrameStack::Key, std::unique_ptr<FrameStack>> table;
    std::mutex lock;
};

// ----------------------------------------------------------------------------
// We make this a reference to a heap-allocated object so that we can avoid
// the destruction on exit. We are in charge of cleaning up the object. Note
// that the object will leak, but this is not a problem.
inline auto& stack_table = *(new StackTable());
