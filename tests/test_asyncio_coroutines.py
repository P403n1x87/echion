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
                "outer_function.<locals>.background_math_function",
                "main",
                "outer_function",
                "outer_function.<locals>.main_coro",
                "outer_function.<locals>.sub_coro",
                "sleep",
            ),
            lambda v: v >= 0.001e6,
        )

        summary.assert_substack(
            "0:MainThread",
            (
                "outer_function.<locals>.background_math_function",
                "background_math",
            ),
            lambda v: v >= 0.001e6,
        )

    else:
        # Main Thread
        summary.assert_substack(
            "0:MainThread",
            (
                "_run_module_as_main",
                "_run_code",
                "<module>",
                "run_until_complete",
                "run_forever",
                "_run_once",
                "select",
                "main",
                "outer_function",
                "main_coro",
                "sleep",
            ),
            lambda v: v >= 0.1e6,
        )

        summary.assert_substack(
            "0:MainThread",
            (
                "_run_module_as_main",
                "_run_code",
                "<module>",
                "run_until_complete",
                "run_forever",
                "_run_once",
                "_run",
                "background_math_function",
                "main",
                "outer_function",
                "main_coro",
                "sub_coro",
                "sleep",
            ),
            lambda v: v >= 0.001e6,
        )

        summary.assert_substack(
            "0:MainThread",
            (
                "_run_module_as_main",
                "_run_code",
                "<module>",
                "run_until_complete",
                "run_forever",
                "_run_once",
                "_run",
                "background_math_function",
                "background_math",
            ),
            lambda v: v >= 0.001e6,
        )
        # Thread Pool Executor
        summary.assert_substack(
            "0:MainThread",
            (
                "_run_module_as_main",
                "_run_code",
                "<module>",
                "run_until_complete",
                "run_forever",
                "_run_once",
                "_run",
                "background_math_function",
                "main",
                "outer_function",
                "main_coro",
                "sub_coro",
                "sleep",
            ),
            lambda v: v >= 0.001e6,
        )
