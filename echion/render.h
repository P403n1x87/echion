// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#include <iostream>
#include <stack>
#include <vector>

#include <echion/stacks.h>
#include <echion/threads.h>

// ----------------------------------------------------------------------------
static void render(FrameStack &stack, std::ostream &output)
{
    for (auto it = stack.rbegin(); it != stack.rend(); ++it)
    {
#if PY_VERSION_HEX >= 0x030c0000
        if ((*it).get().is_entry)
            // This is a shim frame so we skip it.
            continue;
#endif
        (*it).get().render(output);
    }
}

// ----------------------------------------------------------------------------
static void render_where(ThreadInfo &info, FrameStack &stack, std::ostream &output)
{
    output << "    🧵 " << info.name << ":" << std::endl;

    for (auto it = stack.rbegin(); it != stack.rend(); ++it)
        (*it).get().render_where(output);
}
