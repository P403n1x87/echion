import asyncio
from types import TracebackType


async def context_manager_dep() -> None:
    await asyncio.sleep(1.0)


class AsyncContextManager:
    async def __aenter__(self) -> None:
        await context_manager_dep()

    async def __aexit__(self, exc_type: type | None, exc_value: object | None, traceback: TracebackType | None) -> None:
        await asyncio.sleep(1.0)


async def asynchronous_function() -> None:
    async with AsyncContextManager():
        await asyncio.sleep(1.0)


async def main():
    await asynchronous_function()

if __name__ == "__main__":
    asyncio.run(main())
