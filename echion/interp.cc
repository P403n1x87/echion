#include <echion/interp.h>

void for_each_interp(std::function<void(InterpreterInfo& interp)> callback)
{
    InterpreterInfo interpreter_info = {0};

    for (char* interp_addr = (char*)runtime->interpreters.head; interp_addr != NULL;
         interp_addr = (char*)interpreter_info.next)
    {
        if (copy_type(interp_addr + offsetof(PyInterpreterState, id), interpreter_info.id))
            continue;
#if PY_VERSION_HEX >= 0x030b0000
        if (copy_type(interp_addr + offsetof(PyInterpreterState, threads.head),
#else
        if (copy_type(interp_addr + offsetof(PyInterpreterState, tstate_head),
#endif
                      interpreter_info.tstate_head))
            continue;
        if (copy_type(interp_addr + offsetof(PyInterpreterState, next), interpreter_info.next))
            continue;

        callback(interpreter_info);
    };
}
