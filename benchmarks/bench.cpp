// Meant for benchmarking malloc vs custom allocators
#include <algorithm>
#include <array>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "adapters.hpp"
using ns = std::uint64_t;
struct Stats {
  // Meausuring nanoseconds using steady clock
  ns p50{};
  ns p99{};
};

Stats fetchStats(std::vector<ns>& times) {
  Stats stats{};
  if (times.empty())
    return stats;

  std::sort(times.begin(), times.end());

  const std::size_t n = times.size();

  if (n % 2 == 0) {
    stats.p50 = times[n / 2 - 1] / 2 + times[n / 2] / 2 + ((times[n / 2 - 1] % 2 + times[n / 2] % 2) / 2);
  }
  else {
    stats.p50 = times[n / 2];
  }

  stats.p99 = times[(n * 99 + 99) / 100 - 1];

  return stats;
}

// concept for benchmark allocators
template <class A>
concept BenchmarkAllocator = requires(A a, std::size_t n, void* p) {
  { a.allocate(n) } -> std::same_as<void*>;
  { a.deallocate(p) } noexcept -> std::same_as<void>;
  { a.reset() } noexcept -> std::same_as<void>;
};

/*
 *
 * Three types of benchmarks
 * 1) Many allocations of small sizes, then freeing at the end
 * 2) Random allocation of random sizes and freeing of random sizes
 *   a) arena wont participate because its monotonic (aka bump allocator)
 * 3) Allocations of one size, then random freeings
 *
 * Using steady clock
 */

// GCC/Clang barrier: keep each allocation observable in optimized builds.
inline void observeAllocation(void* pointer) noexcept {
  asm volatile("" : : "r"(pointer) : "memory");
}

template <BenchmarkAllocator Alloc>
std::vector<ns> workbenchA(std::size_t size) {
  if (size == 0) {
    throw std::invalid_argument("Allocation size must be positive");
  }

  Alloc allocator{};
  constexpr std::size_t NUM_ALLOCATIONS = 1'000'000;
  constexpr std::size_t BATCH_SIZE = 1000;
  std::array<void*, BATCH_SIZE> pointers{};
  std::vector<ns> times;
  times.reserve(NUM_ALLOCATIONS / BATCH_SIZE);

  for (std::size_t batch = 0; batch < NUM_ALLOCATIONS / BATCH_SIZE; ++batch) {
    std::size_t allocated = 0;
    try {
      const auto start = std::chrono::steady_clock::now();
      for (; allocated < BATCH_SIZE; ++allocated) {
        void* pointer = allocator.allocate(size);
        if (pointer == nullptr) {
          throw std::bad_alloc{};
        }
        pointers[allocated] = pointer;
        observeAllocation(pointer);
      }
      const auto end = std::chrono::steady_clock::now();
      const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
      times.push_back(static_cast<ns>(elapsed));
    }
    catch (...) {
      while (allocated > 0) {
        allocator.deallocate(pointers[--allocated]);
      }
      allocator.reset();
      throw;
    }

    // Measure allocation only; free in reverse order and reset outside the timer.
    while (allocated > 0) {
      allocator.deallocate(pointers[--allocated]);
    }
    allocator.reset();
  }
  return times;
}

void printStats(const char* name, std::vector<ns>& times) {
  const auto stats = fetchStats(times);
  std::cout << name << ',' << times.size() << ',' << stats.p50 << ',' << stats.p99 << '\n';
}

int main() {
  try {
    constexpr std::size_t size = 64;
    auto mallocTimes = workbenchA<MallocAdapter>(size);
    auto freeListTimes = workbenchA<FreeListAdapter>(size);
    auto arenaTimes = workbenchA<ArenaAdapter<>>(size);
    auto poolTimes = workbenchA<PoolAdapter<64>>(size);

    // Each sample is a batch of 1,000 allocations, not an individual allocation.
    std::cout << "allocator,batches,p50_batch_ns,p99_batch_ns\n";
    printStats(MallocAdapter::name, mallocTimes);
    printStats(FreeListAdapter::name, freeListTimes);
    printStats(ArenaAdapter<>::name, arenaTimes);
    printStats(PoolAdapter<64>::name, poolTimes);
  }
  catch (const std::exception& error) {
    std::cerr << "Benchmark failed: " << error.what() << '\n';
    return 1;
  }
}
