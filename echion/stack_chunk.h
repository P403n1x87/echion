// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <exception>
#include <memory>

#include <echion/vm.h>

// ----------------------------------------------------------------------------

class StackChunkError : public std::exception
{
public:
    const char* what() const noexcept override
    {
        return "Cannot create stack chunk object";
    }
};

// ----------------------------------------------------------------------------
class StackChunk
{
public:
    StackChunk() {}

    inline void update(_PyStackChunk* chunk_addr);
    inline void* resolve(void* frame_addr);

private:
    void* origin = NULL;
    struct FreeDeleter
    {
        void operator()(void* ptr) const
        {
            free(ptr);
        }
    };
    std::unique_ptr<char[], FreeDeleter> data = nullptr;
    size_t data_capacity = 0;
    std::unique_ptr<StackChunk> previous = nullptr;
};

// ----------------------------------------------------------------------------
void StackChunk::update(_PyStackChunk* chunk_addr)
{
    _PyStackChunk chunk;

    if (copy_type(chunk_addr, chunk))
        throw StackChunkError();

    origin = chunk_addr;
    // if data_size is not enough, reallocate
    if (chunk.size > data_capacity)
    {
        data_capacity = chunk.size;
        char* new_data = (char*)realloc(data.get(), data_capacity);
        if (!new_data)
        {
            throw StackChunkError();
        }
        data.release();  // Release the old pointer before resetting
        data.reset(new_data);
    }

    // Copy the data up until the size of the chunk
    if (copy_generic(chunk_addr, data.get(), chunk.size))
        throw StackChunkError();

    if (chunk.previous != NULL)
    {
        try
        {
            if (previous == nullptr)
                previous = std::make_unique<StackChunk>();
            previous->update((_PyStackChunk*)chunk.previous);
        }
        catch (StackChunkError& e)
        {
            previous = nullptr;
        }
    }
}

// ----------------------------------------------------------------------------
void* StackChunk::resolve(void* address)
{
    _PyStackChunk* chunk = (_PyStackChunk*)data.get();

    // Check if this chunk contains the address
    if (address >= origin && address < (char*)origin + chunk->size)
        return (char*)chunk + ((char*)address - (char*)origin);

    if (previous)
        return previous->resolve(address);

    return address;
}

// ----------------------------------------------------------------------------

inline std::unique_ptr<StackChunk> stack_chunk = nullptr;
