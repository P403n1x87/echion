#include <echion/config.h>

#include <Python.h>

PyObject* set_interval(PyObject* Py_UNUSED(m), PyObject* args)
{
    unsigned int new_interval;
    if (!PyArg_ParseTuple(args, "I", &new_interval))
        return NULL;

    interval = new_interval;

    Py_RETURN_NONE;
}

void _set_cpu(int new_cpu)
{
    cpu = new_cpu;
}

void _set_ignore_non_running_threads(bool new_ignore_non_running_threads)
{
    ignore_non_running_threads = new_ignore_non_running_threads;
}

PyObject* set_cpu(PyObject* Py_UNUSED(m), PyObject* args)
{
    int new_cpu;
    if (!PyArg_ParseTuple(args, "p", &new_cpu))
        return NULL;

    _set_cpu(new_cpu);

    Py_RETURN_NONE;
}

PyObject* set_memory(PyObject* Py_UNUSED(m), PyObject* args)
{
    int new_memory;
    if (!PyArg_ParseTuple(args, "p", &new_memory))
        return NULL;

    memory = new_memory;

    Py_RETURN_NONE;
}

PyObject* set_native(PyObject* Py_UNUSED(m), PyObject* args)
{
#ifndef UNWIND_NATIVE_DISABLE
    int new_native;
    if (!PyArg_ParseTuple(args, "p", &new_native))
        return NULL;

    native = new_native;
#else
    PyErr_SetString(PyExc_RuntimeError,
                    "Native profiling is disabled, please re-build/install echion without "
                    "UNWIND_NATIVE_DISABLE env var/preprocessor flag");
    return NULL;
#endif  // UNWIND_NATIVE_DISABLE
    Py_RETURN_NONE;
}

PyObject* set_where(PyObject* Py_UNUSED(m), PyObject* args)
{
    int value;
    if (!PyArg_ParseTuple(args, "p", &value))
        return NULL;

    where = value;

    Py_RETURN_NONE;
}

PyObject* set_pipe_name(PyObject* Py_UNUSED(m), PyObject* args)
{
    const char* name;
    if (!PyArg_ParseTuple(args, "s", &name))
        return NULL;

    pipe_name = name;

    Py_RETURN_NONE;
}

PyObject* set_max_frames(PyObject* Py_UNUSED(m), PyObject* args)
{
    unsigned int new_max_frames;
    if (!PyArg_ParseTuple(args, "I", &new_max_frames))
        return NULL;

    max_frames = new_max_frames;

    Py_RETURN_NONE;
}
