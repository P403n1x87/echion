import os
import sys
import typing as t
from functools import wraps
import json
import json
import os
import sys
import typing as t
from functools import wraps
from itertools import count
from pathlib import Path
from shutil import which
from subprocess import PIPE, CalledProcessError, CompletedProcess, Popen, run
from time import sleep

import pytest
from austin.format.mojo import MojoFile


def retry_on_valueerror(
    max_retries: int = 3,
) -> t.Callable[[t.Callable[..., t.Any]], t.Callable[..., t.Any]]:
    """Decorator that retries a test up to max_retries times if ValueError is raised."""

    def decorator(func: t.Callable[..., t.Any]) -> t.Callable[..., t.Any]:
        @wraps(func)
        def wrapper(*args: t.Any, **kwargs: t.Any) -> t.Any:
            last_error: t.Optional[ValueError] = None
            for attempt in range(max_retries):
                try:
                    return func(*args, **kwargs)
                except ValueError as e:
                    last_error = e
                    if attempt < max_retries - 1:
                        print(
                            f"Retry {attempt + 1}/{max_retries} after ValueError: {e}"
                        )

            assert last_error is not None
            raise last_error

        return wrapper

    return decorator


PY = sys.version_info[:2]
PROFILES = Path("profiles")
PROFILES.mkdir(exist_ok=True)


class DataSummary:
    def __init__(self, data: MojoFile) -> None:
        self.data = data
        self.metadata = data.metadata

        self.threads: t.Dict[
            str, dict[t.Union[t.Tuple[str, ...], t.Tuple[t.Tuple[str, int], ...]], int]
        ] = {}
        self.sample_counts: t.Dict[
            str, dict[t.Union[t.Tuple[str, ...], t.Tuple[t.Tuple[str, int], ...]], int]
        ] = {}
        self.total_metric = 0
        self.n_samples = 0

        for sample in data.samples:
            self.n_samples += 1
            frames = sample.frames
            v = sample.metrics[0].value

            if not sample.thread or not v:
                continue

            self.total_metric += v

            stacks = self.threads.setdefault(f"{sample.iid}:{sample.thread}", {})
            sample_counts = self.sample_counts.setdefault(
                f"{sample.iid}:{sample.thread}", {}
            )

            stack = tuple((f.scope.string.value, f.line) for f in frames)
            stacks[stack] = stacks.get(stack, 0) + v
            sample_counts[stack] = sample_counts.get(stack, 0) + 1

            fun_only_stack = tuple(f.scope.string.value for f in frames)
            stacks[fun_only_stack] = stacks.get(fun_only_stack, 0) + v
            sample_counts[fun_only_stack] = sample_counts.get(fun_only_stack, 0) + 1

    @property
    def nthreads(self) -> int:
        return len(self.threads)

    def query(self, thread_name, frames) -> t.Optional[int]:
        try:
            stacks = self.threads[thread_name]
        except KeyError as e:
            raise AssertionError(
                f"Expected thread {thread_name}, found {list(self.threads.keys())}"
            ) from e

        for stack in stacks:
            for i in range(0, len(stack) - len(frames) + 1):
                if stack[i : i + len(frames)] == frames:
                    return stacks[stack]

        return None

    def assert_stack(
        self,
        thread: str,
        frames: t.Union[t.Tuple[str, ...], t.Tuple[t.Tuple[str, int], ...]],
        predicate: t.Callable[[int], bool],
    ) -> None:
        try:
            stack = self.threads[thread][frames]
            assert predicate(stack), stack
        except KeyError:
            if thread not in self.threads:
                raise AssertionError(
                    f"Expected thread {thread}, found {self.threads.keys()}"
                ) from None
            raise AssertionError(
                f"Expected stack {frames}, found {self.threads[thread].keys()}"
            ) from None

    def assert_substack(
        self,
        thread: str,
        frames: t.Union[t.Tuple[str, ...], t.Tuple[t.Tuple[str, int], ...]],
        predicate: t.Callable[[int], bool],
    ) -> None:
        stacks_failing_predicate: list[
            t.Tuple[t.Union[t.Tuple[str, ...], t.Tuple[t.Tuple[str, int], ...]], int]
        ] = []
        try:
            stacks = self.threads[thread]
            for stack in stacks:
                for i in range(0, len(stack) - len(frames) + 1):
                    substack = stack[i : i + len(frames)]
                    if substack == frames:
                        if predicate(stacks[stack]):
                            return

                        stacks_failing_predicate.append((stack, stacks[stack]))
            else:
                if not stacks_failing_predicate:
                    raise AssertionError(
                        f"No matching substack found for frames {frames} in thread {thread}"
                    ) from None

                raise AssertionError(
                    f"No matching substack found for frames {frames} in thread {thread}. "
                    f"Matching stacks failing predicate: {stacks_failing_predicate}"
                ) from None

        except KeyError:
            if thread not in self.threads:
                raise AssertionError(
                    f"Expected thread {thread}, found {self.threads.keys()}"
                ) from None
            raise AssertionError(
                f"Expected stack {frames}, found {self.threads[thread].keys()}"
            ) from None

    def assert_not_substack(
        self,
        thread: str,
        frames: t.Union[t.Tuple[str, ...], t.Tuple[t.Tuple[str, int], ...]],
    ) -> None:
        try:
            self.assert_substack(thread, frames, lambda _: True)
        except AssertionError:
            return

        raise AssertionError(f"Unwanted stack {frames} was found in {thread}")


def run_echion(*args: str) -> CompletedProcess:
    try:
        return run(
            [
                "echion",
                *args,
            ],
            capture_output=True,
            check=True,
            timeout=30,
        )
    except CalledProcessError as e:
        print(e.stdout.decode())
        print(e.stderr.decode())
        raise


def run_target(
    target: str, *args: str
) -> t.Tuple[CompletedProcess, t.Optional[MojoFile]]:
    test_name = sys._getframe(1).f_code.co_name
    output_file = (PROFILES / test_name).with_suffix(".mojo")
    n = count(1)
    while output_file.exists():
        output_file = (PROFILES / f"{test_name}-{next(n)}").with_suffix(".mojo")

    result = run_echion(
        "-o",
        str(output_file),
        *args,
        sys.executable,
        "-m",
        f"tests.{target}",
    )

    if not output_file.is_file():
        return result, None

    m = MojoFile(output_file.open(mode="rb"))
    m.unwind()
    return result, m


def run_with_signal(target: Path, signal: int, delay: float, *args: str) -> Popen:
    p = Popen(
        [
            t.cast(str, which("echion")),
            *args,
            sys.executable,
            "-m",
            f"tests.{target}",
        ],
        stdout=PIPE,
        stderr=PIPE,
    )

    sleep(delay)

    p.send_signal(signal)

    p.wait()

    return p


stealth = pytest.mark.parametrize("stealth", [tuple(), ("--stealth",)])


if sys.platform == "win32":
    requires_sudo = no_sudo = lambda f: f
else:
    requires_sudo = pytest.mark.skipif(
        os.geteuid() != 0, reason="Requires superuser privileges"
    )
    no_sudo = pytest.mark.skipif(
        os.geteuid() == 0, reason="Must not have superuser privileges"
    )


def summary_to_json(summary: DataSummary, line_numbers: bool = False) -> t.Dict[str, t.Any]:
    summary_json = {}
    for thread in summary.threads:
        summary_json[thread] = sorted(
            [
                {
                    "stack": (
                        list(key) if isinstance(next(iter(key)), str)
                        else [f"{item[0]}:{item[1]}" for item in key]
                    ),
                    "metric": value,
                    "count": summary.sample_counts[thread][key],
                }
                for key, value in summary.threads[thread].items()
                if key and ((line_numbers and isinstance(next(iter(key)), tuple)) or (not line_numbers and isinstance(next(iter(key)), str)))
            ],
            key=lambda x: t.cast(float, x["metric"]),
            reverse=True,
        )
    return summary_json


def dump_summary(summary: DataSummary, file: str, line_numbers: bool = False) -> None:
    with open(file, "w") as f:
        json.dump(summary_to_json(summary, line_numbers), f, indent=2)
