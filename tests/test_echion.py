from tests.utils import run_target, retry_on_valueerror


@retry_on_valueerror()
def test_echion():
    result, _ = run_target("target", "-i", "10ms")
    assert result.returncode == 0, result.stderr


@retry_on_valueerror()
def test_echion_replace_code_objects():
    result, _ = run_target("target_bytecode")
    assert result.returncode == 0, result.stderr
