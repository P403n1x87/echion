// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#if PY_VERSION_HEX >= 0x030b0000
#include <cpython/genobject.h>

#define Py_BUILD_CORE
#include <internal/pycore_opcode.h>
#else
#include <genobject.h>
#include <opcode.h>
#endif

#include <utility>

#include <echion/vm.h>

extern "C"
{

    typedef enum
    {
        STATE_PENDING,
        STATE_CANCELLED,
        STATE_FINISHED
    } fut_state;

#if PY_VERSION_HEX >= 0x030b0000
#define FutureObj_HEAD(prefix)       \
    PyObject_HEAD                    \
        PyObject *prefix##_loop;     \
    PyObject *prefix##_callback0;    \
    PyObject *prefix##_context0;     \
    PyObject *prefix##_callbacks;    \
    PyObject *prefix##_exception;    \
    PyObject *prefix##_exception_tb; \
    PyObject *prefix##_result;       \
    PyObject *prefix##_source_tb;    \
    PyObject *prefix##_cancel_msg;   \
    fut_state prefix##_state;        \
    int prefix##_log_tb;             \
    int prefix##_blocking;           \
    PyObject *dict;                  \
    PyObject *prefix##_weakreflist;  \
    PyObject *prefix##_cancelled_exc;

#elif PY_VERSION_HEX >= 0x030a0000
#define FutureObj_HEAD(prefix)       \
    PyObject_HEAD                    \
        PyObject *prefix##_loop;     \
    PyObject *prefix##_callback0;    \
    PyObject *prefix##_context0;     \
    PyObject *prefix##_callbacks;    \
    PyObject *prefix##_exception;    \
    PyObject *prefix##_exception_tb; \
    PyObject *prefix##_result;       \
    PyObject *prefix##_source_tb;    \
    PyObject *prefix##_cancel_msg;   \
    fut_state prefix##_state;        \
    int prefix##_log_tb;             \
    int prefix##_blocking;           \
    PyObject *dict;                  \
    PyObject *prefix##_weakreflist;  \
    _PyErr_StackItem prefix##_cancelled_exc_state;

#elif PY_VERSION_HEX >= 0x03090000
#define FutureObj_HEAD(prefix)      \
    PyObject_HEAD                   \
        PyObject *prefix##_loop;    \
    PyObject *prefix##_callback0;   \
    PyObject *prefix##_context0;    \
    PyObject *prefix##_callbacks;   \
    PyObject *prefix##_exception;   \
    PyObject *prefix##_result;      \
    PyObject *prefix##_source_tb;   \
    PyObject *prefix##_cancel_msg;  \
    fut_state prefix##_state;       \
    int prefix##_log_tb;            \
    int prefix##_blocking;          \
    PyObject *dict;                 \
    PyObject *prefix##_weakreflist; \
    _PyErr_StackItem prefix##_cancelled_exc_state;

#else
#define FutureObj_HEAD(prefix)    \
    PyObject_HEAD                 \
        PyObject *prefix##_loop;  \
    PyObject *prefix##_callback0; \
    PyObject *prefix##_context0;  \
    PyObject *prefix##_callbacks; \
    PyObject *prefix##_exception; \
    PyObject *prefix##_result;    \
    PyObject *prefix##_source_tb; \
    fut_state prefix##_state;     \
    int prefix##_log_tb;          \
    int prefix##_blocking;        \
    PyObject *dict;               \
    PyObject *prefix##_weakreflist;
#endif

    typedef struct
    {
        FutureObj_HEAD(fut)
    } FutureObj;

#if PY_VERSION_HEX >= 0x030a0000
    typedef struct
    {
        FutureObj_HEAD(task)
            PyObject *task_fut_waiter;
        PyObject *task_coro;
        PyObject *task_name;
        PyObject *task_context;
        int task_must_cancel;
        int task_log_destroy_pending;
        int task_num_cancels_requested;
    } TaskObj;

#else
    typedef struct
    {
        FutureObj_HEAD(task)
            PyObject *task_fut_waiter;
        PyObject *task_coro;
        PyObject *task_name;
        PyObject *task_context;
        int task_must_cancel;
        int task_log_destroy_pending;
    } TaskObj;
#endif

    // ---- cr_await ----

#if PY_VERSION_HEX >= 0x030c0000
#define RESUME_QUICK INSTRUMENTED_RESUME
#endif

#if PY_VERSION_HEX >= 0x030b0000
    PyObject *
    PyGen_yf(PyGenObject *gen, PyObject *frame_addr)
    {
        PyObject *yf = NULL;

        if (gen->gi_frame_state < FRAME_CLEARED)
        {
            if (gen->gi_frame_state == FRAME_CREATED)
                return NULL;

            _PyInterpreterFrame frame;
            if (copy_type(frame_addr, frame))
                return NULL;

            _Py_CODEUNIT next;
            if (copy_type(frame.prev_instr + 1, next))
                return NULL;
            if (!(_Py_OPCODE(next) == RESUME || _Py_OPCODE(next) == RESUME_QUICK) || _Py_OPARG(next) < 2)
                return NULL;

            auto localsplus = std::make_unique<PyObject *[]>(frame.stacktop);
            if (copy_generic(frame.localsplus, localsplus.get(), frame.stacktop * sizeof(PyObject *)))
                return NULL;

            yf = localsplus[frame.stacktop - 1];
        }

        return yf;
    }

#elif PY_VERSION_HEX >= 0x030a0000
    PyObject *
    PyGen_yf(PyGenObject *gen, PyObject *frame_addr)
    {
        PyObject *yf = NULL;
        PyFrameObject *f = (PyFrameObject *)frame_addr;

        if (f)
        {
            PyFrameObject frame;
            if (copy_type(f, frame))
                return NULL;

            if (frame.f_lasti < 0)
                return NULL;

            PyCodeObject code;
            if (copy_type(frame.f_code, code))
                return NULL;

            Py_ssize_t s = 0;
            auto c = pybytes_to_bytes_and_size(code.co_code, &s);

            if (c[(frame.f_lasti + 1) * sizeof(_Py_CODEUNIT)] != YIELD_FROM)
                return NULL;

            ssize_t nvalues = frame.f_stackdepth;
            auto stack = std::make_unique<PyObject *[]>(nvalues);
            if (copy_generic(frame.f_valuestack, stack.get(), nvalues * sizeof(PyObject *)))
                return NULL;

            yf = stack[nvalues - 1];
        }

        return yf;
    }

#else
    PyObject *
    PyGen_yf(PyGenObject *gen, PyObject *frame_addr)
    {
        PyObject *yf = NULL;
        PyFrameObject *f = (PyFrameObject *)frame_addr;

        if (frame_addr == NULL)
            return NULL;

        PyFrameObject frame;
        if (copy_type(f, frame))
            return NULL;

        if (frame.f_stacktop)
        {
            if (frame.f_lasti < 0)
                return NULL;

            PyCodeObject code;
            if (copy_type(frame.f_code, code))
                return NULL;

            Py_ssize_t s = 0;
            auto c = pybytes_to_bytes_and_size(code.co_code, &s);

            if (c[f->f_lasti + sizeof(_Py_CODEUNIT)] != YIELD_FROM)
                return NULL;

            auto stacktop = std::make_unique<PyObject *>();
            if (copy_generic(frame.f_stacktop - 1, stacktop.get(), sizeof(PyObject *)))
                return NULL;

            yf = *stacktop;
        }

        return yf;
    }
#endif
}
