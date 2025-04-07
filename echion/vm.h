// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#include <array>
#include <cstdlib>
#include <mutex>
#include <limits.h>
#include <stdexcept>
#include <string>

#include <echion/config.h>

#if defined PL_LINUX
#include <algorithm>
#include <atomic>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <setjmp.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

typedef pid_t proc_ref_t;

ssize_t process_vm_readv(
    pid_t, const struct iovec *, unsigned long liovcnt,
    const struct iovec *remote_iov, unsigned long riovcnt, unsigned long flags);

#define copy_type(addr, dest) (copy_memory(pid, addr, sizeof(dest), &dest))
#define copy_type_p(addr, dest) (copy_memory(pid, addr, sizeof(*dest), dest))
#define copy_generic(addr, dest, size) (copy_memory(pid, (void *)(addr), size, (void *)(dest)))

#elif defined PL_DARWIN
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/machine/kern_return.h>
#include <sys/types.h>
#include <sys/sysctl.h>

typedef mach_port_t proc_ref_t;

#define copy_type(addr, dest) (copy_memory(mach_task_self(), addr, sizeof(dest), &dest))
#define copy_type_p(addr, dest) (copy_memory(mach_task_self(), addr, sizeof(*dest), dest))
#define copy_generic(addr, dest, size) (copy_memory(mach_task_self(), (void *)(addr), size, (void *)(dest)))
#endif


#if defined PL_LINUX
#define SAFE_COPY_ERROR ((ssize_t (*)(pid_t, const struct iovec *, unsigned long, const struct iovec *, unsigned long, unsigned long)) -1)
inline ssize_t (*safe_copy)(pid_t, const struct iovec *, unsigned long, const struct iovec *, unsigned long, unsigned long) = SAFE_COPY_ERROR;

class TrappedVmReader {
private:
    // Whenever we clear tracking, we need to set it to an unreachable address--we don't want to clear it so it resides in the NULL page, as that will erroneously flag "most" segfaults
    static inline void* unreachable_ptr = reinterpret_cast<void*>(static_cast<uintptr_t>(-1));

    static inline jmp_buf jump_buffer;
    static inline struct sigaction old_sigsegv_action;
    static inline struct sigaction old_sigbus_action;

    // Tracks the current read operation
    static inline std::atomic<const void*> current_src_addr {nullptr};
    static inline std::atomic<size_t> current_src_size {0};
    static inline std::atomic<bool> terminated {false};
    static inline std::mutex handler_mutex;

    static void segfault_handler(int sig, siginfo_t *info, void *context) {
        // Before we do anything else, check the si_code to see if this was a `raise()`/`kill()` situation
        if (info->si_code == SI_USER || info->si_code == SI_TKILL) {
            terminated.store(true);
            std::lock_guard<std::mutex> lock(handler_mutex); // Pop the mutex while we change state
            struct sigaction* old_action = (sig == SIGSEGV) ? &old_sigsegv_action : &old_sigbus_action;

            // Reinstall the old handlers--the user code may return gracefully from the situation, but we have to assume that the other handler assumes
            // it is the current handler.  We can't keep this.
            sigaction(SIGSEGV, &old_sigsegv_action, nullptr);
            sigaction(SIGBUS, &old_sigbus_action, nullptr);

            // Handle the different cases
            if (old_action->sa_flags & SA_SIGINFO) {
                old_action->sa_sigaction(sig, info, context);
            } else if (old_action->sa_handler != SIG_IGN && old_action->sa_handler != SIG_DFL) {
                old_action->sa_handler(sig);
            } else {
                // If original handler was default, restore it and explicitly re-raise
                signal(sig, SIG_DFL);
                raise(sig);
            }
            return;
        }

        // The fault address should always be available for a kernel-generated signal.  If it isn't, then all we could do is:
        // * Set a false lookup for the VmReader, assuming this was a read-type operation
        // * Do the normal thing we do when this is a "real" segfault
        // Probably have to do the latter?
        void *fault_addr = info->si_addr;

        auto start = current_src_addr.load();
        auto end = static_cast<const std::byte*>(start) + current_src_size.load();

        if (start && fault_addr >= start && fault_addr <= end) {
            // Lookup failed, so just return
            longjmp(jump_buffer, 1);
        }

        // If we're here, then the fault happened outside of our range.
        // This means we need to restore the original signal handlers and let them handle this condition.
        // Returning is preferable to chaining because it allows the stack/ucontext to be preserved.
        terminated.store(true);
        std::lock_guard<std::mutex> lock(handler_mutex);
        sigaction(SIGSEGV, &old_sigsegv_action, nullptr);
        sigaction(SIGBUS, &old_sigbus_action, nullptr);
        return;
    }

public:
    // Initialize the signal handler
    static bool initialize() {
        if (terminated.load()) {
            return false;
        }

        struct sigaction action;
        memset(&action, 0, sizeof(action));
        action.sa_sigaction = segfault_handler;
        action.sa_flags = SA_SIGINFO;

        // Note that we _explicitly_ do not use SA_NODEFER because we do not want nested handlers
        // This allows us to bounce off at the top level if there are any issues
        bool segv_status = sigaction(SIGSEGV, &action, nullptr) == 0;
        bool bus_status = sigaction(SIGBUS, &action, nullptr) == 0;

        if (!segv_status) {
            sigaction(SIGSEGV, &old_sigsegv_action, nullptr);
            terminated.store(true);
        }
        if (!bus_status) {
            sigaction(SIGBUS, &old_sigbus_action, nullptr);
            terminated.store(true);
        }
        return segv_status && bus_status;
    }

    // Restore the original signal handler
    static void cleanup() {
        sigaction(SIGSEGV, &old_sigsegv_action, nullptr);
        sigaction(SIGBUS, &old_sigbus_action, nullptr);
    }

    // Try to safely copy memory from src to dst of size bytes
    // Returns true if successful, false if a segfault occurred
    // Try to safely copy memory from src to dst of size bytes
    static bool read(void *dst, const void *src, size_t size) {
        // Set up tracking for this read operation
        current_src_addr.store(src);
        current_src_size.store(size);

        if (setjmp(jump_buffer) == 0) {
            // Before we potentially segfault, check to see whether the instrumentation is terminated--if so, just leave
            if (terminated.load()) {
                return false;
            }

            // Copy the memory--this line will segfault if the address is invalid
            // Technically there is a small race condition here, but the only interesting race condition is when a
            // different thread happens to hit a segfault at this exact moment.  Lack of NODEFER means that whoever passes
            // to the handler first will get the signal, so this should be fine.
            memcpy(dst, src, size);

            // Clear tracking
            current_src_addr.store(unreachable_ptr);
            current_src_size.store(0);
            return true;
        } else {
            // We got here from longjmp after a segfault
            current_src_addr.store(unreachable_ptr);
            current_src_size.store(0);
            return false;
        }
    }
};

class VmReader
{
    void *buffer{nullptr};
    size_t sz{0};
    int fd{-1};
    inline static VmReader *instance{nullptr}; // Prevents having to set this in implementation

    void *init(size_t new_sz)
    {
        // Makes a temporary file and ftruncates it to the specified size
        std::array<std::string, 3> tmp_dirs = {"/dev/shm", "/tmp", "/var/tmp"};
        std::string tmp_suffix = "/echion-XXXXXX";
        void *ret = nullptr;

        for (auto &tmp_dir : tmp_dirs)
        {
            // Reset the file descriptor, just in case
            close(fd);
            fd = -1;

              // Create the temporary file
              std::string template_path = tmp_dir + tmp_suffix;
              char template_buf[PATH_MAX];
              strncpy(template_buf, template_path.c_str(), PATH_MAX - 1);
              template_buf[PATH_MAX - 1] = '\0';

              fd = mkstemp(template_buf);
              if (fd == -1) {
                  continue;
              }

              // Unlink the file to ensure it's removed when closed
              if (unlink(template_buf) != 0) {
                // Not sure what to do here, probably fine
              }

            // Make sure we have enough size
            if (ftruncate(fd, new_sz) == -1)
            {
                continue;
            }

            // Map the file
            ret = mmap(NULL, new_sz, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
            if (ret == MAP_FAILED)
            {
                ret = nullptr;
                continue;
            }

            // Successful.  Break.
            sz = new_sz;
            break;
        }

        return ret;
    }

    VmReader(size_t _sz) : sz{_sz}
    {
        buffer = init(sz);
        if (!buffer)
        {
            throw std::runtime_error("Failed to initialize buffer with size " + std::to_string(sz));
        }
        instance = this;
    }

public:
      static VmReader *get_instance()
      {
          if (instance == nullptr)
          {
              static std::once_flag init_flag;
              std::call_once(init_flag, []() {
                  // Nothing to do on failure here since instance is already null
                  try
                  {
                      auto temp = new VmReader(1024 * 1024); // A megabyte?
                      instance = temp;
                  }
              });
          }
          return instance;
      }

    ssize_t safe_copy(pid_t pid,
                      const struct iovec *local_iov, unsigned long liovcnt,
                      const struct iovec *remote_iov, unsigned long riovcnt, unsigned long flags)
    {
        (void)pid;
        (void)flags;
        if (liovcnt != 1 || riovcnt != 1)
        {
            // Unsupported
            return 0;
        }

        // Check to see if we need to resize the buffer
        if (remote_iov[0].iov_len > sz)
        {
            if (ftruncate(fd, remote_iov[0].iov_len) == -1)
            {
                return 0;
            }
            else
            {
                void *tmp = mremap(buffer, sz, remote_iov[0].iov_len, MREMAP_MAYMOVE);
                if (tmp == MAP_FAILED)
                {
                    return 0;
                }
                buffer = tmp; // no need to munmap
                sz = remote_iov[0].iov_len;
            }
        }

          ssize_t ret = pwritev(fd, remote_iov, riovcnt, 0);
          if (ret == -1)
          {
              // Store error details for debugging
              int err = errno;
              return -1;
          }

        // Copy the data from the buffer to the remote process
        memcpy(local_iov[0].iov_base, buffer, local_iov[0].iov_len);
        return ret;
    }

    ~VmReader()
    {
        if (buffer)
        {
            munmap(buffer, sz);
        }
        if (fd != -1)
        {
            close(fd);
        }
        instance = nullptr;
    }
};

/**
 * Initialize the safe copy operation on Linux
 */
inline bool read_process_vm_init()
{
    VmReader *_ = VmReader::get_instance();
    return !!_;
}

inline ssize_t vmreader_safe_copy(pid_t pid,
                             const struct iovec *local_iov, unsigned long liovcnt,
                             const struct iovec *remote_iov, unsigned long riovcnt, unsigned long flags)
{
    auto reader = VmReader::get_instance();
    if (!reader)
        return 0;
    return reader->safe_copy(pid, local_iov, liovcnt, remote_iov, riovcnt, flags);
}

inline ssize_t trappedvmreader_safe_copy(pid_t pid,
                             const struct iovec *local_iov, unsigned long liovcnt,
                             const struct iovec *remote_iov, unsigned long riovcnt, unsigned long flags)
{
    // We only support one iovec for now
    if (liovcnt != 1 || riovcnt != 1) {
        return 0;
    }

    (void)pid;  // Unused parameter
    (void)flags;  // Unused parameter

    // Try to use TrappedVmReader to copy the memory
    bool success = TrappedVmReader::read(
        local_iov[0].iov_base,
        remote_iov[0].iov_base,
        std::min(local_iov[0].iov_len, remote_iov[0].iov_len)
    );

    if (success) {
        return std::min(local_iov[0].iov_len, remote_iov[0].iov_len);
    } else {
        return 0;
    }
}

static inline bool check_process_vm_readv()
{
    char src[128];
    char dst[128];
    struct iovec iov_dst = {dst, sizeof(dst)};
    struct iovec iov_src = {src, sizeof(src)};
    return sizeof(src) == process_vm_readv(getpid(), &iov_dst, 1, &iov_src, 1, 0);

}

/**
 * Initialize the safe copy operation on Linux
 */
inline bool init_safe_copy(int mode)
{
    if (1 == mode) {
        if (check_process_vm_readv()) {
            safe_copy = process_vm_readv;
            return true;
        }
    } else if (2 == mode) {
        // Initialize the TrappedVmReader signal handlers
        if (TrappedVmReader::initialize()) {
            safe_copy = trappedvmreader_safe_copy;
            return true;
        }
    }

    if (safe_copy != vmreader_safe_copy) {
        // If we're not already using the safe copy, try to initialize it
        if (read_process_vm_init()) {
            safe_copy = vmreader_safe_copy;
            return mode != 0; // Return true IFF user had requested writev
        }
    }

    // If we're here, we tried to initialize the safe copy but failed, and the failover failed
    // so we have no safe copy mechanism available
    safe_copy = SAFE_COPY_ERROR;
    return false;
}

/**
 * Initialize the safe copy operation on Linux at static initialization
 * This sets the default behavior, but can be overridden
 */
__attribute__((constructor))
inline bool init_safe_copy_static()
{
    return init_safe_copy(1); // Try to initialize with the default
}
#else
inline bool init_safe_copy(int mode)
{
    // This doesn't do anything, it just provides a symbol so that the caller doesn't have to specialize
    (void)mode;
    return true;
}
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
static inline int copy_memory(proc_ref_t proc_ref, void *addr, ssize_t len, void *buf)
{
    ssize_t result = -1;

    // Early exit on zero page
    if (reinterpret_cast<uintptr_t>(addr) < 4096)
    {
        return result;
    }

#if defined PL_LINUX
    if (safe_copy == SAFE_COPY_ERROR)
    {
        // No safe copy mechanism available
        return -1;
    }
    struct iovec local[1];
    struct iovec remote[1];

    local[0].iov_base = buf;
    local[0].iov_len = len;
    remote[0].iov_base = addr;
    remote[0].iov_len = len;

    result = safe_copy(proc_ref, local, 1, remote, 1, 0);

#elif defined PL_DARWIN
    kern_return_t kr = mach_vm_read_overwrite(
        proc_ref,
        (mach_vm_address_t)addr,
        len,
        (mach_vm_address_t)buf,
        (mach_vm_size_t *)&result);

    if (kr != KERN_SUCCESS)
        return -1;

#endif

    return len != result;
}

inline pid_t pid = 0;

inline void _set_pid(pid_t _pid) {
    pid = _pid;
}

// ----------------------------------------------------------------------------
inline bool _set_vm_read_mode(int new_vm_read_mode)
{
    if (new_vm_read_mode < 0)
    {
        PyErr_SetString(PyExc_RuntimeError, "Invalid vm_read_mode");
        return false;
    }

    if (init_safe_copy(new_vm_read_mode))
    {
        vm_read_mode = new_vm_read_mode;
        return true;
    } else {
#ifdef PL_LINUX
        // If we failed, but the failover worked, then update the mode as such
        if (safe_copy == vmreader_safe_copy)
        {
            // Set the mode to reflect the failover
            vm_read_mode = 0;
        } else {
            // Error
            PyErr_SetString(PyExc_RuntimeError, "Failed to initialize safe copy interfaces");
            vm_read_mode = -1;
        }
#endif
        // Can't get here in macos, since the init process always returns true
    }

    return false;
}

// ----------------------------------------------------------------------------
static PyObject *
set_vm_read_mode(PyObject *Py_UNUSED(m), PyObject *args)
{
    int new_vm_read_mode;
    if (!PyArg_ParseTuple(args, "i", &new_vm_read_mode))
        return NULL;

    if (!_set_vm_read_mode(new_vm_read_mode))
        return NULL;

    Py_RETURN_NONE;
}
