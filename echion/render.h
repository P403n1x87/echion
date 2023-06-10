// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#include <iostream>
#include <stack>
#include <vector>

#include <echion/stacks.h>
#include <echion/threadinfo.h>

// ----------------------------------------------------------------------------
static void render(std::stack<Frame *> &stack, std::ostream &output)
{
    while (!stack.empty())
    {
        Frame *frame = stack.top();
        stack.pop();

        frame->render(output);

        delete frame;
    }
}

// ----------------------------------------------------------------------------
static void render(std::vector<Frame *> &stack, std::ostream &output)
{
    for (Frame *frame : stack)
    {
        frame->render(output);
        delete frame;
    }
    stack.clear();
}

// ----------------------------------------------------------------------------
static void render_where(ThreadInfo *info, std::stack<Frame *> &stack, std::ostream &output)
{
    output << "    🧵 " << info->name << ":" << std::endl;
    while (!stack.empty())
    {
        Frame *frame = stack.top();
        stack.pop();

        frame->render_where(output);

        delete frame;
    }
}

// ----------------------------------------------------------------------------
static void render_where(ThreadInfo *info, std::vector<Frame *> &stack, std::ostream &output)
{
    output << "    🧵 \033[1m" << info->name << "\033[0m " << (info->is_running() ? "🏃" : "😴") << std::endl
           << std::endl;
    for (Frame *frame : stack)
    {
        frame->render_where(output);

        delete frame;
    }
    stack.clear();
}
