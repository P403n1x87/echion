from signal import SIGQUIT

from tests.utils import run_with_signal


def test_where():
    p = run_with_signal("target_where", SIGQUIT, 1, "-w")
    assert p.returncode == 0

    err = p.stderr.read().decode()

    assert "🐴 Echion reporting for duty" in err
    assert "🧵 MainThread:" in err
    assert "_run_module_as_main" in err
    assert "main" in err
