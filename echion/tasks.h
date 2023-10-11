// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <exception>
#include <echion/strings.h>

class TaskInfo
{
public:
    class Error : public std::exception
    {
    };

    std::string name;

    TaskInfo(void *);
};

// ----------------------------------------------------------------------------
TaskInfo::TaskInfo(void *task_addr)
{
    try
    {
        name = pyunicode_to_utf8(NULL);
    }
    catch (StringError &)
    {
        throw Error();
    }
}
