// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#if PY_VERSION_HEX >= 0x030c0000
// https://github.com/python/cpython/issues/108216#issuecomment-1696565797
#undef _PyGC_FINALIZED
#endif
#include <frameobject.h>
#if PY_VERSION_HEX >= 0x030d0000
#define Py_BUILD_CORE
#include <internal/pycore_code.h>
#endif // PY_VERSION_HEX >= 0x030d0000
#if PY_VERSION_HEX >= 0x030b0000
#include <internal/pycore_frame.h>
#endif

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <functional>

#ifndef UNWIND_NATIVE_DISABLE
#include <cxxabi.h>
#define UNW_LOCAL_ONLY
#include <libunwind.h>
#endif // UNWIND_NATIVE_DISABLE

#include <echion/cache.h>
#include <echion/mojo.h>
#include <echion/render.h>
#include <echion/strings.h>
#include <echion/vm.h>

// ----------------------------------------------------------------------------
class Frame
{
public:
    using Ref = std::reference_wrapper<Frame>;
    using Ptr = std::unique_ptr<Frame>;
    using Key = uintptr_t;

    // ------------------------------------------------------------------------
    class Error : public std::exception
    {
    public:
        const char *what() const noexcept override
        {
            return "Cannot read frame";
        }
    };

    // ------------------------------------------------------------------------
    class LocationError : public Error
    {
    public:
        const char *what() const noexcept override
        {
            return "Cannot determine frame location information";
        }
    };

    // ------------------------------------------------------------------------
    Key cache_key = 0;
    StringTable::Key filename = 0;
    StringTable::Key name = 0;

    struct _location
    {
        int line = 0;
        int line_end = 0;
        int column = 0;
        int column_end = 0;
    } location;

#if PY_VERSION_HEX >= 0x030b0000
    bool is_entry = false;
#endif

    static Frame &read(PyObject *frame_addr, PyObject **prev_addr);
#if PY_VERSION_HEX >= 0x030b0000
    static Frame &read_local(_PyInterpreterFrame *frame_addr, PyObject **prev_addr);
#endif

    static Frame &get(PyCodeObject *code_addr, int lasti);
    static Frame &get(PyObject *frame);
#ifndef UNWIND_NATIVE_DISABLE
    static Frame &get(unw_cursor_t &cursor);
#endif // UNWIND_NATIVE_DISABLE
    static Frame &get(StringTable::Key name);

    Frame(StringTable::Key name) : name(name) {};
    Frame(PyObject *frame);
    Frame(PyCodeObject *code, int lasti);
#ifndef UNWIND_NATIVE_DISABLE
    Frame(unw_cursor_t &cursor, unw_word_t pc);
#endif // UNWIND_NATIVE_DISABLE

private:
    void inline infer_location(PyCodeObject *code, int lasti);
    static inline Key key(PyCodeObject *code, int lasti);
    static inline Key key(PyObject *frame);
};

// ----------------------------------------------------------------------------

inline auto INVALID_FRAME = Frame(StringTable::INVALID);
inline auto UNKNOWN_FRAME = Frame(StringTable::UNKNOWN);

// We make this a raw pointer to prevent its destruction on exit, since we
// control the lifetime of the cache.
inline LRUCache<uintptr_t, Frame> *frame_cache{nullptr};

// ----------------------------------------------------------------------------
void init_frame_cache(size_t capacity);
void reset_frame_cache();

// ------------------------------------------------------------------------
// Renderer functions defined in render.h that need to know about Frame
// are implemented here.
// ------------------------------------------------------------------------

// ------------------------------------------------------------------------
inline void WhereRenderer::render_frame(Frame &frame)
{
    auto name_str = string_table.lookup(frame.name);
    auto filename_str = string_table.lookup(frame.filename);
    auto line = frame.location.line;

    if (filename_str.rfind("native@", 0) == 0)
    {
        WhereRenderer::get().render_message(
            "\033[38;5;248;1m" + name_str + "\033[0m \033[38;5;246m(" +
            filename_str + "\033[0m:\033[38;5;246m" + std::to_string(line) +
            ")\033[0m");
    }
    else
    {
        WhereRenderer::get().render_message(
            "\033[33;1m" + name_str + "\033[0m (\033[36m" + filename_str +
            "\033[0m:\033[32m" + std::to_string(line) + "\033[0m)");
    }
}

// ------------------------------------------------------------------------
inline void MojoRenderer::render_frame(Frame &frame)
{
    frame_ref(frame.cache_key);
}
