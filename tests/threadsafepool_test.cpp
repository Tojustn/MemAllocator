#include "../include/threadsafepool.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <barrier>
#include <cstddef>
#include <thread>
#include <unordered_set>
#include <vector>

TEST_CASE("threads repeatedly reuse their local cache") {
  ThreadSafePoolAllocator<int> allocator{};
  constexpr int NUM_THREADS = 4;
  constexpr int ITERATIONS = 10'000;
  std::atomic<bool> failed{false};

  {
    std::vector<std::jthread> threads;
    threads.reserve(NUM_THREADS);

    for (int i = 0; i < NUM_THREADS; ++i) {
      threads.emplace_back([&allocator, &failed, i] {
        ThreadCache<int> cache{allocator};

        for (int j = 0; j < ITERATIONS; ++j) {
          int* ptr = cache.allocate();

          *ptr = i * ITERATIONS + j;
          if (*ptr != i * ITERATIONS + j) {
            failed.store(true, std::memory_order_relaxed);
          }

          cache.deallocate(ptr);
        }
      });
    }
  }

  REQUIRE_FALSE(failed.load(std::memory_order_relaxed));
}

TEST_CASE("threads hold simultaneous allocations across refills and flushes") {
  ThreadSafePoolAllocator<int> allocator{};
  constexpr int NUM_THREADS = 4;
  constexpr int ALLOCATIONS_PER_THREAD = 512;
  std::barrier sync_point{NUM_THREADS};
  std::atomic<bool> failed{false};
  std::vector<std::vector<int*>> allocations(NUM_THREADS);

  {
    std::vector<std::jthread> threads;
    threads.reserve(NUM_THREADS);

    for (int i = 0; i < NUM_THREADS; ++i) {
      threads.emplace_back([&allocator, &allocations, &failed, &sync_point, i] {
        ThreadCache<int> cache{allocator};
        auto& local_allocations = allocations[i];
        local_allocations.reserve(ALLOCATIONS_PER_THREAD);

        for (int j = 0; j < ALLOCATIONS_PER_THREAD; ++j) {
          int* ptr = cache.allocate();
          *ptr = i * ALLOCATIONS_PER_THREAD + j;
          local_allocations.push_back(ptr);
        }

        sync_point.arrive_and_wait();

        for (int j = 0; j < ALLOCATIONS_PER_THREAD; ++j) {
          if (*local_allocations[j] != i * ALLOCATIONS_PER_THREAD + j) {
            failed.store(true, std::memory_order_relaxed);
          }
        }

        sync_point.arrive_and_wait();

        for (int* ptr : local_allocations) {
          cache.deallocate(ptr);
        }
      });
    }
  }

  REQUIRE_FALSE(failed.load(std::memory_order_relaxed));

  std::unordered_set<int*> unique_allocations;
  for (const auto& local_allocations : allocations) {
    for (int* ptr : local_allocations) {
      REQUIRE(unique_allocations.insert(ptr).second);
    }
  }
}
