// Measure allocation/deallocation throughput as contention increases.
#include <algorithm>
#include <array>
#include <barrier>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <latch>
#include <mutex>
#include <new>
#include <thread>
#include <vector>

#include "pool.hpp"
#include "threadsafepool.hpp"

namespace {

constexpr std::size_t OBJECT_SIZE = 64;
constexpr std::size_t OPERATIONS_PER_THREAD = 1'000'000;
constexpr std::size_t LIVE_SET = 256;
constexpr std::size_t NUM_RUNS = 5;
constexpr std::array<std::size_t, 4> THREAD_COUNTS{1, 2, 4, 8};

struct alignas(std::max_align_t) Slot {
  std::byte data[OBJECT_SIZE];
};

static_assert(sizeof(Slot) == OBJECT_SIZE);

inline void observeAllocation(void* pointer) noexcept {
  asm volatile("" : : "r"(pointer) : "memory");
}

struct MallocBenchmark {
  static constexpr const char* name = "malloc";

  struct Worker {
    explicit Worker(MallocBenchmark&) noexcept {
    }

    void* allocate() {
      void* pointer = std::malloc(sizeof(Slot));
      if (pointer == nullptr) {
        throw std::bad_alloc{};
      }
      return pointer;
    }

    void deallocate(void* pointer) noexcept {
      std::free(pointer);
    }
  };
};

struct GlobalLockPoolBenchmark {
  static constexpr const char* name = "global_lock_pool";

  struct Worker {
    explicit Worker(GlobalLockPoolBenchmark& owner) noexcept : owner_(owner) {
    }

    void* allocate() {
      std::lock_guard<std::mutex> lock{owner_.mutex_};
      return owner_.allocator_.allocate();
    }

    void deallocate(void* pointer) {
      std::lock_guard<std::mutex> lock{owner_.mutex_};
      owner_.allocator_.deallocate(static_cast<Slot*>(pointer));
    }

  private:
    GlobalLockPoolBenchmark& owner_;
  };

private:
  PoolAllocator<Slot> allocator_;
  std::mutex mutex_;
};

struct ThreadCachingPoolBenchmark {
  static constexpr const char* name = "thread_cache_pool";

  struct Worker {
    explicit Worker(ThreadCachingPoolBenchmark& owner) noexcept : cache_(owner.allocator_) {
    }

    void* allocate() {
      return cache_.allocate();
    }

    void deallocate(void* pointer) {
      cache_.deallocate(static_cast<Slot*>(pointer));
    }

  private:
    ThreadCache<Slot> cache_;
  };

private:
  ThreadSafePoolAllocator<Slot> allocator_;
};

struct LocalOnlyThreadCachingPoolBenchmark {
  static constexpr const char* name = "thread_cache_no_transfers";

  struct Worker {
    explicit Worker(LocalOnlyThreadCachingPoolBenchmark& owner) noexcept : cache_(owner.allocator_) {
    }

    void* allocate() {
      return cache_.allocate();
    }

    void deallocate(void* pointer) {
      cache_.deallocate(static_cast<Slot*>(pointer));
    }

  private:
    ThreadCache<Slot, LIVE_SET, 32, LIVE_SET> cache_;
  };

private:
  ThreadSafePoolAllocator<Slot, LIVE_SET, 32, LIVE_SET> allocator_;
};

template <typename Worker>
void runPairs(Worker& worker, std::size_t pair_count) {
  std::array<void*, LIVE_SET> pointers{};
  std::size_t remaining = pair_count;

  while (remaining > 0) {
    const std::size_t batch_size = std::min(remaining, pointers.size());
    std::size_t allocated = 0;

    try {
      for (; allocated < batch_size; ++allocated) {
        pointers[allocated] = worker.allocate();
        observeAllocation(pointers[allocated]);
      }
    } catch (...) {
      while (allocated > 0) {
        worker.deallocate(pointers[--allocated]);
      }
      throw;
    }

    for (std::size_t i = 0; i < batch_size; ++i) {
      worker.deallocate(pointers[i]);
    }

    remaining -= batch_size;
  }
}

struct RunResult {
  std::uint64_t elapsed_ns{};
  double million_pairs_per_second{};
};

template <typename Benchmark>
RunResult runOnce(std::size_t thread_count) {
  Benchmark benchmark;
  std::barrier start_barrier{static_cast<std::ptrdiff_t>(thread_count + 1)};
  std::barrier cleanup_barrier{static_cast<std::ptrdiff_t>(thread_count + 1)};
  std::latch finished{static_cast<std::ptrdiff_t>(thread_count)};
  std::vector<std::exception_ptr> errors(thread_count);
  std::vector<std::jthread> threads;
  threads.reserve(thread_count);

  for (std::size_t worker_index = 0; worker_index < thread_count; ++worker_index) {
    threads.emplace_back([&, worker_index] {
      typename Benchmark::Worker worker{benchmark};

      try {
        runPairs(worker, LIVE_SET);
      } catch (...) {
        errors[worker_index] = std::current_exception();
      }

      start_barrier.arrive_and_wait();

      if (errors[worker_index] == nullptr) {
        try {
          runPairs(worker, OPERATIONS_PER_THREAD);
        } catch (...) {
          errors[worker_index] = std::current_exception();
        }
      }

      finished.count_down();
      cleanup_barrier.arrive_and_wait();
    });
  }

  const auto start = std::chrono::steady_clock::now();
  start_barrier.arrive_and_wait();
  finished.wait();
  const auto end = std::chrono::steady_clock::now();

  cleanup_barrier.arrive_and_wait();
  threads.clear();

  for (const auto& error : errors) {
    if (error != nullptr) {
      std::rethrow_exception(error);
    }
  }

  const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
  const std::uint64_t elapsed_ns = static_cast<std::uint64_t>(elapsed.count());
  const double elapsed_seconds = std::chrono::duration<double>(end - start).count();
  const std::size_t total_pairs = thread_count * OPERATIONS_PER_THREAD;

  return RunResult{elapsed_ns, static_cast<double>(total_pairs) / elapsed_seconds / 1'000'000.0};
}

double median(std::vector<double> values) {
  std::sort(values.begin(), values.end());
  const std::size_t middle = values.size() / 2;
  if (values.size() % 2 == 0) {
    return (values[middle - 1] + values[middle]) / 2.0;
  }
  return values[middle];
}

struct Summary {
  const char* allocator{};
  std::size_t thread_count{};
  double median_million_pairs_per_second{};
};

template <typename Benchmark>
void runConfiguration(std::size_t thread_count, std::vector<Summary>& summaries) {
  std::vector<double> throughputs;
  throughputs.reserve(NUM_RUNS);

  for (std::size_t run = 1; run <= NUM_RUNS; ++run) {
    const RunResult result = runOnce<Benchmark>(thread_count);
    throughputs.push_back(result.million_pairs_per_second);

    std::cout << Benchmark::name << ',' << thread_count << ',' << run << ',' << thread_count * OPERATIONS_PER_THREAD
              << ',' << result.elapsed_ns << ',' << result.million_pairs_per_second << '\n';
  }

  summaries.push_back(Summary{Benchmark::name, thread_count, median(throughputs)});
}

void runBenchmarks() {
  std::vector<Summary> summaries;
  summaries.reserve(THREAD_COUNTS.size() * 4);

  std::cout << std::fixed << std::setprecision(3);
  std::cout << "allocator,threads,run,total_pairs,elapsed_ns,million_pairs_per_second\n";

  for (const std::size_t thread_count : THREAD_COUNTS) {
    runConfiguration<MallocBenchmark>(thread_count, summaries);
    runConfiguration<GlobalLockPoolBenchmark>(thread_count, summaries);
    runConfiguration<ThreadCachingPoolBenchmark>(thread_count, summaries);
    runConfiguration<LocalOnlyThreadCachingPoolBenchmark>(thread_count, summaries);
  }

  std::cout << "\nsummary\n";
  std::cout << "allocator,threads,median_million_pairs_per_second\n";
  for (const Summary& summary : summaries) {
    std::cout << summary.allocator << ',' << summary.thread_count << ',' << summary.median_million_pairs_per_second
              << '\n';
  }
}

} // namespace

int main() {
  try {
    runBenchmarks();
  } catch (const std::exception& error) {
    std::cerr << "Multithreaded benchmark failed: " << error.what() << '\n';
    return 1;
  }
}
