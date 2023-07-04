# This file is part of "echion" which is released under MIT.
#
# Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

import atexit
import os
import sys
from threading import Thread

import echion.core as ec


# We cannot unregister the fork hook, so we use this flag instead
do_on_fork = True


def restart_on_fork():
    global do_on_fork
    if not do_on_fork:
        return

    # Restart sampling after fork
    ec.stop()
    ec.init()
    start()


def start():
    global do_on_fork

    # Set the configuration
    ec.set_interval(int(os.getenv("ECHION_INTERVAL", 1000)))
    ec.set_cpu(bool(int(os.getenv("ECHION_CPU", 0))))
    ec.set_native(bool(int(os.getenv("ECHION_NATIVE", 0))))
    ec.set_where(bool(int(os.getenv("ECHION_WHERE", 0) or 0)))

    # Monkey-patch the standard library
    for module in ("echion.monkey.asyncio", "echion.monkey.threading"):
        __import__(module)
        sys.modules[module].patch()

    do_on_fork = True
    os.register_at_fork(after_in_child=restart_on_fork)
    atexit.register(stop)

    if int(os.getenv("ECHION_STEALTH", 0)):
        ec.start_async()
    else:
        Thread(target=ec.start, name="echion.core.sampler", daemon=True).start()


def stop():
    global do_on_fork

    ec.stop()

    for module in ("echion.monkey.asyncio", "echion.monkey.threading"):
        sys.modules[module].unpatch()

    atexit.unregister(stop)
    do_on_fork = False
