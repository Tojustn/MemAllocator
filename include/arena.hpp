#pragma once
// Arena Allocator (local and monotonic)
#include <array>
#include <cassert>
#include <cstddef>
template <std::size_t N>
class ArenaAllocator {
public:
  ArenaAllocator() = default;
  ~ArenaAllocator() = default;
  ArenaAllocator(const ArenaAllocator&) = delete;
  ArenaAllocator& operator=(const ArenaAllocator&) = delete;

  // Allocate memory making sure it aligns
  void* allocate(std::size_t bytes, std::size_t align = alignof(max_align_t)) {

    assert(align != 0 && (align & (align - 1)) == 0 && align <= 64);
    // Rounds up to the nearest multiple of align
    // Add offset_ + align - 1 then bit mask to round down
    std::size_t aligned = (offset_ + align - 1) & ~(align - 1);
    if (aligned >= N || bytes > N - aligned) {
      return nullptr;
    }

    offset_ = aligned + bytes;
    // Return data() + aligned for the address
    void* ptr = buffer_.data() + aligned;
    return ptr;
  }
  void reset() {
    offset_ = 0;
  }

private:
  // array of bytes of N size
  alignas(
      64) std::array<std::byte, N> buffer_; // alignas(64) because we coudl hand out ints or doubles or more than 1 byte
  std::size_t offset_{0};                   // Current position in the array buffer
};
