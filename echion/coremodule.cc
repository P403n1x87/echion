// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#if PY_VERSION_HEX >= 0x030c0000
// https://github.com/python/cpython/issues/108216#issuecomment-1696565797
#undef _PyGC_FINALIZED
#endif

#include <echion/tasks.h>

// ----------------------------------------------------------------------------
static PyObject *
start(PyObject *Py_UNUSED(m), PyObject *Py_UNUSED(args))
{
    try
    {
        auto a = new TaskInfo(NULL);
    }
    catch (const TaskInfo::Error &)
    {
    }

    Py_RETURN_NONE;
}

// ----------------------------------------------------------------------------
static PyMethodDef echion_core_methods[] = {
    {"start", start, METH_NOARGS, "Start the stack sampler"},
    {NULL, NULL, 0, NULL} /* Sentinel */
};

// ----------------------------------------------------------------------------
static struct PyModuleDef coremodule = {
    PyModuleDef_HEAD_INIT,
    "core", /* name of module */
    NULL,   /* module documentation, may be NULL */
    -1,     /* size of per-interpreter state of the module,
               or -1 if the module keeps state in global variables. */
    echion_core_methods,
};

// ----------------------------------------------------------------------------
PyMODINIT_FUNC
PyInit_core(void)
{
    PyObject *m;

    m = PyModule_Create(&coremodule);
    if (m == NULL)
        return NULL;

    return m;
}
