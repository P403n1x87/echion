import asyncio
import math
import time


# Start an asyncio loop BEFORE importing profiler modules
# This simulates the bug scenario where a loop exists before profiling is enabled
loop = asyncio.new_event_loop()
asyncio.set_event_loop(loop)


async def outer_function():
    async def background_wait_function() -> None:
        await asyncio.sleep(2.5)

    async def background_math_function() -> None:
        s = 0.0
        for i in range(100000):
            s += math.sin(i)

    background_task = loop.create_task(background_wait_function(), name="Task-background_wait")
    math_task = loop.create_task(background_math_function(), name="Task-background_math")
    assert background_task is not None

    sleep_time = 0.2
    loop_run_time = 0.75

    async def sub_coro() -> None:
        start_time = time.time()
        while time.time() < start_time + loop_run_time:
            await asyncio.sleep(sleep_time)

    async def main_coro():
        await sub_coro()
        await asyncio.sleep(0.25)

    result = await main_coro()

    await background_task
    await math_task

    return result

main_task = loop.create_task(outer_function(), name="Task-main")
loop.run_until_complete(main_task)
