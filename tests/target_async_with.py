import asyncio
from typing import Union
from types import TracebackType


async def context_manager_dep() -> None:
    await asyncio.sleep(0.25)


async def some_function() -> None:
    await asyncio.sleep(0.25)


class AsyncContextManager:
    async def __aenter__(self) -> None:
        await context_manager_dep()

    async def __aexit__(self, exc_type: Union[type, None], exc_value: Union[object, None], traceback: Union[TracebackType, None]) -> None:
        await asyncio.sleep(0.25)


async def asynchronous_function() -> None:
    async with AsyncContextManager():
        await some_function()


async def main():
    await asynchronous_function()

if __name__ == "__main__":
    asyncio.run(main())
