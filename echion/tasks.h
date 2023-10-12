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
#include <internal/pycore_opcode.h>
#else
#include <genobject.h>
#include <opcode.h>
#endif

#include <exception>
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
#include <echion/threads.h>
#include <echion/timing.h>

#include <echion/cpython/tasks.h>

class GenInfo
{
public:
    typedef std::unique_ptr<GenInfo> Ptr;

    class Error : public std::exception
    {
    public:
        const char *what() const noexcept override
        {
            return "Cannot create generator info object";
        }
    };

    PyObject *origin = NULL;
    PyObject *frame = NULL;

    GenInfo::Ptr await = nullptr;

    bool is_running = false;

    GenInfo(PyObject *gen_addr);
};

GenInfo::GenInfo(PyObject *gen_addr)
{
    PyGenObject gen;

    if (copy_type(gen_addr, gen) || !PyCoro_CheckExact(&gen))
        throw Error();

    origin = gen_addr;

#if PY_VERSION_HEX >= 0x030b0000
    // The frame follows the generator object
    frame = (gen.gi_frame_state == FRAME_CLEARED)
                ? NULL
                : (PyObject *)((char *)gen_addr + offsetof(PyGenObject, gi_iframe));
#else
    frame = (PyObject *)gen.gi_frame;
#endif

    PyFrameObject f;
    if (copy_type(frame, f))
        throw Error();

    PyObject *yf = (frame != NULL ? PyGen_yf(&gen, frame) : NULL);
    if (yf != NULL && yf != gen_addr)
    {
        try
        {
            await = std::make_unique<GenInfo>(yf);
        }
        catch (GenInfo::Error &)
        {
            await = nullptr;
        }
    }

#if PY_VERSION_HEX >= 0x030b0000
    is_running = (gen.gi_frame_state == FRAME_EXECUTING);
#elif PY_VERSION_HEX >= 0x030a0000
    is_running = (frame != NULL) ? _PyFrame_IsExecuting(&f) : false;
#else
    is_running = gen.gi_running;
#endif
}

// ----------------------------------------------------------------------------

class TaskInfo
{
public:
    class Error : public std::exception
    {
    public:
        const char *what() const noexcept override
        {
            return "Cannot create task info object";
        }
    };

    PyObject *origin = NULL;
    PyObject *loop = NULL;

    GenInfo::Ptr coro = nullptr;
    Frame::Ptr root_frame = nullptr;

    std::string name;

    // Information to reconstruct the async stack as best as we can
    TaskInfo *waiter = NULL;

    TaskInfo(TaskObj *);
    ~TaskInfo()
    {
        delete waiter;
    }

    static TaskInfo current(PyObject *);
};

static std::unordered_map<PyObject *, PyObject *> task_link_map;
static std::mutex task_link_map_lock;

// ----------------------------------------------------------------------------
TaskInfo::TaskInfo(TaskObj *task_addr)
{
    TaskObj task;
    if (copy_type(task_addr, task))
        throw Error();

    try
    {
        coro = std::make_unique<GenInfo>(task.task_coro);
    }
    catch (GenInfo::Error &)
    {
        throw Error();
    }

    origin = (PyObject *)task_addr;

    try
    {
#if PY_VERSION_HEX >= 0x030c0000
        // The task name might hold a PyLong for deferred task name formatting.
        PyLongObject name_obj;
        name = (!copy_type(task.task_name, name_obj) && PyLong_CheckExact(&name_obj))
                   ? "Task-" + std::to_string(PyLong_AsLong((PyObject *)&name_obj))
                   : pyunicode_to_utf8(task.task_name);
#else
        name = pyunicode_to_utf8(task.task_name);
#endif
    }
    catch (StringError &)
    {
        throw Error();
    }

    root_frame = std::make_unique<Frame>(name);
    loop = task.task_loop;

    try
    {
        if (task.task_fut_waiter)
            waiter = new TaskInfo((TaskObj *)task.task_fut_waiter); // TODO: Make lazy?
    }
    catch (TaskInfo::Error &)
    {
        waiter = NULL;
    }
}

// ----------------------------------------------------------------------------
TaskInfo TaskInfo::current(PyObject *loop)
{
    if (loop == NULL)
        throw Error();

    try
    {
        MirrorDict current_tasks_dict(asyncio_current_tasks);
        PyObject *task = current_tasks_dict.get_item(loop);
        if (task == NULL)
            throw Error();

        return TaskInfo((TaskObj *)task);
    }
    catch (MirrorError &e)
    {
        throw Error();
    }
}

// ----------------------------------------------------------------------------
// TODO: Make this a "for_each_task" function?
static std::unique_ptr<std::vector<TaskInfo *>>
get_all_tasks(PyObject *loop)
{
    try
    {
        MirrorSet scheduled_tasks_set(asyncio_scheduled_tasks);
        auto scheduled_tasks = scheduled_tasks_set.as_unordered_set();

        auto tasks = std::make_unique<std::vector<TaskInfo *>>();

        for (auto task_wr_addr : *scheduled_tasks)
        {
            PyWeakReference task_wr;
            if (copy_type(task_wr_addr, task_wr))
                continue;

            try
            {
                TaskInfo *task_info = new TaskInfo((TaskObj *)task_wr.wr_object);
                if (loop != NULL && task_info->loop != loop)
                {
                    delete task_info;
                    continue;
                }

                tasks->push_back(task_info);
            }
            catch (TaskInfo::Error &e)
            {
                // We failed to get this task but we keep going
            }
        }

        if (asyncio_eager_tasks != NULL)
        {
            MirrorSet mirror(asyncio_eager_tasks);
            auto eager_tasks = mirror.as_unordered_set();

            if (eager_tasks == nullptr)
                return NULL;

            for (auto task_addr : *eager_tasks)
            {
                try
                {
                    TaskInfo *task_info = new TaskInfo((TaskObj *)task_addr);
                    if (loop != NULL && task_info->loop != loop)
                    {
                        delete task_info;
                        continue;
                    }

                    tasks->push_back(task_info);
                }
                catch (TaskInfo::Error &e)
                {
                    // We failed to get this task but we keep going
                }
            }
        }

        return tasks;
    }
    catch (MirrorError &e)
    {
        throw TaskInfo::Error();
    }
}

// ----------------------------------------------------------------------------

typedef std::pair<TaskInfo *, FrameStack *> TaskStack;

static std::vector<TaskStack *> current_tasks;

// ----------------------------------------------------------------------------

static size_t unwind_task(TaskInfo *info, FrameStack &stack)
{
    // TODO: Check for running task.
    std::stack<PyObject *> coro_frames;

    // Unwind the coro chain
    for (auto coro = info->coro.get(); coro != NULL; coro = coro->await.get())
    {
        if (coro->frame != NULL)
            coro_frames.push(coro->frame);
    }

    int count = 0;

    // Unwind the coro frames
    while (!coro_frames.empty())
    {
        PyObject *frame = coro_frames.top();
        coro_frames.pop();

        count += unwind_frame(frame, stack);
    }

    return count;
}

// ----------------------------------------------------------------------------
static void unwind_tasks(ThreadInfo *info)
{
    std::unordered_set<TaskInfo *> leaf_tasks;
    std::unordered_set<PyObject *> parent_tasks;
    std::unordered_map<PyObject *, TaskInfo *> waitee_map; // Indexed by task origin
    std::unordered_map<PyObject *, TaskInfo *> origin_map; // Indexed by task origin

    auto all_tasks = get_all_tasks((PyObject *)info->asyncio_loop);

    {
        std::lock_guard<std::mutex> lock(task_link_map_lock);

        // Clean up the task_link_map. Remove entries associated to tasks that
        // no longer exist.
        std::unordered_set<PyObject *> all_task_origins;
        std::transform(all_tasks->cbegin(), all_tasks->cend(),
                       std::inserter(all_task_origins, all_task_origins.begin()),
                       [](const TaskInfo *task)
                       { return task->origin; });

        std::vector<PyObject *> to_remove;
        for (auto kv : task_link_map)
        {
            if (all_task_origins.find(kv.first) == all_task_origins.end())
                to_remove.push_back(kv.first);
        }
        for (auto key : to_remove)
            task_link_map.erase(key);

        // Determine the parent tasks from the gather links.
        std::transform(task_link_map.cbegin(), task_link_map.cend(),
                       std::inserter(parent_tasks, parent_tasks.begin()),
                       [](const std::pair<PyObject *, PyObject *> &kv)
                       { return kv.second; });
    }

    for (TaskInfo *task : *all_tasks)
    {
        origin_map[task->origin] = task;

        if (task->waiter != NULL)
            waitee_map[task->waiter->origin] = task;
        else if (parent_tasks.find(task->origin) == parent_tasks.end())
            leaf_tasks.insert(task);
    }

    for (TaskInfo *task : leaf_tasks)
    {
        FrameStack *stack = new FrameStack();
        TaskInfo *current_task = task;
        while (current_task)
        {
            int stack_size = unwind_task(current_task, *stack);

            if (current_task->coro->is_running)
            {
                // Undo the stack unwinding
                // TODO[perf]: not super-efficient :(
                for (int i = 0; i < stack_size; i++)
                    stack->pop_back();

                // Instead we get part of the thread stack
                FrameStack temp_stack;
                size_t nframes = python_stack.size() - stack_size + 1;
                // TODO: assert nframe >= 0
                for (size_t i = 0; i < nframes; i++)
                {
                    auto python_frame = python_stack.front();
                    temp_stack.push_front(python_frame);
                    python_stack.pop_front();
                }
                while (!temp_stack.empty())
                {
                    stack->push_front(temp_stack.front());
                    temp_stack.pop_front();
                }
            }

            // Add the task name frame
            stack->push_back(*current_task->root_frame);

            // Get the next task in the chain
            PyObject *task_origin = current_task->origin;
            current_task = waitee_map[task_origin];
            if (!current_task)
            {
                // Check for, e.g., gather links
                std::lock_guard<std::mutex> lock(task_link_map_lock);

                current_task = origin_map[task_link_map[task_origin]];
            }
        }

        // Finish off with the remaining thread stack
        for (auto p = python_stack.begin(); p != python_stack.end(); p++)
            stack->push_back(*p);

        current_tasks.push_back(new TaskStack(task, stack));
    }
}
