#pragma once
#include <cstddef>
#include <new>
#include <sys/mman.h>
struct MmapRegion {
  MmapRegion* next;
};
struct FreeSlot {
  FreeSlot* next;
};
template <typename T>
class PoolAllocator {
public:
  PoolAllocator() {
    static_assert(OBJECT_SIZE <= BLOCK_CAPACITY, "OBJECT SIZE cannot be greater than block capacity");
  };

  T* allocate() {
    if (end_ == nullptr || end_ - cursor_ < OBJECT_ALIGNED_SIZE) {
      cursor_ = fetchNewBlock() + REGION_HEADER_SIZE;
      end_ = cursor_ + BLOCK_CAPACITY;
    }
    T* return_ptr{nullptr};

    // Try inner blocks first
    if (free_slot_ != nullptr) {
      return_ptr = reinterpret_cast<T*>(free_slot_);
      free_slot_ = free_slot_->next;
      return return_ptr;
    }
    else {
      return_ptr = cursor_;
      cursor_ += OBJECT_SIZE;
    }
  }

private:
  constexpr static std::size_t REGION_SIZE{4096};
  constexpr static std::size_t OBJECT_SIZE{sizeof(T)};
  static constexpr std::size_t REGION_HEADER_SIZE =
      (sizeof(MmapRegion) + alignof(MmapRegion) - 1) & ~(alignof(MmapRegion) - 1);
  static constexpr std::size_t BLOCK_CAPACITY = REGION_SIZE - REGION_HEADER_SIZE;
  // GEt the aligned size
  static constexpr std::size_t OBJECT_ALIGNED_SIZE = (OBJECT_SIZE + alignof(T) - 1) & ~(alignof(T) - 1);

  FreeSlot* free_slot_{nullptr};
  // cursor keeps track of the current "size"
  std::byte* cursor_{nullptr};
  std::byte* end_{nullptr};

  MmapRegion* regions_{nullptr};
  void* fetchNewBlock() {
    // mmap is automatically page aligned
    void* byte_block = mmap(nullptr, REGION_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (byte_block == MAP_FAILED) {
      throw std::bad_alloc{};
    }
    regions_ = new (byte_block) MmapRegion{regions_};
    return static_cast<std::byte*>(byte_block) + REGION_HEADER_SIZE;
  }
};
