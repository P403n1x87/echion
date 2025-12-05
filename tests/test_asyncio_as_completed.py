import json
from tests.utils import PY, DataSummary, run_target, retry_on_valueerror, dump_summary, summary_to_json


@retry_on_valueerror()
def test_asyncio_as_completed():
    result, data = run_target("target_asyncio_as_completed")
    assert result.returncode == 0, result.stderr.decode()

    assert data is not None
    md = data.metadata
    assert md["mode"] == "wall"
    assert md["interval"] == "1000"

    summary = DataSummary(data)

    dump_summary(summary, "summary_asyncio_as_completed.json")

    # We expect MainThread and the sampler
    expected_nthreads = 2
    assert summary.nthreads == expected_nthreads, summary.threads
    assert summary.total_metric >= 1.4 * 1e6

    try:
        if PY >= (3, 13):
            for i in range(3, 12):
                summary.assert_substack(
                    "0:MainThread",
                    (
                        "Task-1",
                        "main",
                        "_AsCompletedIterator._wait_for_one",
                        "Queue.get",
                        f"Task-{i}",
                        "wait_and_return_delay",
                        "other",
                        "sleep",
                    ),
                    lambda v: v >= 0.00,
                )

        elif PY >= (3, 11):
            for i in range(3, 12):
                summary.assert_substack(
                    "0:MainThread",
                    (
                        "Task-1",
                        "main",
                        "as_completed.<locals>._wait_for_one",
                        "Queue.get",
                        f"Task-{i}",
                        "wait_and_return_delay",
                        "other",
                        "sleep",
                    ),
                    lambda v: v >= 0.00,
                )
        else:
            for i in range(3, 12):
                summary.assert_substack(
                    "0:MainThread",
                    (
                        "Task-1",
                        "main",
                        "_wait_for_one",
                        "get",
                        f"Task-{i}",
                        "wait_and_return_delay",
                        "other",
                        "sleep",
                    ),
                    lambda v: v >= 0.00,
                )
    except AssertionError:
        print(json.dumps(summary_to_json(summary), indent=4))
        raise