import pytest

from tests.utils import DataSummary, run_target, retry_on_valueerror, dump_summary


@pytest.mark.xfail(reason="This test is very flaky")
@retry_on_valueerror()
def test_asyncio_async_generator_wall_time() -> None:
    result, data = run_target("target_async_generator")
    assert result.returncode == 0, result.stderr.decode()

    assert data is not None
    md = data.metadata
    assert md["mode"] == "wall"
    assert md["interval"] == "1000"

    summary = DataSummary(data)
    dump_summary(summary, "summary_asyncio_async_generator.json")

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

    summary.assert_substack(
        "0:MainThread",
        (
            "Task-1",
            "main",
            "asynchronous_function",
            "async_generator",
            "async_generator_dep",
            "deep_dependency",
            "sleep",
        ),
        lambda v: v >= 0.0
    )
