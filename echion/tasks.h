// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#include <memory>
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

    PyObject* origin = NULL;
    PyObject* frame = NULL;

    GenInfo::Ptr await = nullptr;

    bool is_running = false;

    [[nodiscard]] static Result<GenInfo> create(PyObject* gen_addr);

    GenInfo(PyObject* origin, PyObject* frame, std::unique_ptr<GenInfo> await, bool is_running)
        : origin(origin), frame(frame), await(std::move(await)), is_running(is_running)
    {
    }

    GenInfo(GenInfo&& other) noexcept = default;
    GenInfo& operator=(GenInfo&& other) noexcept = default;

private:
    GenInfo(const GenInfo&) = delete;
    GenInfo& operator=(const GenInfo&) = delete;
};

[[nodiscard]] inline Result<GenInfo> GenInfo::create(PyObject* gen_addr)
{
    PyGenObject gen;

    if (copy_type(gen_addr, gen) || !PyCoro_CheckExact(&gen))
        return Result<GenInfo>::error(ErrorKind::GenInfoError);

    auto origin = gen_addr;

#if PY_VERSION_HEX >= 0x030b0000
    // The frame follows the generator object
    auto frame = (gen.gi_frame_state == FRAME_CLEARED)
                ? NULL
                : (PyObject*)((char*)gen_addr + offsetof(PyGenObject, gi_iframe));
#else
    auto frame = (PyObject*)gen.gi_frame;
#endif

    PyFrameObject f;
    if (copy_type(frame, f))
        return Result<GenInfo>::error(ErrorKind::GenInfoError);

    PyObject* yf = (frame != NULL ? PyGen_yf(&gen, frame) : NULL);
    std::unique_ptr<GenInfo> await = nullptr;
    if (yf != NULL && yf != gen_addr)
    {
        auto maybe_gen_info = GenInfo::create(yf);
        if (maybe_gen_info) {
            await = std::make_unique<GenInfo>(std::move(*maybe_gen_info));
        }
    }

#if PY_VERSION_HEX >= 0x030b0000
    auto is_running = (gen.gi_frame_state == FRAME_EXECUTING);
#elif PY_VERSION_HEX >= 0x030a0000
    auto is_running = (frame != NULL) ? _PyFrame_IsExecuting(&f) : false;
#else
    auto is_running = gen.gi_running;
#endif

    return Result<GenInfo>(GenInfo(origin, frame, std::move(await), is_running));
}

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

    static Result<TaskInfo> create(TaskObj*);

    static Result<TaskInfo> current(PyObject*);
    
    TaskInfo(TaskInfo&& other) noexcept = default;
    TaskInfo& operator=(TaskInfo&& other) noexcept = default;
    
    inline size_t unwind(FrameStack&);

private:
    TaskInfo(PyObject* origin, PyObject* loop, std::unique_ptr<GenInfo> coro, StringTable::Key name, std::unique_ptr<TaskInfo> waiter)
        : origin(origin), loop(loop), coro(std::move(coro)), name(name), waiter(std::move(waiter))
    {
    }

    TaskInfo(const TaskInfo&) = delete;
    TaskInfo& operator=(const TaskInfo&) = delete;
};

inline std::unordered_map<PyObject*, PyObject*> task_link_map;
inline std::mutex task_link_map_lock;

// ----------------------------------------------------------------------------
[[nodiscard]] inline Result<TaskInfo> TaskInfo::create(TaskObj* task_addr)
{
    TaskObj task;
    
    if (copy_type(task_addr, task))
        return Result<TaskInfo>::error(ErrorKind::TaskInfoError);

    auto maybe_coro = GenInfo::create(task.task_coro);
    if (!maybe_coro)
        return Result<TaskInfo>::error(ErrorKind::TaskInfoError);
        
    auto coro = std::make_unique<GenInfo>(std::move(*maybe_coro));

    auto origin = (PyObject*)task_addr;

    auto maybe_name = string_table.key(task.task_name);
    if (!maybe_name)
        return Result<TaskInfo>::error(ErrorKind::TaskInfoError);

    auto loop = task.task_loop;

    std::unique_ptr<TaskInfo> waiter = nullptr;
    if (task.task_fut_waiter)
    {
        auto maybe_waiter = TaskInfo::create((TaskObj*)task.task_fut_waiter);
        if (maybe_waiter)
        {
            waiter = std::make_unique<TaskInfo>(std::move(*maybe_waiter));
        }
    }
    
    return Result<TaskInfo>(TaskInfo(origin, loop, std::move(coro), *maybe_name, std::move(waiter)));
}

// ----------------------------------------------------------------------------
[[nodiscard]] inline Result<TaskInfo> TaskInfo::current(PyObject* loop)
{
    if (loop == NULL)
        return Result<TaskInfo>::error(ErrorKind::TaskInfoError);

    auto maybe_dict = MirrorDict::create(asyncio_current_tasks);
    if (!maybe_dict)
        return Result<TaskInfo>::error(ErrorKind::TaskInfoError);
    
    auto maybe_task = maybe_dict->get_item(loop);
    if (!maybe_task)
        return Result<TaskInfo>::error(ErrorKind::TaskInfoError);
        
    PyObject* task = *maybe_task;
    if (task == NULL)
        return Result<TaskInfo>::error(ErrorKind::TaskInfoError);

    return TaskInfo::create((TaskObj*)task);
}

// ----------------------------------------------------------------------------
// TODO: Make this a "for_each_task" function?
inline Result<std::vector<TaskInfo::Ptr>> get_all_tasks(PyObject* loop)
{
    std::vector<TaskInfo::Ptr> tasks;
    if (loop == NULL)
        return tasks;

    auto maybe_scheduled_set = MirrorSet::create(asyncio_scheduled_tasks);
    if (!maybe_scheduled_set) {
        return Result<std::vector<TaskInfo::Ptr>>::error(ErrorKind::MirrorError);
    }

    auto maybe_scheduled_tasks = maybe_scheduled_set->as_unordered_set();
    if (!maybe_scheduled_tasks)
        return tasks;

    for (auto task_wr_addr : *maybe_scheduled_tasks)
    {
        PyWeakReference task_wr;
        if (copy_type(task_wr_addr, task_wr))
            continue;

        auto maybe_task = TaskInfo::create((TaskObj*)task_wr.wr_object);
        if (maybe_task) {
            auto task_info = std::make_unique<TaskInfo>(std::move(*maybe_task));
            if (task_info->loop == loop)
                tasks.push_back(std::move(task_info));
        }
    }

    if (asyncio_eager_tasks != NULL)
    {
        auto maybe_eager_set = MirrorSet::create(asyncio_eager_tasks);
        if (!maybe_eager_set) {
            return Result<std::vector<TaskInfo::Ptr>>::error(ErrorKind::MirrorError);
        }

        auto maybe_eager_tasks = maybe_eager_set->as_unordered_set();
        if (!maybe_eager_tasks)
        {
            return Result<std::vector<TaskInfo::Ptr>>::error(ErrorKind::MirrorError);
        }

        for (auto task_addr : *maybe_eager_tasks)
        {
            auto maybe_task_info = TaskInfo::create((TaskObj*)task_addr);
            if (!maybe_task_info) {
                continue;
            }

            auto task_info = std::make_unique<TaskInfo>(std::move(*maybe_task_info));
            if (task_info->loop == loop)
                tasks.push_back(std::move(task_info));
        }
    }

    return tasks;
}

// ----------------------------------------------------------------------------

inline std::vector<std::unique_ptr<StackInfo>> current_tasks;

// ----------------------------------------------------------------------------

inline size_t TaskInfo::unwind(FrameStack& stack)
{
    // TODO: Check for running task.
    std::stack<PyObject*> coro_frames;

    // Unwind the coro chain
    for (auto coro = this->coro.get(); coro != NULL; coro = coro->await.get())
    {
        if (coro->frame != NULL)
            coro_frames.push(coro->frame);
    }

    int count = 0;

    // Unwind the coro frames
    while (!coro_frames.empty())
    {
        PyObject* frame = coro_frames.top();
        coro_frames.pop();

        count += unwind_frame(frame, stack);
    }

    return count;
}
