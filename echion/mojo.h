// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#include <exception>
#include <ostream>

#define MOJO_VERSION 3

enum MojoEvent
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
using mojo_int_t = long;
using mojo_uint_t = unsigned long;
using mojo_ref_t = unsigned long;
#else
using mojo_int_t = long long;
using mojo_uint_t = unsigned long long;
using mojo_ref_t = unsigned long long;
#endif

// Bitmask to ensure that we encode at most 4 bytes for an integer.
#define MOJO_INT32 ((mojo_ref_t)(1 << (6 + 7 * 3)) - 1)

// ----------------------------------------------------------------------------
class MojoWriter
{
public:
    MojoWriter() {}

    class Error : public std::exception
    {
    };

    // ------------------------------------------------------------------------
    void
    open()
    {
        output.open(std::getenv("ECHION_OUTPUT"));
        if (!output.is_open())
        {
            std::cerr << "Failed to open output file " << std::getenv("ECHION_OUTPUT") << std::endl;
            throw Error();
        }
    }

    // ------------------------------------------------------------------------
    void close()
    {
        std::lock_guard<std::mutex> guard(lock);

        output.flush();
        output.close();
    }

    // ------------------------------------------------------------------------
    void inline header()
    {
        std::lock_guard<std::mutex> guard(lock);

        output << "MOJ";
        integer(MOJO_VERSION);
    }

    // ------------------------------------------------------------------------
    void inline metadata(const std::string &label, const std::string &value)
    {
        std::lock_guard<std::mutex> guard(lock);

        event(MOJO_METADATA);
        string(label);
        string(value);
    }

    // ------------------------------------------------------------------------
    void inline stack(mojo_int_t pid, mojo_int_t iid, const std::string &thread_name)
    {
        std::lock_guard<std::mutex> guard(lock);

        event(MOJO_STACK);
        integer(pid);
        integer(iid);
        string(thread_name);
    }

    // ------------------------------------------------------------------------
    void inline frame(
        mojo_ref_t key,
        mojo_ref_t filename,
        mojo_ref_t name,
        mojo_int_t line,
        mojo_int_t line_end,
        mojo_int_t column,
        mojo_int_t column_end)
    {
        std::lock_guard<std::mutex> guard(lock);

        event(MOJO_FRAME);
        ref(key);
        ref(filename);
        ref(name);
        integer(line);
        integer(line_end);
        integer(column);
        integer(column_end);
    }

    // ------------------------------------------------------------------------
    void inline frame_ref(mojo_ref_t key)
    {
        std::lock_guard<std::mutex> guard(lock);

        if (key == 0)
        {
            event(MOJO_FRAME_INVALID);
        }
        else
        {
            event(MOJO_FRAME_REF);
            ref(key);
        }
    }

    // ------------------------------------------------------------------------
    void inline frame_kernel(const std::string &scope)
    {
        std::lock_guard<std::mutex> guard(lock);

        event(MOJO_FRAME_KERNEL);
        string(scope);
    }

    // ------------------------------------------------------------------------
    void inline metric_time(mojo_int_t value)
    {
        std::lock_guard<std::mutex> guard(lock);

        event(MOJO_METRIC_TIME);
        integer(value);
    }

    // ------------------------------------------------------------------------
    void inline metric_memory(mojo_int_t value)
    {
        std::lock_guard<std::mutex> guard(lock);

        event(MOJO_METRIC_MEMORY);
        integer(value);
    }

    // ------------------------------------------------------------------------
    void inline string(mojo_ref_t key, const std::string &value)
    {
        std::lock_guard<std::mutex> guard(lock);

        event(MOJO_STRING);
        ref(key);
        string(value);
    }

    // ------------------------------------------------------------------------
    void inline string_ref(mojo_ref_t key)
    {
        std::lock_guard<std::mutex> guard(lock);

        event(MOJO_STRING_REF);
        ref(key);
    }

private:
    std::ofstream output;
    std::mutex lock;

    void inline event(MojoEvent event) { output.put((char)event); }
    void inline string(const std::string &string) { output << string << '\0'; }
    void inline string(const char *string) { output << string << '\0'; }
    void inline ref(mojo_ref_t value) { integer(MOJO_INT32 & value); }
    void inline integer(mojo_int_t n)
    {
        mojo_uint_t integer = n < 0 ? -n : n;
        bool sign = n < 0;

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
};

// ----------------------------------------------------------------------------

static MojoWriter mojo;
