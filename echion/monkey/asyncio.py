from __future__ import annotations

import asyncio
import sys
import typing as t
from asyncio import tasks
from asyncio.events import BaseDefaultEventLoopPolicy
from functools import wraps
from threading import current_thread
from typing import Iterator

import echion.core as echion


# -----------------------------------------------------------------------------

_set_event_loop = BaseDefaultEventLoopPolicy.set_event_loop


@wraps(_set_event_loop)
def set_event_loop(self, loop) -> None:
    echion.track_asyncio_loop(t.cast(int, current_thread().ident), loop)
    return _set_event_loop(self, loop)


# -----------------------------------------------------------------------------

_gather = tasks._GatheringFuture.__init__  # type: ignore[attr-defined]


@wraps(_gather)
def gather(self, children, *, loop):
    # Link the parent gathering task to the gathered children
    parent = tasks.current_task(loop)

    assert parent is not None

    for child in children:
        echion.link_tasks(parent, child)

    return _gather(self, children, loop=loop)


# -----------------------------------------------------------------------------

_wait = tasks._wait  # type: ignore[attr-defined]

T = t.TypeVar("T")


@wraps(tasks._wait)  # type: ignore[attr-defined]
def wait(
    fs: t.Iterable[asyncio.Future[T]],
    timeout: t.Optional[float],
    return_when,
    loop: asyncio.AbstractEventLoop,
) -> t.Coroutine[t.Any, t.Any, tuple[t.Set[t.Any], t.Set[t.Any]]]:
    parent = tasks.current_task(loop)
    assert parent is not None

    for child in fs:
        echion.link_tasks(parent, t.cast(asyncio.Task, child))

    return _wait(fs, timeout, return_when, loop)


# -----------------------------------------------------------------------------

_as_completed = tasks.as_completed


@wraps(_as_completed)
def as_completed(
    fs: t.Iterable[asyncio.Future[T]], *args: t.Any, **kwargs: t.Any
) -> Iterator[asyncio.Future[T]]:
    # Link the parent task to the tasks being waited on
    loop = kwargs.get("loop")
    parent = tasks.current_task(loop)

    if parent is not None:
        futures = {asyncio.ensure_future(f, loop=loop) for f in set(fs)}
        for child in futures:
            echion.link_tasks(parent, t.cast(asyncio.Task, child))

    return _as_completed(futures, *args, **kwargs)


# -----------------------------------------------------------------------------


def patch():
    BaseDefaultEventLoopPolicy.set_event_loop = set_event_loop  # type: ignore[method-assign]
    tasks._GatheringFuture.__init__ = gather  # type: ignore[attr-defined]
    tasks._wait = wait  # type: ignore[attr-defined]
    tasks.as_completed = as_completed  # type: ignore[attr-defined]
    if hasattr(asyncio, "as_completed"):
        asyncio.as_completed = as_completed


def unpatch():
    BaseDefaultEventLoopPolicy.set_event_loop = _set_event_loop  # type: ignore[method-assign]
    tasks._GatheringFuture.__init__ = _gather  # type: ignore[attr-defined]
    tasks._wait = _wait  # type: ignore[attr-defined]
    tasks.as_completed = _as_completed  # type: ignore[attr-defined]
    if hasattr(asyncio, "as_completed"):
        asyncio.as_completed = _as_completed


def track():
    if sys.hexversion >= 0x030C0000:
        scheduled_tasks = (
            tasks._scheduled_tasks.data  # pyright: ignore[reportAttributeAccessIssue]
        )
        eager_tasks = tasks._eager_tasks  # pyright: ignore[reportAttributeAccessIssue]
    else:
        scheduled_tasks = (
            tasks._all_tasks.data  # pyright: ignore[reportAttributeAccessIssue]
        )
        eager_tasks = None

    echion.init_asyncio(tasks._current_tasks, scheduled_tasks, eager_tasks)  # type: ignore[attr-defined]
