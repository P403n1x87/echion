// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#if PY_VERSION_HEX >= 0x030c0000
// https://github.com/python/cpython/issues/108216#issuecomment-1696565797
#undef _PyGC_FINALIZED
#endif

#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>

#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined PL_DARWIN
#include <pthread.h>
#endif

#include <echion/config.h>
#include <echion/signals.h>
#include <echion/stacks.h>
#include <echion/state.h>
#include <echion/threads.h>
#include <echion/timing.h>

// ----------------------------------------------------------------------------
static void do_where(std::ostream &stream)
{
    stream << "\r"
           << "🐴 Echion reporting for duty" << std::endl
           << std::endl;

    // TODO: Add support for tasks
    for_each_thread(
        [&stream](PyThreadState *tstate, ThreadInfo &thread) -> void
        {
            thread.unwind(tstate);
            if (native)
            {
                interleave_stacks();
                thread.render_where(interleaved_stack, stream);
            }
            else
                thread.render_where(python_stack, stream);
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
static inline void
_start()
{
    init_frame_cache(MAX_FRAMES * (1 + native));

    install_signals();

#if defined PL_DARWIN
    // Get the wall time clock resource.
    host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
#endif
}

// ----------------------------------------------------------------------------
static inline void
_stop()
{
    // Clean up the thread info map. When not running async, we need to guard
    // the map lock because we are not in control of the sampling thread.
    {
        const std::lock_guard<std::mutex> guard(thread_info_map_lock);

        thread_info_map.clear();
    }

#if defined PL_DARWIN
    mach_port_deallocate(mach_task_self(), cclock);
#endif

    restore_signals();

    reset_frame_cache();
}

// ----------------------------------------------------------------------------
static inline void
_sampler()
{
    // This function can run without the GIL on the basis that these assumptions
    // hold:
    // 1. The interpreter state object lives as long as the process itself.

    if (where)
    {
        auto pipe_name = std::filesystem::temp_directory_path() / ("echion-" + std::to_string(getpid()));
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
            [=](PyThreadState *tstate, ThreadInfo &thread)
            { thread.sample(tstate, wall_time); });

        while (gettime() < end_time && running)
            sched_yield();

        last_time = now;
    }

    output.close();

    teardown_where();
}

static void
sampler()
{
    _start();
    _sampler();
    _stop();
}

// ----------------------------------------------------------------------------
static void _init()
{
#if PY_VERSION_HEX >= 0x03090000
    interp = PyInterpreterState_Get();
#else
    interp = _PyInterpreterState_Get();
#endif
    pid = getpid();
}

// ----------------------------------------------------------------------------
static PyObject *
start_async(PyObject *Py_UNUSED(m), PyObject *Py_UNUSED(args))
{
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

    {
        const std::lock_guard<std::mutex> guard(thread_info_map_lock);

        if (thread_info_map.find(thread_id) != thread_info_map.end())
        {
            // Thread is already tracked so we update its info
            auto &thread = *thread_info_map.find(thread_id)->second;

            thread.name = thread_name;
            thread.native_id = native_id;
            thread.update_cpu_time();
        }
        else
        {
            // Untracked thread. Create a new info entry.
            thread_info_map.emplace(
                thread_id,
                std::make_unique<ThreadInfo>(thread_id, native_id, thread_name));
        }
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

        thread_info_map.erase(thread_id);
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
            thread_info_map.find(thread_id)->second->asyncio_loop = (loop != Py_None)
                                                                        ? (uintptr_t)loop
                                                                        : 0;
        }
    }

    Py_RETURN_NONE;
}

// ----------------------------------------------------------------------------
static PyObject *
init_asyncio(PyObject *Py_UNUSED(m), PyObject *args)
{
    if (!PyArg_ParseTuple(args, "OOO", &asyncio_current_tasks, &asyncio_scheduled_tasks, &asyncio_eager_tasks))
        return NULL;

    if (asyncio_eager_tasks == Py_None)
        asyncio_eager_tasks = NULL;

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
    {"init_asyncio", init_asyncio, METH_VARARGS, "Initialise asyncio tracking"},
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
