# This file is part of "echion" which is released under MIT.
#
# Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

import sys
from pathlib import Path

from setuptools import Extension
from setuptools import find_packages
from setuptools import setup


PLATFORM = sys.platform.lower()

LDADD = {
    "linux": ["-l:libunwind.a", "-l:liblzma.a"],
    "win32": ["psapi.lib", "ntdll.lib", "imagehlp.lib", "dbghelp.lib"],
}

# add option to colorize compiler output
COLORS = {
    "darwin": ["-fcolor-diagnostics"],
    "linux": ["-fdiagnostics-color=always"],
}

CFLAGS = {
    "darwin": ["-Wextra", "-mmacosx-version-min=10.15"],
    "linux": ["-Wextra"],
}

echionmodule = Extension(
    "echion.core",
    sources=["echion/coremodule.cc"],
    include_dirs=["."],
    define_macros=[(f"PL_{PLATFORM.upper()}", None)],
    extra_compile_args=["-std=c++17", "-Wall"]
    + CFLAGS.get(PLATFORM, [])
    + COLORS.get(PLATFORM, []),
    extra_link_args=LDADD.get(PLATFORM, []),
)

setup(
    name="echion",
    author="Gabriele N. Tornetta",
    description="In-process Python sampling profiler",
    long_description=Path("README.md")
    .read_text()
    .replace(
        'src="art/', 'src="https://raw.githubusercontent.com/P403n1x87/echion/main/art/'
    ),
    ext_modules=[echionmodule],
    entry_points={
        "console_scripts": ["echion=echion.__main__:main"],
    },
    packages=find_packages(exclude=["tests"]),
)
