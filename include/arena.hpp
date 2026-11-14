
#pragma once

#include <array>
#include <cassert>
#include <cstddef>

template <std::size_t N>
class ArenaAllocator {
public:
  ArenaAllocator() = default;

  ArenaAllocator(const ArenaAllocator&) = delete;
  ArenaAllocator& operator=(const ArenaAllocator&) = delete;

  void* allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t)) {
    assert(alignment != 0 && (alignment & (alignment - 1)) == 0 && alignment <= 64);

    // Round offset up to the next multiple of alignment
    std::size_t aligned = (offset_ + alignment - 1) & ~(alignment - 1);

    // Check if the allocation fits
    if (aligned > N || bytes > N - aligned) {
      return nullptr;
    }

    // Return the aligned address
    void* ptr = buffer_.data() + aligned;

    // Advance the bump pointer
    offset_ = aligned + bytes;

    return ptr;
  }

  void reset() noexcept {
    offset_ = 0;
  }

  std::size_t used() const noexcept {
    return offset_;
  }

  constexpr std::size_t capacity() const noexcept {
    return N;
  }

private:
  alignas(64) std::array<std::byte, N> buffer_{};
  std::size_t offset_ = 0;
};
