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

// --- Per-thread jump buffer stack pointer
static __thread sigjmp_buf* t_segv_env = nullptr;  // top of a linked "stack" of envs

// --- Alt signal stack (process-wide)
static stack_t g_altstack;
static struct sigaction g_old_segv;

// Tiny, async-signal-safe handler.
// On fault: jump to the most recent probe point if present; else re-raise.
static void segv_handler(int sig, siginfo_t* si, void* uctx) {
    (void)sig;
    (void)si;
    (void)uctx;

    auto* env = t_segv_env;
    if (env) {
        // Jump back to the probe site (returning 1 from sigsetjmp)
        siglongjmp(*env, 1);
    }

    // XXX: This isn't async-signal-safe so I let's not keep it
    // No probe to catch this: restore default and re-raise to crash normally.
    // sigaction(SIGSEGV, &g_old_segv, nullptr);
    raise(SIGSEGV);
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

    return 0;
}

// Scope helper: push/pop the thread-local jump target safely.
struct segv_scope {
    sigjmp_buf env;
    sigjmp_buf* prev;
};

static inline void segv_scope_push(struct segv_scope* s) {
    s->prev = t_segv_env;
    t_segv_env = &s->env;
}

static inline void segv_scope_pop(struct segv_scope* s) {
    (void)s;
    t_segv_env = s->prev;
}

}

// Public API: attempt to copy N bytes from SRC to DST via direct read.
// Returns the number of bytes read on success, -1 if the read would segfault.
inline static int safe_memcpy(void* dst, const void* src, size_t n) {
    struct segv_scope scope;
    segv_scope_push(&scope);

    // Save signal mask with sigsetjmp variant so we return with the same mask.
    if (sigsetjmp(scope.env, /*savesigs=*/0) == 0) {
        // TODO: Page-bound reads? Currently all-or-nothing.

        // Barrier so the compiler doesn't hoist the copy past setjmp.
        __asm__ __volatile__("" ::: "memory");
        memcpy(dst, src, n);
        __asm__ __volatile__("" ::: "memory");
        segv_scope_pop(&scope);
        return n;
    }

    // We arrived here from segv_handler via siglongjmp()
    segv_scope_pop(&scope);
    errno = EFAULT;
    return -1;
}

inline static ssize_t safe_memcpy_wrapper(
    pid_t,
    const struct iovec *__dstvec,
    unsigned long int __dstiovcnt,
    const struct iovec *__srcvec,
    unsigned long int __srciovcnt,
    unsigned long int)
{
    assert(__dstiovcnt == 1);
    assert(__srciovcnt == 1);

    return safe_memcpy(__dstvec->iov_base, __srcvec->iov_base, std::min(__dstvec->iov_len, __srcvec->iov_len));
}
