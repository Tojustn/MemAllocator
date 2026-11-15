#include "freelist.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <limits>

namespace {
constexpr std::size_t alignment = alignof(std::max_align_t);
constexpr std::size_t regionHeaderSize = (sizeof(MmapRegion) + alignment - 1) & ~(alignment - 1);
constexpr std::size_t blockCapacity = 4096 - regionHeaderSize - sizeof(FreeNodeHeader);
// Return 1 for mapped memory, 0 for unmapped memory, -1 for query errors.
int mappingState(void* base) {
  unsigned char residency = 0;
  if (mincore(base, 1, &residency) == 0) {
    return 1;
  }
  return errno == ENOMEM ? 0 : -1;
}
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

TEST_CASE("freelist coalesces adjacent blocks in either direction", "[freelist][coalescing]") {
  FreeListAllocator allocator;
  void* first = allocator.allocate(64);
  void* second = allocator.allocate(64);
  auto* guard = static_cast<unsigned char*>(allocator.allocate(64));
  std::memset(guard, 0x5a, 64);

  SECTION("forward: free the later block first") {
    allocator.deallocate(second);
    allocator.deallocate(first);
  }
  SECTION("backward: free the earlier block first") {
    allocator.deallocate(first);
    allocator.deallocate(second);
  }

  // Neither individual block can satisfy this request.
  auto* merged = static_cast<unsigned char*>(allocator.allocate(128));
  REQUIRE(merged == first);
  std::memset(merged, 0xa5, 128);
  for (std::size_t i = 0; i < 64; ++i) {
    REQUIRE(guard[i] == 0x5a);
  }
}

TEST_CASE("freelist recovers a whole region for every three-block free order", "[freelist][coalescing]") {
  constexpr std::array<std::array<std::size_t, 3>, 6> orders{{
      {0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}}};
  for (const auto& order : orders) {
    CAPTURE(order[0], order[1], order[2]);
    FreeListAllocator allocator;
    std::array<void*, 3> blocks{
        allocator.allocate(64), allocator.allocate(64),
        allocator.allocate(blockCapacity - 128 - 2 * sizeof(FreeNodeHeader))};
    for (auto index : order) {
      allocator.deallocate(blocks[index]);
    }
    CHECK(allocator.allocate(blockCapacity) == blocks[0]);
  }
}

TEST_CASE("freelist does not coalesce across a live allocation", "[freelist][coalescing]") {
  FreeListAllocator allocator;
  void* first = allocator.allocate(64);
  auto* guard = static_cast<unsigned char*>(allocator.allocate(64));
  void* last = allocator.allocate(blockCapacity - 128 - 2 * sizeof(FreeNodeHeader));
  std::memset(guard, 0x5a, 64);
  allocator.deallocate(first);
  allocator.deallocate(last);

  // Only a new region can satisfy a full-capacity request while guard is live.
  auto* large = static_cast<unsigned char*>(allocator.allocate(blockCapacity));
  REQUIRE(large != first);
  std::memset(large, 0xa5, blockCapacity);
  for (std::size_t i = 0; i < 64; ++i) {
    REQUIRE(guard[i] == 0x5a);
  }
  allocator.deallocate(guard);
  CHECK(allocator.allocate(blockCapacity) == first);
}

TEST_CASE("freelist destructor unmaps every owned region", "[freelist][regions]") {
  std::array<void*, 3> bases{};
  std::array<int, 3> before{};
  std::array<int, 3> after{};
  {
    FreeListAllocator allocator;
    std::array<void*, 3> blocks{};
    for (std::size_t i = 0; i < blocks.size(); ++i) {
      blocks[i] = allocator.allocate(blockCapacity);
      bases[i] = static_cast<std::byte*>(blocks[i]) - sizeof(FreeNodeHeader) - regionHeaderSize;
      before[i] = mappingState(bases[i]);
    }
    // Cleanup must find both free and fully allocated regions.
    allocator.deallocate(blocks[1]);
  }
  // Query immediately, before assertion/reporting code can allocate memory.
  for (std::size_t i = 0; i < bases.size(); ++i) {
    after[i] = mappingState(bases[i]);
  }
  for (std::size_t i = 0; i < bases.size(); ++i) {
    CAPTURE(i);
    CHECK(before[i] == 1);
    CHECK(after[i] == 0);
  }
}

TEST_CASE("freelist can destroy an unused allocator", "[freelist][regions]") {
  REQUIRE_NOTHROW([] { FreeListAllocator allocator; }());
}
