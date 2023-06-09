// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <iostream>

// Sampling interval
static unsigned int interval = 1000;

// CPU Time mode
static int cpu = 0;

// Output stream
static std::ofstream output;

// Native stack sampling
static int native = 0;

// ----------------------------------------------------------------------------
static PyObject *
set_interval(PyObject *m, PyObject *args)
{
    unsigned int new_interval;
    if (!PyArg_ParseTuple(args, "I", &new_interval))
        return NULL;

    interval = new_interval;

    Py_RETURN_NONE;
}

// ----------------------------------------------------------------------------
static PyObject *
set_cpu(PyObject *m, PyObject *args)
{
    int new_cpu;
    if (!PyArg_ParseTuple(args, "p", &new_cpu))
        return NULL;

    cpu = new_cpu;

    Py_RETURN_NONE;
}

// ----------------------------------------------------------------------------
static PyObject *
set_native(PyObject *m, PyObject *args)
{
    int new_native;
    if (!PyArg_ParseTuple(args, "p", &new_native))
        return NULL;

    native = new_native;

    Py_RETURN_NONE;
}
