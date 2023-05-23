// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <unicodeobject.h>

#include <cstdint>

#include <echion/vm.h>

// ----------------------------------------------------------------------------
static char *
pybytes_to_bytes_and_size(PyObject *bytes_addr, Py_ssize_t *size)
{
    PyBytesObject bytes;

    if (copy_type(bytes_addr, bytes))
        return NULL;

    *size = bytes.ob_base.ob_size;
    if (*size < 0 || *size > (1 << 20))
        return NULL;

    char *data = new char[*size];
    if (copy_generic(((char *)bytes_addr) + offsetof(PyBytesObject, ob_sval), data, *size))
    {
        delete[] data;
        return NULL;
    }

    return data;
}

// ----------------------------------------------------------------------------
static const char *
pyunicode_to_utf8(PyObject *str_addr)
{
    PyUnicodeObject str;
    if (copy_type(str_addr, str))
        return NULL;

    PyASCIIObject &ascii = str._base._base;

    if (ascii.state.kind != 1)
        return NULL;

    const char *data = ascii.state.compact ? (const char *)(((uint8_t *)str_addr) + sizeof(ascii)) : (const char *)str._base.utf8;
    if (data == NULL)
        return NULL;

    Py_ssize_t size = ascii.state.compact ? ascii.length : str._base.utf8_length;
    if (size < 0 || size > 1024)
        return NULL;

    char *dest = new char[size + 1];
    if (copy_generic(data, dest, size))
    {
        delete[] dest;
        return NULL;
    }

    dest[size] = '\0';

    return dest;
}
