// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <exception>
#include <memory>

// ----------------------------------------------------------------------------

class StackChunkError : public std::exception
{
public:
  const char *what() const noexcept override
  {
    return "Cannot create stack chunk object";
  }
};

// ----------------------------------------------------------------------------
class StackChunk
{
public:
  StackChunk(PyThreadState *tstate) : StackChunk((_PyStackChunk *)tstate->datastack_chunk) {}

  void *resolve(void *frame_addr);

private:
  void *origin = NULL;
  std::unique_ptr<char[]> data = nullptr;
  std::unique_ptr<StackChunk> previous = nullptr;

  StackChunk(_PyStackChunk *chunk_addr);
};

// ----------------------------------------------------------------------------
StackChunk::StackChunk(_PyStackChunk *chunk_addr)
{
  _PyStackChunk chunk;

  // Copy the chunk header first
  if (copy_type(chunk_addr, chunk))
    throw StackChunkError();

  origin = chunk_addr;
  data = std::make_unique<char[]>(chunk.size);

  // Copy the full chunk
  if (copy_generic(chunk_addr, data.get(), chunk.size))
    throw StackChunkError();

  if (chunk.previous != NULL)
  {
    try
    {
      previous = std::unique_ptr<StackChunk>{new StackChunk((_PyStackChunk *)chunk.previous)};
    }
    catch (StackChunkError &e)
    {
      previous = nullptr;
    }
  }
}

// ----------------------------------------------------------------------------
void *StackChunk::resolve(void *address)
{
  _PyStackChunk *chunk = (_PyStackChunk *)data.get();

  // Check if this chunk contains the address
  if (address >= origin && address < (char *)origin + chunk->size)
    return (char *)chunk + ((char *)address - (char *)origin);

  if (previous)
    return previous->resolve(address);

  return address;
}

// ----------------------------------------------------------------------------

static std::unique_ptr<StackChunk> stack_chunk = nullptr;
