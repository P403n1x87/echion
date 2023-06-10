// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <condition_variable>
#include <mutex>
#include <thread>

static PyThreadState *main_thread = NULL;

static std::thread *sampler_thread = nullptr;

static int running = 0;

static std::thread *where_thread = nullptr;
static std::condition_variable where_cv;
static std::mutex where_lock;
