// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#if defined __GNUC__ && defined HAVE_STD_ATOMIC
#undef HAVE_STD_ATOMIC
#endif
#define Py_BUILD_CORE
#include <internal/pycore_pystate.h>

#include <condition_variable>
#include <mutex>
#include <thread>

static _PyRuntimeState *runtime = &_PyRuntime;
static PyThreadState *current_tstate = NULL;

static std::thread *sampler_thread = nullptr;

static int running = 0;

static std::thread *where_thread = nullptr;
static std::condition_variable where_cv;
static std::mutex where_lock;

static PyObject *asyncio_current_tasks = NULL;
static PyObject *asyncio_scheduled_tasks = NULL; // WeakSet
static PyObject *asyncio_eager_tasks = NULL;     // set
