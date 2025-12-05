import json
from tests.utils import DataSummary, run_target, retry_on_valueerror


@retry_on_valueerror()
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

    try:
        # Test that we see the stitched stacks (Task-1 / outer / wait / _wait / inner-<index> / inner / sleep)
        for i in range(10):
            summary.assert_substack(
                "0:MainThread",
                (
                    "Task-1",
                    "main",
                    "outer",
                    "wait",
                    "_wait",
                    f"inner-{i}",
                    "inner",
                    "sleep",
                ),
                lambda v: v >= 0.0,
            )
    except AssertionError:
        print(json.dumps(summary_json, indent=2))
        raise
