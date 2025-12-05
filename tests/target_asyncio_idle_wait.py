import asyncio


async def inner2() -> None:
    await asyncio.sleep(1)


async def inner1() -> None:
    t = asyncio.create_task(inner2())

    await t


async def outer():
    await inner1()


async def async_main():
    await outer()


def main_sync():
    asyncio.run(async_main())


if __name__ == "__main__":
    main_sync()
