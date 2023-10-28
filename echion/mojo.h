// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#include <echion/config.h>

#define MOJO_VERSION 3

enum
{
    MOJO_RESERVED,
    MOJO_METADATA,
    MOJO_STACK,
    MOJO_FRAME,
    MOJO_FRAME_INVALID,
    MOJO_FRAME_REF,
    MOJO_FRAME_KERNEL,
    MOJO_GC,
    MOJO_IDLE,
    MOJO_METRIC_TIME,
    MOJO_METRIC_MEMORY,
    MOJO_STRING,
    MOJO_STRING_REF,
    MOJO_MAX,
};

#if defined __arm__
typedef unsigned long mojo_int_t;
#else
typedef unsigned long long mojo_int_t;
#endif

// Bitmask to ensure that we encode at most 4 bytes for an integer.
#define MOJO_INT32 ((mojo_int_t)(1 << (6 + 7 * 3)) - 1)

// Primitives

#define mojo_event(event)        \
    {                            \
        output.put((char)event); \
    }

#define mojo_string(string) \
    output << string;       \
    output.put('\0');

static inline void
mojo_integer(mojo_int_t integer, int sign)
{
    unsigned char byte = integer & 0x3f;
    if (sign)
        byte |= 0x40;

    integer >>= 6;
    if (integer)
        byte |= 0x80;

    output.put(byte);

    while (integer)
    {
        byte = integer & 0x7f;
        integer >>= 7;
        if (integer)
            byte |= 0x80;
        output.put(byte);
    }
}

// We expect the least significant bits to be varied enough to provide a valid
// key. This way we can keep the size of references to a maximum of 4 bytes.
#define mojo_ref(integer) (mojo_integer(MOJO_INT32 & ((mojo_int_t)integer), 0))

// Mojo events

#define mojo_header()                  \
    {                                  \
        output << "MOJ";               \
        mojo_integer(MOJO_VERSION, 0); \
        output.flush();                \
    }

#define mojo_metadata(label, value) \
    mojo_event(MOJO_METADATA);      \
    mojo_string(label);             \
    mojo_string(value);

#define mojo_stack(pid, iid, tid) \
    mojo_event(MOJO_STACK);       \
    mojo_integer(pid, 0);         \
    mojo_integer(iid, 0);         \
    output << std::hex << tid;    \
    output.put('\0');

#define mojo_frame(key, frame)                 \
    mojo_event(MOJO_FRAME);                    \
    mojo_integer(frame->cache_key, 0);         \
    mojo_ref(frame->filename);                 \
    mojo_ref(frame->name);                     \
    mojo_integer(frame->location.line, 0);     \
    mojo_integer(frame->location.line_end, 0); \
    mojo_integer(frame->location.column, 0);   \
    mojo_integer(frame->location.column_end, 0);

static inline void
mojo_frame_ref(mojo_int_t key)
{
    if (key == 0)
    {
        mojo_event(MOJO_FRAME_INVALID);
    }
    else
    {
        mojo_event(MOJO_FRAME_REF);
        mojo_integer(key, 0);
    }
}

#define mojo_frame_kernel(scope)   \
    mojo_event(MOJO_FRAME_KERNEL); \
    mojo_string(scope);

#define mojo_metric_time(value)   \
    mojo_event(MOJO_METRIC_TIME); \
    mojo_integer(value, 0);

#define mojo_metric_memory(value)   \
    mojo_event(MOJO_METRIC_MEMORY); \
    mojo_integer(value < 0 ? -value : value, value < 0);

#define mojo_string_event(key, string) \
    mojo_event(MOJO_STRING);           \
    mojo_ref(key);                     \
    mojo_string(string);

#define mojo_string_ref(key)     \
    mojo_event(MOJO_STRING_REF); \
    mojo_ref(key);
