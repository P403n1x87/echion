// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <weakrefobject.h>

#if PY_VERSION_HEX >= 0x030b0000
#include <cpython/genobject.h>

#define Py_BUILD_CORE
#if PY_VERSION_HEX >= 0x030d0000
#include <opcode.h>
#else
#include <internal/pycore_opcode.h>
#endif  // PY_VERSION_HEX >= 0x030d0000
#else
#include <genobject.h>
#include <opcode.h>
#endif  // PY_VERSION_HEX >= 0x30b0000

#include <mutex>
#include <stack>
#include <unordered_map>
#include <vector>

#include <echion/config.h>
#include <echion/errors.h>
#include <echion/frame.h>
#include <echion/mirrors.h>
#include <echion/stacks.h>
#include <echion/state.h>
#include <echion/strings.h>
#include <echion/timing.h>

#include <echion/cpython/tasks.h>

// Max number of recursive calls GenInfo::GenInfo and TaskInfo::TaskInfo can do
// before raising an error.
const constexpr size_t MAX_RECURSION_DEPTH = 250;

class GenInfo
{
public:
    typedef std::unique_ptr<GenInfo> Ptr;

    PyObject* origin = NULL;
    PyObject* frame = NULL;

    GenInfo::Ptr await = nullptr;

    bool is_running = false;

    [[nodiscard]] static Result<GenInfo::Ptr> create(PyObject* gen_addr);
    GenInfo(PyObject* origin, PyObject* frame, GenInfo::Ptr await, bool is_running);
};

// ----------------------------------------------------------------------------

class TaskInfo
{
public:
    typedef std::unique_ptr<TaskInfo> Ptr;
    typedef std::reference_wrapper<TaskInfo> Ref;

    PyObject* origin = NULL;
    PyObject* loop = NULL;

    GenInfo::Ptr coro = nullptr;

    StringTable::Key name;

    // Information to reconstruct the async stack as best as we can
    TaskInfo::Ptr waiter = nullptr;

    [[nodiscard]] static Result<TaskInfo::Ptr> create(TaskObj*);
    TaskInfo(PyObject* origin, PyObject* loop, GenInfo::Ptr coro, StringTable::Key name,
             TaskInfo::Ptr waiter);

    [[nodiscard]] static Result<TaskInfo::Ptr> current(PyObject*);
    size_t unwind(FrameStack&);
};

[[nodiscard]] Result<std::vector<TaskInfo::Ptr>> get_all_tasks(PyObject* loop);

inline std::unordered_map<PyObject*, PyObject*> task_link_map;
inline std::mutex task_link_map_lock;

inline std::vector<std::unique_ptr<StackInfo>> current_tasks;
