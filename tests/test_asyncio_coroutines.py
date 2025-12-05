import json

from tests.utils import PY, DataSummary, run_target, dump_summary, summary_to_json, retry_on_valueerror


@retry_on_valueerror()
def test_asyncio_coroutines_wall_time():
    result, data = run_target("target_async_coroutines")
    assert result.returncode == 0, result.stderr.decode()

    assert data is not None
    md = data.metadata
    assert md["mode"] == "wall"
    assert md["interval"] == "1000"

    summary = DataSummary(data)
    dump_summary(summary, "summary_asyncio_coroutines.json")

    # We expect MainThread and the sampler
    expected_nthreads = 2
    assert summary.nthreads == expected_nthreads, summary.threads
    assert summary.total_metric >= 1.4 * 1e6

    # Test stacks and expected values
    if PY >= (3, 11):
        summary.assert_substack(
            "0:MainThread",
            (
                "Task-main",
                "outer_function",
                "Task-background_wait",
                "outer_function.<locals>.background_wait_function",
                "sleep",
            ),
            lambda v: v >= 0.001e6,
        )

        summary.assert_substack(
            "0:MainThread",
            (
                "Task-background_wait",
                "outer_function.<locals>.background_wait_function",
                "sleep",
            ),
            lambda v: v >= 0.001e6,
        )

        summary.assert_substack(
            "0:MainThread",
            ("Task-main", "outer_function"),
            lambda v: v >= 0.001e6,
        )

        summary.assert_substack(
            "0:MainThread",
            (
                "Task-main",
                "outer_function",
                "outer_function.<locals>.main_coro",
                "outer_function.<locals>.sub_coro",
                "sleep",
            ),
            lambda v: v >= 0.001e6,
        )

        try:
            summary.assert_not_substack(
                "0:MainThread",
                (
                    "outer_function.<locals>.background_math_function",
                    "Task-background_wait",
                    "outer_function.<locals>.background_wait_function",
                    "sleep",
                ),
            )
        except AssertionError:
            print(json.dumps(summary_to_json(summary), indent=4))
            raise
    else:  # PY < (3, 11)
        summary.assert_substack(
            "0:MainThread",
            (
                "Task-main",
                "outer_function",
                "Task-background_wait",
                "background_wait_function",
                "sleep",
            ),
            lambda v: v >= 0.001e6,
        )

        summary.assert_substack(
            "0:MainThread",
            (
                "Task-background_wait",
                "background_wait_function",
                "sleep",
            ),
            lambda v: v >= 0.001e6,
        )

        summary.assert_substack(
            "0:MainThread",
            ("Task-main", "outer_function"),
            lambda v: v >= 0.001e6,
        )

        summary.assert_substack(
            "0:MainThread",
            (
                "Task-main",
                "outer_function",
                "main_coro",
                "sub_coro",
                "sleep",
            ),
            lambda v: v >= 0.001e6,
        )

        try:
            summary.assert_not_substack(
                "0:MainThread",
                (
                    "background_math_function",
                    "Task-background_wait",
                    "background_wait_function",
                    "sleep",
                ),
            )
        except AssertionError:
            print(json.dumps(summary_to_json(summary), indent=4))
            raise
