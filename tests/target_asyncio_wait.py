import asyncio
import random
from typing import cast, List, Set


async def inner() -> None:
    await asyncio.sleep(random.random())


async def outer() -> None:
    coros: List[asyncio.Task] = [
        asyncio.create_task(inner(), name=f"inner-{i}") for i in range(10)
    ]

    initial_coro_count = len(coros)
    completed = 0
    while completed < initial_coro_count:
        done, pending = await asyncio.wait(coros, return_when=asyncio.FIRST_COMPLETED)
        completed += len(done)
        coros = list(cast(Set[asyncio.Task], pending))


async def main():
    await outer()


if __name__ == "__main__":
    asyncio.run(main())
