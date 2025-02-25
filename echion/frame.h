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
#if PY_VERSION_HEX >= 0x030b0000
static int _read_varint(unsigned char *table, ssize_t size, ssize_t *i)
{
    ssize_t guard = size - 1;
    if (*i >= guard)
        return 0;

    int val = table[++*i] & 63;
    int shift = 0;
    while (table[*i] & 64 && *i < guard)
    {
        shift += 6;
        val |= (table[++*i] & 63) << shift;
    }
    return val;
}

// ----------------------------------------------------------------------------
static int _read_signed_varint(unsigned char *table, ssize_t size,
                               ssize_t *i)
{
    int val = _read_varint(table, size, i);
    return (val & 1) ? -(val >> 1) : (val >> 1);
}
#endif

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

    // ------------------------------------------------------------------------

    Frame(StringTable::Key name) : name(name) {};

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

    // ------------------------------------------------------------------------
    Frame(PyObject *frame);
    Frame(PyCodeObject *code, int lasti);
#ifndef UNWIND_NATIVE_DISABLE
    Frame(unw_cursor_t &cursor, unw_word_t pc);
#endif // UNWIND_NATIVE_DISABLE

private:
    // ------------------------------------------------------------------------
    void inline infer_location(PyCodeObject *code, int lasti)
    {
        unsigned int lineno = code->co_firstlineno;
        Py_ssize_t len = 0;

#if PY_VERSION_HEX >= 0x030b0000
        auto table = pybytes_to_bytes_and_size(code->co_linetable, &len);
        if (table == nullptr)
            throw LocationError();

        auto table_data = table.get();

        for (Py_ssize_t i = 0, bc = 0; i < len; i++)
        {
            bc += (table[i] & 7) + 1;
            int code = (table[i] >> 3) & 15;
            unsigned char next_byte = 0;
            switch (code)
            {
            case 15:
                break;

            case 14: // Long form
                lineno += _read_signed_varint(table_data, len, &i);

                this->location.line = lineno;
                this->location.line_end = lineno + _read_varint(table_data, len, &i);
                this->location.column = _read_varint(table_data, len, &i);
                this->location.column_end = _read_varint(table_data, len, &i);

                break;

            case 13: // No column data
                lineno += _read_signed_varint(table_data, len, &i);

                this->location.line = lineno;
                this->location.line_end = lineno;
                this->location.column = this->location.column_end = 0;

                break;

            case 12: // New lineno
            case 11:
            case 10:
                if (i >= len - 2)
                    throw LocationError();

                lineno += code - 10;

                this->location.line = lineno;
                this->location.line_end = lineno;
                this->location.column = 1 + table[++i];
                this->location.column_end = 1 + table[++i];

                break;

            default:
                if (i >= len - 1)
                    throw LocationError();

                next_byte = table[++i];

                this->location.line = lineno;
                this->location.line_end = lineno;
                this->location.column = 1 + (code << 3) + ((next_byte >> 4) & 7);
                this->location.column_end = this->location.column + (next_byte & 15);
            }

            if (bc > lasti)
                break;
        }

#elif PY_VERSION_HEX >= 0x030a0000
        auto table = pybytes_to_bytes_and_size(code->co_linetable, &len);
        if (table == nullptr)
            throw LocationError();

        lasti <<= 1;
        for (int i = 0, bc = 0; i < len; i++)
        {
            int sdelta = table[i++];
            if (sdelta == 0xff)
                break;

            bc += sdelta;

            int ldelta = table[i];
            if (ldelta == 0x80)
                ldelta = 0;
            else if (ldelta > 0x80)
                lineno -= 0x100;

            lineno += ldelta;
            if (bc > lasti)
                break;
        }

#else
        auto table = pybytes_to_bytes_and_size(code->co_lnotab, &len);
        if (table == nullptr)
            throw LocationError();

        for (int i = 0, bc = 0; i < len; i++)
        {
            bc += table[i++];
            if (bc > lasti)
                break;

            if (table[i] >= 0x80)
                lineno -= 0x100;

            lineno += table[i];
        }

#endif

        this->location.line = lineno;
        this->location.line_end = lineno;
        this->location.column = 0;
        this->location.column_end = 0;
    }

    // ------------------------------------------------------------------------
    static inline Key key(PyCodeObject *code, int lasti)
    {
        return (((uintptr_t)(((uintptr_t)code) & MOJO_INT32) << 16) | lasti);
    }

    // ------------------------------------------------------------------------
    static inline Key key(PyObject *frame)
    {

#if PY_VERSION_HEX >= 0x030d0000
        _PyInterpreterFrame *iframe = (_PyInterpreterFrame *)frame;
        const int lasti = _PyInterpreterFrame_LASTI(iframe);
        PyCodeObject *code = (PyCodeObject *)iframe->f_executable;
#elif PY_VERSION_HEX >= 0x030b0000
        const _PyInterpreterFrame *iframe = (_PyInterpreterFrame *)frame;
        const int lasti = _PyInterpreterFrame_LASTI(iframe);
        PyCodeObject *code = iframe->f_code;
#else
        const PyFrameObject *py_frame = (PyFrameObject *)frame;
        const int lasti = py_frame->f_lasti;
        PyCodeObject *code = py_frame->f_code;
#endif
        return key(code, lasti);
    }
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
