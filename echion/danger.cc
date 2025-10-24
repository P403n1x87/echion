#include <echion/danger.h>

#include <cstdio>

#include <algorithm>
#include <cerrno>

#include <echion/state.h>

thread_local stack_t g_altstack;
struct sigaction g_old_segv;
struct sigaction g_old_bus;

thread_local ThreadAltStack t_altstack;

__thread volatile sig_atomic_t t_faulted;
__thread volatile void* t_landing_ip;

void arm_landing(void* p) {
    t_landing_ip = p;
    __asm__ __volatile__("" ::: "memory");
}

void disarm_landing() {
    __asm__ __volatile__("" ::: "memory");
    t_landing_ip = nullptr;
}

void segv_handler(int signo, siginfo_t*, void* uctx) {
    if (!t_landing_ip) {
        struct sigaction *old = (signo == SIGSEGV) ? &g_old_segv : &g_old_bus;

        // Restore the previous handler and re-raise
        sigaction(signo, old, NULL);
        raise(signo);
    }

    t_faulted = 1;

    ucontext_t* uc = reinterpret_cast<ucontext_t*>(uctx);
#if defined(__APPLE__)

// macOS
#  if defined(__x86_64__)
    // RIP is in the saved general-purpose register set
    ((ucontext_t*)uc)->uc_mcontext->__ss.__rip = (uint64_t)(uintptr_t)t_landing_ip;
#  elif defined(__i386__)
    ((ucontext_t*)uc)->uc_mcontext->__ss.__eip = (uint32_t)(uintptr_t)t_landing_ip;
#  elif defined(__aarch64__)
    ((ucontext_t*)uc)->uc_mcontext->__ss.__pc  = (uint64_t)(uintptr_t)t_landing_ip;
#  else
#    error "Unsupported macOS architecture"
#  endif

#else

// Linux / other libcs
#  if defined(__x86_64__) && defined(REG_RIP)
    (static_cast<ucontext_t*>(uc))->uc_mcontext.gregs[REG_RIP] = static_cast<greg_t>(reinterpret_cast<uintptr_t>(t_landing_ip));
#  elif defined(__i386__) && defined(REG_EIP)
    ((ucontext_t*)uc)->uc_mcontext.gregs[REG_EIP] = (greg_t)(uintptr_t)t_landing_ip;
#  elif defined(__aarch64__)
    ((ucontext_t*)uc)->uc_mcontext.pc = (uintptr_t)t_landing_ip; // or gregs[REG_PC] depending on libc
#  else
#    error "Set instruction pointer not implemented for this architecture/libc"
#  endif

#endif
}

int init_segv_catcher() {
    ensure_altstack_for_this_thread();

    // 1) Alternate signal stack so handler runs even if the thread's normal stack is bad.
    const size_t sz = 1 << 20; // 1 MiB is plenty for a tiny handler
    
    void* mem = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
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

void ensure_altstack_for_this_thread() {
    t_altstack.ensure_installed();
}

safe_memcpy_return_t safe_memcpy(void* dst, const void* src, size_t n) {
    ensure_altstack_for_this_thread();

    t_faulted = 0;

    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    safe_memcpy_return_t rem = n;

    // Publish landing target for the handler
    arm_landing(&&landing);

    // Copy in page-bounded chunks (at most one fault per bad page)
    while (rem && !t_faulted) {
        safe_memcpy_return_t to_src_pg = 4096 - (reinterpret_cast<uintptr_t>(s) & 4095);
        safe_memcpy_return_t to_dst_pg = 4096 - (reinterpret_cast<uintptr_t>(d) & 4095);
        safe_memcpy_return_t chunk = std::min(rem, std::min(to_src_pg, to_dst_pg));

        // Optional early probe to fault before entering large memcpy
        (void)*reinterpret_cast<volatile const uint8_t*>(s);

        // If this faults, we'll resume at `landing` label
        (void)memcpy(d, s, chunk);

        d += chunk; s += chunk; rem -= chunk;
    }

landing:
    disarm_landing();

    if (t_faulted) {
        errno = EFAULT;
        return -1;
    }

    return n;
}

#if defined PL_LINUX
ssize_t safe_memcpy_wrapper(
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
#elif defined PL_DARWIN
kern_return_t safe_memcpy_wrapper
(
	vm_map_read_t target_task,
	mach_vm_address_t address,
	mach_vm_size_t size,
	mach_vm_address_t data,
	mach_vm_size_t *outsize
) {
    (void)target_task;

    auto copied = safe_memcpy(reinterpret_cast<void*>(address), reinterpret_cast<void*>(data), size);
    *outsize = copied;
    return copied == size ? KERN_SUCCESS : KERN_FAILURE;
}
#endif