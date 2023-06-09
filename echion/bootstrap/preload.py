# This file is part of "echion" which is released under MIT.
#
# Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

import echion.core as echion
import atexit
import os
from threading import current_thread, Thread

main_thread = current_thread()
echion.track_thread(main_thread.ident, main_thread.name)

_thread_set_ident = Thread._set_ident
_thread_bootstrap_inner = Thread._bootstrap_inner


def thread_set_ident(self, *args, **kwargs):
    _thread_set_ident(self, *args, **kwargs)
    # This is the point when the thread identifier is set, so we can map it to
    # the thread name.
    echion.track_thread(self.ident, self.name)


def thread_bootstrap_inner(self):
    _thread_bootstrap_inner(self)
    # This is the point when the thread is about to exit, so we can unmap the
    # thread identifier from the thread name.
    echion.untrack_thread(self.ident)


Thread._set_ident = thread_set_ident  # sets the thread name
Thread._bootstrap_inner = thread_bootstrap_inner  # unsets the thread name
# TODO: Patch Thread.name.fset to set the thread name in echion
# TODO: Patching needs to happen on module import, in case we need to perform
#       module clean-up on start-up (e.g. gevent support).


def start_echion():
    if int(os.getenv("ECHION_STEALTH", 0)):
        echion.start_async()
    else:
        Thread(target=echion.start, name="echion.core.sampler", daemon=True).start()


def restart_on_fork():
    # Restart sampling after fork
    echion.stop()
    echion.init()
    start_echion()


os.register_at_fork(after_in_child=restart_on_fork)
atexit.register(echion.stop)

# Configure Echion
echion.set_interval(int(os.getenv("ECHION_INTERVAL", 1000)))
echion.set_cpu(int(os.getenv("ECHION_CPU", 0)))
echion.set_native(int(os.getenv("ECHION_NATIVE", 0)))

# Start sampling
start_echion()
