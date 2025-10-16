#include <assert.h>
#include <cassert>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <ucontext.h>
#include <unistd.h>

#if defined PL_DARWIN
#include <pthread.h>

#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/machine/kern_return.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#endif

int init_segv_catcher();

#if defined PL_LINUX
ssize_t safe_memcpy_wrapper(
    pid_t,
    const struct iovec *__dstvec,
    unsigned long int __dstiovcnt,
    const struct iovec *__srcvec,
    unsigned long int __srciovcnt,
    unsigned long int
);
#elif defined PL_DARWIN
kern_return_t safe_memcpy_wrapper
(
	vm_map_read_t target_task,
	mach_vm_address_t address,
	mach_vm_size_t size,
	mach_vm_address_t data,
	mach_vm_size_t *outsize
);
#endif

struct ThreadAltStack {
private:
    inline static constexpr size_t kAltStackSize = 1<<20; // 1 MiB

public:
    void* mem = nullptr;
    size_t size = 0;
    bool ready = false;

    void ensure_installed() {
        if (ready) {
            return;
        }

        // If an altstack is already present, keep it.
        stack_t cur{};
        if (sigaltstack(nullptr, &cur) == 0 && !(cur.ss_flags & SS_DISABLE)) {
            ready = true;
            return;
        }

        void* mem = mmap(nullptr, kAltStackSize, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        assert(mem != MAP_FAILED);

        stack_t ss{};
        ss.ss_sp = mem;
        ss.ss_size = kAltStackSize;
        ss.ss_flags = 0;
        int rc = sigaltstack(&ss, nullptr);
        assert(rc == 0);

        this->mem = mem;
        this->size = kAltStackSize;
        this->ready = true;
    }

    ~ThreadAltStack() {
        if (!ready) {
            return;
        }

        // Optional cleanup: disable and free. Safe at thread exit.
        stack_t disable{};
        disable.ss_flags = SS_DISABLE;
        (void)sigaltstack(&disable, nullptr);
        munmap(mem, size);
    }
};
