// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.
#pragma once

#include <Python.h>
#if PY_VERSION_HEX >= 0x030c0000
#include <internal/pycore_long.h>
// Note: Even if use the right PYLONG_BITS_IN_DIGIT that is specified in the
// Python we use to build echion, it can be different from the Python that is
// used to run the program.
#if PYLONG_BITS_IN_DIGIT == 30
typedef uint32_t digit;
#elif PYLONG_BITS_IN_DIGIT == 15
typedef unsigned short digit;
#else
#error "Unsupported PYLONG_BITS_IN_DIGIT"
#endif  // PYLONG_BITS_IN_DIGIT
#endif  // PY_VERSION_HEX >= 0x030c0000

#include <exception>

#include <echion/vm.h>

class LongError : public std::exception
{
    const char* what() const noexcept override;
};

long long pylong_to_llong(PyObject* long_addr);
