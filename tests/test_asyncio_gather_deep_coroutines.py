from tests.utils import DataSummary
from tests.utils import run_target


def test_asyncio_gather_deep_coroutines():
    result, data = run_target("target_gather_coroutines_deep")
    assert result.returncode == 0, result.stderr.decode()

    assert data is not None
    md = data.metadata
    assert md["mode"] == "wall"
    assert md["interval"] == "1000"

    summary = DataSummary(data)

    summary_json = {}
    for thread in summary.threads:
        summary_json[thread] = [
            {
                "stack": key,
                "metric": value,
            }
            for key, value in summary.threads[thread].items()
            if key and isinstance(next(iter(key)), str)
        ]

    # We expect to see one stack for Task-1 / Task-2 / inner_1 and one for Task-1 / Task-3 / inner_2
    try:
        summary.assert_substack(
            "0:MainThread",
            (
                "Task-1",
                "main",
                "Task-2",
                "inner",
                "deep",
                "deeper",
                "sleep",
            ),
            lambda v: v >= 0.0,
        )
        summary.assert_substack(
            "0:MainThread",
            (
                "Task-1",
                "main",
                "Task-3",
                "inner",
                "deep",
                "deeper",
                "sleep",
            ),
            lambda v: v >= 0.0,
        )
    except AssertionError:
        # Search the other way around - Task 1 / Task 3 / inner_1 and Task 1 / Task 2 / inner_2
        summary.assert_substack(
            "0:MainThread",
            (
                "Task-1",
                "main",
                "Task-2",
                "inner",
                "deep",
                "deeper",
                "sleep",
            ),
            lambda v: v >= 0.0,
        )
        summary.assert_substack(
            "0:MainThread",
            (
                "Task-1",
                "main",
                "Task-3",
                "inner",
                "deep",
                "deeper",
                "sleep",
            ),
            lambda v: v >= 0.0,
        )
