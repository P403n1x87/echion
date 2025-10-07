// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#include <exception>
#include <memory>
#include <vector>

#define PY_SSIZE_T_CLEAN
#define Py_BUILD_CORE

#include <Python.h>

#if defined __GNUC__ && defined HAVE_STD_ATOMIC
#undef HAVE_STD_ATOMIC
#endif

#if PY_VERSION_HEX >= 0x030c0000
// https://github.com/python/cpython/issues/108216#issuecomment-1696565797
#undef _PyGC_FINALIZED
#endif

#include <internal/pycore_pystate.h>

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
