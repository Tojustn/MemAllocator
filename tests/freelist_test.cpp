#include "freelist.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>

namespace {
constexpr std::size_t alignment = alignof(std::max_align_t);
constexpr std::size_t regionHeaderSize = (sizeof(MmapRegion) + alignment - 1) & ~(alignment - 1);
constexpr std::size_t blockCapacity = 4096 - regionHeaderSize - sizeof(FreeNodeHeader);
} // namespace

TEST_CASE("freelist rejects invalid allocation sizes", "[freelist][basic]") {
  FreeListAllocator allocator;

  CHECK_THROWS_AS(allocator.allocate(0), std::bad_alloc);
  CHECK_THROWS_AS(allocator.allocate(blockCapacity + 1), std::bad_alloc);
  CHECK_THROWS_AS(allocator.allocate(std::numeric_limits<std::size_t>::max()), std::bad_alloc);
  CHECK(allocator.allocate(16) != nullptr);
}

TEST_CASE("freelist allocations are aligned and retain independent contents", "[freelist][basic]") {
  FreeListAllocator allocator;
  constexpr std::array<std::size_t, 5> sizes{1, 7, 16, 33, 128};
  std::array<unsigned char*, sizes.size()> pointers{};

  for (std::size_t i = 0; i < sizes.size(); ++i) {
    pointers[i] = static_cast<unsigned char*>(allocator.allocate(sizes[i]));
    REQUIRE(pointers[i] != nullptr);
    CHECK(reinterpret_cast<std::uintptr_t>(pointers[i]) % alignof(std::max_align_t) == 0);
    std::memset(pointers[i], static_cast<int>(i + 1), sizes[i]);
  }

  for (std::size_t i = 0; i < sizes.size(); ++i) {
    CAPTURE(i);
    for (std::size_t j = 0; j < sizes[i]; ++j) {
      REQUIRE(pointers[i][j] == i + 1);
    }
  }
}

TEST_CASE("freelist allocates another page after an exact fit", "[freelist][basic]") {
  FreeListAllocator allocator;
  auto* first = static_cast<unsigned char*>(allocator.allocate(blockCapacity));
  REQUIRE(first != nullptr);
  std::memset(first, 0x5a, blockCapacity);

  auto* second = static_cast<unsigned char*>(allocator.allocate(blockCapacity));
  REQUIRE(second != nullptr);
  REQUIRE(second != first);
  std::memset(second, 0xa5, blockCapacity);

  for (std::size_t i = 0; i < blockCapacity; ++i) {
    REQUIRE(first[i] == 0x5a);
  }
}

TEST_CASE("freelist reuses a freed whole block", "[freelist][basic]") {
  FreeListAllocator allocator;
  void* original = allocator.allocate(blockCapacity);
  REQUIRE(original != nullptr);
  allocator.deallocate(original);
  CHECK(allocator.allocate(blockCapacity) == original);
}

TEST_CASE("freelist reuses a small freed block without overlapping a live neighbor", "[freelist][basic]") {
  FreeListAllocator allocator;
  void* first = allocator.allocate(64);
  auto* neighbor = static_cast<unsigned char*>(allocator.allocate(64));
  REQUIRE(first != nullptr);
  REQUIRE(neighbor != nullptr);
  std::memset(neighbor, 0x5a, 64);

  allocator.deallocate(first);
  CHECK(allocator.allocate(64) == first);
  void* another = allocator.allocate(64);
  REQUIRE(another != nullptr);

  const auto anotherAddress = reinterpret_cast<std::uintptr_t>(another);
  const auto neighborAddress = reinterpret_cast<std::uintptr_t>(neighbor);
  CHECK((anotherAddress + 64 <= neighborAddress || neighborAddress + 64 <= anotherAddress));
  for (std::size_t i = 0; i < 64; ++i) {
    REQUIRE(neighbor[i] == 0x5a);
  }
}
