// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#include <cstdint>
#include <mutex>
#include <sstream>
#include <unordered_map>

#if defined PL_LINUX
#include <time.h>
#elif defined PL_DARWIN
#include <mach/clock.h>
#include <mach/mach.h>
#endif

#include <echion/timing.h>

class ThreadInfo
{
public:
    uintptr_t thread_id;
    unsigned long native_id;
    const char *name;
#if defined PL_DARWIN
    mach_port_t mach_port;
#endif
    microsecond_t cpu_time;

    uintptr_t asyncio_loop;

    void update_cpu_time();
    bool is_running();

    ~ThreadInfo()
    {
        delete[] name;
    };
};

void ThreadInfo::update_cpu_time()
{
  return;
}
//#if defined PL_LINUX
//    clockid_t cid;
//    if (pthread_getcpuclockid((pthread_t)this->thread_id, &cid))
//        return;
//
//    struct timespec ts;
//    if (clock_gettime(cid, &ts))
//        return;
//
//    this->cpu_time = TS_TO_MICROSECOND(ts);
//#elif defined PL_DARWIN
//    thread_basic_info_data_t info;
//    mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
//    kern_return_t kr = thread_info((thread_act_t)this->mach_port, THREAD_BASIC_INFO, (thread_info_t)&info, &count);
//
//    if (kr != KERN_SUCCESS || (info.flags & TH_FLAGS_IDLE))
//        return;
//
//    this->cpu_time = TV_TO_MICROSECOND(info.user_time) + TV_TO_MICROSECOND(info.system_time);
//#endif
//}

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

static std::unordered_map<uintptr_t, ThreadInfo *> thread_info_map; // indexed by thread_id
static std::mutex thread_info_map_lock;

static ThreadInfo *current_thread_info = NULL;
