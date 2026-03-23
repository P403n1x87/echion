import asyncio


async def worker():
    await asyncio.sleep(3)


async def main():
    tasks = [
        asyncio.create_task(worker(), name=f"Worker-{i}")
        for i in range(5)
    ]
    await asyncio.gather(*tasks)


asyncio.run(main())
