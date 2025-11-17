import pytest

from tests.utils import DataSummary, run_target


def test_asyncio_wait():
    result, data = run_target("target_asyncio_wait")
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

    # Test stacks and expected values
    summary.assert_substack(
        "0:MainThread",
        (
            "Task-1",
            "main",
            "outer",
            "wait",
            "_wait",
        ),
        lambda v: v >= 0.0,
    )

    summary.assert_substack(
        "0:MainThread",
        (
            "inner-9",
            "inner",
            "sleep",
        ),
        lambda v: v >= 0.0,
    )

    pytest.xfail(reason="This currently does not work")
    # Ideally, we should see the following stack (including Tasks being wait'ed)
    summary.assert_substack(
        "0:MainThread",
        (
            "Task-1",
            "main",
            "outer",
            "wait",
            "_wait",
            "inner-0",
            "sleep",
        ),
        lambda v: v >= 0.0,
    )
