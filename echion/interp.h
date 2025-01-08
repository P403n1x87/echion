// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#if PY_VERSION_HEX >= 0x03090000
#define Py_BUILD_CORE
#if defined __GNUC__ && defined HAVE_STD_ATOMIC
#undef HAVE_STD_ATOMIC
#endif
#include <internal/pycore_interp.h>
#endif

#include <functional>

#include <echion/state.h>
#include <echion/vm.h>

static void for_each_interp(std::function<void(PyInterpreterState *interp)> callback)
{
    PyInterpreterState interp;
    for (PyInterpreterState *interp_addr = runtime->interpreters.head;
         !copy_type(interp_addr, interp);
         interp_addr = interp.next)
        callback(&interp);
}
