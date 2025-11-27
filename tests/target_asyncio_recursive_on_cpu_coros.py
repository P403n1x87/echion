import asyncio
import time


def sync_code() -> int:
    target = time.time() + 1
    result = 0
    while time.time() < target:
        result += 1

    return result


def sync_code_outer() -> int:
    return sync_code()


async def inner3() -> int:
    return sync_code_outer()


async def inner2() -> int:
    return await inner3()


async def inner1() -> int:
    return await inner2()


async def outer():
    return await inner1()


async def async_main():
    return await outer()


def main_sync():
    asyncio.run(async_main())


if __name__ == "__main__":
    main_sync()
