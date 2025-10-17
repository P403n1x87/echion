// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <string>

// Sampling interval
inline unsigned int interval = 1000;

// CPU Time mode
inline int cpu = 0;

// For cpu time mode, Echion only unwinds threads that're running by default.
// Set this to false to unwind all threads.
inline bool ignore_non_running_threads = true;

// Memory events
inline int memory = 0;

// Native stack sampling
inline int native = 0;

// Where mode
inline int where = 0;

// Maximum number of frames to unwind
inline unsigned int max_frames = 2048;

// Pipe name (where mode IPC)
inline std::string pipe_name;

// ----------------------------------------------------------------------------
PyObject* set_interval(PyObject* Py_UNUSED(m), PyObject* args);

// ----------------------------------------------------------------------------
void _set_cpu(int new_cpu);

// ----------------------------------------------------------------------------
void _set_ignore_non_running_threads(bool new_ignore_non_running_threads);

// ----------------------------------------------------------------------------
PyObject* set_cpu(PyObject* Py_UNUSED(m), PyObject* args);

// ----------------------------------------------------------------------------
PyObject* set_memory(PyObject* Py_UNUSED(m), PyObject* args);

// ----------------------------------------------------------------------------
PyObject* set_native(PyObject* Py_UNUSED(m), PyObject* args);

// ----------------------------------------------------------------------------
PyObject* set_where(PyObject* Py_UNUSED(m), PyObject* args);

// ----------------------------------------------------------------------------
PyObject* set_pipe_name(PyObject* Py_UNUSED(m), PyObject* args);

// ----------------------------------------------------------------------------
PyObject* set_max_frames(PyObject* Py_UNUSED(m), PyObject* args);
