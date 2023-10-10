// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <unicodeobject.h>

#include <cstdint>
#include <optional>
#include <string>

#include <echion/vm.h>

// ----------------------------------------------------------------------------
static std::unique_ptr<unsigned char[]>
pybytes_to_bytes_and_size(PyObject *bytes_addr, Py_ssize_t *size)
{
    PyBytesObject bytes;

    if (copy_type(bytes_addr, bytes))
        return nullptr;

    *size = bytes.ob_base.ob_size;
    if (*size < 0 || *size > (1 << 20))
        return nullptr;

    auto data = std::make_unique<unsigned char[]>(*size);
    if (copy_generic(((char *)bytes_addr) + offsetof(PyBytesObject, ob_sval), data.get(), *size))
        return nullptr;

    return data;
}

// ----------------------------------------------------------------------------
static std::optional<std::string>
pyunicode_to_utf8(PyObject *str_addr)
{
    PyUnicodeObject str;
    if (copy_type(str_addr, str))
        return {};

    PyASCIIObject &ascii = str._base._base;

    if (ascii.state.kind != 1)
        return {};

    const char *data = ascii.state.compact ? (const char *)(((uint8_t *)str_addr) + sizeof(ascii)) : (const char *)str._base.utf8;
    if (data == NULL)
        return {};

    Py_ssize_t size = ascii.state.compact ? ascii.length : str._base.utf8_length;
    if (size < 0 || size > 1024)
        return {};

    auto dest = std::string(size, '\0');
    if (copy_generic(data, dest.c_str(), size))
        return {};

    return {dest};
}
