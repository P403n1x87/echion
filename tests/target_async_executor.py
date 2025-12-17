import asyncio
import time


def slow_sync_function() -> None:
    time.sleep(1)


async def asynchronous_function() -> None:
    await asyncio.get_running_loop().run_in_executor(executor=None, func=slow_sync_function)


async def main():
    await asynchronous_function()

if __name__ == "__main__":
    asyncio.run(main())
