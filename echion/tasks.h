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

#include <exception>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <echion/config.h>
#include <echion/frame.h>
#include <echion/mirrors.h>
#include <echion/stacks.h>
#include <echion/state.h>
#include <echion/strings.h>
#include <echion/timing.h>

#include <echion/cpython/tasks.h>

class GenInfo
{
public:
    typedef std::unique_ptr<GenInfo> Ptr;

    class Error : public std::exception
    {
    public:
        const char* what() const noexcept override;
    };

    PyObject* origin = NULL;
    PyObject* frame = NULL;

    GenInfo::Ptr await = nullptr;

    bool is_running = false;

    GenInfo(PyObject* gen_addr);
};

// ----------------------------------------------------------------------------

class TaskInfo
{
public:
    typedef std::unique_ptr<TaskInfo> Ptr;
    typedef std::reference_wrapper<TaskInfo> Ref;

    class Error : public std::exception
    {
    public:
        const char* what() const noexcept override;
    };

    class GeneratorError : public Error
    {
    public:
        const char* what() const noexcept override;
    };

    PyObject* origin = NULL;
    PyObject* loop = NULL;

    GenInfo::Ptr coro = nullptr;

    StringTable::Key name;

    // Information to reconstruct the async stack as best as we can
    TaskInfo::Ptr waiter = nullptr;

    TaskInfo(TaskObj*);

    static TaskInfo current(PyObject*);
    size_t unwind(FrameStack&);
};

inline std::unordered_map<PyObject*, PyObject*> task_link_map;
inline std::mutex task_link_map_lock;

// ----------------------------------------------------------------------------

inline std::vector<std::unique_ptr<StackInfo>> current_tasks;

std::vector<TaskInfo::Ptr> get_all_tasks(PyObject* loop);
