#include "arena.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

struct Record {
  std::uint32_t id;
  double value;
  std::array<char, 7> tag;
};

struct alignas(32) CacheEntry {
  std::array<std::byte, 32> data;
};

struct alignas(64) CacheLine {
  std::array<std::uint64_t, 8> words;
};

bool isAligned(const void* pointer, std::size_t alignment) {
  return reinterpret_cast<std::uintptr_t>(pointer) % alignment == 0;
}

template <typename T, std::size_t ArenaSize>
T* allocateObject(ArenaAllocator<ArenaSize>& arena) {
  void* storage = arena.allocate(sizeof(T), alignof(T));
  return storage == nullptr ? nullptr : std::construct_at(static_cast<T*>(storage));
}

} // namespace

TEST_CASE("arena honors explicit power-of-two alignments", "[arena][alignment]") {
  ArenaAllocator<512> arena;

  constexpr std::array<std::size_t, 7> alignments{1, 2, 4, 8, 16, 32, 64};
  for (const std::size_t alignment : alignments) {
    CAPTURE(alignment);
    const void* allocation = arena.allocate(1, alignment);

    REQUIRE(allocation != nullptr);
    CHECK(isAligned(allocation, alignment));
  }
}

TEST_CASE("arena stores types with different size and alignment requirements", "[arena][types]") {
  ArenaAllocator<512> arena;

  auto* byte = allocateObject<std::uint8_t>(arena);
  auto* integer = allocateObject<std::uint32_t>(arena);
  auto* real = allocateObject<double>(arena);
  auto* record = allocateObject<Record>(arena);
  auto* cacheEntry = allocateObject<CacheEntry>(arena);
  auto* cacheLine = allocateObject<CacheLine>(arena);

  REQUIRE(byte != nullptr);
  REQUIRE(integer != nullptr);
  REQUIRE(real != nullptr);
  REQUIRE(record != nullptr);
  REQUIRE(cacheEntry != nullptr);
  REQUIRE(cacheLine != nullptr);

  CHECK(isAligned(byte, alignof(std::uint8_t)));
  CHECK(isAligned(integer, alignof(std::uint32_t)));
  CHECK(isAligned(real, alignof(double)));
  CHECK(isAligned(record, alignof(Record)));
  CHECK(isAligned(cacheEntry, alignof(CacheEntry)));
  CHECK(isAligned(cacheLine, alignof(CacheLine)));

  *integer = 42;
  *real = 3.5;
  record->id = 7;
  cacheLine->words[3] = 99;

  CHECK(*integer == 42);
  CHECK(*real == 3.5);
  CHECK(record->id == 7);
  CHECK(cacheLine->words[3] == 99);
}

TEST_CASE("arena can hold a standard vector object", "[arena][types][vector]") {
  ArenaAllocator<128> arena;

  auto* values = allocateObject<std::vector<int>>(arena);
  REQUIRE(values != nullptr);
  REQUIRE(isAligned(values, alignof(std::vector<int>)));

  values->push_back(10);
  values->push_back(20);
  values->push_back(30);

  CHECK(values->size() == 3);
  CHECK((*values)[0] == 10);
  CHECK((*values)[1] == 20);
  CHECK((*values)[2] == 30);

  std::destroy_at(values);
}

TEST_CASE("arena inserts padding between differently aligned allocations", "[arena][padding]") {
  ArenaAllocator<128> arena;

  const void* first = arena.allocate(1, 1);
  const void* second = arena.allocate(sizeof(std::uint64_t), alignof(std::uint64_t));
  const void* third = arena.allocate(sizeof(CacheEntry), alignof(CacheEntry));

  REQUIRE(first != nullptr);
  REQUIRE(second != nullptr);
  REQUIRE(third != nullptr);

  CHECK(isAligned(second, alignof(std::uint64_t)));
  CHECK(isAligned(third, alignof(CacheEntry)));
  CHECK(reinterpret_cast<std::uintptr_t>(second) > reinterpret_cast<std::uintptr_t>(first));
  CHECK(reinterpret_cast<std::uintptr_t>(third) > reinterpret_cast<std::uintptr_t>(second));
}

TEST_CASE("arena reports exhaustion without overrunning its buffer", "[arena][capacity]") {
  ArenaAllocator<64> arena;

  REQUIRE(arena.allocate(64, 1) != nullptr);
  CHECK(arena.allocate(1, 1) == nullptr);
}

TEST_CASE("reset makes arena storage available again", "[arena][reset]") {
  ArenaAllocator<64> arena;

  void* beforeReset = arena.allocate(64, 1);
  REQUIRE(beforeReset != nullptr);
  REQUIRE(arena.allocate(1, 1) == nullptr);

  arena.reset();

  void* afterReset = arena.allocate(64, 1);
  REQUIRE(afterReset != nullptr);
  CHECK(afterReset == beforeReset);
}
