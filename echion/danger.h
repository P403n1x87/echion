#include <algorithm>
#include <cassert>
#include <signal.h>
#include <setjmp.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <errno.h>
#include <signal.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>

namespace {

// --- Alt signal stack (process-wide)
static stack_t g_altstack;
static struct sigaction g_old_segv;
static struct sigaction g_old_bus;


static __thread volatile sig_atomic_t t_faulted;
static __thread void* t_landing_ip;

// SIGSEGV handler: mark fault + redirect to landing label
static void segv_handler(int, siginfo_t*, void* uctx) {
    if (!t_landing_ip) return;        // no guard set → not our probe; let default handler run (up to you)
    t_faulted = 1;

    ucontext_t* uc = (ucontext_t*)uctx;
#if defined(__x86_64__)
    uc->uc_mcontext.gregs[REG_RIP] = (greg_t)(uintptr_t)t_landing_ip;
#elif defined(__aarch64__)
    uc->uc_mcontext.pc = (uintptr_t)t_landing_ip;
#else
# error "Set saved PC for your architecture"
#endif
    // return → kernel rt_sigreturn → resume at landing label
}


// Install once at program/library init.
inline static int init_segv_catcher() {
    // 1) Alternate signal stack so handler runs even if the thread's normal stack is bad.
    const size_t sz = 1 << 20; // 1 MiB is plenty for a tiny handler
    
    void* mem = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        return -1;
    }

    g_altstack.ss_sp = mem;
    g_altstack.ss_size = sz;
    g_altstack.ss_flags = 0;
    // TODO: call sigaltstack once per thread, with an ensure-like function
    if (sigaltstack(&g_altstack, NULL) != 0) {
        return -1;
    }

    // 2) Install handler (SA_SIGINFO for the ucontext if you ever need it; SA_ONSTACK to use altstack)
    struct sigaction sa{};
    sa.sa_sigaction = segv_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    if (sigaction(SIGSEGV, &sa, &g_old_segv) != 0) {
        return -1;
    }
    if (sigaction(SIGBUS,  &sa, &g_old_bus)  != 0) {
        return -1;
    }

    return 0;
}

#include <signal.h>
#include <ucontext.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <algorithm>

int safe_memcpy(void* dst, const void* src, size_t n) {
    t_faulted = 0;

    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    size_t rem = n;

    // Publish landing target for the handler
    t_landing_ip = &&landing;

    // Copy in page-bounded chunks (at most one fault per bad page)
    while (rem && !t_faulted) {
        size_t to_src_pg = 4096 - ((uintptr_t)s & 4095);
        size_t to_dst_pg = 4096 - ((uintptr_t)d & 4095);
        size_t chunk = std::min(rem, std::min(to_src_pg, to_dst_pg));

        // Optional early probe to fault before entering large memcpy
        (void)*(volatile const uint8_t*)s;

        // If this faults, we’ll resume at `landing` label
        memcpy(d, s, chunk);

        d += chunk; s += chunk; rem -= chunk;
    }

landing:               // ← handler redirects RIP here
    t_landing_ip = NULL;      // disarm for safety (avoid miscatching unrelated faults)

    if (t_faulted) { errno = EFAULT; return -1; }
    return (int)n;
}

}

inline static ssize_t safe_memcpy_wrapper(
    pid_t,
    const struct iovec *__dstvec,
    unsigned long int __dstiovcnt,
    const struct iovec *__srcvec,
    unsigned long int __srciovcnt,
    unsigned long int)
{
    (void)__dstiovcnt;
    (void)__srciovcnt;
    assert(__dstiovcnt == 1);
    assert(__srciovcnt == 1);

    return safe_memcpy(__dstvec->iov_base, __srcvec->iov_base, std::min(__dstvec->iov_len, __srcvec->iov_len));
}
