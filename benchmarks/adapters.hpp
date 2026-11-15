#pragma once

#include <cstddef>
#include <cstdlib>
#include <new>

#include "../include/arena.hpp"
#include "../include/freelist.hpp"
#include "../include/pool.hpp"

// allocate() returns raw storage. Check for nullptr or std::bad_alloc.
// Before reset(), destroy objects and deallocate all outstanding allocations.
// Only the arena needs reset(); other allocators reclaim slots in deallocate().
struct MallocAdapter {
  static constexpr const char* name = "malloc";
  void* allocate(std::size_t n) noexcept {
    return std::malloc(n);
  }
  void deallocate(void* p) noexcept {
    std::free(p);
  }
  void reset() noexcept {
  }
};

struct FreeListAdapter {
  static constexpr const char* name = "freelist";
  FreeListAllocator a;
  void* allocate(std::size_t n) {
    return a.allocate(n);
  }
  void deallocate(void* p) noexcept {
    a.deallocate(p);
  }
  void reset() noexcept {
  }
};

// Capacity must cover all allocations between resets, including alignment padding.
// The buffer is inline: use heap storage for the adapter when choosing large capacities.
template <std::size_t Capacity = 1024 * 1024>
struct ArenaAdapter {
  static constexpr const char* name = "arena";
  static constexpr std::size_t kCapacity = Capacity;
  ArenaAllocator<Capacity> a;
  void* allocate(std::size_t n) noexcept {
    return a.allocate(n);
  }
  void deallocate(void*) noexcept {
  } // Reclaimed together by reset().
  void reset() noexcept {
    a.reset();
  }
};

template <std::size_t SlotSize = 64>
struct PoolAdapter {
  static_assert(SlotSize > 0, "Pool slots must have positive size");
  static constexpr const char* name = "pool";
  static constexpr std::size_t kSlot = SlotSize;
  struct alignas(std::max_align_t) Slot {
    std::byte data[SlotSize];
  };
  PoolAllocator<Slot> a;
  void* allocate(std::size_t n) {
    // Reject requests that cannot fit instead of silently returning too little storage.
    if (n == 0 || n > kSlot) {
      throw std::bad_alloc{};
    }
    return a.allocate();
  }
  void deallocate(void* p) noexcept {
    a.deallocate(static_cast<Slot*>(p));
  }
  void reset() noexcept {
  }
};
