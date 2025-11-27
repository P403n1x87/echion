import sys

from tests.utils import PY
from tests.utils import DataSummary
from tests.utils import dump_summary
from tests.utils import run_target
from tests.utils import retry_on_valueerror


@retry_on_valueerror()
def test_asyncio_within_function():
    result, data = run_target("target_asyncio_within_function")
    assert result.returncode == 0, result.stderr.decode()

    assert data is not None
    md = data.metadata
    assert md["mode"] == "wall"
    assert md["interval"] == "1000"

    summary = DataSummary(data)

    summary_json = {}
    for thread in summary.threads:
        summary_json[thread] = sorted(
            [
                {
                    "stack": key,
                    "metric": value,
                }
                for key, value in summary.threads[thread].items()
                if key and isinstance(next(iter(key)), str)
            ],
            key=lambda x: x["metric"],
            reverse=True,
        )

    dump_summary(summary, "summary_asyncio_within_function.json")

    # We expect MainThread and the sampler
    expected_nthreads = 2
    assert summary.nthreads == expected_nthreads, summary.threads
    assert summary.total_metric >= 1.4 * 1e6

    # sync_main / async_starter / (Task-1) / async_main / outer / inner / synchronous_code / synchronous_code_dep (/ time.sleep)
    if PY >= (3, 11):
        summary.assert_substack(
            "0:MainThread",
            (
                "_run_module_as_main",
                "_run_code",
                "<module>",
                "sync_main",
                "async_starter",
                "run",
                "Runner.run",
                "BaseEventLoop.run_until_complete",
                "BaseEventLoop.run_forever",
                "BaseEventLoop._run_once",
                "Handle._run",
                "Task-1",
                "async_main",
                "outer",
                "inner",
                "synchronous_code",
                "synchronous_code_dep"
                # We don't have time.sleep because it's a C function.
            ),
            lambda v: v >= (0.25 * 0.9) * 1e6,
        )

        # sync_main / async_starter / (Task-1) / async_main / outer / asyncio.sleep
        summary.assert_substack(
            "0:MainThread",
            (
                "<module>",
                "sync_main",
                "async_starter",
                "run",
                "Runner.run",
                "BaseEventLoop.run_until_complete",
                "BaseEventLoop.run_forever",
                "BaseEventLoop._run_once",
                f'{"KqueueSelector" if sys.platform == "darwin" else "EpollSelector"}.select',
                "Task-1",
                "async_main",
                "outer",
                "sleep",  # asyncio.sleep
            ),
            lambda v: v >= (0.25 * 0.9) * 1e6,
        )

        # sync_main / async_starter / (Task-1) / async_main / outer / asyncio.sleep
        summary.assert_stack(
            "0:MainThread",
            (
                "_run_module_as_main",
                "_run_code",
                "<module>",
                "sync_main",
            ),
            lambda v: v >= (0.25 * 0.9) * 1e6,
        )
    else:

        summary.assert_substack(
            "0:MainThread",
            (
                "_run_module_as_main",
                "_run_code",
                "<module>",
                "sync_main",
                "async_starter",
                "run",
                "run_until_complete",
                "run_forever",
                "_run_once",
                "_run",
                "Task-1",
                "async_main",
                "outer",
                "inner",
                "synchronous_code",
                "synchronous_code_dep"
                # We don't have time.sleep because it's a C function.
            ),
            lambda v: v >= (0.25 * 0.9) * 1e6,
        )

        # sync_main / async_starter / (Task-1) / async_main / outer / asyncio.sleep
        summary.assert_substack(
            "0:MainThread",
            (
                "<module>",
                "sync_main",
                "async_starter",
                "run",
                "run_until_complete",
                "run_forever",
                "_run_once",
                "select",
                "Task-1",
                "async_main",
                "outer",
                "sleep",  # asyncio.sleep
            ),
            lambda v: v >= (0.25 * 0.9) * 1e6,
        )

        # sync_main / async_starter / (Task-1) / async_main / outer / asyncio.sleep
        summary.assert_stack(
            "0:MainThread",
            (
                "_run_module_as_main",
                "_run_code",
                "<module>",
                "sync_main",
            ),
            lambda v: v >= (0.25 * 0.9) * 1e6,
        )
