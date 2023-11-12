// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#include <Python.h>
#define Py_BUILD_CORE

#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <sstream>
#include <unordered_map>

#if defined PL_LINUX
#include <time.h>
#elif defined PL_DARWIN
#include <mach/clock.h>
#include <mach/mach.h>
#endif

#include <echion/signals.h>
#include <echion/stacks.h>
#include <echion/tasks.h>
#include <echion/timing.h>

class ThreadInfo
{
public:
    using Ptr = std::unique_ptr<ThreadInfo>;

    class Error : public std::exception
    {
    public:
        const char *what() const noexcept override
        {
            return "Cannot create thread info object";
        }
    };

    uintptr_t thread_id;
    unsigned long native_id;

    std::string name;

#if defined PL_LINUX
    clockid_t cpu_clock_id;
#elif defined PL_DARWIN
    mach_port_t mach_port;
#endif
    microsecond_t cpu_time;

    uintptr_t asyncio_loop = 0;

    void update_cpu_time();
    bool is_running();

    void sample(int64_t, PyThreadState *, microsecond_t);
    void unwind(PyThreadState *);

    void render_where(FrameStack &stack, std::ostream &output)
    {
        output << "    🧵 " << name << ":" << std::endl;

        for (auto it = stack.rbegin(); it != stack.rend(); ++it)
            (*it).get().render_where(output);
    }

    // ------------------------------------------------------------------------
    ThreadInfo(uintptr_t thread_id, unsigned long native_id, const char *name)
        : thread_id(thread_id), native_id(native_id), name(name)
    {
#if defined PL_LINUX
        // Try to check that the thread_id is a valid pointer to a pthread
        // structure. Calling pthread_getcpuclockid on an invalid memory address
        // will cause a segmentation fault.
        char buffer[32] = "";
        if (copy_generic((void *)thread_id, buffer, sizeof(buffer)))
            throw Error();
        pthread_getcpuclockid((pthread_t)thread_id, &cpu_clock_id);
#elif defined PL_DARWIN
        mach_port = pthread_mach_thread_np((pthread_t)thread_id);
#endif
        update_cpu_time();
    };

private:
    void unwind_tasks();
};

void ThreadInfo::update_cpu_time()
{
#if defined PL_LINUX
    struct timespec ts;
    if (clock_gettime(cpu_clock_id, &ts))
        return;

    this->cpu_time = TS_TO_MICROSECOND(ts);
#elif defined PL_DARWIN
    thread_basic_info_data_t info;
    mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
    kern_return_t kr = thread_info((thread_act_t)this->mach_port, THREAD_BASIC_INFO, (thread_info_t)&info, &count);

    if (kr != KERN_SUCCESS || (info.flags & TH_FLAGS_IDLE))
        return;

    this->cpu_time = TV_TO_MICROSECOND(info.user_time) + TV_TO_MICROSECOND(info.system_time);
#endif
}

bool ThreadInfo::is_running()
{
#if defined PL_LINUX
    char buffer[2048] = "";

    std::ostringstream file_name_stream;
    file_name_stream << "/proc/self/task/" << this->native_id << "/stat";

    int fd = open(file_name_stream.str().c_str(), O_RDONLY);
    if (fd == -1)
        return -1;

    if (read(fd, buffer, 2047) == 0)
    {
        close(fd);
        return -1;
    }

    close(fd);

    char *p = strchr(buffer, ')');
    if (p == NULL)
        return -1;

    p += 2;
    if (*p == ' ')
        p++;

    return (*p == 'R');

#elif defined PL_DARWIN
    thread_basic_info_data_t info;
    mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
    kern_return_t kr = thread_info(
        (thread_act_t)this->mach_port,
        THREAD_BASIC_INFO,
        (thread_info_t)&info,
        &count);

    if (kr != KERN_SUCCESS)
        return -1;

    return info.run_state == TH_STATE_RUNNING;

#endif
}

// ----------------------------------------------------------------------------

// We make this a reference to a heap-allocated object so that we can avoid
// the destruction on exit. We are in charge of cleaning up the object. Note
// that the object will leak, but this is not a problem.
static std::unordered_map<uintptr_t, ThreadInfo::Ptr> &thread_info_map =
    *(new std::unordered_map<uintptr_t, ThreadInfo::Ptr>()); // indexed by thread_id

static std::mutex thread_info_map_lock;

// ----------------------------------------------------------------------------
void ThreadInfo::unwind(PyThreadState *tstate)
{
    if (native)
    {
        // Lock on the signal handler. Will get unlocked once the handler is
        // done unwinding the native stack.
        const std::lock_guard<std::mutex> guard(sigprof_handler_lock);

        // Pass the current thread state to the signal handler. This is needed
        // to unwind the Python stack from within it.
        current_tstate = tstate;

        // Send a signal to the thread to unwind its native stack.
        pthread_kill((pthread_t)tstate->thread_id, SIGPROF);

        // Lock to wait for the signal handler to finish unwinding the native
        // stack. Release the lock immediately after so that it is available
        // for the next thread.
        sigprof_handler_lock.lock();
    }
    else
    {
        unwind_python_stack(tstate);
        if (asyncio_loop)
        {
            try
            {
                unwind_tasks();
            }
            catch (TaskInfo::Error &)
            {
                // We failed to unwind tasks
            }
        }
    }
}

// ----------------------------------------------------------------------------
void ThreadInfo::unwind_tasks()
{
    std::vector<TaskInfo::Ref> leaf_tasks;
    std::unordered_set<PyObject *> parent_tasks;
    std::unordered_map<PyObject *, TaskInfo::Ref> waitee_map; // Indexed by task origin
    std::unordered_map<PyObject *, TaskInfo::Ref> origin_map; // Indexed by task origin

    auto all_tasks = get_all_tasks((PyObject *)asyncio_loop);

    {
        std::lock_guard<std::mutex> lock(task_link_map_lock);

        // Clean up the task_link_map. Remove entries associated to tasks that
        // no longer exist.
        std::unordered_set<PyObject *> all_task_origins;
        std::transform(all_tasks.cbegin(), all_tasks.cend(),
                       std::inserter(all_task_origins, all_task_origins.begin()),
                       [](const TaskInfo::Ptr &task)
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

    for (auto &task : all_tasks)
    {
        origin_map.emplace(task->origin, std::ref(*task));

        if (task->waiter != NULL)
            waitee_map.emplace(task->waiter->origin, std::ref(*task));
        else if (parent_tasks.find(task->origin) == parent_tasks.end())
            leaf_tasks.push_back(std::ref(*task));
    }

    for (auto &task : leaf_tasks)
    {
        auto stack = std::make_unique<FrameStack>();
        for (auto current_task = task;;)
        {
            auto &task = current_task.get();

            int stack_size = task.unwind(*stack);

            if (task.coro->is_running)
            {
                // Undo the stack unwinding
                // TODO[perf]: not super-efficient :(
                for (int i = 0; i < stack_size; i++)
                    stack->pop_back();

                // Instead we get part of the thread stack
                FrameStack temp_stack;
                ssize_t nframes = python_stack.size() - stack_size + 1;
                for (ssize_t i = 0; i < nframes; i++)
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
            stack->push_back(Frame::get(task.name));

            // Get the next task in the chain
            PyObject *task_origin = task.origin;
            if (waitee_map.find(task_origin) != waitee_map.end())
            {
                current_task = waitee_map.find(task_origin)->second;
                continue;
            }

            {
                // Check for, e.g., gather links
                std::lock_guard<std::mutex> lock(task_link_map_lock);

                if (
                    task_link_map.find(task_origin) != task_link_map.end() &&
                    origin_map.find(task_link_map[task_origin]) != origin_map.end())
                {
                    current_task = origin_map.find(task_link_map[task_origin])->second;
                    continue;
                }
            }

            break;
        }

        // Finish off with the remaining thread stack
        for (auto p = python_stack.begin(); p != python_stack.end(); p++)
            stack->push_back(*p);

        current_tasks.push_back(std::move(stack));
    }
}

// ----------------------------------------------------------------------------
void ThreadInfo::sample(int64_t iid, PyThreadState *tstate, microsecond_t delta)
{
    if (cpu)
    {
        microsecond_t previous_cpu_time = cpu_time;
        update_cpu_time();

        if (!is_running())
            // If the thread is not running, then we skip it.
            return;

        delta = cpu_time - previous_cpu_time;
    }

    unwind(tstate);

    // Asyncio tasks
    if (current_tasks.empty())
    {
        // Print the PID and thread name
        mojo.stack(pid, iid, name);

        // Print the stack
        if (native)
        {
            interleave_stacks();
            interleaved_stack.render();
        }
        else
            python_stack.render();

        // Print the metric
        mojo.metric_time(delta);
    }
    else
    {
        for (auto &task_stack : current_tasks)
        {
            mojo.stack(pid, iid, name);

            if (native)
            {
                // NOTE: These stacks might be non-sensical, especially with
                // Python < 3.11.
                interleave_stacks(*task_stack);
                interleaved_stack.render();
            }
            else
                task_stack->render();

            mojo.metric_time(delta);
        }

        current_tasks.clear();
    }
}

// ----------------------------------------------------------------------------
static void for_each_thread(PyInterpreterState *interp, std::function<void(PyThreadState *, ThreadInfo &)> callback)
{
    std::unordered_set<PyThreadState *> threads;
    std::unordered_set<PyThreadState *> seen_threads;

    threads.clear();
    seen_threads.clear();

    // Start from the thread list head
    threads.insert(PyInterpreterState_ThreadHead(interp));

    while (!threads.empty())
    {
        // Pop the next thread
        PyThreadState *tstate_addr = *threads.begin();
        threads.erase(threads.begin());

        // Mark the thread as seen
        seen_threads.insert(tstate_addr);

        // Since threads can be created and destroyed at any time, we make
        // a copy of the structure before trying to read its fields.
        PyThreadState tstate;
        if (copy_type(tstate_addr, tstate))
            // We failed to copy the thread so we skip it.
            continue;

        {
            const std::lock_guard<std::mutex> guard(thread_info_map_lock);

            if (thread_info_map.find(tstate.thread_id) == thread_info_map.end())
            {
                // If the threading module was not imported in the target then
                // we mistakenly take the hypno thread as the main thread. We
                // assume that any missing thread is the actual main thread.
#if PY_VERSION_HEX >= 0x030b0000
                auto native_id = tstate.native_thread_id;
#else
                auto native_id = getpid();
#endif
                try
                {
                    thread_info_map.emplace(
                        tstate.thread_id,
                        std::make_unique<ThreadInfo>(tstate.thread_id, native_id, "MainThread"));
                }
                catch (ThreadInfo::Error &)
                {
                    // We failed to create the thread info object so we skip it.
                    // We'll likely try again later with the valid thread
                    // information.
                    continue;
                }
            }

            // Call back with the thread state and thread info.
            callback(&tstate, *thread_info_map.find(tstate.thread_id)->second);
        }

        // Enqueue the unseen threads that we can reach from this thread.
        if (tstate.next != NULL && seen_threads.find(tstate.next) == seen_threads.end())
            threads.insert(tstate.next);
        if (tstate.prev != NULL && seen_threads.find(tstate.prev) == seen_threads.end())
            threads.insert(tstate.prev);
    }
}
