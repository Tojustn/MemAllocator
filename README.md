# MemLab

A C++20 project exploring memory allocation on Linux, with arena, free-list,
fixed-size pool, and thread-caching pool allocators benchmarked against glibc
`malloc`.

## Allocators

| Allocator | Strategy |
|---|---|
| `ArenaAllocator<N>` | Bump allocation in a fixed buffer; reclaim everything with `reset()`. |
| `FreeListAllocator` | First-fit allocation with block splitting and coalescing. |
| `PoolAllocator<T>` | Reusable fixed-size slots backed by 4 KiB `mmap` regions. |
| `ThreadSafePoolAllocator<T>` | A mutex-protected pool with per-thread caches and batched transfers. |

The arena, free list, and base pool are single-threaded. Each `ThreadCache<T>`
must stay on its owning thread, and its central pool must outlive it. The arena
cannot free individual allocations; free-list allocations must fit in one region.

## Requirements

- Linux
- A C++20 compiler (GCC or Clang)
- CMake 3.20 or newer

Catch2 3.16.1 is fetched automatically when it is not already installed.

## Build and test

Configure a Release build with tests and benchmarks enabled:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DMEMORY_ALLOCATORS_BUILD_TESTS=ON \
  -DMEMORY_ALLOCATORS_BUILD_BENCHMARKS=ON
cmake --build build -j
```

Run the correctness tests:

```bash
ctest --test-dir build --output-on-failure
```

## Usage example

The allocators return raw storage, so the caller controls object construction
and destruction:

```cpp
#include "pool.hpp"

#include <memory>

struct Order {
  int id;
  double price;
  int quantity;
};

int main() {
  PoolAllocator<Order> pool;

  Order* order = std::construct_at(pool.allocate(), Order{1, 100.50, 10});

  // Use order...

  std::destroy_at(order);
  pool.deallocate(order);
}
```

`ArenaAllocator` and `FreeListAllocator` similarly return raw storage, but
their allocation and reclamation policies differ as described above.

## Benchmarks

```bash
./build/benchmarks/allocator_bench
./build/benchmarks/allocator_thread_bench
```

Recorded on an AMD Ryzen 7 6800H, Ubuntu 24.04.5, GCC 13.3.0, with a Release
build (`-O3 -DNDEBUG`) and sanitizers disabled. Results depend on the machine
and workload.

### Single-threaded latency

Both workloads use 64-byte allocations and 1,000-operation batches. Each of
five runs measures one million operations after warmup. Values below are the
averages of each run's p50 and p99 batch latencies; setup and cleanup are untimed.

**Allocation only**

| Allocator | Average p50 | Average p99 |
|---|---:|---:|
| glibc `malloc` | 4,775 ns | 5,612 ns |
| First-fit free list | 1,380 ns | 1,769 ns |
| Monotonic arena | 922 ns | 1,064 ns |
| Fixed-size pool | 1,164 ns | 1,260 ns |

**Random replacement** — allocate and free pseudorandom slots in a live set
of 1,000 objects. The arena is excluded because it cannot free individual objects.

| Allocator | Average p50 | Average p99 |
|---|---:|---:|
| glibc `malloc` | 7,784 ns | 11,601 ns |
| First-fit free list | 2,286 ns | 3,408 ns |
| Fixed-size pool | 1,693 ns | 1,909 ns |

The fixed-size pool's average p50 is **4.10x faster** than glibc `malloc` for
allocation only and **4.60x faster** for random replacement on this workload.

### Multithreaded throughput

Each worker performs one million allocation/deallocation pairs in batches of
256 live, 64-byte objects. Results are aggregate million pairs per second,
reported as the median of five runs after warmup.

| Allocator | 1 thread | 2 threads | 4 threads | 8 threads |
|---|---:|---:|---:|---:|
| glibc `malloc` | 76.9 M pairs/s | 139.4 M pairs/s | 292.0 M pairs/s | 422.6 M pairs/s |
| Global-lock pool | 88.8 M pairs/s | 15.7 M pairs/s | 13.4 M pairs/s | 10.4 M pairs/s |
| Thread-caching pool | 295.9 M pairs/s | 121.3 M pairs/s | 99.2 M pairs/s | 82.4 M pairs/s |
| Thread cache, no timed transfers | 429.9 M pairs/s | 853.3 M pairs/s | 1,817.8 M pairs/s | 3,295.8 M pairs/s |

Batching improves throughput over a global lock, but the normal thread cache
loses throughput as threads increase because refills and flushes contend on the
central pool. The no-transfer configuration keeps the entire batch in each
thread's cache to isolate local-cache performance; it is a diagnostic result,
with short timings that need longer runs before drawing broader conclusions.

## Project layout

- `include/` — allocator implementations.
- `tests/` — Catch2 correctness tests.
- `benchmarks/` — latency and multithreaded throughput benchmarks.
- `scripts/` — timing utilities.
