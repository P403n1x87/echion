import asyncio
from typing import AsyncGenerator


async def deep_dependency():
    # This is a regular (non-generator) coroutine called
    # by an async generator.
    await asyncio.sleep(0.15)


async def async_generator_dep(i: int) -> AsyncGenerator[int, None]:
    # This is an async generator called by an async generator.
    # We want to make sure that recursive async generators are correctly sampled.
    for j in range(i):
        await deep_dependency()
        yield j


async def async_generator() -> AsyncGenerator[int, None]:
    # This is an async generator called by a coroutine.
    # We want to make sure we unwind async generators correctly.
    for i in range(5):
        async for j in async_generator_dep(i):
            yield j


async def asynchronous_function() -> None:
    # This is a normal (non-generator) coroutine that calls into an async generator.
    # Stack samples should not stopped at this function, they should continue unwinding
    # into the async generator.
    async for _ in async_generator():
        pass


async def main():
    await asynchronous_function()


if __name__ == "__main__":
    asyncio.run(main())
