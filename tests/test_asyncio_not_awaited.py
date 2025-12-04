import sys

from tests.utils import PY, DataSummary, run_target, dump_summary


def test_asyncio_not_awaited_wall_time() -> None:
    result, data = run_target("target_async_not_awaited")
    assert result.returncode == 0, result.stderr.decode()

    assert data is not None
    md = data.metadata
    assert md["mode"] == "wall"
    assert md["interval"] == "1000"

    summary = DataSummary(data)
    dump_summary(summary, "summary_asyncio_not_awaited.json")

    # We should see a stack for Task-1 / parent / Task-not_awaited / not_awaited / sleep
    # Even though Task-1 does not await Task-not_awaited, the fact that there is a weak (parent - child) link
    # means that Task-not_awaited is under Task-1.
    summary.assert_substack(
        "0:MainThread",
        (
            "Task-1",
            "parent",
            "Task-not_awaited",
            "func_not_awaited",
            "sleep",
        ),
        lambda v: v > 0.0,
    )

    # We should see a stack for Task-1 / parent / Task-awaited / awaited / sleep
    # That is because Task-1 is awaiting Task-awaited.
    summary.assert_substack(
        "0:MainThread",
        (
            "Task-1",
            "parent",
            "Task-awaited",
            "func_awaited",
            "sleep",
        ),
        lambda v: v > 0.0,
    )

    # We should never see the Task-not_awaited Frame without Task-1 up in the Stack
    # or it would mean we are not properly following parent-child links.
    summary.assert_not_substack(
        "0:MainThread",
        (
            (
                ("KqueueSelector" if sys.platform == "darwin" else "EpollSelector")
                if PY >= (3, 11)
                else ""
            )
            + "select",
            "Task-not_awaited",
            "func_not_awaited",
            "sleep",
        ),
    )
