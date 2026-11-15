#pragma once
#include "mmap_region.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <sys/mman.h>
struct FreeSlot {
  FreeSlot* next;
};
template <typename T>
class PoolAllocator {
public:
  PoolAllocator() noexcept {
    static_assert(SLOT_SIZE <= BLOCK_CAPACITY, "Slot size cannot be greater than block capacity");
  };
  ~PoolAllocator() noexcept {
    while (regions_ != nullptr) {
      MmapRegion* next_region = regions_->next;
      munmap(static_cast<void*>(regions_), REGION_SIZE);
      regions_ = next_region;
    }
  }

  PoolAllocator(const PoolAllocator&) = delete;
  PoolAllocator& operator=(const PoolAllocator&) = delete;

  T* allocate() {
    T* return_ptr{nullptr};
    // Try inner blocks first
    if (free_slot_ != nullptr) {
      return_ptr = reinterpret_cast<T*>(free_slot_);
      free_slot_ = free_slot_->next;
      return return_ptr;
    }

    if (end_ == nullptr || static_cast<std::size_t>(end_ - cursor_) < SLOT_SIZE) {
      cursor_ = fetchNewBlock();
      end_ = cursor_ + BLOCK_CAPACITY;
    }

    return_ptr = reinterpret_cast<T*>(cursor_);
    cursor_ += SLOT_SIZE;
    return return_ptr;
  }

  void deallocate(T* ptr) noexcept {
    if (ptr == nullptr) {
      return;
    }
    // deallocate -> if in middle put in free slots

    // Do we care about keepign free slots sorted? logn insertions
    // Prob not doesnt really matter
    FreeSlot* new_slot = new (static_cast<void*>(ptr)) FreeSlot{free_slot_};
    free_slot_ = new_slot;
  }

private:
  constexpr static std::size_t REGION_SIZE{4096};
  static constexpr std::size_t SLOT_ALIGNMENT = std::max(alignof(T), alignof(FreeSlot));
  static constexpr std::size_t SLOT_SIZE =
      (std::max(sizeof(T), sizeof(FreeSlot)) + SLOT_ALIGNMENT - 1) & ~(SLOT_ALIGNMENT - 1);
  static constexpr std::size_t REGION_HEADER_SIZE = (sizeof(MmapRegion) + SLOT_ALIGNMENT - 1) & ~(SLOT_ALIGNMENT - 1);
  static_assert(REGION_HEADER_SIZE < REGION_SIZE, "Aligned header must fit within the region");
  static constexpr std::size_t BLOCK_CAPACITY = REGION_SIZE - REGION_HEADER_SIZE;

  FreeSlot* free_slot_{nullptr};
  // cursor keeps track of the current "size"
  std::byte* cursor_{nullptr};
  std::byte* end_{nullptr};

  MmapRegion* regions_{nullptr};
  std::byte* fetchNewBlock() {
    // mmap is automatically page aligned
    void* byte_block = mmap(nullptr, REGION_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (byte_block == MAP_FAILED) {
      throw std::bad_alloc{};
    }
    regions_ = new (byte_block) MmapRegion{regions_};
    return static_cast<std::byte*>(byte_block) + REGION_HEADER_SIZE;
  }
};
