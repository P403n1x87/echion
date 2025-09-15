import asyncio
import time
import sys

from . import task_modifier


async def foobar():
    time.sleep(0.5)


async def baz():
    await foobar()
    await asyncio.sleep(0.5)


async def bar():
    await baz()
    await asyncio.create_task(baz(), name="Task-baz")


async def foo():
    task = asyncio.create_task(bar(), name="Task-bar")
    if sys.version_info >= (3, 13):
        task_modifier.set_task_name_to_big_int(task)
    await task


async def main():
    await foo()


if __name__ == "__main__":
    asyncio.run(main())
