from time import sleep
from threading import Thread


def foo():
    sleep(0.5)


def bar():
    sleep(1)
    foo()


def main():
    bar()


if __name__ == "__main__":
    t = Thread(target=main, name="SecondaryThread")
    t.start()

    main()

    t.join()
