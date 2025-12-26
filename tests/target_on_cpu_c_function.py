import math
import hashlib
import time


def complex_computation(max_time: float) -> float:
    x=1.0
    end = time.monotonic() + max_time
    while time.monotonic() < end:
        x = math.pow(x, 1.5)

    return x

def confirm_hashes(max_time: float) -> tuple[int, str]:
    start = time.monotonic()
    i=10000
    while time.monotonic() - start < max_time:
        input = b"0123" * i
        hash = hashlib.sha256(input).hexdigest()
        i += 1

    return i, hash 

def main() -> None:
    run_time = 3
    start = time.monotonic()
    while time.monotonic() - start < run_time:
        x=complex_computation(max_time=run_time / 2)
        i, hash = confirm_hashes(max_time=run_time / 2)
        print(f"Complex: {x} Hash {i}: {hash}")

    print(f"Total time: {time.monotonic() - start}")

main()
