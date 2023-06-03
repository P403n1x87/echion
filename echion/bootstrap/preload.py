# This file is part of "echion" which is released under MIT.
#
# Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

import atexit
import os
from threading import Thread

import echion.core as ec

# TODO: Trigger patching on import of the relevant modules
import echion.monkey.asyncio  # noqa
import echion.monkey.threading  # noqa


def start_echion():
    if int(os.getenv("ECHION_STEALTH", 0)):
        ec.start_async()
    else:
        Thread(target=ec.start, name="echion.core.sampler", daemon=True).start()


def restart_on_fork():
    # Restart sampling after fork
    ec.stop()
    ec.init()
    start_echion()


os.register_at_fork(after_in_child=restart_on_fork)
atexit.register(ec.stop)

# Configure Echion
ec.set_interval(int(os.getenv("ECHION_INTERVAL", 1000)))
ec.set_cpu(bool(int(os.getenv("ECHION_CPU", 0))))
ec.set_native(bool(int(os.getenv("ECHION_NATIVE", 0))))
ec.set_where(bool(int(os.getenv("ECHION_WHERE", 0))))

# Start sampling
start_echion()
