from tests.utils import DataSummary
from tests.utils import run_target
from tests.utils import retry_on_valueerror
from tests.utils import dump_summary


@retry_on_valueerror()
def test_on_cpu_c_function():
    result, data = run_target("target_on_cpu_c_function", "-c")
    assert result.returncode == 0, result.stderr.decode()
    print(result.stderr.decode())
    print(result.stdout.decode())

    assert data is not None
    md = data.metadata
    assert md["mode"] == "cpu"
    assert md["interval"] == "1000"

    summary = DataSummary(data)
    dump_summary(summary, "summary_on_cpu_c_function.json")

    summary.assert_substack(
        "0:MainThread",
        (
            "main",
            "confirm_hashes",
            "sha256",
        ),
        lambda v: v >= 0.0,
    )
    summary.assert_substack(
        "0:MainThread",
        (
            "main",
            "complex_computation",
            "sin",
        ),
        lambda v: v >= 0.0,
    )
