#include <echion/greenlets.h>

GreenletInfo::GreenletInfo(ID id, PyObject* frame, StringTable::Key name)
    : greenlet_id(id), frame(frame), name(name)
{
}

// ----------------------------------------------------------------------------

int GreenletInfo::unwind(PyObject* frame, PyThreadState* tstate, FrameStack& stack)
{
    PyObject* frame_addr = NULL;
#if PY_VERSION_HEX >= 0x030d0000
    frame_addr =
        frame == Py_None
            ? (PyObject*)tstate->current_frame
            : reinterpret_cast<PyObject*>(reinterpret_cast<struct _frame*>(frame)->f_frame);
#elif PY_VERSION_HEX >= 0x030b0000
    if (frame == Py_None)
    {
        _PyCFrame cframe;
        _PyCFrame* cframe_addr = tstate->cframe;
        if (copy_type(cframe_addr, cframe))
            // TODO: Invalid frame
            return 0;

        frame_addr = (PyObject*)cframe.current_frame;
    }
    else
    {
        frame_addr = reinterpret_cast<PyObject*>(reinterpret_cast<struct _frame*>(frame)->f_frame);
    }

#else  // Python < 3.11
    frame_addr = frame == Py_None ? (PyObject*)tstate->frame : frame;
#endif
    auto count = unwind_frame(frame_addr, stack);

    stack.push_back(Frame::get(name));

    return count + 1;  // We add an extra count for the frame with the greenlet
                       // name.
}
