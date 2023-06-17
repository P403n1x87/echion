# This file is part of "echion" which is released under MIT.
#
# Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

from pathlib import Path
import sys
from setuptools import find_packages, setup, Extension

PLATFORM = sys.platform.lower()

LDADD = {
    "linux": ["-l:libunwind.a", "-l:liblzma.a"],
}

# add option to colorize compiler output

COLORS = [
    "-fdiagnostics-color=always" if PLATFORM == "linux" else "-fcolor-diagnostics"
]

echionmodule = Extension(
    "echion.core",
    sources=["echion/coremodule.cc"],
    include_dirs=["."],
    define_macros=[(f"PL_{PLATFORM.upper()}", None)],
    extra_compile_args=["-std=c++17", "-Wall", "-Wextra"] + COLORS,
    extra_link_args=LDADD.get(PLATFORM, []),
)

setup(
    name="echion",
    author="Gabriele N. Tornetta",
    version="0.1.0",
    description="In-process Python sampling profiler",
    long_description=Path("README.md").read_text(),
    ext_modules=[echionmodule],
    entry_points={
        "console_scripts": ["echion=echion.__main__:main"],
    },
    packages=find_packages(exclude=["tests"]),
)
