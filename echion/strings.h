// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#include <unordered_map>

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <unicodeobject.h>

#include <cstdint>
#include <string>

#ifndef UNWIND_NATIVE_DISABLE
#include <cxxabi.h>
#define UNW_LOCAL_ONLY
#include <libunwind.h>
#endif  // UNWIND_NATIVE_DISABLE


#include <echion/long.h>
#include <echion/render.h>
#include <echion/vm.h>


std::unique_ptr<unsigned char[]> pybytes_to_bytes_and_size(PyObject* bytes_addr, Py_ssize_t* size);

[[nodiscard]] Result<std::string> pyunicode_to_utf8(PyObject* str_addr);

// ----------------------------------------------------------------------------
class StringTable : public std::unordered_map<uintptr_t, std::string>
{
public:
    using Key = uintptr_t;

    static constexpr Key INVALID = 1;
    static constexpr Key UNKNOWN = 2;

    // Python string object
    [[nodiscard]] Result<Key> key(PyObject* s);

    // Python string object
    [[nodiscard]] Key key_unsafe(PyObject* s);

#ifndef UNWIND_NATIVE_DISABLE
    // Native filename by program counter
    [[nodiscard]] Key key(unw_word_t pc);

    // Native scope name by unwinding cursor
    [[nodiscard]] Result<Key> key(unw_cursor_t& cursor);
#endif  // UNWIND_NATIVE_DISABLE

    [[nodiscard]] Result<std::string*> lookup(Key key);

    StringTable();

private:
    std::mutex table_lock;
};

// We make this a reference to a heap-allocated object so that we can avoid
// the destruction on exit. We are in charge of cleaning up the object. Note
// that the object will leak, but this is not a problem.
inline StringTable& string_table = *(new StringTable());
