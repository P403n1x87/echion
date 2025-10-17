// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <csignal>
#include <mutex>

#include <echion/stacks.h>
#include <echion/state.h>

inline std::mutex sigprof_handler_lock;

void sigprof_handler([[maybe_unused]] int signum);

void sigquit_handler([[maybe_unused]] int signum);

void install_signals();

void restore_signals();
