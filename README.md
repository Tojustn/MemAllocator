# MemLab

MemLab is a C++20 project for exploring memory-allocation strategies and their
performance characteristics on Linux. It implements three allocators from
scratch and compares them with glibc `malloc` using repeatable allocation
workloads.

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

The current suite contains 23 tests covering alignment, exhaustion, reset,
block reuse, splitting and coalescing behavior, multiple mappings, object
lifetimes, and region cleanup. All 23 tests also pass with AddressSanitizer and
UndefinedBehaviorSanitizer enabled.

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

## Project layout

```text
include/
  arena.hpp          Monotonic arena allocator
  pool.hpp           Fixed-size pool allocator
  freelist.hpp       First-fit free-list allocator
  mmap_region.hpp    Metadata shared by mmap-backed allocators
tests/               Catch2 correctness tests
benchmarks/
  adapters.hpp       Common interface used by the benchmark suite
  bench.cpp          Benchmark workloads and CSV-style reporting
```

## Limitations

- The allocators are single-threaded and provide no synchronization.
- The pool supports one slot size per allocator instance.
- The arena only reclaims memory through a full reset.
- The free list accepts allocations no larger than one allocator region.
- Benchmark results are sensitive to hardware, compiler, system load, and
  allocator implementation details.
