from tests.utils import PY, DataSummary
from tests.utils import dump_summary, run_target, retry_on_valueerror


@retry_on_valueerror()
def test_asyncio_recursive_on_cpu_tasks():
    result, data = run_target("target_asyncio_recursive_on_cpu_tasks", "-c")
    assert result.returncode == 0, result.stderr.decode()

    assert data is not None
    md = data.metadata
    assert md["mode"] == "cpu"
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

    dump_summary(summary, "summary_asyncio_recursive_on_cpu_tasks.json")

    if PY >= (3, 11):
        summary.assert_stack(
            "0:MainThread",
            (
                "_run_module_as_main",
                "_run_code",
                "<module>",
                "main_sync",
                "run",
                "Runner.run",
                "BaseEventLoop.run_until_complete",
                "BaseEventLoop.run_forever",
                "BaseEventLoop._run_once",
                "Handle._run",
                "Task-1",
                "async_main",
                "outer",
                "inner1",
                "Task-2",
                "inner2",
                "inner3",
                "sync_code_outer",
                "sync_code",
            ),
            lambda v: v >= 0.9 * 1e6,
        )
    else:
        summary.assert_stack(
            "0:MainThread",
            (
                "_run_module_as_main",
                "_run_code",
                "<module>",
                "main_sync",
                "run",
                "run_until_complete",
                "run_forever",
                "_run_once",
                "_run",
                "Task-1",
                "async_main",
                "outer",
                "inner1",
                "Task-2",
                "inner2",
                "inner3",
                "sync_code_outer",
                "sync_code",
            ),
            lambda v: v >= 0.9 * 1e6,
        )
