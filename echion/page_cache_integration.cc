// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#include <echion/page_cache.h>
#include <echion/vm.h>

// Implementation of PageMemoryCache::direct_system_read
int PageMemoryCache::direct_system_read(pid_t pid, void* remote_addr, size_t size, void* local_buf) {
    return copy_memory_direct(static_cast<proc_ref_t>(pid), remote_addr, static_cast<ssize_t>(size), local_buf);
}

// Integration function that connects page cache to the vm system
int use_page_cache_for_read(proc_ref_t proc_ref, void* addr, ssize_t len, void* buf) {
    // Convert proc_ref_t to pid_t for Linux, handle other platforms
#if defined PL_LINUX
    pid_t pid = proc_ref;
    int result = get_page_cache().cached_read(pid, addr, static_cast<size_t>(len), buf);
    return result;
#elif defined PL_DARWIN
    // For Darwin, we don't have a direct pid, so fall back to direct copy for now
    // TODO: Implement Darwin-specific page caching using mach_vm_read
    return copy_memory_direct(proc_ref, addr, len, buf);
#else
    // Unsupported platform, fall back to direct copy
    return copy_memory_direct(proc_ref, addr, len, buf);
#endif
}
