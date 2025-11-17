import asyncio


async def inner_1():
    await asyncio.sleep(1)


async def inner_2():
    await asyncio.sleep(2)


async def main():
    await asyncio.gather(inner_1(), inner_2())


asyncio.run(main())
