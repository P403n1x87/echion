import time


def generator2(j: int):
    for i in range(1000):
        yield i * j


def generator():
    for i in range(1000):
        yield from generator2(i)
        time.sleep(0.001)


def my_function():
    for _ in generator():
        pass


if __name__ == "__main__":
    my_function()
