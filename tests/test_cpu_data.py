import pytest

from tests.utils import PY
from tests.utils import DataSummary
from tests.utils import run_target
from tests.utils import stealth


@stealth
def test_cpu_time(stealth):
    result, data = run_target("target_cpu", *stealth, "-c")
    assert result.returncode == 0, result.stderr.decode()

    md = data.metadata
    assert md["mode"] == "cpu"
    assert md["interval"] == "1000"

    summary = DataSummary(data)

    expected_nthreads = 3 - bool(stealth)
    assert summary.nthreads == expected_nthreads
    assert summary.total_metric >= 0.5 * 1e6 * (2 - bool(stealth))
    assert summary.nsamples >= summary.total_metric / 2000

    # Test line numbers
    assert summary.query("MainThread", (("main", 22), ("bar", 17))) is None
    assert (
        summary.query(
            "SecondaryThread",
            ("Thread.run" if PY >= (3, 11) else "run", "keep_cpu_busy"),
        )
        is not None
    )

    # Test stacks and expected values
    summary.assert_stack(
        "MainThread",
        (
            "_run_module_as_main",
            "_run_code",
            "<module>",
            "keep_cpu_busy",
        ),
        lambda v: v >= 3e5,
    )

    if PY >= (3, 11):
        summary.assert_stack(
            "SecondaryThread",
            (
                "Thread._bootstrap",
                "thread_bootstrap_inner",
                "Thread._bootstrap_inner",
                "Thread.run",
                "keep_cpu_busy",
            ),
            lambda v: v >= 3e5,
        )
        if not bool(stealth):
            summary.assert_stack(
                "echion.core.sampler",
                (
                    "Thread._bootstrap",
                    "thread_bootstrap_inner",
                    "Thread._bootstrap_inner",
                    "Thread.run",
                ),
                lambda v: v >= 0.5e6,
            )
    else:
        summary.assert_stack(
            "SecondaryThread",
            (
                "_bootstrap",
                "thread_bootstrap_inner",
                "_bootstrap_inner",
                "run",
                "keep_cpu_busy",
            ),
            lambda v: v >= 3e5,
        )
        if not bool(stealth):
            summary.assert_stack(
                "echion.core.sampler",
                (
                    "_bootstrap",
                    "thread_bootstrap_inner",
                    "_bootstrap_inner",
                    "run",
                ),
                lambda v: v >= 0.5e6,
            )


@stealth
@pytest.mark.xfail
def test_cpu_time_native(stealth):
    result, data = run_target("target_cpu", *stealth, "-cn")
    assert result.returncode == 0, result.stderr.decode()

    md = data.metadata
    assert md["mode"] == "cpu"
    assert md["interval"] == "1000"

    summary = DataSummary(data)

    expected_nthreads = 3 - bool(stealth)
    assert summary.nthreads == expected_nthreads
    assert summary.total_metric >= 0.5 * 1e6 * (2 - bool(stealth))
    # Native stack sampling is slower so we omit the upper bound check
    assert summary.nsamples * 500 <= summary.total_metric

    # Test line numbers. This only works with CFrame
    if PY >= (3, 11):
        assert (
            summary.query(
                "MainThread",
                (
                    ("<module>", 48),
                    ("keep_cpu_busy", 40),
                ),
            )
            is not None
        )
        assert summary.query("SecondaryThread", (("keep_cpu_busy", 41),)) is not None
    else:
        assert summary.query("MainThread", (("keep_cpu_busy", 41),)) is not None
        assert summary.query("SecondaryThread", (("keep_cpu_busy", 41),)) is not None
