// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <condition_variable>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_set>

#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <echion/config.h>
#include <echion/frame.h>
#include <echion/mirrors.h>
#include <echion/render.h>
#include <echion/signals.h>
#include <echion/stacks.h>
#include <echion/state.h>
#include <echion/tasks.h>
#include <echion/threads.h>
#include <echion/timing.h>
#include <echion/vm.h>

// ----------------------------------------------------------------------------
static void unwind_thread(PyThreadState *tstate, ThreadInfo *info)
{
    if (native)
    {
        // Lock on the signal handler. Will get unlocked once the handler is
        // done unwinding the native stack.
        const std::lock_guard<std::mutex> guard(sigprof_handler_lock);

        // Pass the current thread state to the signal handler. This is needed
        // to unwind the Python stack from within it.
        current_tstate = tstate;
        current_thread_info = info;

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
        if (info->asyncio_loop != 0)
            unwind_tasks(tstate, info);
    }
}

// ----------------------------------------------------------------------------
static void for_each_thread(std::function<void(PyThreadState *, ThreadInfo *)> callback)
{
    std::unordered_set<PyThreadState *> threads;
    std::unordered_set<PyThreadState *> seen_threads;

    threads.clear();
    seen_threads.clear();

    // Start from the main thread
    threads.insert(main_thread);

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
                return;

            ThreadInfo *info = thread_info_map[tstate.thread_id];

            // Call back with the thread state and thread info.
            callback(&tstate, info);
        }

        // Enqueue the unseen threads that we can reach from this thread.
        if (tstate.next != NULL && seen_threads.find(tstate.next) == seen_threads.end())
            threads.insert(tstate.next);
        if (tstate.prev != NULL && seen_threads.find(tstate.prev) == seen_threads.end())
            threads.insert(tstate.prev);
    }
}

// ----------------------------------------------------------------------------
static void sample_thread(PyThreadState *tstate, ThreadInfo *info, microsecond_t delta)
{
    unsigned long native_id = 0;
    const char *thread_name = NULL;

    native_id = info->native_id;
    if (native_id == 0)
        // If we fail to retrieve the native thread ID, then quite likely the
        // pthread structure has been destroyed and we should stop trying to
        // resolve any more memory addresses.
        return;

    if (cpu)
    {
        microsecond_t previous_cpu_time = info->cpu_time;
        info->update_cpu_time();

        if (!info->is_running())
            // If the thread is not running, then we skip it.
            return;

        delta = info->cpu_time - previous_cpu_time;
    }

    thread_name = info->name;
    if (thread_name == NULL)
        return;

    unwind_thread(tstate, info);

    // Asyncio tasks
    if (current_tasks.empty())
    {
        // Print the PID and thread name
        output << "P" << pid << ";T" << thread_name << " (" << native_id << ")";

        // Print the stack
        if (native)
        {
            interleave_stacks();
            render(interleaved_stack, output);
        }
        else
            render(python_stack, output);

        // Print the metric
        output << " " << delta << std::endl;
    }
    else
    {
        for (auto task_pair : current_tasks)
        {
            output << "P" << pid << ";T" << thread_name << " (" << native_id << ")";

            FrameStack *task_stack = task_pair->second;
            if (native)
            {
                // NOTE: These stacks might be non-sensical, especially with
                // Python < 3.11.
                interleave_stacks(task_stack);
                render(interleaved_stack, output);
            }
            else
                render(*task_stack, output);

            output << " " << delta << std::endl;
        }
        // TODO: Memory leak?
        current_tasks.clear();
    }
}

// ----------------------------------------------------------------------------
static void do_where(std::ostream &stream)
{
    stream << "\r"
           << "🐴 Echion reporting for duty" << std::endl
           << std::endl;

    // TODO: Add support for tasks
    for_each_thread(
        [&stream](PyThreadState *tstate, ThreadInfo *info) -> void
        {
            unwind_thread(tstate, info);
            if (native)
            {
                interleave_stacks();
                render_where(info, interleaved_stack, stream);
            }
            else
                render_where(info, python_stack, stream);
            stream << std::endl;
        });
}

// ----------------------------------------------------------------------------
static void where_listener()
{
    for (;;)
    {
        std::unique_lock<std::mutex> lock(where_lock);
        where_cv.wait(lock);

        if (!running)
            break;

        do_where(std::cerr);
    }
}

// ----------------------------------------------------------------------------
static void setup_where()
{
    where_thread = new std::thread(where_listener);
}

static void teardown_where()
{
    if (where_thread != nullptr)
    {
        {
            std::lock_guard<std::mutex> lock(where_lock);

            where_cv.notify_one();
        }

        where_thread->join();

        where_thread = nullptr;
    }
}

// ----------------------------------------------------------------------------
static void sampler()
{
    // This thread can run without the GIL on the basis that these assumptions
    // hold:
    // 1. The main thread lives as long as the process itself.

    if (where)
    {
        auto pipe_name = "/tmp/echion-" + std::to_string(getpid());
        std::ofstream pipe(pipe_name, std::ios::out);
        if (!pipe)
        {
            std::cerr << "Failed to open pipe " << pipe_name << std::endl;
            return;
        }

        do_where(pipe);

        running = 0;

        return;
    }

    setup_where();

    last_time = gettime();

    // The main thread has likely done some work already, so we prime the per-
    // thread CPU time mapping with the current CPU time.
    if (cpu)
    {
        const std::lock_guard<std::mutex> guard(thread_info_map_lock);

        // TODO: Check that the main thread has been tracked!
        ThreadInfo *info = thread_info_map[main_thread->thread_id];
        info->update_cpu_time();
    }

    output.open(std::getenv("ECHION_OUTPUT"));
    if (!output.is_open())
    {
        std::cerr << "Failed to open output file " << std::getenv("ECHION_OUTPUT") << std::endl;
        return;
    }

    output << "# mode: " << (cpu ? "cpu" : "wall") << std::endl;
    output << "# interval: " << interval << std::endl;

    // Install the signal handler if we are sampling the native stacks.
    if (native)
        // We use SIGPROF to sample the stacks within each thread.
        signal(SIGPROF, sigprof_handler);

    while (running)
    {
        microsecond_t now = gettime();
        microsecond_t end_time = now + interval;
        microsecond_t wall_time = now - last_time;

        for_each_thread(
            [=](PyThreadState *tstate, ThreadInfo *info)
            { sample_thread(tstate, info, wall_time); });

        while (gettime() < end_time)
            sched_yield();

        last_time = now;
    }

    output.close();

    teardown_where();
}

// ----------------------------------------------------------------------------
static void _init()
{
    main_thread = PyThreadState_Get();
    pid = getpid();
#if defined PL_DARWIN
    host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
#endif
}

// ----------------------------------------------------------------------------
static void _start()
{
    init_frame_cache(MAX_FRAMES * (1 + native));

    install_signals();
}

// ----------------------------------------------------------------------------
static PyObject *
start_async(PyObject *Py_UNUSED(m), PyObject *Py_UNUSED(args))
{
    _start();

    if (!running)
    {
        // TODO: Since we have a global state, we should not allow multiple ways
        // of starting the sampler.
        if (sampler_thread == nullptr)
        {
            running = 1;
            sampler_thread = new std::thread(sampler);
        }
    }

    Py_RETURN_NONE;
}

// ----------------------------------------------------------------------------
static PyObject *
start(PyObject *Py_UNUSED(m), PyObject *Py_UNUSED(args))
{
    _start();

    if (!running)
    {
        // TODO: Since we have a global state, we should not allow multiple ways
        // of starting the sampler.
        running = 1;

        // Run the sampler without the GIL
        Py_BEGIN_ALLOW_THREADS;
        sampler();
        Py_END_ALLOW_THREADS;
    }

    Py_RETURN_NONE;
}

// ----------------------------------------------------------------------------
static PyObject *
stop(PyObject *Py_UNUSED(m), PyObject *Py_UNUSED(args))
{
    running = 0;

    // Stop the sampling thread
    if (sampler_thread != nullptr)
    {
        sampler_thread->join();
        sampler_thread = nullptr;
    }

    // Clean up the thread info map. When not running async, we need to guard
    // the map lock because we are not in control of the sampling thread.
    {
        const std::lock_guard<std::mutex> guard(thread_info_map_lock);

        while (!thread_info_map.empty())
        {
            ThreadInfo *info = thread_info_map.begin()->second;
            thread_info_map.erase(thread_info_map.begin());
            delete info;
        }
    }

#if defined PL_DARWIN
    mach_port_deallocate(mach_task_self(), cclock);
#endif

    restore_signals();

    destroy_frame_cache();

    Py_RETURN_NONE;
}

// ----------------------------------------------------------------------------
static PyObject *
track_thread(PyObject *Py_UNUSED(m), PyObject *args)
{
    uintptr_t thread_id; // map key
    const char *thread_name;
    pid_t native_id;

    if (!PyArg_ParseTuple(args, "lsi", &thread_id, &thread_name, &native_id))
        return NULL;

    const char *name = strdup(thread_name);
    if (name == NULL)
        Py_RETURN_NONE;

    {
        const std::lock_guard<std::mutex> guard(thread_info_map_lock);

        ThreadInfo *info = NULL;

        if (thread_info_map.find(thread_id) != thread_info_map.end())
        {
            // Thread is already tracked so we update its info
            info = thread_info_map[thread_id];
            if (info->name != NULL)
                std::free((void *)info->name);
        }
        else
        {
            // Untracked thread. Create a new info entry.
            info = new ThreadInfo();
        }

        info->thread_id = thread_id;
        info->name = name;
        info->native_id = native_id;
#if defined PL_DARWIN
        info->mach_port = mach_thread_self();
#endif
        info->update_cpu_time();

        thread_info_map[thread_id] = info;
    }

    Py_RETURN_NONE;
}

// ----------------------------------------------------------------------------
static PyObject *
untrack_thread(PyObject *Py_UNUSED(m), PyObject *args)
{
    unsigned long thread_id;
    if (!PyArg_ParseTuple(args, "l", &thread_id))
        return NULL;

    {
        const std::lock_guard<std::mutex> guard(thread_info_map_lock);

        if (thread_info_map.find(thread_id) != thread_info_map.end())
        {
            ThreadInfo *info = thread_info_map[thread_id];
            thread_info_map.erase(thread_id);
            delete info;
        }
    }

    Py_RETURN_NONE;
}

// ----------------------------------------------------------------------------
static PyObject *
init(PyObject *Py_UNUSED(m), PyObject *Py_UNUSED(args))
{
    _init();

    Py_RETURN_NONE;
}

// ----------------------------------------------------------------------------
static PyObject *
track_asyncio_loop(PyObject *Py_UNUSED(m), PyObject *args)
{
    uintptr_t thread_id; // map key
    PyObject *loop;

    if (!PyArg_ParseTuple(args, "lO", &thread_id, &loop))
        return NULL;

    {
        std::lock_guard<std::mutex> guard(thread_info_map_lock);

        if (thread_info_map.find(thread_id) != thread_info_map.end())
        {
            ThreadInfo *info = thread_info_map[thread_id];
            info->asyncio_loop = loop != Py_None ? (uintptr_t)loop : 0;
        }
    }

    Py_RETURN_NONE;
}

// ----------------------------------------------------------------------------
static PyObject *
init_asyncio(PyObject *Py_UNUSED(m), PyObject *args)
{
    if (!PyArg_ParseTuple(args, "OO", &asyncio_current_tasks, &asyncio_all_tasks))
        return NULL;

    Py_RETURN_NONE;
}

// ----------------------------------------------------------------------------
static PyObject *
link_tasks(PyObject *Py_UNUSED(m), PyObject *args)
{
    PyObject *parent, *child;

    if (!PyArg_ParseTuple(args, "OO", &parent, &child))
        return NULL;

    {
        std::lock_guard<std::mutex> guard(task_link_map_lock);

        task_link_map[child] = parent;
    }

    Py_RETURN_NONE;
}

// ----------------------------------------------------------------------------
static PyMethodDef echion_core_methods[] = {
    {"start", start, METH_NOARGS, "Start the stack sampler"},
    {"start_async", start_async, METH_NOARGS, "Start the stack sampler asynchronously"},
    {"stop", stop, METH_NOARGS, "Stop the stack sampler"},
    {"track_thread", track_thread, METH_VARARGS, "Map the name of a thread with its identifier"},
    {"untrack_thread", untrack_thread, METH_VARARGS, "Untrack a terminated thread"},
    {"init", init, METH_NOARGS, "Initialize the stack sampler (usually after a fork)"},
    // Task support
    {"track_asyncio_loop", track_asyncio_loop, METH_VARARGS, "Map the name of a task with its identifier"},
    {"init_asyncio", init_asyncio, METH_VARARGS, "Enter a task"},
    {"link_tasks", link_tasks, METH_VARARGS, "Link two tasks"},
    // Configuration interface
    {"set_interval", set_interval, METH_VARARGS, "Set the sampling interval"},
    {"set_cpu", set_cpu, METH_VARARGS, "Set whether to use CPU time instead of wall time"},
    {"set_native", set_native, METH_VARARGS, "Set whether to sample the native stacks"},
    {"set_where", set_where, METH_VARARGS, "Set whether to use where mode"},
    {NULL, NULL, 0, NULL} /* Sentinel */
};

// ----------------------------------------------------------------------------
static struct PyModuleDef coremodule = {
    PyModuleDef_HEAD_INIT,
    "core", /* name of module */
    NULL,   /* module documentation, may be NULL */
    -1,     /* size of per-interpreter state of the module,
               or -1 if the module keeps state in global variables. */
    echion_core_methods,
};

// ----------------------------------------------------------------------------
PyMODINIT_FUNC
PyInit_core(void)
{
    PyObject *m;

    m = PyModule_Create(&coremodule);
    if (m == NULL)
        return NULL;

    // We make the assumption that this module is loaded by the main thread.
    // TODO: These need to be reset after a fork.
    _init();

    return m;
}
