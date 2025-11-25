import asyncio


async def fib(n):
    if n < 2:
        return n

    return await fib(n - 1) + await fib(n - 2)


async def f1():
    await f2()


async def f2():
    await f3()


async def f3():
    await asyncio.gather(
        asyncio.create_task(f4(), name="F4-1"),
        asyncio.create_task(f4(), name="F4-2"),
    )


async def f4():
    await f5()


async def f5():
    await fib(31)


async def main():
    await f1()


asyncio.run(main())