from tests.utils import PY, DataSummary, run_target, dump_summary, retry_on_valueerror


@retry_on_valueerror()
def test_asyncio_executor():
    result, data = run_target("target_async_executor")
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

    dump_summary(summary, "summary_asyncio_executor.json")

    expected_nthreads = 3
    assert summary.nthreads >= expected_nthreads, summary.threads
    assert summary.total_metric >= 1.4 * 1e6
    # Test stacks and expected values
    if PY >= (3, 11):
        # Main Thread
        summary.assert_substack(
            "0:MainThread",
            (
                "Task-1",
                "main",
                "asynchronous_function",
            ),
            lambda v: v > 0.00,
        )

        # Thread Pool Executor
        summary.assert_substack(
            "0:asyncio_0",
            (
                "Thread._bootstrap",
                "thread_bootstrap_inner",
                "Thread._bootstrap_inner",
                "Thread.run",
                "_worker",
                "_WorkItem.run",
                "slow_sync_function",
            ),
            lambda v: v >= 0.01e6,
        )
    else:
        # Main Thread
        summary.assert_substack(
            "0:MainThread",
            (
                "Task-1",
                "main",
                "asynchronous_function",
            ),
            lambda v: v > 0.0,
        )

        if PY >= (3, 9):
            # Thread Pool Executor
            summary.assert_substack(
                "0:asyncio_0",
                (
                    "_bootstrap",
                    "thread_bootstrap_inner",
                    "_bootstrap_inner",
                    "run",
                    "_worker",
                    "run",
                    "slow_sync_function"
                ),
                lambda v: v >= 0.1e6,
            )
        else:
            # Thread Pool Executor
            summary.assert_substack(
                "0:ThreadPoolExecutor-0_0",
                (
                    "_bootstrap",
                    "thread_bootstrap_inner",
                    "_bootstrap_inner",
                    "run",
                    "_worker",
                    "run",
                    "slow_sync_function"
                ),
                lambda v: v >= 0.1e6,
            )
