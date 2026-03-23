// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#if PY_VERSION_HEX >= 0x030e0000
#include <internal/pycore_interpframe_structs.h>
#endif

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#if defined __GNUC__ && defined HAVE_STD_ATOMIC
#undef HAVE_STD_ATOMIC
#endif
#define Py_BUILD_CORE
#include <internal/pycore_pystate.h>

#include <memory>
#include <vector>

#include <echion/errors.h>
#include <echion/vm.h>

const constexpr size_t MAX_CHUNK_SIZE = 256 * 1024; // 256KB


// ----------------------------------------------------------------------------
class StackChunk
{
public:
    StackChunk() {}

    [[nodiscard]] inline Result<void> update(_PyStackChunk* chunk_addr);
    inline void* resolve(void* frame_addr);
    inline bool is_valid() const;

private:
    static constexpr size_t kMaxChunkDepth = 64;

    void*  origin        = NULL;
    size_t copied_size   = 0;   // bytes actually copied — NOT chunk->size from remote
    size_t data_capacity = 0;
    std::vector<char> data;
    std::unique_ptr<StackChunk> previous = nullptr;

    [[nodiscard]] inline Result<void> update_with_depth(_PyStackChunk* chunk_addr, size_t depth);
};

// ----------------------------------------------------------------------------
Result<void> StackChunk::update(_PyStackChunk* chunk_addr)
{
    return update_with_depth(chunk_addr, 0);
}

// ----------------------------------------------------------------------------
Result<void> StackChunk::update_with_depth(_PyStackChunk* chunk_addr, size_t depth)
{
    if (depth >= kMaxChunkDepth)
    {
        return ErrorKind::StackChunkError;
    }

    _PyStackChunk chunk;

    if (copy_type(chunk_addr, chunk))
    {
        return ErrorKind::StackChunkError;
    }

    // It's possible that the memory we read is corrupted/not valid anymore and the
    // chunk.size is not meaningful. Weed out those cases here to make sure we don't
    // try to allocate absurd amounts of memory.
    if (chunk.size > MAX_CHUNK_SIZE)
    {
        return ErrorKind::StackChunkError;
    }

    origin = chunk_addr;
    // if data_capacity is not enough, reallocate.
    if (chunk.size > data_capacity)
    {
        data_capacity = std::max(chunk.size, data_capacity);
        data.resize(data_capacity);
    }

    // Copy the data up until the size of the chunk
    if (copy_generic(chunk_addr, data.data(), chunk.size))
    {
        return ErrorKind::StackChunkError;
    }

    // Store the size we actually copied. This is critical for bounds checking in resolve().
    // We must NOT read chunk->size from the copied data because a race condition can cause
    // it to be larger than what was actually copied, leading to out-of-bounds access.
    copied_size = chunk.size;

    if (chunk.previous != NULL)
    {
        // Guard against a self-referential chunk pointer (corrupted memory).
        if (chunk.previous == chunk_addr)
        {
            previous = nullptr;
            return Result<void>::ok();
        }

        if (previous == nullptr)
            previous = std::make_unique<StackChunk>();

        auto update_success =
            previous->update_with_depth(reinterpret_cast<_PyStackChunk*>(chunk.previous), depth + 1);
        if (!update_success)
        {
            previous = nullptr;
        }
    }
    else
    {
        previous = nullptr;
    }

    return Result<void>::ok();
}

// ----------------------------------------------------------------------------
void* StackChunk::resolve(void* address)
{
    // If data is not properly initialized, simply return the address
    if (!is_valid())
    {
        return address;
    }

    // Use copied_size for bounds checking, NOT chunk->size from the copied data.
    // A race condition during copying can cause the header's size field to be larger
    // than what was actually copied, leading to out-of-bounds access and SEGV.
    //
    // We also verify the ENTIRE _PyInterpreterFrame fits within the copied buffer,
    // not just the start address. Without this, a frame near the end of the chunk
    // would pass the start-address check but subsequent field accesses (e.g. ->owner,
    // ->instr_ptr) would read past the buffer (heap-buffer-overflow).
#if PY_VERSION_HEX >= 0x030b0000
    constexpr size_t frame_size = sizeof(_PyInterpreterFrame);
#else
    constexpr size_t frame_size = 0;
#endif
    auto origin_char  = reinterpret_cast<char*>(origin);
    auto address_char = reinterpret_cast<char*>(address);

    if (address_char >= origin_char && address_char < origin_char + copied_size)
    {
        if (address_char + frame_size <= origin_char + copied_size)
            return data.data() + (address_char - origin_char);

        // Frame starts in this chunk but extends past what was copied.
        // Return nullptr to signal that this frame cannot be safely read.
        // Do NOT fall through to copy_type on the original address because
        // the chunk memory may have been modified by the target thread since
        // our snapshot, leading to stale pointers and SIGSEGV.
        return nullptr;
    }

    if (previous)
        return previous->resolve(address);

    return address;
}

// ----------------------------------------------------------------------------
bool StackChunk::is_valid() const
{
    return data_capacity > 0 && copied_size > 0
        && copied_size >= sizeof(_PyStackChunk)
        && data.size() >= copied_size
        && data.data() != nullptr && origin != nullptr;
}

// ----------------------------------------------------------------------------

inline std::unique_ptr<StackChunk> stack_chunk = nullptr;
