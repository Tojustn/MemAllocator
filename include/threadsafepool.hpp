#pragma once
#include "pool.hpp"
#include <cstdlib>
#include <mutex>

// Each thread has a thread local cache
// Keeps track of memory the cache has previously owned
// Threads dont have to call the lockguarded pool allocator

template <typename T, std::size_t RefillAmount = 32, std::size_t BatchAmount = 32, std::size_t MaxCacheSize = 128>
class ThreadCache;

// thread safe version of the pool allocator
template <typename T, std::size_t RefillAmount = 32, std::size_t BatchAmount = 32, std::size_t MaxCacheSize = 128>
class ThreadSafePoolAllocator {
public:
  ThreadSafePoolAllocator() = default;
  ~ThreadSafePoolAllocator() = default;
  ThreadSafePoolAllocator& operator=(const ThreadSafePoolAllocator&) = delete;
  ThreadSafePoolAllocator(const ThreadSafePoolAllocator&) = delete;

private:
  friend class ThreadCache<T, RefillAmount, BatchAmount, MaxCacheSize>;

  struct LocalFreeSlot {
    LocalFreeSlot* next = nullptr;
  };
  struct LocalCache {
    LocalFreeSlot* head = nullptr;
    std::size_t count = 0;
  };

  static_assert(sizeof(LocalFreeSlot) <= sizeof(FreeSlot));
  static_assert(alignof(LocalFreeSlot) <= alignof(FreeSlot));
  static_assert(RefillAmount > 0);
  static_assert(BatchAmount > 0);
  static_assert(MaxCacheSize >= BatchAmount);

  // Allocate and deallocate dont need mutexes since they are thread local
  T* allocate(LocalCache& cache) {
    if (cache.head == nullptr) {
      refill(cache);
    }

    LocalFreeSlot* allocated_ptr = cache.head;
    cache.head = cache.head->next;
    cache.count -= 1;
    return reinterpret_cast<T*>(allocated_ptr);
  }
  void deallocate(LocalCache& cache, T* ptr) {
    if (ptr == nullptr) {
      return;
    }
    LocalFreeSlot* free_slot = new (ptr) LocalFreeSlot{cache.head};
    cache.head = free_slot;
    cache.count += 1;
    if (cache.count > MAX_CACHE_SIZE) {
      flush(cache);
    }
  }

  void releaseCache(LocalCache& cache) {
    std::lock_guard<std::mutex> lg{mutex_};
    while (cache.head != nullptr) {
      LocalFreeSlot* next_head = cache.head->next;
      allocator_.deallocate(reinterpret_cast<T*>(cache.head));
      cache.head = next_head;
    }

    cache.count = 0;
  }
  static constexpr std::size_t REFILL_AMOUNT{RefillAmount};
  static constexpr std::size_t BATCH_AMOUNT{BatchAmount};
  static constexpr std::size_t MAX_CACHE_SIZE{MaxCacheSize};
  PoolAllocator<T> allocator_{};
  std::mutex mutex_{};
  // refill needs a mutex since it is requesting memory directly from the global allocator
  void refill(LocalCache& cache) {
    std::lock_guard<std::mutex> lg{mutex_};
    for (std::size_t i = 0; i < REFILL_AMOUNT; ++i) {

      void* allocated_ptr = static_cast<void*>(allocator_.allocate());
      // This is ok because in my pool allocator Each FreeSlot is the same size as a T
      // Meaning that LocalFreeSlot is automatically padded due to that rule
      LocalFreeSlot* free_slot = new (allocated_ptr) LocalFreeSlot{cache.head};
      cache.head = free_slot;
      cache.count += 1;
    }
  }

  void flush(LocalCache& cache) {
    std::lock_guard<std::mutex> lg{mutex_};
    for (std::size_t i = 0; i < BATCH_AMOUNT; ++i) {
      LocalFreeSlot* next_head = cache.head->next;
      allocator_.deallocate(reinterpret_cast<T*>(cache.head));
      cache.head = next_head;
      cache.count -= 1;
    }
  }
};

template <typename T, std::size_t RefillAmount, std::size_t BatchAmount, std::size_t MaxCacheSize>
class ThreadCache {
public:
  ThreadCache(ThreadSafePoolAllocator<T, RefillAmount, BatchAmount, MaxCacheSize>& owner) : owner_(&owner) {
  }
  ~ThreadCache() {
    owner_->releaseCache(cache_);
  }
  ThreadCache(const ThreadCache&) = delete;
  ThreadCache& operator=(const ThreadCache&) = delete;

  ThreadCache(ThreadCache&&) = delete;
  ThreadCache& operator=(ThreadCache&&) = delete;
  T* allocate() {
    return owner_->allocate(cache_);
  }

  void deallocate(T* ptr) {
    owner_->deallocate(cache_, ptr);
  }

private:
  ThreadSafePoolAllocator<T, RefillAmount, BatchAmount, MaxCacheSize>* owner_;
  typename ThreadSafePoolAllocator<T, RefillAmount, BatchAmount, MaxCacheSize>::LocalCache cache_;
};
