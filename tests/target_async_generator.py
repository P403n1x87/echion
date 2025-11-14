import asyncio
from typing import AsyncGenerator


async def deep_dependency():
    await asyncio.sleep(0.05)


async def async_generator_dep(i: int) -> AsyncGenerator[int, None]:
    for j in range(i):
        await deep_dependency()
        yield j


async def async_generator() -> AsyncGenerator[int, None]:
    for i in range(10):
        async for j in async_generator_dep(i):
            yield j


async def asynchronous_function() -> None:
    async for i in async_generator():
        print(i)


async def main():
    await asynchronous_function()


if __name__ == "__main__":
    asyncio.run(main())
