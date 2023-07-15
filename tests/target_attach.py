from time import monotonic as time


def sleep(t):
    end = time() + t
    while time() <= end:
        pass


def foo():
    sleep(0.1)


def bar():
    sleep(0.2)
    foo()


def main():
    while True:
        bar()
        print(os.getpid())


if __name__ == "__main__":
    import os

    print(os.getpid())
    main()
