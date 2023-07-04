# This file is part of "echion" which is released under MIT.
#
# Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

import typing as t
from threading import Thread
from threading import _active

import echion.core as echion


_thread_set_ident = Thread._set_ident  # type: ignore[attr-defined]
_thread_bootstrap_inner = Thread._bootstrap_inner  # type: ignore[attr-defined]


def thread_set_ident(self, *args: t.Any, **kwargs: t.Any) -> None:
    _thread_set_ident(self, *args, **kwargs)
    # This is the point when the thread identifier is set, so we can map it to
    # the thread name.
    echion.track_thread(self.ident, self.name)


def thread_bootstrap_inner(self) -> None:
    _thread_bootstrap_inner(self)
    # This is the point when the thread is about to exit, so we can unmap the
    # thread identifier from the thread name.
    echion.untrack_thread(self.ident)


def patch():
    Thread._set_ident = thread_set_ident  # type: ignore[attr-defined]
    Thread._bootstrap_inner = thread_bootstrap_inner  # type: ignore[attr-defined]

    for thread_id, thread in _active.items():
        echion.track_thread(thread_id, thread.name)

    # TODO: Patch Thread.name.fset to set the thread name in echion
    # TODO: Patching needs to happen on module import, in case we need to perform
    #       module clean-up on start-up (e.g. gevent support).


def unpatch():
    Thread._set_ident = _thread_set_ident  # type: ignore[attr-defined]
    Thread._bootstrap_inner = _thread_bootstrap_inner  # type: ignore[attr-defined]
