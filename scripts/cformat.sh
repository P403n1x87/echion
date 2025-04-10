#!/bin/bash
set -euo pipefail

if ! command -v clang-format &> /dev/null; then
    echo "Error: clang-format is not installed or not in PATH"
    exit 1
fi

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
REPO_ROOT="$(realpath "$SCRIPT_DIR/..")"
ECHION_DIR="$REPO_ROOT/echion"
if [ ! -d "$ECHION_DIR" ]; then
    echo "Error: echion directory not found at $ECHION_DIR"
    exit 1
fi

if [ ! -f "$SCRIPT_DIR/.clang-format" ]; then
    echo "Error: .clang-format not found in $SCRIPT_DIR"
    exit 1
fi

# If no CLANG_FORMAT_COMMAND is set, use clang-format
CLANG_FORMAT_COMMAND=${CLANG_FORMAT_COMMAND:-clang-format}

find "$ECHION_DIR" \( -name "*.h" -o -name "*.cc" \) -type f -print0 |
    xargs -0 -n1 ${CLANG_FORMAT_COMMAND} -i -style=file:"$SCRIPT_DIR/.clang-format"
