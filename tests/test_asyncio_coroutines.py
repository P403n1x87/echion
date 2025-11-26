from tests.utils import PY, DataSummary, run_target


def test_asyncio_coroutines_wall_time():
    result, data = run_target("target_async_coroutines")
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

    with open("summary_asyncio_coroutines.json", "w") as f:
        import json

        json.dump(summary_json, f, indent=2)

    # We expect MainThread and the sampler
    expected_nthreads = 2
    assert summary.nthreads == expected_nthreads, summary.threads
    assert summary.total_metric >= 1.4 * 1e6

    # Test stacks and expected values
    if PY >= (3, 11):
        # TODO: these stacks need to be adapted to Python 3.11 (qual names have changed)
        # but in the current state they don't work at all anyway.
        # Thread Pool Executor
        summary.assert_substack(
            "0:MainThread",
            (
                "main", # Task name
                "outer_function", # coroutine
                "outer_function.<locals>.main_coro", # coroutine
                "outer_function.<locals>.sub_coro", # coroutine
                "sleep", # coroutine
            ),
            lambda v: v >= 0.001e6,
        )

        summary.assert_substack(
            "0:MainThread",
            (
                "main", # Task name
                "outer_function", # coroutine
                "background_wait", # sub-Task name
                "outer_function.<locals>.background_task_func", # coroutine
                "sleep",
            ),
            lambda v: v >= 0.001e6,
        )

        summary.assert_substack(
            "0:MainThread",
            (
                "background_math", # Task name
                "outer_function.<locals>.background_math_function", # coroutine
            ),
            lambda v: v >= 0.001e6,
        )

    else:
        # Main Thread
        summary.assert_substack(
            "0:MainThread",
            (
                "main", # Task name
                "outer_function",
                "main_coro",
                "sub_coro",
                "sleep",
            ),
            lambda v: v >= 0.1e6,
        )

        summary.assert_substack(
            "0:MainThread",
            (
                "main", # Task name
                "outer_function", # coroutine
                "background_wait", # sub-Task name
                "background_task_func", # coroutine
                "sleep",
            ),
            lambda v: v >= 0.001e6,
        )

        summary.assert_substack(
            "0:MainThread",
            (
                "background_math", # Task name
                "background_math_function", # coroutine
            ),
            lambda v: v >= 0.001e6,
        )

