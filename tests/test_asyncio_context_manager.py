import pytest

from tests.utils import PY, DataSummary, run_target


@pytest.mark.xfail(condition=PY >= (3, 13), reason="Sampling async context manager stacks does not work on >=3.13")
def test_asyncio_context_manager_wall_time():
    result, data = run_target("target_async_with")
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

    expected_nthreads = 2
    assert summary.nthreads >= expected_nthreads, summary.threads
    assert summary.total_metric >= 1.4 * 1e6
    # Test stacks and expected values
    if PY >= (3, 11):
        # Context Manager Enter
        summary.assert_substack(
            "0:MainThread",
            (
                "Task-1",
                "main",
                "asynchronous_function",
                "AsyncContextManager.__aenter__",
                "context_manager_dep",
                "sleep",
            ),
            lambda v: v >= 0.0
        )

        # Context Manager Exit
        summary.assert_substack(
            "0:MainThread",
            (
                "Task-1",
                "main",
                "asynchronous_function",
                "AsyncContextManager.__aexit__",
                "sleep",
            ),
            lambda v: v >= 0.0,
        )
    else:

        # Context Manager Enter
        summary.assert_substack(
            "0:MainThread",
            (
                "Task-1",
                "main",
                "asynchronous_function",
                "__aenter__",
                "context_manager_dep",
                "sleep",
            ),
            lambda v: v >= 0.0,
        )

        # Actual function call
        summary.assert_substack(
            "0:MainThread",
            (
                "Task-1",
                "main",
                "asynchronous_function",
                "some_function",
                "sleep",
            ),
            lambda v: v >= 0.0,
        )

        # Context Manager Exit
        summary.assert_substack(
            "0:MainThread",
            (
                "Task-1",
                "main",
                "asynchronous_function",
                "__aexit__",
                "sleep",
            ),
            lambda v: v >= 0.0,
        )
