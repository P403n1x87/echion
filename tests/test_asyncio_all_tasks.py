from tests.utils import DataSummary, run_target, retry_on_valueerror


@retry_on_valueerror()
def test_all_named_tasks_visible():
    """Regression test for the 3.14 asyncio linked-list task traversal.

    In Python 3.14+, asyncio tasks are stored in a per-thread linked list
    (_PyThreadStateImpl::asyncio_tasks_head) rather than a WeakSet.  If the
    linked-list traversal in threads.h is broken, named tasks will be absent
    from the profile even though they are actively running.

    Five named Worker-N tasks all sleep for 3 s, giving the profiler ample
    time to sample.  Every task name must appear at least once.
    """
    result, data = run_target("target_asyncio_all_tasks")
    assert result.returncode == 0, result.stderr.decode()

    assert data is not None
    summary = DataSummary(data)

    missing = []
    for i in range(5):
        name = f"Worker-{i}"
        found = any(
            name in frame
            for stack in summary.threads.get("0:MainThread", {})
            for frame in stack
        )
        if not found:
            missing.append(name)

    assert not missing, (
        f"Named asyncio tasks missing from profile — "
        f"check linked-list traversal in threads.h: {missing}"
    )
