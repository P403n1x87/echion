// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#include <cstdlib>
#include <cstring>

#if defined PL_LINUX
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

typedef pid_t proc_ref_t;

class TestVm;

ssize_t process_vm_readv(pid_t, const struct iovec*, unsigned long liovcnt,
                         const struct iovec* remote_iov, unsigned long riovcnt,
                         unsigned long flags);

#define copy_type(addr, dest) (copy_memory(pid, addr, sizeof(dest), &dest))
#define copy_type_p(addr, dest) (copy_memory(pid, addr, sizeof(*dest), dest))
#define copy_generic(addr, dest, size) (copy_memory(pid, (void*)(addr), size, (void*)(dest)))

#elif defined PL_DARWIN
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/machine/kern_return.h>
#include <sys/sysctl.h>
#include <sys/types.h>

typedef mach_port_t proc_ref_t;

#define copy_type(addr, dest) (copy_memory(mach_task_self(), addr, sizeof(dest), &dest))
#define copy_type_p(addr, dest) (copy_memory(mach_task_self(), addr, sizeof(*dest), dest))
#define copy_generic(addr, dest, size) \
    (copy_memory(mach_task_self(), (void*)(addr), size, (void*)(dest)))
#endif

// Some checks are done at static initialization, so use this to read them at runtime
inline bool failed_safe_copy = false;

#if defined PL_LINUX
inline ssize_t (*safe_copy)(pid_t, const struct iovec*, unsigned long, const struct iovec*,
                            unsigned long, unsigned long) = process_vm_readv;

class VmReader
{
    void* buffer{nullptr};
    size_t sz{0};
    int fd{-1};
    inline static VmReader* instance{nullptr};  // Prevents having to set this in implementation

    void* init(size_t new_sz);

    VmReader(size_t _sz);

public:
    static VmReader* get_instance();

#ifdef GTEST_TEST 
    static void reset() {
        if (instance) {
            delete instance;
            instance = nullptr;
        }
    }

    friend TestVm;
#endif

    ssize_t safe_copy(pid_t pid, const struct iovec* local_iov, unsigned long liovcnt,
                      const struct iovec* remote_iov, unsigned long riovcnt, unsigned long flags);

    ~VmReader();
};

/**
 * Initialize the safe copy operation on Linux
 */
bool read_process_vm_init();

ssize_t vmreader_safe_copy(pid_t pid, const struct iovec* local_iov, unsigned long liovcnt,
                           const struct iovec* remote_iov, unsigned long riovcnt,
                           unsigned long flags);

/**
 * Initialize the safe copy operation on Linux
 *
 * This occurs at static init
 */
__attribute__((constructor)) void init_safe_copy();
#endif

/**
 * Copy a chunk of memory from a portion of the virtual memory of another
 * process.
 * @param proc_ref_t  the process reference (platform-dependent)
 * @param void *      the remote address
 * @param ssize_t     the number of bytes to read
 * @param void *      the destination buffer, expected to be at least as large
 *                    as the number of bytes to read.
 *
 * @return  zero on success, otherwise non-zero.
 */
int copy_memory(proc_ref_t proc_ref, void* addr, ssize_t len, void* buf);

inline pid_t pid = 0;

void _set_pid(pid_t _pid);
