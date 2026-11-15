# MemLab

MemLab is a C++20 project for exploring memory-allocation strategies and their
performance characteristics on Linux. It implements single-threaded arena,
free-list, and pool allocators plus a thread-caching pool, and compares them
with glibc `malloc` using repeatable workloads.

In local benchmarks of 64-byte allocations, the fixed-size pool delivered
**4.10x and 4.60x speedups** over glibc `malloc` in allocation-only and random
replacement workloads, respectively. Results depend on the machine and
workload; the complete methodology and environment appear below.

## Allocators

### Monotonic arena

`ArenaAllocator<N>` owns a fixed-size, inline buffer and advances a bump
pointer for each allocation. It supports power-of-two alignments up to 64 bytes
and reclaims all storage at once with `reset()`. Individual allocations cannot
be freed.

### Fixed-size pool

`PoolAllocator<T>` serves equally sized, properly aligned slots from 4 KiB
regions obtained with `mmap`. Freed slots are linked into a free list and reused
by later allocations.

### First-fit free list

`FreeListAllocator` obtains 4 KiB regions with `mmap`, searches free blocks
using a first-fit policy, splits blocks when possible, and coalesces adjacent
free blocks during deallocation. Owned regions are released with `munmap` when
the allocator is destroyed.

### Thread-caching pool

`ThreadSafePoolAllocator<T>` owns a central `PoolAllocator<T>` protected by a
mutex. Each worker creates a `ThreadCache<T>` that obtains and returns slots in
batches, allowing most allocation and deallocation operations to use a private
free list without locking the central pool. A cache refills with 32 slots,
flushes 32 slots when it grows beyond 128, and releases its remaining slots when
destroyed.

The central allocator must outlive every cache created from it, and a
`ThreadCache` must only be used by its owning thread:

```cpp
#include "threadsafepool.hpp"

#include <thread>
#include <vector>

int main() {
  ThreadSafePoolAllocator<int> allocator;

  {
    std::vector<std::jthread> threads;
    for (int i = 0; i < 4; ++i) {
      threads.emplace_back([&allocator, i] {
        ThreadCache<int> cache{allocator};

        int* value = cache.allocate();
        *value = i;
        cache.deallocate(value);
      });
    }
  } // All caches and threads are destroyed before allocator.
}
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

The current suite contains 25 tests covering alignment, exhaustion, reset,
block reuse, splitting and coalescing behavior, multiple mappings, object
lifetimes, region cleanup, simultaneous multithreaded allocations, cache
refills, and cache flushing. All 25 tests pass with AddressSanitizer,
UndefinedBehaviorSanitizer, and ThreadSanitizer.

Configure and run the ThreadSanitizer build separately from ASan/UBSan:

```bash
cmake -S . -B build-tsan \
  -DMEMORY_ALLOCATORS_ENABLE_TSAN=ON \
  -DMEMORY_ALLOCATORS_BUILD_BENCHMARKS=OFF
cmake --build build-tsan --target memory_allocators_tests -j
TSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build-tsan --output-on-failure
```

## Benchmarks

Run the benchmark executable with:

```bash
./build/benchmarks/allocator_bench
```

The suite benchmarks 64-byte allocations with two workloads:

1. **Allocation only:** allocate batches of 1,000 objects, time allocation,
   then deallocate outside the timed region.
2. **Random replacement:** maintain a live set of 1,000 objects and repeatedly
   deallocate and replace a pseudorandom slot using a fixed seed.

Each allocator performs five independent runs per workload. A new allocator is
constructed for every run, and each run performs an untimed warmup sized to the
workload's maximum live set before measurement begins. Each run then contains
1,000 batches and one million measured operations.

The program calculates p50 and p99 batch latency independently for each run,
then reports the arithmetic mean of the five run-level p50 values and the five
run-level p99 values. The reported p50 is therefore not the median of one
combined five-million-operation sample.

`observeAllocation()` passes every returned pointer through a GCC compiler
barrier, keeping allocations observable in optimized builds. Setup, warmup,
random-index generation, and final cleanup occur outside the timed regions.

### Results

The following results are from a local Linux Release build. Times are
nanoseconds per 1,000-operation batch and are averages across five runs.

| Environment | Value |
|---|---|
| CPU | AMD Ryzen 7 6800H (8 cores, 16 threads) |
| Operating system | Ubuntu 24.04.5 LTS, x86-64 |
| Compiler | GCC 13.3.0 |
| CMake build type | `Release` |
| Compiler flags | `-O3 -DNDEBUG -std=c++20 -Wall -Wextra -Wpedantic` |
| Sanitizers | Disabled during benchmark runs |

#### Allocation only

| Allocator | Average p50 | Average p99 |
|---|---:|---:|
| glibc `malloc` | 4,775 ns | 5,612 ns |
| First-fit free list | 1,380 ns | 1,769 ns |
| Monotonic arena | 922 ns | 1,064 ns |
| Fixed-size pool | 1,164 ns | 1,260 ns |

For this workload, the pool delivered approximately a **4.10x speedup** over
glibc `malloc`, corresponding to 75.6% lower median batch latency.

#### Random replacement

| Allocator | Average p50 | Average p99 |
|---|---:|---:|
| glibc `malloc` | 7,784 ns | 11,601 ns |
| First-fit free list | 2,286 ns | 3,408 ns |
| Fixed-size pool | 1,693 ns | 1,909 ns |

For this workload, the pool delivered approximately a **4.60x speedup** over
glibc `malloc`, corresponding to 78.3% lower median batch latency.

The arena is intentionally excluded from random replacement because it does not
support individual deallocation. These measurements describe this specific
machine, object size, compiler configuration, and workload; they are not a
general claim that a custom allocator always outperforms `malloc`.

### Multithreaded throughput benchmark

The multithreaded benchmark compares glibc `malloc`, a pool protected by one
global mutex, and the thread-caching pool. It also includes a diagnostic
thread-cache configuration that performs no central-pool transfers during the
timed region. Run it from the Release build with:

```bash
./build/benchmarks/allocator_thread_bench
```

For each allocator, it runs one, two, four, and eight worker threads. Every
worker completes one million allocation/deallocation pairs in batches of 256
live 64-byte objects. Each configuration performs an untimed warmup and five
measured runs. Barriers exclude thread creation, warmup, and cache destruction
from the timed region and release all workers together. Results are reported as
aggregate million allocation/deallocation pairs per second, with the median of
the five runs used for the summary.

For the normal thread-cache configuration, the batch size intentionally exceeds
the 32-slot refill batch and 128-slot retained limit, exercising private-cache
operations, central-pool refills, cache flushes, region growth, and mutex
contention. The diagnostic configuration refills 256 slots and retains 256
slots. Its warmup fully populates each worker's cache, so every timed batch runs
entirely on its local free list; cache cleanup remains outside the timer.

| Allocator | 1 thread | 2 threads | 4 threads | 8 threads |
|---|---:|---:|---:|---:|
| glibc `malloc` | 76.9 M pairs/s | 139.4 M pairs/s | 292.0 M pairs/s | 422.6 M pairs/s |
| Global-lock pool | 88.8 M pairs/s | 15.7 M pairs/s | 13.4 M pairs/s | 10.4 M pairs/s |
| Thread-caching pool | 295.9 M pairs/s | 121.3 M pairs/s | 99.2 M pairs/s | 82.4 M pairs/s |
| Thread cache, no timed transfers | 429.9 M pairs/s | 853.3 M pairs/s | 1,817.8 M pairs/s | 3,295.8 M pairs/s |

On this workload, ordinary batching substantially reduces contention compared
with the global-lock pool, but the normal thread-caching pool does not scale
positively beyond one thread. Removing timed central-pool transfers changes its
one-to-eight-thread scaling from 0.28x to 7.67x, strongly identifying refill and
flush contention as the primary bottleneck in this experiment. The diagnostic
fast-path measurements complete in only a few milliseconds and should be
confirmed with longer runs and profiling before being used as headline
performance claims. These results describe one local run and should be
regenerated for other machines and allocator changes.

The normal configuration begins each steady-state 256-object batch with 128
cached slots. Allocating the batch consumes those slots and requires four
32-slot refills. Returning the batch crosses the 128-slot limit four times,
causing four 32-slot flushes. Each worker therefore enters the central pool's
critical section approximately eight times per batch, and each critical section
performs 32 linked-list transfers while holding the same mutex. As the thread
count grows, these transfers serialize otherwise independent workers. The
no-transfer diagnostic starts each timed batch with 256 cached slots and retains
all 256 afterward, avoiding those critical sections entirely.

#### Optimizer verification

The benchmark's Release assembly was inspected to confirm that the compiler did
not eliminate the unusually fast local-cache workload. The generated
`runPairs<LocalOnlyThreadCachingPoolBenchmark::Worker>` function retains:

- the allocation loop that loads each free slot's `next` pointer, stores the
  returned address in the 256-entry pointer array, and decrements the cache
  count;
- the deallocation loop that writes each slot's free-list link and updates the
  cache head and count; and
- the outer operation counter, which subtracts each completed batch until all
  one million pairs have executed.

`observeAllocation()` also passes every returned address through a compiler
barrier with a memory clobber. A `perf stat` run of the complete benchmark suite
recorded approximately 73.1 billion retired instructions and 16.2 billion
branches, further confirming that the workload executes rather than collapsing
to an empty timing loop. The no-transfer result is aggregate throughput: its
roughly 3.3 billion pairs per second at eight threads corresponds to about 2.4
nanoseconds per pair on each worker, not 0.3 nanoseconds on one core.

## Project layout

```text
include/
  arena.hpp          Monotonic arena allocator
  pool.hpp           Fixed-size pool allocator
  threadsafepool.hpp Thread-caching pool allocator
  freelist.hpp       First-fit free-list allocator
  mmap_region.hpp    Metadata shared by mmap-backed allocators
tests/               Catch2 correctness tests
benchmarks/
  adapters.hpp       Common interface used by the benchmark suite
  bench.cpp          Benchmark workloads and CSV-style reporting
  multithreadbench.cpp Multithreaded throughput and scaling benchmark
```

## Limitations

- The arena, free-list, and base pool allocators are single-threaded.
- The thread-caching pool requires its owner to outlive all thread caches, and
  each cache must remain confined to one thread.
- The pool supports one slot size per allocator instance.
- The arena only reclaims memory through a full reset.
- The free list accepts allocations no larger than one allocator region.
- Benchmark results are sensitive to hardware, compiler, system load, and
  allocator implementation details.
