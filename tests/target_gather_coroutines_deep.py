import asyncio


async def deeper():
    await asyncio.sleep(1.0)


async def deep():
    await deeper()


async def inner():
    await deep()


async def main():
    await asyncio.gather(inner(), inner())


asyncio.run(main())
