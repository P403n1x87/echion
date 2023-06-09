// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#include <iostream>
#include <stack>
#include <vector>

#include <echion/stacks.h>

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
