// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <frameobject.h>
#if PY_VERSION_HEX >= 0x030b0000
#include <internal/pycore_frame.h>
#endif

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <functional>
#include <iostream>

#include <cxxabi.h>
#define UNW_LOCAL_ONLY
#include <libunwind.h>

#include <echion/cache.h>
#include <echion/strings.h>
#include <echion/vm.h>

#define MOJO_INT32 ((uintptr_t)(1 << (6 + 7 * 3)) - 1)

class Frame
{
public:
    typedef std::reference_wrapper<Frame> Ref;
    typedef std::unique_ptr<Frame> Ptr;

    class Error : public std::exception
    {
    public:
        const char *what() const noexcept override
        {
            return "Cannot read frame";
        }
    };

    class LocationError : public Error
    {
    public:
        const char *what() const noexcept override
        {
            return "Cannot determine frame location information";
        }
    };

    std::string filename;
    std::string name;

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

    void render(std::ostream &stream)
    {
        stream << ";" << filename << ":" << name << ":" << location.line;
    }

    void render_where(std::ostream &stream)
    {
        if ((filename).rfind("native@", 0) == 0)
            stream << "          \033[38;5;248;1m" << name
                   << "\033[0m \033[38;5;246m(" << filename
                   << "\033[0m:\033[38;5;246m" << location.line
                   << ")\033[0m" << std::endl;
        else
            stream << "          \033[33;1m" << name
                   << "\033[0m (\033[36m" << filename
                   << "\033[0m:\033[32m" << location.line
                   << "\033[0m)" << std::endl;
    }

    Frame(const char *name) : name({std::string(name)}){};
    Frame(std::string &name) : name(name){};

    static Frame &read(PyObject *, PyObject **);
    static Frame &read(PyObject *frame_addr)
    {
        PyObject *unused;
        return Frame::read(frame_addr, &unused);
    }

    static Frame &get(PyCodeObject *code, int lasti);
    static Frame &get(unw_word_t pc, const char *name, unw_word_t offset);

    Frame(PyCodeObject *, int);
    Frame(unw_word_t, const char *, unw_word_t);

private:
    void infer_location(PyCodeObject *, int);

    static inline uintptr_t key(PyCodeObject *code, int lasti)
    {
        return (((uintptr_t)(((uintptr_t)code) & MOJO_INT32) << 16) | lasti);
    }
};

#if PY_VERSION_HEX >= 0x030b0000
// ----------------------------------------------------------------------------
static inline int
_read_varint(unsigned char *table, ssize_t *i)
{
    int val = table[++*i] & 63;
    int shift = 0;
    while (table[*i] & 64)
    {
        shift += 6;
        val |= (table[++*i] & 63) << shift;
    }
    return val;
}

// ----------------------------------------------------------------------------
static inline int
_read_signed_varint(unsigned char *table, ssize_t *i)
{
    int val = _read_varint(table, i);
    return (val & 1) ? -(val >> 1) : (val >> 1);
}
#endif

// ----------------------------------------------------------------------------
void Frame::infer_location(PyCodeObject *code, int lasti)
{
    unsigned int lineno = code->co_firstlineno;
    Py_ssize_t len = 0;

#if PY_VERSION_HEX >= 0x030b0000
    auto table = pybytes_to_bytes_and_size(code->co_linetable, &len);
    if (table == nullptr)
        return;

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
            lineno += _read_signed_varint(table_data, &i);

            this->location.line = lineno;
            this->location.line_end = lineno + _read_varint(table_data, &i);
            this->location.column = _read_varint(table_data, &i);
            this->location.column_end = _read_varint(table_data, &i);

            break;

        case 13: // No column data
            lineno += _read_signed_varint(table_data, &i);

            this->location.line = lineno;
            this->location.line_end = lineno;
            this->location.column = this->location.column_end = 0;

            break;

        case 12: // New lineno
        case 11:
        case 10:
            lineno += code - 10;

            this->location.line = lineno;
            this->location.line_end = lineno;
            this->location.column = 1 + table[++i];
            this->location.column_end = 1 + table[++i];

            break;

        default:
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
        return;

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
        return;

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

// ----------------------------------------------------------------------------
Frame::Frame(PyCodeObject *code, int lasti)
{
    try
    {
        filename = pyunicode_to_utf8(code->co_filename);
#if PY_VERSION_HEX >= 0x030b0000
        name = pyunicode_to_utf8(code->co_qualname);
#else
        name = pyunicode_to_utf8(code->co_name);
#endif
    }
    catch (StringError &)
    {
        throw Error();
    }

    infer_location(code, lasti);
}

Frame::Frame(unw_word_t pc, const char *name, unw_word_t offset)
{
    filename = std::string(32, '\0');
    std::snprintf((char *)filename.c_str(), 32, "native@%p", (void *)pc);

    // Try to demangle C++ names
    char *demangled = NULL;
    if (name[0] == '_' && name[1] == 'Z')
    {
        int status;
        demangled = abi::__cxa_demangle(name, NULL, NULL, &status);
        if (status == 0)
            name = demangled;
    }

    // Make a copy
    this->name = std::string(name);

    if (demangled != NULL)
        std::free(demangled);

    location.line = offset;
}

// ----------------------------------------------------------------------------

static Frame INVALID_FRAME("<invalid>");
static Frame UNKNOWN_FRAME("<unknown>");

static std::unique_ptr<LRUCache<uintptr_t, Frame>> frame_cache = nullptr;

static void init_frame_cache(size_t capacity)
{
    frame_cache = std::make_unique<LRUCache<uintptr_t, Frame>>(capacity);
}

static void reset_frame_cache()
{
    frame_cache.reset();
}

Frame &Frame::get(PyCodeObject *code_addr, int lasti)
{
    PyCodeObject code;
    if (copy_type(code_addr, code))
        return INVALID_FRAME;

    uintptr_t frame_key = Frame::key(code_addr, lasti);

    try
    {
        return frame_cache->lookup(frame_key);
    }
    catch (LRUCache<uintptr_t, Frame>::LookupError &)
    {
        try
        {
            auto new_frame = std::make_unique<Frame>(&code, lasti);
            auto &f = *new_frame;
            frame_cache->store(frame_key, std::move(new_frame));
            return f;
        }
        catch (Frame::Error &)
        {
            return INVALID_FRAME;
        }
    }
}

Frame &Frame::get(unw_word_t pc, const char *name, unw_word_t offset)
{
    uintptr_t frame_key = (uintptr_t)pc;
    try
    {
        return frame_cache->lookup(frame_key);
    }
    catch (LRUCache<uintptr_t, Frame>::LookupError &)
    {
        auto frame = std::make_unique<Frame>(pc, name, offset);
        auto &f = *frame;
        frame_cache->store(frame_key, std::move(frame));
        return f;
    }
}

Frame &Frame::read(PyObject *frame_addr, PyObject **prev_addr)
{
#if PY_VERSION_HEX >= 0x030b0000
    _PyInterpreterFrame iframe;

    if (copy_type(frame_addr, iframe))
        throw Error();

    // We cannot use _PyInterpreterFrame_LASTI because _PyCode_CODE reads
    // from the code object.
    const int lasti = ((int)(iframe.prev_instr - (_Py_CODEUNIT *)(iframe.f_code))) - offsetof(PyCodeObject, co_code_adaptive) / sizeof(_Py_CODEUNIT);
    auto &frame = Frame::get(iframe.f_code, lasti);

    if (&frame != &INVALID_FRAME)
    {
#if PY_VERSION_HEX >= 0x030c0000
        frame.is_entry = (iframe.owner == FRAME_OWNED_BY_CSTACK); // Shim frame
#else
        frame.is_entry = iframe.is_entry;
#endif
    }

    *prev_addr = &frame == &INVALID_FRAME ? NULL : (PyObject *)iframe.previous;

#else // Python < 3.11
    // Unwind the stack from leaf to root and store it in a stack. This way we
    // can print it from root to leaf.
    PyFrameObject py_frame;

    if (copy_type(frame_addr, py_frame))
        throw Error();

    auto &frame = Frame::get(py_frame.f_code, py_frame.f_lasti);

    *prev_addr = (&frame == &INVALID_FRAME) ? NULL : (PyObject *)py_frame.f_back;
#endif

    return frame;
}
