from pathlib import Path
from subprocess import run
import sys
import typing as t


def run_target(target: Path, output: t.Optional[Path] = None):
    output_file = str(output) if output is not None else f"{target.stem}.echion"

    return run(
        [
            "echion",
            "-o",
            output_file,
            sys.executable,
            "-m",
            f"tests.{target}",
        ],
        capture_output=True,
        check=True,
    )


def test_echion(tmp_path):
    output = tmp_path / "output.echion"
    result = run_target("target", output)
    assert result.returncode == 0, result.stderr

    data = output.read_text()
    assert "# mode: wall" in data, data
    assert "MainThread" in data, data
    assert "echion.core.sampler" in data, data
    assert "SecondaryThread" in data, data
    assert "foo" in data, data
    assert "bar" in data, data


def test_echion_replace_code_objects(tmp_path):
    output = tmp_path / "output.echion"
    result = run_target("target_bytecode", output)
    assert result.returncode == 0, result.stderr
