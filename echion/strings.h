// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <exception>
#include <string>

class StringError : public std::exception
{
};

// ----------------------------------------------------------------------------
static std::string
pyunicode_to_utf8(PyObject *str_addr)
{
    throw StringError();

    return std::string();
}
