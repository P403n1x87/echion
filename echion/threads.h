// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#include <Python.h>
#define Py_BUILD_CORE

#include <algorithm>
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
#elif defined PL_WIN32
#include <windows.h>
#include <realtimeapiset.h>
#include <winternl.h>
#include <Ntstatus.h>

typedef enum _THREAD_STATE
{
    StateInitialized,
    StateReady,
    StateRunning,
    StateStandby,
    StateTerminated,
    StateWait,
    StateTransition,
    StateUnknown
} THREAD_STATE;

typedef enum _KWAIT_REASON
{
    Executive = 0,
    FreePage = 1,
    PageIn = 2,
    PoolAllocation = 3,
    DelayExecution = 4,
    Suspended = 5,
    UserRequest = 6,
    WrExecutive = 7,
    WrFreePage = 8,
    WrPageIn = 9,
    WrPoolAllocation = 10,
    WrDelayExecution = 11,
    WrSuspended = 12,
    WrUserRequest = 13,
    WrEventPair = 14,
    WrQueue = 15,
    WrLpcReceive = 16,
    WrLpcReply = 17,
    WrVirtualMemory = 18,
    WrPageOut = 19,
    WrRendezvous = 20,
    Spare2 = 21,
    Spare3 = 22,
    Spare4 = 23,
    Spare5 = 24,
    WrCalloutStack = 25,
    WrKernel = 26,
    WrResource = 27,
    WrPushLock = 28,
    WrMutex = 29,
    WrQuantumEnd = 30,
    WrDispatchInt = 31,
    WrPreempted = 32,
    WrYieldExecution = 33,
    WrFastMutex = 34,
    WrGuardedMutex = 35,
    WrRundown = 36,
    MaximumWaitReason = 37
} KWAIT_REASON;

typedef struct _SYSTEM_THREAD
{
    LARGE_INTEGER KernelTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER CreateTime;
    ULONG WaitTime;
    PVOID StartAddress;
    CLIENT_ID ClientId;
    KPRIORITY Priority;
    LONG BasePriority;
    ULONG ContextSwitchCount;
    THREAD_STATE State;
    KWAIT_REASON WaitReason;
} SYSTEM_THREAD, *PSYSTEM_THREAD;

static PVOID _pi_buffer = NULL;
static ULONG _pi_buffer_size = 0;
#endif

#include <echion/signals.h>
#include <echion/stacks.h>
#include <echion/tasks.h>
#include <echion/timing.h>
#include <echion/vm.h>

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
#elif defined PL_WIN32
    HANDLE thread_handle;
#endif
    microsecond_t cpu_time;

    uintptr_t asyncio_loop = 0;

    void update_cpu_time();
    int is_running();

    void sample(int64_t, PyThreadState *, microsecond_t);
    void unwind(PyThreadState *);

    void render_where(FrameStack &stack, std::ostream &output)
    {
        output << "    🧵 " << name << ":" << std::endl;

        for (auto it = stack.rbegin(); it != stack.rend(); ++it)
            (*it).get().render_where(output);
    }

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
#elif defined PL_WIN32
        thread_handle = OpenThread(THREAD_ALL_ACCESS, FALSE, (DWORD)native_id);
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

#elif defined PL_WIN32
    // Note that this is not a measure in microseconds, but of CPU cycles.
    // The Win32 API doc says that no attempt should be made to convert this
    // metric to elapsed time. However, here we make the assumption that the
    // CPU frequency is of the order of GHz, so we divide by 1000 to get a
    // measure that can be compared to a microsecond.
    QueryThreadCycleTime(thread_handle, (ULONG64 *)&cpu_time);
    cpu_time >>= 10;

#endif
}

int ThreadInfo::is_running()
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

#elif defined PL_WIN32
    ULONG n;
    NTSTATUS status = NtQuerySystemInformation(
        SystemProcessInformation,
        _pi_buffer,
        _pi_buffer_size,
        &n);

    if (status == STATUS_INFO_LENGTH_MISMATCH)
    {
        // Buffer was too small so we reallocate a larger one and try again.
        _pi_buffer_size = n;
        PVOID _new_buffer = realloc(_pi_buffer, n);
        if (_new_buffer == NULL)
            return -1;

        _pi_buffer = _new_buffer;
        return is_running();
    }

    if (status != STATUS_SUCCESS)
        return -1;

    SYSTEM_PROCESS_INFORMATION *pi = (SYSTEM_PROCESS_INFORMATION *)_pi_buffer;
    while (pi->UniqueProcessId != (HANDLE)pid)
    {
        if (pi->NextEntryOffset == 0)
            return -1;

        pi = (SYSTEM_PROCESS_INFORMATION *)(((BYTE *)pi) + pi->NextEntryOffset);
    }

    SYSTEM_THREAD *ti = (SYSTEM_THREAD *)((char *)pi + sizeof(SYSTEM_PROCESS_INFORMATION));
    for (DWORD i = 0; i < pi->NumberOfThreads; i++, ti++)
    {
        if (ti->ClientId.UniqueThread == (HANDLE)thread_id)
            return ti->State == StateRunning;
    }

    return -1;

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
#if defined PL_LINUX || defined PL_DARWIN
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
#endif
    {
#if defined PL_WIN32
        if (native)
            unwind_native_stack(name == "echion.core.sampler" ? GetCurrentThread() : thread_handle);
#endif
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
        output << "P" << pid << ";T" << iid << ":" << name;

        // Print the stack
        if (native)
        {
            interleave_stacks();
            interleaved_stack.render(output);
        }
        else
            python_stack.render(output);

        // Print the metric
        output << " " << delta << std::endl;
    }
    else
    {
        for (auto &task_stack : current_tasks)
        {
            output << "P" << pid << ";T" << iid << ":" << name;

            if (native)
            {
                // NOTE: These stacks might be non-sensical, especially with
                // Python < 3.11.
                interleave_stacks(*task_stack);
                interleaved_stack.render(output);
            }
            else
                task_stack->render(output);

            output << " " << delta << std::endl;
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
