import sys
from signal import SIGQUIT
from subprocess import PIPE
from subprocess import Popen
from time import sleep

from tests.utils import run_target


def test_where():
    target = Popen(
        [sys.executable, "-m", "tests.target_where"], stdout=PIPE, stderr=PIPE
    )
    sleep(0.5)

    result, _ = run_target("target_where", "-w", str(target.pid))
    assert result.returncode == 0

    target.wait()

    err = result.stdout.decode()

    assert "🐴 Echion reporting for duty" in err
    assert "🧵 MainThread:" in err
    assert "_run_module_as_main" in err
    assert "main" in err
