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

#include <cxxabi.h>
#define UNW_LOCAL_ONLY
#include <libunwind.h>

#include <echion/cache.h>
#include <echion/render.h>
#include <echion/strings.h>
#include <echion/vm.h>

#define MOJO_INT32 ((uintptr_t)(1 << (6 + 7 * 3)) - 1)

#define PRINT_THREAD_AND_FUNC() \
    std::cout << "Thread ID: " << gettid() \
              << ", Function: " << __func__ << std::endl;

class Frame
{
public:
    typedef std::reference_wrapper<Frame> Ref;

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

    const StringTable::Key filename = 0;
    const StringTable::Key name = 0;

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

    void render()
    {
        PRINT_THREAD_AND_FUNC();
        std::string *name_str = nullptr;
        std::string *filename_str = nullptr;
        try {
            name_str = &string_table.lookup(name);
            std::cout << "[" << gettid() << "] (" << this << ") name_str is " << *name_str << ", name is " << std::hex << name << std::endl;
        } catch (...)  {
            std::cout << "[" << gettid() << "] (" << this << ") name is " << std::hex << name << std::endl;
        }

        try {
            filename_str = &string_table.lookup(filename);
        } catch (...)  {
            std::cout << "[" << gettid() << "] (" << this << ") filename is " << std::hex << filename << std::endl;
            std::abort();
        }
        Renderer::get().render_python_frame(*filename_str, *name_str, location.line);
    }

    void render_where()
    {
        PRINT_THREAD_AND_FUNC();
        std::cout << "UNEXPECTED!!!!!!!!!!!!!!!!!" << std::endl;
        auto name_str = string_table.lookup(name);
        auto filename_str = string_table.lookup(filename);
        auto line = location.line;
        if (filename_str.rfind("native@", 0) == 0)
            Renderer::get().render_python_frame(filename_str, name_str, line);
        else
            Renderer::get().render_native_frame(filename_str, name_str, line);
    }

    Frame(StringTable::Key name) : name(name){};

    static Frame &read(PyObject *, PyObject **);
    static Frame &read(PyObject *frame_addr)
    {
        PRINT_THREAD_AND_FUNC();
        PyObject *unused;
        return Frame::read(frame_addr, &unused);
    }

    ~Frame() {
      std::cout << "Frame destructor called [" << gettid() << "](" << this << ")" << std::endl;
    }

    static Frame &get(PyCodeObject *, int);
    static Frame &get(StringTable::Key);

    Frame(PyCodeObject *, int);

    Frame(unw_cursor_t &, unw_word_t);
    static Frame &get(unw_cursor_t &);

private:
    void infer_location(PyCodeObject *, int);

    static inline uintptr_t key(PyCodeObject *code, int lasti)
    {
        PRINT_THREAD_AND_FUNC();
        return (((uintptr_t)(((uintptr_t)code) & MOJO_INT32) << 16) | lasti);
    }
};

static auto INVALID_FRAME = Frame(StringTable::INVALID);
static auto UNKNOWN_FRAME = Frame(StringTable::UNKNOWN);

#if PY_VERSION_HEX >= 0x030b0000
// ----------------------------------------------------------------------------
static inline int
_read_varint(unsigned char *table, ssize_t size, ssize_t *i)
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
static inline int
_read_signed_varint(unsigned char *table, ssize_t size, ssize_t *i)
{
    int val = _read_varint(table, size, i);
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

// ----------------------------------------------------------------------------
Frame::Frame(PyCodeObject *code, int lasti) :
        filename{string_table.key(code->co_filename)},
#if PY_VERSION_HEX >= 0x030b0000
        name{string_table.key(code->co_qualname)}
#else
        name{string_table.key(code->co_name)}
#endif
{
    try
    {
        std::cout << "[" << gettid() << "] (" << this << ") stashed name_str is " << string_table.lookup(name) << ", name is " << std::hex << name << std::endl;
    }
    catch (StringTable::Error &)
    {
        std::cout << "STRING ERROR" << std::endl;
        throw Error();
    }

    infer_location(code, lasti);
}

Frame::Frame(unw_cursor_t &cursor, unw_word_t pc) : name{string_table.key(cursor)}, filename{string_table.key(pc)}
{
    try
    {
        std::cout << "[" << gettid() << "] (" << this << ") stashed name_str is " << string_table.lookup(name) << ", name is " << std::hex << name << std::endl;
    }
    catch (StringTable::Error &)
    {
        throw Error();
    }
}

// ----------------------------------------------------------------------------

// We make this a raw pointer to prevent its destruction on exit, since we
// control the lifetime of the cache.
static LRUCache<uintptr_t, Frame> *frame_cache = nullptr;

static void init_frame_cache(size_t capacity)
{
    frame_cache = new LRUCache<uintptr_t, Frame>(capacity);
}

static void reset_frame_cache()
{
    PRINT_THREAD_AND_FUNC();
    delete frame_cache;
    frame_cache = nullptr;
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

Frame &Frame::get(unw_cursor_t &cursor)
{
    unw_word_t pc;
    unw_get_reg(&cursor, UNW_REG_IP, &pc);
    if (pc == 0)
        throw Error();

    uintptr_t frame_key = (uintptr_t)pc;
    try
    {
        return frame_cache->lookup(frame_key);
    }
    catch (LRUCache<uintptr_t, Frame>::LookupError &)
    {
        try
        {
            auto frame = std::make_unique<Frame>(cursor, pc);
            auto &f = *frame;
            frame_cache->store(frame_key, std::move(frame));
            return f;
        }
        catch (Frame::Error &)
        {
            return UNKNOWN_FRAME;
        }
    }
}

Frame &Frame::get(StringTable::Key name)
{
    uintptr_t frame_key = (uintptr_t)name;
    try
    {
        return frame_cache->lookup(frame_key);
    }
    catch (LRUCache<uintptr_t, Frame>::LookupError &)
    {
        auto frame = std::make_unique<Frame>(name);
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
