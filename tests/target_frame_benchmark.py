#!/usr/bin/env python3
"""
Performance measurement target for frame walking overhead.

This creates various stack patterns that can be used to measure
the performance impact of frame walking with different characteristics:
- Deep stacks (many frames to walk)
- Wide execution (many threads)
- Mixed patterns
"""

import time
import threading
import sys
import os
from concurrent.futures import ThreadPoolExecutor


def fibonacci_recursive(n: int) -> int:
    """Create a deep recursive stack."""
    if n <= 1:
        return n
    return fibonacci_recursive(n - 1) + fibonacci_recursive(n - 2)


def nested_calls_deep(depth: int) -> None:
    """Create a linear deep call stack."""
    if depth <= 0:
        # Busy work at the bottom of the stack
        start = time.time()
        counter = 0
        while time.time() - start < 0.1:  # 100ms of work
            counter += 1
        return
    
    # Add some local variables to make frames more interesting
    local_var1 = f"frame_{depth}"
    local_var2 = [i for i in range(10)]
    local_var3 = {"depth": depth, "data": local_var2}
    
    nested_calls_deep(depth - 1)


def mixed_workload(thread_id: int, duration: float) -> None:
    """Mixed workload with various call patterns."""
    end_time = time.time() + duration
    iteration = 0
    
    while time.time() < end_time:
        iteration += 1
        
        if iteration % 3 == 0:
            # Deep recursive calls
            try:
                fibonacci_recursive(15)  # Creates ~30k frames
            except RecursionError:
                pass
        elif iteration % 3 == 1:
            # Linear deep calls
            nested_calls_deep(30)
        else:
            # Busy work with shallow stack
            counter = 0
            for _ in range(10000):
                counter += len(str(counter))
        
        # Small sleep to allow profiler to sample
        time.sleep(0.001)


def benchmark_deep_stacks():
    """Benchmark with very deep call stacks."""
    print("=== Deep Stack Benchmark ===")
    print(f"PID: {os.getpid()}")
    
    def deep_worker():
        try:
            nested_calls_deep(100)  # 100 frame deep stack
        except RecursionError:
            nested_calls_deep(50)   # Fallback to 50 if recursion limit hit
    
    # Create multiple threads with deep stacks
    threads = []
    for i in range(4):
        thread = threading.Thread(target=deep_worker, name=f"DeepWorker-{i}")
        threads.append(thread)
    
    start_time = time.time()
    for thread in threads:
        thread.start()
    
    for thread in threads:
        thread.join()
    
    duration = time.time() - start_time
    print(f"Deep stack benchmark completed in {duration:.2f}s")


def benchmark_wide_execution():
    """Benchmark with many threads (wide execution)."""
    print("=== Wide Execution Benchmark ===")
    print(f"PID: {os.getpid()}")
    
    duration = 5.0
    num_threads = 10
    
    # Use ThreadPoolExecutor for better thread management
    with ThreadPoolExecutor(max_workers=num_threads) as executor:
        futures = []
        for i in range(num_threads):
            future = executor.submit(mixed_workload, i, duration)
            futures.append(future)
        
        # Wait for all to complete
        for future in futures:
            future.result()
    
    print(f"Wide execution benchmark with {num_threads} threads completed")


def benchmark_mixed_patterns():
    """Benchmark with mixed call patterns."""
    print("=== Mixed Patterns Benchmark ===")
    print(f"PID: {os.getpid()}")
    
    duration = 8.0
    
    # Different thread types
    threads = [
        threading.Thread(target=mixed_workload, args=(0, duration), name="Mixed-0"),
        threading.Thread(target=mixed_workload, args=(1, duration), name="Mixed-1"),
        threading.Thread(target=lambda: nested_calls_deep(80), name="Deep-Linear"),
        threading.Thread(target=lambda: [fibonacci_recursive(12) for _ in range(10)], name="Deep-Recursive"),
    ]
    
    start_time = time.time()
    for thread in threads:
        thread.start()
    
    for thread in threads:
        thread.join()
    
    actual_duration = time.time() - start_time
    print(f"Mixed patterns benchmark completed in {actual_duration:.2f}s")


if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(description="Frame walking performance target")
    parser.add_argument("--pattern", choices=["deep", "wide", "mixed", "all"], 
                       default="all", help="Benchmark pattern to run")
    
    args = parser.parse_args()
    
    print("Frame Walking Performance Target")
    print("=" * 40)
    print("This program creates various call stack patterns for profiling.")
    print("Run echion on this process to measure frame walking performance.")
    print()
    
    if args.pattern in ["deep", "all"]:
        benchmark_deep_stacks()
        if args.pattern == "all":
            time.sleep(1)
    
    if args.pattern in ["wide", "all"]:
        benchmark_wide_execution()
        if args.pattern == "all":
            time.sleep(1)
    
    if args.pattern in ["mixed", "all"]:
        benchmark_mixed_patterns()
    
    print("\nAll benchmarks completed!")
