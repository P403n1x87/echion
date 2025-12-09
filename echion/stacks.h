// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <deque>
#include <unordered_set>

#include <echion/config.h>
#include <echion/frame.h>
#if PY_VERSION_HEX >= 0x030b0000
#include "echion/stack_chunk.h"
#endif // PY_VERSION_HEX >= 0x030b0000
#include <echion/errors.h>

// ----------------------------------------------------------------------------

class FrameStack : public std::deque<Frame::Ref>
{
  public:
    using Key = Frame::Key;
    using Ptr = std::unique_ptr<FrameStack>;

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
        for (auto it = this->rbegin(); it != this->rend(); ++it) {
#if PY_VERSION_HEX >= 0x030c0000
            if ((*it).get().is_entry)
                // This is a shim frame so we skip it.
                continue;
#endif
            Renderer::get().render_frame((*it).get());
        }
    }

    void render_where()
    {
        for (auto it = this->rbegin(); it != this->rend(); ++it)
        {
#if PY_VERSION_HEX >= 0x030c0000
            if ((*it).get().is_entry)
                // This is a shim frame so we skip it.
                continue;
#endif
            WhereRenderer::get().render_frame((*it).get());
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

inline FrameStack python_stack;
inline FrameStack native_stack;
inline FrameStack interleaved_stack;

// ----------------------------------------------------------------------------
static size_t
unwind_frame(PyObject* frame_addr, FrameStack& stack)
{
    std::unordered_set<PyObject*> seen_frames; // Used to detect cycles in the stack
    int count = 0;

    PyObject* current_frame_addr = frame_addr;
    while (current_frame_addr != NULL && stack.size() < max_frames) {
        if (seen_frames.find(current_frame_addr) != seen_frames.end())
            break;

        seen_frames.insert(current_frame_addr);

#if PY_VERSION_HEX >= 0x030b0000
        auto maybe_frame = Frame::read(reinterpret_cast<_PyInterpreterFrame*>(current_frame_addr),
                                       reinterpret_cast<_PyInterpreterFrame**>(&current_frame_addr));
#else
        auto maybe_frame = Frame::read(current_frame_addr, &current_frame_addr);
#endif
        if (!maybe_frame) {
            break;
        }

        if (maybe_frame->get().name == StringTable::C_FRAME) {
            continue;
        }

        stack.push_back(*maybe_frame);
        count++;
    }

    return count;
}

// ----------------------------------------------------------------------------
static void
unwind_python_stack(PyThreadState* tstate, FrameStack& stack)
{
    stack.clear();
#if PY_VERSION_HEX >= 0x030b0000
    if (stack_chunk == nullptr) {
        stack_chunk = std::make_unique<StackChunk>();
    }

    if (!stack_chunk->update(reinterpret_cast<_PyStackChunk*>(tstate->datastack_chunk))) {
        stack_chunk = nullptr;
    }
#endif

#if PY_VERSION_HEX >= 0x030d0000
    PyObject* frame_addr = reinterpret_cast<PyObject*>(tstate->current_frame);
#elif PY_VERSION_HEX >= 0x030b0000
    _PyCFrame cframe;
    _PyCFrame* cframe_addr = tstate->cframe;
    if (copy_type(cframe_addr, cframe))
        // TODO: Invalid frame
        return;

    PyObject* frame_addr = reinterpret_cast<PyObject*>(cframe.current_frame);
#else // Python < 3.11
    PyObject* frame_addr = reinterpret_cast<PyObject*>(tstate->frame);
#endif
    unwind_frame(frame_addr, stack);
}

// ----------------------------------------------------------------------------
static void
unwind_python_stack(PyThreadState* tstate)
{
    unwind_python_stack(tstate, python_stack);
}

static Result<void> interleave_stacks(FrameStack& python_stack)
{
    interleaved_stack.clear();

    auto p = python_stack.rbegin();
    // The last two frames are usually the signal trampoline and the signal
    // handler. We skip them.
    for (auto n = native_stack.rbegin(); n != native_stack.rend() - 2; ++n)
    {
        auto native_frame = *n;

        auto maybe_name = string_table.lookup(native_frame.get().name);
        if (!maybe_name)
        {
            return ErrorKind::LookupError;
        }

        const auto& name = maybe_name->get();
        if (name.find("PyEval_EvalFrameDefault") != std::string::npos)
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

    return Result<void>::ok();
}

// ----------------------------------------------------------------------------
static Result<void> interleave_stacks()
{
    return interleave_stacks(python_stack);
}

// ----------------------------------------------------------------------------
class StackInfo
{
  public:
    StringTable::Key task_name;
    bool on_cpu;
    FrameStack stack;

    StackInfo(StringTable::Key task_name, bool on_cpu)
      : task_name(task_name)
      , on_cpu(on_cpu)
    {
    }
};

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
    FrameStack& retrieve(FrameStack::Key stack_key)
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
inline auto& stack_table = *(new StackTable());