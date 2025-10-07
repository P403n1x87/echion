#include <echion/stack_chunk.h>

const char* StackChunkError::what() const noexcept
{
    return "Cannot create stack chunk object";
}

// ----------------------------------------------------------------------------
void StackChunk::update(_PyStackChunk* chunk_addr)
{
    _PyStackChunk chunk;

    if (copy_type(chunk_addr, chunk))
        throw StackChunkError();

    origin = chunk_addr;
    // if data_capacity is not enough, reallocate.
    if (chunk.size > data_capacity)
    {
        data_capacity = std::max(chunk.size, data_capacity);
        data.resize(data_capacity);
    }

    // Copy the data up until the size of the chunk
    if (copy_generic(chunk_addr, data.data(), chunk.size))
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
    // If data is not properly initialized, simply return the address
    if (!is_valid())
    {
        return address;
    }

    _PyStackChunk* chunk = (_PyStackChunk*)data.data();

    // Check if this chunk contains the address
    if (address >= origin && address < (char*)origin + chunk->size)
        return (char*)chunk + ((char*)address - (char*)origin);

    if (previous)
        return previous->resolve(address);

    return address;
}

// ----------------------------------------------------------------------------
bool StackChunk::is_valid() const
{
    return data_capacity > 0 &&
           data.size() > 0 &&
           data.size() >= sizeof(_PyStackChunk) &&
           data.data() != nullptr &&
           origin != nullptr;
}
