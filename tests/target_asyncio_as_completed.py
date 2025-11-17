import asyncio


async def inner():
    await asyncio.sleep(0.5)


async def outer():
    as_completed = next(asyncio.as_completed([inner()]))
    await as_completed


asyncio.run(outer())
