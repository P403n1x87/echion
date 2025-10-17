#include <echion/signals.h>

void sigprof_handler([[maybe_unused]] int signum)
{
#ifndef UNWIND_NATIVE_DISABLE
    unwind_native_stack();
#endif  // UNWIND_NATIVE_DISABLE
    unwind_python_stack(current_tstate);
    // NOTE: Native stacks for tasks is non-trivial, so we skip it for now.

    sigprof_handler_lock.unlock();
}

void sigquit_handler([[maybe_unused]] int signum)
{
    // Wake up the where thread
    std::lock_guard<std::mutex> lock(where_lock);
    where_cv.notify_one();
}

void install_signals()
{
    signal(SIGQUIT, sigquit_handler);

    if (native)
        signal(SIGPROF, sigprof_handler);
}

void restore_signals()
{
    signal(SIGQUIT, SIG_DFL);

    if (native)
        signal(SIGPROF, SIG_DFL);
}
