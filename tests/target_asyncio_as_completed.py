import random
import asyncio


async def other(t: float):
    await asyncio.sleep(t)


async def wait_and_return_delay(t: float) -> float:
    await other(t)
    return t


async def main() -> None:
    # Create a mix of Tasks and Coroutines
    futures = [
        asyncio.create_task(wait_and_return_delay(i / 10))
        if i % 2 == 0
        else wait_and_return_delay(i / 10)
        for i in range(10)
    ]
    random.shuffle(futures)
    result: list[float] = []
    for future in asyncio.as_completed(futures):
        result.append(await future)

    assert sorted(result) == result


if __name__ == "__main__":
    asyncio.run(main())
