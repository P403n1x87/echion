# This file is part of "echion" which is released under MIT.
#
# Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

import argparse
import os
from pathlib import Path
import sys

from echion import __version__


def main():
    parser = argparse.ArgumentParser(
        description="In-process CPython frame stack sampler",
        prog="echion",
    )
    parser.add_argument(
        "command", nargs=argparse.REMAINDER, type=str, help="Command string to execute."
    )
    parser.add_argument(
        "-i",
        "--interval",
        help="sampling interval in microseconds",
        default=1000,
        type=int,
    )
    parser.add_argument(
        "-c",
        "--cpu",
        help="sample stacks on CPU only",
        action="store_true",
    )
    parser.add_argument(
        "-o",
        "--output",
        help="output location (can use %%(pid) to insert the process ID)",
        type=str,
        default="%%(pid).echion",
    )
    parser.add_argument(
        "-s",
        "--stealth",
        help="stealth mode (sampler thread is not accounted for)",
        action="store_true",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="verbose logging",
    )
    parser.add_argument(
        "-V",
        "--version",
        action="version",
        version="%(prog)s " + __version__,
    )
    args = parser.parse_args()

    root_dir = Path(__file__).parent

    bootstrap_dir = root_dir / "bootstrap"

    if not args.command:
        parser.print_help()
        sys.exit(1)

    executable = args.command[0]
    env = dict(os.environ)

    env["ECHION_INTERVAL"] = str(args.interval)
    env["ECHION_CPU"] = str(int(bool(args.cpu)))
    env["ECHION_OUTPUT"] = args.output.replace("%%(pid)", str(os.getpid()))
    env["ECHION_STEALTH"] = str(int(bool(args.stealth)))

    python_path = os.getenv("PYTHONPATH")
    env["PYTHONPATH"] = (
        os.path.pathsep.join((str(bootstrap_dir), python_path))
        if python_path
        else bootstrap_dir
    )

    try:
        os.execvpe(executable, args.command, env)  # TODO: Cross-platform?
    except (OSError, PermissionError):
        print(
            "echion: executable '%s' does not have executable permissions.\n"
            % executable
        )
        parser.print_usage()
        sys.exit(1)
