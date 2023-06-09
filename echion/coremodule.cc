// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <fstream>
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
#include <echion/render.h>
#include <echion/stacks.h>
#include <echion/threadinfo.h>
#include <echion/timing.h>
#include <echion/vm.h>

// ----------------------------------------------------------------------------

static PyThreadState *main_thread = NULL;

static std::thread *sampler_thread = nullptr;

static int running = 0;

// ----------------------------------------------------------------------------
static inline unsigned long
get_native_id()
{
#if defined PL_LINUX
    return gettid();
#elif defined PL_DARWIN
    uint64_t tid;
    pthread_threadid_np(NULL, &tid);
    return tid;
#endif
}

// ----------------------------------------------------------------------------

static std::mutex sigprof_handler_lock;

void sigprof_handler(int signum)
{
    unwind_native_stack();
    unwind_python_stack(current_tstate);
    // std::cout << "Thread " << get_native_id() << " has " << native_stack.size() << " frames" << std::endl;

    sigprof_handler_lock.unlock();
}

// ----------------------------------------------------------------------------
static void thread__unwind(PyThreadState *tstate, microsecond_t delta)
{
    unsigned long native_id = 0;
    const char *thread_name = NULL;

    {
        const std::lock_guard<std::mutex> guard(thread_info_map_lock);

        if (thread_info_map.find(tstate->thread_id) == thread_info_map.end())
            return;

        ThreadInfo *info = thread_info_map[tstate->thread_id];

        if (native)
        {
            // Lock on the signal handler. Will get unlocked once the handler is
            // done unwinding the native stack.
            const std::lock_guard<std::mutex> guard(sigprof_handler_lock);

            // Pass the current thread state to the signal handler. This is needed
            // to unwind the Python stack from within it.
            current_tstate = tstate;

            // Send a signal to the thread to unwind its native stack.
            pthread_kill((pthread_t)info->thread_id, SIGPROF);

            // Lock to wait for the signal handler to finish unwinding the native
            // stack. Release the lock immediately after so that it is available
            // for the next thread.
            sigprof_handler_lock.lock();
        }
        else
        {
            // Unwind just the Python stack
            unwind_python_stack(tstate);
        }

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
    }

    // Print the PID and thread name
    output << "P" << pid << ";T" << thread_name << " (" << native_id << ")";

    // TODO: Make this conditional on the configuration
    // unwind_python_stack(tstate);

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

// ----------------------------------------------------------------------------
static void sampler()
{
    // This thread can run without the GIL on the basis that these assumptions
    // hold:
    // 1. The main thread lives as long as the process itself.

    std::unordered_set<PyThreadState *> threads;
    std::unordered_set<PyThreadState *> seen_threads;

    last_time = gettime();

    // The main thread has likely done some work already, so we prime the per-
    // thread CPU time mapping with the current CPU time.

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

            // Unwind the thread stack.
            thread__unwind(&tstate, wall_time);

            // Enqueue the unseen threads that we can reach from this thread.
            if (tstate.next != NULL && seen_threads.find(tstate.next) == seen_threads.end())
                threads.insert(tstate.next);
            if (tstate.prev != NULL && seen_threads.find(tstate.prev) == seen_threads.end())
                threads.insert(tstate.prev);
        }

        while (gettime() < end_time)
            sched_yield();

        last_time = now;
    }
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
static PyObject *
start_async(PyObject *m, PyObject *args)
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
start(PyObject *m, PyObject *args)
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
stop(PyObject *m, PyObject *args)
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

    Py_RETURN_NONE;
}

// ----------------------------------------------------------------------------
static PyObject *
track_thread(PyObject *m, PyObject *args)
{
    uintptr_t thread_id; // map key
    const char *thread_name;

    if (!PyArg_ParseTuple(args, "ls", &thread_id, &thread_name))
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
        info->native_id = get_native_id();
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
untrack_thread(PyObject *m, PyObject *args)
{
    unsigned long thread_id;
    if (!PyArg_ParseTuple(args, "l", &thread_id))
        return NULL;

    {
        const std::lock_guard<std::mutex> guard(thread_info_map_lock);

        if (thread_info_map.find(thread_id) != thread_info_map.end())
        {
            ThreadInfo *info = thread_info_map[thread_id];
#if defined PL_DARWIN
            mach_port_deallocate(mach_task_self(), info->mach_port);
#endif
            thread_info_map.erase(thread_id);
            delete info;
        }
    }

    Py_RETURN_NONE;
}

// ----------------------------------------------------------------------------
static PyObject *
init(PyObject *m, PyObject *args)
{
    _init();

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
    // Configuration interface
    {"set_interval", set_interval, METH_VARARGS, "Set the sampling interval"},
    {"set_cpu", set_cpu, METH_VARARGS, "Set whether to use CPU time instead of wall time"},
    {"set_native", set_native, METH_VARARGS, "Set whether to sample the native stacks"},
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
