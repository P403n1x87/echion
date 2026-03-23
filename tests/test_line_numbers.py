from tests.utils import DataSummary, run_target, retry_on_valueerror


@retry_on_valueerror()
def test_exact_line_numbers():
    """Regression test for instr_ptr arithmetic in frame.cc.

    In Python 3.14+, instr_ptr points AT the current instruction (not past
    it), so the -1 subtraction used for 3.13 must be dropped.  A wrong offset
    silently produces line numbers that are off by one or more — this test
    catches that regression without requiring a crash.

    Expected line numbers in target_line_numbers.py:
      top:13   — the middle() call
      middle:9 — the leaf() call
      leaf:5   — the time.sleep(2) call
    """
    result, data = run_target("target_line_numbers")
    assert result.returncode == 0, result.stderr.decode()

    assert data is not None

    summary = DataSummary(data)
    assert summary.nthreads >= 1

    assert (
        summary.query("0:MainThread", (("top", 13), ("middle", 9), ("leaf", 5)))
        is not None
    ), "Exact line numbers wrong — check instr_ptr arithmetic in frame.cc (Frame::read)"
