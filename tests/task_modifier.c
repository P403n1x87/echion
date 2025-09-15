#define PY_SSIZE_T_CLEAN
#include <Python.h>

typedef enum {
    FUTURE_PENDING = 0,
    FUTURE_CANCELLED = 1,
    FUTURE_FINISHED = 2
} fut_state;

//#if PY_VERSION_HEX >= 0x030d0000
#define FutureObj_HEAD(prefix)                                  \
    PyObject_HEAD PyObject* prefix##_loop;                      \
    PyObject* prefix##_callback0;                               \
    PyObject* prefix##_context0;                                \
    PyObject* prefix##_callbacks;                               \
    PyObject* prefix##_exception;                               \
    PyObject* prefix##_exception_tb;                            \
    PyObject* prefix##_result;                                  \
    PyObject* prefix##_source_tb;                               \
    PyObject* prefix##_cancel_msg;                              \
    PyObject* prefix##_cancelled_exc;                           \
    fut_state prefix##_state;                                   \
    /* These bitfields need to be at the end of the struct      \
       so that these and bitfields from TaskObj are contiguous. \
    */                                                          \
    unsigned prefix##_log_tb : 1;                               \
    unsigned prefix##_blocking : 1;

//typedef struct {
//    Py_ssize_t ob_refcnt;
//    PyTypeObject *ob_type;
//    PyObject *task_loop;
//    PyObject *task_callbacks;
//    PyObject *task_exception;
//    PyObject *task_result;
//    PyObject *task_source_tb;
//    fut_state task_state;
//    int task_log_tb;
//    int task_blocking;
//    PyObject *dict;
//    PyObject *task_weakreflist;
//    unsigned task_must_cancel: 1;
//    unsigned task_log_destroy_pending: 1;
//    int task_num_cancels_requested;
//    PyObject *task_fut_waiter;
//    PyObject *task_coro;
//    PyObject *task_name;
//    PyObject *task_context;
//    void *task_node_next;
//    void *task_node_prev;
//#ifdef Py_GIL_DISABLED
//    uintptr_t task_tid;
//#endif
//} TaskObj;

typedef struct
{
    FutureObj_HEAD(task);
    unsigned task_must_cancel : 1;
    unsigned task_log_destroy_pending : 1;
    int task_num_cancels_requested;
    PyObject* task_fut_waiter;
    PyObject* task_coro;
    PyObject* task_name;
    PyObject* task_context;
} TaskObj;

static PyObject*
set_task_name_to_big_int(PyObject *self, PyObject *args)
{
    PyObject *task_obj;

    if (!PyArg_ParseTuple(args, "O", &task_obj)) {
        return NULL;
    }

    if (!PyObject_IsInstance(task_obj, (PyObject*)&PyBaseObject_Type)) {
        PyErr_SetString(PyExc_TypeError, "Expected a Task object");
        return NULL;
    }

    TaskObj *task = (TaskObj *)task_obj;

    PyObject *old_name = task->task_name;
    task->task_name = PyLong_FromString("10_000_000_000_000_000_000_000_000_000_000_000_000", NULL, 10);

    if (task->task_name == NULL) {
        task->task_name = old_name;
        return NULL;
    }

    Py_XDECREF(old_name);

    Py_RETURN_NONE;
}

static PyMethodDef TaskModifierMethods[] = {
    {"set_task_name_to_big_int", set_task_name_to_big_int, METH_VARARGS,
     "Set task_name field of asyncio Task to PyLongObject with value 10_000_000_000_000_000_000_000_000_000_000_000_000"},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef taskmodifiermodule = {
    PyModuleDef_HEAD_INIT,
    "task_modifier",
    "Module to modify asyncio Task internal fields",
    -1,
    TaskModifierMethods
};

PyMODINIT_FUNC
PyInit_task_modifier(void)
{
    return PyModule_Create(&taskmodifiermodule);
}
