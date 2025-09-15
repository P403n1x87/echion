import subprocess
import sys
from pathlib import Path

from tests.utils import run_target


def setup_test_extension():
    """Build the test extension if it doesn't exist"""
    test_dir = Path(__file__).parent
    print("Building test extension...")
    try:
        subprocess.run([
            sys.executable, "setup_test_extensions.py", "build_ext", "--inplace"
        ], cwd=test_dir, check=True)
        print("Test extension built successfully!")
    except subprocess.CalledProcessError as e:
        print(f"Failed to build test extension: {e}")
        sys.exit(1)

    # Add to path
    if str(test_dir) not in sys.path:
        sys.path.insert(0, str(test_dir))

# Call this before importing task_modifier
setup_test_extension()


def test_async_with_test_extension():
    _, _ = run_target("target_async")
