import json

from tests.utils import DataSummary
from tests.utils import run_target
from tests.utils import dump_summary
from tests.utils import retry_on_valueerror
from tests.utils import summary_to_json


@retry_on_valueerror()
def test_whatever_async():
    result, data = run_target("target_whatever")
    assert result.returncode == 0, result.stderr.decode()

    assert data is not None
    md = data.metadata
    assert md["mode"] == "wall"
    assert md["interval"] == "1000"

    summary = DataSummary(data)
    dump_summary(summary, "summary_whatever_async.json")

    try:
        # We expect to see one stack for Task-1 / Task-2 / inner_1 and one for Task-1 / Task-3 / inner_2
        try:
            summary.assert_substack(
                "0:MainThread",
                (
                    "Task-1",
                    "main",
                    "Task-2",
                    "inner_1",
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
                    "inner_2",
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
                    "inner_2",
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
                    "inner_1",
                    "sleep",
                ),
                lambda v: v >= 0.0,
            )
    except AssertionError:
        print("stderr", result.stderr.decode())
        print(json.dumps(summary_to_json(summary), indent=4))
        raise
