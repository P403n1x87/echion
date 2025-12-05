import asyncio
import time


def synchronous_code_dep() -> None:
    time.sleep(0.25)


def synchronous_code() -> None:
    synchronous_code_dep()


async def inner() -> None:
    synchronous_code()


async def outer() -> None:
    await inner()
    await asyncio.sleep(0.25)


async def async_main() -> None:
    await outer()


def async_starter() -> None:
    asyncio.run(async_main())


def sync_main() -> None:
    async_starter()
    time.sleep(0.25)


if __name__ == "__main__":
    sync_main()
