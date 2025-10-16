#include <echion/danger.h>
#include <echion/state.h>

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <csetjmp>
#include <cstdio>
#include <sys/mman.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

struct sigaction g_old_segv;
struct sigaction g_old_bus;

thread_local ThreadAltStack t_altstack;

thread_local volatile sig_atomic_t t_faulted = 0;

// We "arm" by publishing a valid jmp env for this thread.
thread_local sigjmp_buf t_jmpenv;
thread_local volatile sig_atomic_t t_jmpenv_armed = 0;

static inline void arm_landing() {
    t_jmpenv_armed = 1;
    __asm__ __volatile__("" ::: "memory");
}

static inline void disarm_landing() {
    __asm__ __volatile__("" ::: "memory");
    t_jmpenv_armed = 0;
}

static void segv_handler(int signo, siginfo_t*, void*) {
    if (!t_jmpenv_armed) {
        struct sigaction* old = (signo == SIGSEGV) ? &g_old_segv : &g_old_bus;
        // Restore the previous handler and re-raise so default/old handling occurs.
        sigaction(signo, old, nullptr);
        raise(signo);
        return;
    }

    t_faulted = 1;

    // Jump back to the armed site. Use 1 so sigsetjmp returns nonzero.
    siglongjmp(t_jmpenv, 1);
}

static inline void ensure_altstack_for_this_thread() {
    t_altstack.ensure_installed();
}

int init_segv_catcher() {
    ensure_altstack_for_this_thread();

    // Reserve a page of memory just like before (not strictly necessary here,
    // but kept to preserve behavior/footprint).
    const size_t sz = 1 << 20;
    void* mem = mmap(nullptr, sz, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        return -1;
    }

    struct sigaction sa{};
    sa.sa_sigaction = segv_handler;
    sigemptyset(&sa.sa_mask);
    // SA_SIGINFO for 3-arg handler; SA_ONSTACK to run on alt stack.
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    if (sigaction(SIGSEGV, &sa, &g_old_segv) != 0) {
        return -1;
    }
    if (sigaction(SIGBUS, &sa, &g_old_bus) != 0) {
        // Try to roll back SIGSEGV install on failure.
        sigaction(SIGSEGV, &g_old_segv, nullptr);
        return -1;
    }

    return 0;
}

#if defined PL_LINUX
using safe_memcpy_return_t = ssize_t;
#elif defined PL_DARWIN
using safe_memcpy_return_t = mach_vm_size_t;
#endif

safe_memcpy_return_t safe_memcpy(void* dst, const void* src, size_t n) {
    ensure_altstack_for_this_thread();

    t_faulted = 0;

    auto* d = static_cast<uint8_t*>(dst);
    auto* s = static_cast<const uint8_t*>(src);
    safe_memcpy_return_t rem = static_cast<safe_memcpy_return_t>(n);

    // Arm and capture a landing site. Save/restore signal mask with savesigs=1.
    arm_landing();
    if (sigsetjmp(t_jmpenv, /*savesigs=*/1) != 0) {
        // We arrived here from siglongjmp after a fault.
        goto landing;
    }

    // Copy in page-bounded chunks (at most one fault per bad page).
    while (rem && !t_faulted) {
        safe_memcpy_return_t to_src_pg =
            4096 - (static_cast<uintptr_t>(reinterpret_cast<uintptr_t>(s)) & 4095);
        safe_memcpy_return_t to_dst_pg =
            4096 - (static_cast<uintptr_t>(reinterpret_cast<uintptr_t>(d)) & 4095);
        safe_memcpy_return_t chunk = std::min(rem, std::min(to_src_pg, to_dst_pg));

        // Optional early probe to fault before entering large memcpy
        (void)*reinterpret_cast<volatile const uint8_t*>(s);

        // If this faults, we'll siglongjmp back to the sigsetjmp above.
        (void)memcpy(d, s, static_cast<size_t>(chunk));

        d += chunk;
        s += chunk;
        rem -= chunk;
    }

landing:
    disarm_landing();

    if (t_faulted) {
        errno = EFAULT;
        return -1;
    }

    return static_cast<safe_memcpy_return_t>(n);
}

#if defined PL_LINUX
ssize_t safe_memcpy_wrapper(
    pid_t,
    const struct iovec* __dstvec,
    unsigned long int __dstiovcnt,
    const struct iovec* __srcvec,
    unsigned long int __srciovcnt,
    unsigned long int) {
    (void)__dstiovcnt;
    (void)__srciovcnt;
    assert(__dstiovcnt == 1);
    assert(__srciovcnt == 1);

    size_t to_copy = std::min(__dstvec->iov_len, __srcvec->iov_len);
    return safe_memcpy(__dstvec->iov_base, __srcvec->iov_base, to_copy);
}
#elif defined PL_DARWIN
kern_return_t safe_memcpy_wrapper(
    vm_map_read_t target_task,
    mach_vm_address_t address,
    mach_vm_size_t size,
    mach_vm_address_t data,
    mach_vm_size_t* outsize) {
    (void)target_task;

    auto copied = safe_memcpy(reinterpret_cast<void*>(address),
                              reinterpret_cast<void*>(data),
                              static_cast<size_t>(size));
    *outsize = copied;
    return copied == size ? KERN_SUCCESS : KERN_FAILURE;
}
#endif
