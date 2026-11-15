#include "pool.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cerrno>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace {
// Return 1 for mapped memory, 0 for unmapped memory, -1 for query errors.
int mappingState(void* base) {
  unsigned char residency = 0;
  if (mincore(base, 1, &residency) == 0) {
    return 1;
  }
  return errno == ENOMEM ? 0 : -1;
}

struct alignas(64) CacheLine {
  std::array<std::uint64_t, 8> words;
};

static_assert(!std::is_copy_constructible_v<PoolAllocator<int>>);
static_assert(!std::is_copy_assignable_v<PoolAllocator<int>>);
} // namespace

TEST_CASE("pool preserves independent allocations across regions", "[pool][regions]") {
  PoolAllocator<std::uint64_t> pool;
  std::vector<std::uint64_t*> pointers;
  for (std::uint64_t i = 0; i < 2000; ++i) {
    auto* pointer = pool.allocate();
    REQUIRE(pointer != nullptr);
    pointers.push_back(std::construct_at(pointer, i));
  }
  for (std::size_t i = 0; i < pointers.size(); ++i) {
    REQUIRE(*pointers[i] == i);
    std::destroy_at(pointers[i]);
    pool.deallocate(pointers[i]);
  }
  // Drain the free list spanning several regions, then resume fresh allocation.
  for (auto it = pointers.rbegin(); it != pointers.rend(); ++it) {
    REQUIRE(pool.allocate() == *it);
  }
  auto* fresh = pool.allocate();
  for (auto* pointer : pointers) {
    REQUIRE(fresh != pointer);
  }
}

TEST_CASE("pool aligns every slot including slots in new regions", "[pool][alignment]") {
  PoolAllocator<CacheLine> pool;
  std::vector<CacheLine*> pointers;
  for (std::uint64_t i = 0; i < 200; ++i) {
    auto* pointer = pool.allocate();
    REQUIRE(reinterpret_cast<std::uintptr_t>(pointer) % alignof(CacheLine) == 0);
    std::construct_at(pointer)->words.fill(i);
    pointers.push_back(pointer);
  }
  for (std::size_t i = 0; i < pointers.size(); ++i) {
    for (auto word : pointers[i]->words) {
      REQUIRE(word == i);
    }
    std::destroy_at(pointers[i]);
    pool.deallocate(pointers[i]);
  }
}

TEST_CASE("pool frees tiny objects without overwriting live neighbors", "[pool][reuse]") {
  PoolAllocator<char> pool;
  auto* first = std::construct_at(pool.allocate(), 'a');
  auto* middle = std::construct_at(pool.allocate(), 'b');
  auto* last = std::construct_at(pool.allocate(), 'c');
  std::destroy_at(middle);
  pool.deallocate(middle);
  CHECK(*first == 'a');
  CHECK(*last == 'c');
  auto* reused = pool.allocate();
  REQUIRE(reused == middle);
  std::construct_at(reused, 'd');
  CHECK(*first == 'a');
  CHECK(*last == 'c');
  CHECK(*reused == 'd');
  std::destroy_at(first);
  std::destroy_at(reused);
  std::destroy_at(last);
}

TEST_CASE("pool ignores null deallocation with an empty or populated free list", "[pool][reuse]") {
  PoolAllocator<int> pool;
  pool.deallocate(nullptr);
  auto* first = pool.allocate();
  auto* second = pool.allocate();
  pool.deallocate(first);
  pool.deallocate(nullptr);
  pool.deallocate(second);
  pool.deallocate(nullptr);
  CHECK(pool.allocate() == second);
  CHECK(pool.allocate() == first);
}

TEST_CASE("pool supports caller-managed nontrivial object lifetimes", "[pool][lifetime]") {
  PoolAllocator<std::string> pool;
  auto* value = std::construct_at(pool.allocate(), 200, 'x');
  CHECK(*value == std::string(200, 'x'));
  std::destroy_at(value);
  pool.deallocate(value);
  auto* reused = pool.allocate();
  REQUIRE(reused == value);
  std::construct_at(reused, "reused");
  CHECK(*reused == "reused");
  std::destroy_at(reused);
  pool.deallocate(reused);
}

TEST_CASE("pool can destroy an unused allocator", "[pool][regions]") {
  REQUIRE_NOTHROW([] { PoolAllocator<int> pool; }());
}

TEST_CASE("pool destructor unmaps all regions including reused regions", "[pool][regions]") {
  // With an 8-byte header, a 4088-byte object exactly fills the payload.
  using LargeObject = std::array<std::byte, 4096 - sizeof(MmapRegion)>;
  std::array<void*, 3> bases{};
  std::array<int, 3> before{};
  std::array<int, 3> after{};
  {
    PoolAllocator<LargeObject> pool;
    std::array<LargeObject*, 3> pointers{};
    for (std::size_t i = 0; i < pointers.size(); ++i) {
      pointers[i] = pool.allocate();
      bases[i] = reinterpret_cast<std::byte*>(pointers[i]) - sizeof(MmapRegion);
      before[i] = mappingState(bases[i]);
    }
    pool.deallocate(pointers[0]);
    REQUIRE(pool.allocate() == pointers[0]);
    pool.deallocate(pointers[1]);
  }
  // Query before assertion reporting can allocate and reuse an unmapped address.
  for (std::size_t i = 0; i < bases.size(); ++i) {
    after[i] = mappingState(bases[i]);
  }
  for (std::size_t i = 0; i < bases.size(); ++i) {
    CAPTURE(i);
    CHECK(before[i] == 1);
    CHECK(after[i] == 0);
  }
}
