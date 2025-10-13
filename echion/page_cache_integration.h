// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#include <cstddef>  // For ssize_t

// Forward declarations to avoid circular includes
#if defined PL_LINUX
typedef int proc_ref_t;
#elif defined PL_DARWIN
typedef int proc_ref_t;  // Actually mach_port_t, but int works for declaration
#endif

// Integration function that connects page cache to the vm system
int use_page_cache_for_read(proc_ref_t proc_ref, void* addr, ssize_t len, void* buf);
