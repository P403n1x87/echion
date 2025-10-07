// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#include <Python.h>

#include <optional>
#include <unordered_map>

#include <sys/resource.h>

#include <echion/config.h>
#include <echion/interp.h>
#include <echion/mojo.h>
#include <echion/stacks.h>
#include <echion/threads.h>

// ----------------------------------------------------------------------------
class ResidentMemoryTracker
{
public:
    size_t size;

    // ------------------------------------------------------------------------
    ResidentMemoryTracker();

    // ------------------------------------------------------------------------
    bool check();

private:
    // ------------------------------------------------------------------------
    void update();
};

inline ResidentMemoryTracker rss_tracker;

// ----------------------------------------------------------------------------

class MemoryStats
{
public:
    int64_t iid;
    std::string thread_name;

    FrameStack::Key stack;

    size_t count;
    ssize_t size;

    // ------------------------------------------------------------------------
    MemoryStats(int iid, std::string thread_name, FrameStack::Key stack, size_t count, size_t size);

    // ------------------------------------------------------------------------
    void render();
};

// ----------------------------------------------------------------------------
struct MemoryTableEntry
{
    FrameStack::Key stack;
    size_t size;
};

// ----------------------------------------------------------------------------
class MemoryTable : public std::unordered_map<void*, MemoryTableEntry>
{
public:
    // ------------------------------------------------------------------------
    void link(void* address, FrameStack::Key stack, size_t size);

    // ------------------------------------------------------------------------
    std::optional<MemoryTableEntry> unlink(void* address);

private:
    std::mutex lock;
};

// ----------------------------------------------------------------------------
class StackStats
{
public:
    // ------------------------------------------------------------------------
    void update(PyThreadState* tstate, FrameStack::Key stack, size_t size);

    // ------------------------------------------------------------------------
    void update(MemoryTableEntry& entry);

    // ------------------------------------------------------------------------
    void flush();

    // ------------------------------------------------------------------------
    void clear();

private:
    std::mutex lock;
    std::unordered_map<FrameStack::Key, MemoryStats> map;
};

// ----------------------------------------------------------------------------

// We make this a reference to a heap-allocated object so that we can avoid
// the destruction on exit. We are in charge of cleaning up the object. Note
// that the object will leak, but this is not a problem.
inline auto& stack_stats = *(new StackStats());
inline auto& memory_table = *(new MemoryTable());

// ----------------------------------------------------------------------------
void general_alloc(void* address, size_t size);

// ----------------------------------------------------------------------------
void general_free(void* address);

// ----------------------------------------------------------------------------
void* echion_malloc(void* ctx, size_t n);

// ----------------------------------------------------------------------------
void* echion_calloc(void* ctx, size_t nelem, size_t elsize);

// ----------------------------------------------------------------------------
void* echion_realloc(void* ctx, void* p, size_t n);

// ----------------------------------------------------------------------------
void echion_free(void* ctx, void* p);

// ----------------------------------------------------------------------------

// DEV: We define this macro on the basis of the knowledge that the domains are
//      defined as an enum.
#define ALLOC_DOMAIN_COUNT 3

inline PyMemAllocatorEx original_allocators[ALLOC_DOMAIN_COUNT];
inline PyMemAllocatorEx echion_allocator = {NULL, echion_malloc, echion_calloc, echion_realloc,
                                            echion_free};

// ----------------------------------------------------------------------------
void setup_memory();

// ----------------------------------------------------------------------------
void teardown_memory();
