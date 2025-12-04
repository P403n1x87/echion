import pytest

from tests.utils import run_target, retry_on_valueerror


@retry_on_valueerror()
def test_fault_handler_enable_disable(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("ECHION_USE_FAST_COPY_MEMORY", "1")
    result, _ = run_target("target_fault_handler_enable_disable")
    assert result.returncode == 0, result.stderr.decode()


@retry_on_valueerror()
def test_fault_handler_enabled_from_env_no_fast_copy_memory(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setenv("PYTHONFAULTHANDLER", "1")
    monkeypatch.setenv("ECHION_USE_FAST_COPY_MEMORY", "0")
    result, _ = run_target("target_fault_handler_enable_disable")
    assert result.returncode == 0, result.stderr.decode()


@retry_on_valueerror()
@pytest.mark.xfail(reason="This raises a segmentation fault")
def test_fault_handler_enabled_from_env(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("PYTHONFAULTHANDLER", "1")
    monkeypatch.setenv("ECHION_USE_FAST_COPY_MEMORY", "1")
    result, _ = run_target("target_fault_handler_enable_disable")
    assert result.returncode == 0, result.stderr.decode()


@retry_on_valueerror()
def test_fault_handler_enabled_from_env_no_faulthandler_calls(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setenv("PYTHONFAULTHANDLER", "1")
    monkeypatch.setenv("ECHION_USE_FAST_COPY_MEMORY", "0")
    result, _ = run_target("target")
    assert result.returncode == 0, result.stderr.decode()
