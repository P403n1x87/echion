import typing as t
from asyncio import tasks
from asyncio.events import BaseDefaultEventLoopPolicy
from functools import wraps
from threading import current_thread

import echion.core as echion


# -----------------------------------------------------------------------------

set_event_loop = BaseDefaultEventLoopPolicy.set_event_loop


@wraps(set_event_loop)
def _set_event_loop(self, loop) -> None:
    echion.track_asyncio_loop(t.cast(int, current_thread().ident), loop)
    return set_event_loop(self, loop)


BaseDefaultEventLoopPolicy.set_event_loop = _set_event_loop  # type: ignore[method-assign]

# -----------------------------------------------------------------------------

_gather = tasks._GatheringFuture.__init__  # type: ignore[attr-defined]


@wraps(_gather)
def gather(self, children, *, loop):
    try:
        return _gather(self, children, loop=loop)
    finally:
        # Link the parent gathering task to the gathered children
        parent = tasks.current_task(loop)
        for child in children:
            echion.link_tasks(parent, child)


tasks._GatheringFuture.__init__ = gather  # type: ignore[attr-defined]

# -----------------------------------------------------------------------------

echion.init_asyncio(tasks._current_tasks, tasks._all_tasks.data)  # type: ignore[attr-defined]
