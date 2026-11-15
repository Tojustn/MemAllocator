// Compare malloc with the custom allocator implementations.
#include <algorithm>
#include <array>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <ratio>
#include <stdexcept>
#include <vector>

#include "adapters.hpp"

namespace {

// -----------------------------------------------------------------------------
// Benchmark configuration
// -----------------------------------------------------------------------------

using Nanoseconds = std::uint64_t;

constexpr std::size_t ALLOCATION_SIZE = 64;
constexpr std::size_t NUM_ALLOCATIONS = 1'000'000;
constexpr std::size_t BATCH_SIZE = 1'000;
constexpr std::size_t NUM_BATCHES = NUM_ALLOCATIONS / BATCH_SIZE;
constexpr std::size_t LIVE_SET = 1'000;
constexpr std::size_t NUM_RUNS = 5;

// -----------------------------------------------------------------------------
// Result statistics
// -----------------------------------------------------------------------------

struct Stats {
  Nanoseconds p50{};
  Nanoseconds p99{};
};

Stats calculateStats(std::vector<Nanoseconds>& times) {
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

// Allocator interface and shared helpers

template <class A>
concept BenchmarkAllocator = requires(A a, std::size_t n, void* p) {
  { a.allocate(n) } -> std::same_as<void*>;
  { a.deallocate(p) } noexcept -> std::same_as<void>;
  { a.reset() } noexcept -> std::same_as<void>;
};

// GCC/Clang barrier: keep each allocation observable in optimized builds.
inline void observeAllocation(void* pointer) noexcept {
  // The pointer must remain in a register and memory may be observed or changed.
  asm volatile("" : : "r"(pointer) : "memory");
}

template <BenchmarkAllocator Alloc>
void warmup(Alloc& allocator, std::size_t size, std::size_t count) {
  std::vector<void*> pointers;
  pointers.reserve(count);

  for (std::size_t i = 0; i < count; ++i) {
    void* ptr = allocator.allocate(size);
    observeAllocation(ptr);
    pointers.push_back(ptr);
  }

  for (auto it = pointers.rbegin(); it != pointers.rend(); ++it) {
    allocator.deallocate(*it);
  }

  allocator.reset();
}

std::vector<std::size_t> generateRandomIndices() {
  std::mt19937 rng(42);
  std::uniform_int_distribution<std::size_t> distribution(0, LIVE_SET - 1);

  std::vector<std::size_t> randomIndices(NUM_ALLOCATIONS);
  for (auto& index : randomIndices) {
    index = distribution(rng);
  }

  return randomIndices;
}

// Scenario 1: allocation latency

// Time allocation in batches. Deallocation and reset happen outside the timer.
template <BenchmarkAllocator Alloc>
std::vector<Nanoseconds> benchmarkAllocation(std::size_t size) {
  if (size == 0) {
    throw std::invalid_argument("Allocation size must be positive");
  }

  Alloc allocator{};
  std::array<void*, BATCH_SIZE> pointers{};
  std::vector<Nanoseconds> times;
  times.reserve(NUM_BATCHES);

  warmup(allocator, size, BATCH_SIZE);

  for (std::size_t batch = 0; batch < NUM_BATCHES; ++batch) {
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
      times.push_back(static_cast<Nanoseconds>(elapsed));
    } catch (...) {
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

// Scenario 2: allocation allocation_only
allocator, runs, batches_per_run, avg_p50_batch_ns, avg_p99_batch_ns malloc, 5, 1000, 4775, 5612 freelist, 5, 1000,
    1380, 1769 arena, 5, 1000, 922, 1064 pool, 5, 1000, 1164,
    1260

    random_deallocation allocator,
    runs, batches_per_run, avg_p50_batch_ns, avg_p99_batch_ns malloc, 5, 1000, 7784, 11601 freelist, 5, 1000, 2286,
    3408 pool, 5, 1000, 1693,
    1909with random deallocation
    // pre allocate live ptrs, then deallocate randomly and allocate right after
    template <BenchmarkAllocator Alloc>
    std::vector<Nanoseconds> benchmarkRandomDeallocation(std::size_t size) {
  Alloc allocator{};

  std::vector<Nanoseconds> times;
  times.reserve(NUM_BATCHES);
  // random indices for deallocation
  std::vector<std::size_t> random_indices{generateRandomIndices()};
  warmup(allocator, size, LIVE_SET);
  std::array<void*, LIVE_SET> pointers{};
  for (std::size_t i = 0; i < LIVE_SET; ++i) {
    pointers[i] = allocator.allocate(size);
  }

  for (std::size_t i = 0; i < NUM_BATCHES; ++i) {

    std::size_t allocation_number{0};
    try {
      const auto start = std::chrono::steady_clock::now();
      for (; allocation_number < BATCH_SIZE; ++allocation_number) {
        std::size_t index = random_indices[allocation_number + (i * BATCH_SIZE)];
        allocator.deallocate(pointers[index]);
        pointers[index] = nullptr;
        pointers[index] = allocator.allocate(size);
        observeAllocation(pointers[index]);
      }
      const auto end = std::chrono::steady_clock::now();
      const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

      times.push_back(static_cast<Nanoseconds>(elapsed));
    } catch (...) {

      for (auto pointer = pointers.rbegin(); pointer != pointers.rend(); ++pointer) {
        if (*pointer != nullptr) {
          allocator.deallocate(*pointer);
        }
      }
      allocator.reset();
      throw;
    }
  }

  for (auto pointer = pointers.rbegin(); pointer != pointers.rend(); ++pointer) {
    if (*pointer != nullptr) {
      allocator.deallocate(*pointer);
    }
  }
  allocator.reset();

  return times;
}
// Reporting and benchmark suite

void printAverageStats(const char* name, const Stats& totals) {
  std::cout << name << ',' << NUM_RUNS << ',' << NUM_BATCHES << ',' << totals.p50 / NUM_RUNS << ','
            << totals.p99 / NUM_RUNS << '\n';
}

template <BenchmarkAllocator Alloc>
void runAllocationBenchmark(std::size_t size) {
  Stats totals{};
  for (std::size_t run = 0; run < NUM_RUNS; ++run) {
    auto times = benchmarkAllocation<Alloc>(size);
    const auto stats = calculateStats(times);
    totals.p50 += stats.p50;
    totals.p99 += stats.p99;
  }
  printAverageStats(Alloc::name, totals);
}

template <BenchmarkAllocator Alloc>
void runRandomDeallocationBenchmark(std::size_t size) {
  Stats totals{};
  for (std::size_t run = 0; run < NUM_RUNS; ++run) {
    auto times = benchmarkRandomDeallocation<Alloc>(size);
    const auto stats = calculateStats(times);
    totals.p50 += stats.p50;
    totals.p99 += stats.p99;
  }
  printAverageStats(Alloc::name, totals);
}

void runBenchmarks() {
  // Each sample is one batch, not one individual allocation.
  std::cout << "allocation_only\n";
  std::cout << "allocator,runs,batches_per_run,avg_p50_batch_ns,avg_p99_batch_ns\n";
  runAllocationBenchmark<MallocAdapter>(ALLOCATION_SIZE);
  runAllocationBenchmark<FreeListAdapter>(ALLOCATION_SIZE);
  runAllocationBenchmark<ArenaAdapter<>>(ALLOCATION_SIZE);
  runAllocationBenchmark<PoolAdapter<64>>(ALLOCATION_SIZE);

  std::cout << "\nrandom_deallocation\n";
  std::cout << "allocator,runs,batches_per_run,avg_p50_batch_ns,avg_p99_batch_ns\n";
  runRandomDeallocationBenchmark<MallocAdapter>(ALLOCATION_SIZE);
  runRandomDeallocationBenchmark<FreeListAdapter>(ALLOCATION_SIZE);
  runRandomDeallocationBenchmark<PoolAdapter<64>>(ALLOCATION_SIZE);
}

} // namespace

int main() {
  try {
    runBenchmarks();
  } catch (const std::exception& error) {
    std::cerr << "Benchmark failed: " << error.what() << '\n';
    return 1;
  }
}
