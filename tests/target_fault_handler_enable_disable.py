import faulthandler
import time


def main():
    start = time.time()

    # Enable, then disable Python's fault handler
    # This should not crash the Profiler, although we may see some
    # memory errors (caused by the Profiler) reported by Python.
    faulthandler.enable()
    time.sleep(0.5)
    faulthandler.disable()

    count = 0
    while True:
        now = time.time()
        if now - start > 2:
            break

        count += 1


if __name__ == "__main__":
    main()
