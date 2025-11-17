import pytest

from tests.utils import PY, DataSummary, run_target


def test_asyncio_as_completed():
    result, data = run_target("target_asyncio_as_completed")
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

    # We expect MainThread and the sampler
    expected_nthreads = 2
    assert summary.nthreads == expected_nthreads, summary.threads
    assert summary.total_metric >= 1.4 * 1e6

    # Test stacks and expected values
    # TODO: these stacks need to be adapted to Python 3.11 (qual names have changed)
    # but in the current state they don't work at all anyway.
    # Thread Pool Executor
    if PY >= (3, 11):
        if PY >= (3, 13):
            summary.assert_substack(
                "0:MainThread",
                (
                    "Task-1",
                    "outer",
                    "_AsCompletedIterator._wait_for_one",
                    "Queue.get",
                ),
                lambda v: v >= 0.001e6,
            )
        else:
            summary.assert_substack(
                "0:MainThread",
                (
                    "Task-1",
                    "outer",
                    "as_completed.<locals>._wait_for_one",
                    "Queue.get",
                ),
                lambda v: v >= 0.001e6,
            )

        summary.assert_substack(
            "0:MainThread",
            (
                "Task-2",
                "inner",
                "sleep",
            ),
            lambda v: v >= 0.001e6,
        )

        pytest.xfail(reason="This currently does not work")
        # Ideally, we should see the following stack (including Tasks being as_complete'd)
        summary.assert_substack(
            "0:MainThread",
            (
                "Task-1",
                "outer",
                "as_completed.<locals>._wait_for_one",
                "Queue.get",
                "Task-2",
                "inner",
                "sleep",
            ),
            lambda v: v >= 0.001e6,
        )

    else:
        summary.assert_substack(
            "0:MainThread",
            (
                "Task-1",
                "outer",
                "_wait_for_one",
                "get",
            ),
            lambda v: v >= 0.001e6,
        )

        summary.assert_substack(
            "0:MainThread",
            (
                "Task-2",
                "inner",
                "sleep",
            ),
            lambda v: v >= 0.001e6,
        )

        pytest.xfail(reason="This currently does not work")
        # Ideally, we should see the following stack (including Tasks being as_complete'd)
        summary.assert_substack(
            "0:MainThread",
            (
                "Task-1",
                "outer",
                "_wait_for_one",
                "get",
                "Task-2",
                "inner",
                "sleep",
            ),
            lambda v: v >= 0.001e6,
        )
