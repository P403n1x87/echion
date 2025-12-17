import math
import time


def wait_500_ms_on_import():
    start = time.time()
    while time.time() - start < 0.5:
        time.sleep(0.0001)


def helper_function(a: int, b: float) -> float:
    total = 0.0
    while total < 1000:
        total += abs(math.sin(a * b))
        time.sleep(0.0005)
        a += 1

    return total


wait_500_ms_on_import()
