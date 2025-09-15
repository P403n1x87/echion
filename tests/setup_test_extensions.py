#!/usr/bin/env python3
"""
Setup script for test-only extensions.
This builds the task_modifier extension for testing purposes.
"""

from setuptools import setup, Extension
import sys
import sysconfig

# Get Python include and library paths
python_version = f"{sys.version_info.major}.{sys.version_info.minor}"
include_dir = f"{sysconfig.get_path('include')}"
library_dir = f"{sys.prefix}/lib"

# Define the task_modifier extension
task_modifier = Extension(
    'task_modifier',
    sources=['task_modifier.c'],
    include_dirs=[include_dir],
    library_dirs=[library_dir],
    libraries=[f'python{python_version}'],
    extra_compile_args=['-std=c99', '-Wall', '-Wextra'],
)

if __name__ == '__main__':
    setup(
        name='task_modifier_test',
        ext_modules=[task_modifier],
        zip_safe=False,
    )
