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

#include <echion/strings.h>
#include <echion/vm.h>

class Frame
{
public:
    const char *filename;
    const char *name;
    struct _location
    {
        int line;
        int line_end;
        int column;
        int column_end;
    } location;

    Frame(PyCodeObject &, int);
    Frame(const char *);
    ~Frame();

    bool is_valid();

    static uintptr_t key(PyCodeObject &, int);

private:
    int infer_location(PyCodeObject *, int);
};

#if PY_VERSION_HEX >= 0x030b0000
// ----------------------------------------------------------------------------
static inline int
_read_varint(unsigned char *table, size_t *i)
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
_read_signed_varint(unsigned char *table, size_t *i)
{
    int val = _read_varint(table, i);
    return (val & 1) ? -(val >> 1) : (val >> 1);
}
#endif

// ----------------------------------------------------------------------------
int Frame::infer_location(PyCodeObject *code, int lasti)
{
    unsigned int lineno = code->co_firstlineno;
    Py_ssize_t len = 0;
    unsigned char *table = NULL;

#if PY_VERSION_HEX >= 0x030b0000
    table = (unsigned char *)pybytes_to_bytes_and_size(code->co_linetable, &len);
    if (table == NULL)
        return 1;

    for (size_t i = 0, bc = 0; i < len; i++)
    {
        bc += (table[i] & 7) + 1;
        int code = (table[i] >> 3) & 15;
        unsigned char next_byte = 0;
        switch (code)
        {
        case 15:
            break;

        case 14: // Long form
            lineno += _read_signed_varint(table, &i);

            this->location.line = lineno;
            this->location.line_end = lineno + _read_varint(table, &i);
            this->location.column = _read_varint(table, &i);
            this->location.column_end = _read_varint(table, &i);

            break;

        case 13: // No column data
            lineno += _read_signed_varint(table, &i);

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

    return 0;

#elif PY_VERSION_HEX >= 0x030a0000
    table = (unsigned char *)pybytes_to_bytes_and_size(code->co_linetable, &len);
    if (table == NULL)
        return 1;

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
    table = (unsigned char *)pybytes_to_bytes_and_size(code->co_lnotab, &len);
    if (table == NULL)
        return 1;

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

    return 0;
}

Frame::Frame(const char *name)
{
    this->filename = "";
    this->name = name;
    this->location.line = 0;
    this->location.line_end = 0;
    this->location.column = 0;
    this->location.column_end = 0;
}

// ----------------------------------------------------------------------------
Frame::Frame(PyCodeObject &code, int lasti)
{
    this->filename = pyunicode_to_utf8(code.co_filename);
#if PY_VERSION_HEX >= 0x030b0000
    this->name = pyunicode_to_utf8(code.co_qualname);
#else
    this->name = pyunicode_to_utf8(code.co_name);
#endif
    this->infer_location(&code, lasti);
}

Frame::~Frame()
{
    delete[] this->filename;
    delete[] this->name;
}

bool Frame::is_valid()
{
    return this->filename != NULL && this->name != NULL && this->location.line != 0;
}
