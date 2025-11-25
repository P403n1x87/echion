
from tests.utils import DataSummary
from tests.utils import run_target


def test_asyncio_fib_wall_time():
    result, data = run_target("target_fib")
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

    with open("summary.json", "w") as f:
        import json

        json.dump(summary_json, f, indent=2)

    # We expect to see one stack for Task-1 / Task-2 / inner_1 and one for Task-1 / Task-3 / inner_2
    summary.assert_substack(
        "0:MainThread",
        (
            "Task-1",
            "main",
            "f1",
            "f2",
            "f3",
            "F4-1",
            "f4",
            "f5",
            "fib",
            "fib",
            "fib",
            "fib",
            "fib",
            "fib",
            "fib",
            "fib",
        ),
        lambda v: v >= 0.0,
    )

    summary.assert_substack(
        "0:MainThread",
        (
            "Task-1",
            "main",
            "f1",
            "f2",
            "f3",
            "F4-2",
            "f4",
            "f5",
            "fib",
            "fib",
            "fib",
            "fib",
            "fib",
            "fib",
            "fib",
            "fib",
        ),
        lambda v: v >= 0.0,
    )