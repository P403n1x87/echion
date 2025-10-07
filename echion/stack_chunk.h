// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <exception>
#include <memory>
#include <vector>

#include <echion/vm.h>

// ----------------------------------------------------------------------------

class StackChunkError : public std::exception
{
public:
    const char* what() const noexcept override;
};

// ----------------------------------------------------------------------------
class StackChunk
{
public:
    StackChunk() {}

    void update(_PyStackChunk* chunk_addr);
    void* resolve(void* frame_addr);
    bool is_valid() const;

private:
    void* origin = NULL;
    std::vector<char> data;
    size_t data_capacity = 0;
    std::unique_ptr<StackChunk> previous = nullptr;
};

// ----------------------------------------------------------------------------

inline std::unique_ptr<StackChunk> stack_chunk = nullptr;
