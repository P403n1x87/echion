from tests.utils import DataSummary, run_target


def test_generators_stacks():
    result, data = run_target("target_generators")
    assert result.returncode == 0, result.stderr.decode()

    assert data is not None
    md = data.metadata
    assert md["mode"] == "wall"
    assert md["interval"] == "1000"

    summary = DataSummary(data)

    assert summary.nthreads == 2, summary.threads

    # Test stacks and expected values
    # Main Thread
    summary.assert_substack(
        "0:MainThread",
        (
            "_run_module_as_main",
            "_run_code",
            "<module>",
            "my_function",
            "generator",
            "generator2",
        ),
        lambda v: v >= 0.0,
    )
