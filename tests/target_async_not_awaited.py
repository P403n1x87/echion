import asyncio


async def func_not_awaited() -> None:
    await asyncio.sleep(0.5)


async def func_awaited() -> None:
    await asyncio.sleep(1)


async def parent() -> asyncio.Task:
    t_not_awaited = asyncio.create_task(func_not_awaited(), name="Task-not_awaited")
    t_awaited = asyncio.create_task(func_awaited(), name="Task-awaited")

    await t_awaited

    # At this point, we have not awaited t_not_awaited but it should have finished
    # before t_awaited as the delay is much shorter.
    # Returning it to avoid the warning on unused variable.
    return t_not_awaited


def main():
    asyncio.run(parent())


if __name__ == "__main__":
    main()
