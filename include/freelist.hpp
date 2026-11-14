
#pragma once

#include <cstddef>
#include <functional>
#include <new>
#include <sys/mman.h>

// Linked list of mmap regions for munmap usage
struct MmapRegion {
  MmapRegion* next;
};
struct FreeNodeHeader {
  FreeNodeHeader* next;
  std::size_t size;
};

// Lazily load memory
// Request a page at a time (4096 bytes)
class FreeListAllocator {
public:
  FreeListAllocator() = default;
  ~FreeListAllocator() {
    MmapRegion* current = regions_;

    while (current != nullptr) {
      MmapRegion* next = current->next;
      munmap(current, REGION_SIZE);
      current = next;
    }
  }
  FreeListAllocator(const FreeListAllocator&) = delete;
  FreeListAllocator& operator=(const FreeListAllocator&) = delete;

  void* allocate(std::size_t bytes) {

    if (bytes <= 0 || bytes > BLOCK_CAPACITY) {
      throw std::bad_alloc{};
    }

    std::size_t aligned_bytes = (bytes + ALIGNMENT - 1) & ~(ALIGNMENT - 1);

    if (head_ == nullptr) {
      void* byte_block = fetchNewBlock();
      head_ = new (byte_block) FreeNodeHeader{nullptr, BLOCK_CAPACITY};
    }

    // First Fit Policy
    // Link is a pointer to the address of head_
    FreeNodeHeader** link = &head_;

    // Keep iterating until we find a sufficiently large block
    while (*link != nullptr && (*link)->size < aligned_bytes) {
      // link = pointer to the pointer of A->next, *link is the actual next node
      link = &(*link)->next;
    }

    FreeNodeHeader* current_node = *link;
    // No suitable free block found, make new block
    if (current_node == nullptr) {
      void* byte_block = fetchNewBlock();

      current_node = new (byte_block) FreeNodeHeader{nullptr, BLOCK_CAPACITY};
    }

    std::byte* allocated_ptr{reinterpret_cast<std::byte*>(current_node) + HEADER_SIZE};
    if ((current_node)->size >= aligned_bytes + HEADER_SIZE + ALIGNMENT) {
      std::byte* new_header_ptr = allocated_ptr + aligned_bytes;

      FreeNodeHeader* remainder =
          new (new_header_ptr) FreeNodeHeader{(current_node)->next, (current_node)->size - aligned_bytes - HEADER_SIZE};
      current_node->size = aligned_bytes;
      *link = remainder;
    }
    else {
      *(link) = (current_node)->next;
    }
    return allocated_ptr;
  }

  // Deallocate the ptr, can be from any address we allocated not just the end
  void deallocate(void* ptr) {
    if (ptr == nullptr) {
      return;
    }
    FreeNodeHeader* header = reinterpret_cast<FreeNodeHeader*>(static_cast<std::byte*>(ptr) - HEADER_SIZE);
    FreeNodeHeader** link = &head_;
    FreeNodeHeader* prev_node = nullptr;
    while (*link != nullptr && std::less<FreeNodeHeader*>{}(*link, header)) {
      prev_node = *link;
      link = &(*link)->next;
    }
    header->next = *link;
    FreeNodeHeader* next_node = header->next;
    *link = header;

    // FOrward coalesce
    // The blocks cannot have a live allcoation in use
    // Add the current node size and check if the next pinter is there meaning that the entire block is free
    //
    // The nodes must be from the same mmaping for ease of munmap
    if (next_node != nullptr &&
        reinterpret_cast<std::byte*>(header) + HEADER_SIZE + header->size == reinterpret_cast<std::byte*>(next_node)) {
      header->size += next_node->size + HEADER_SIZE;
      header->next = next_node->next;
    }

    if (prev_node != nullptr && reinterpret_cast<std::byte*>(prev_node) + prev_node->size + HEADER_SIZE ==
                                    reinterpret_cast<std::byte*>(*link)) {
      prev_node->size += header->size + HEADER_SIZE;
      prev_node->next = header->next;
    }
  }

private:
  // Maps a region and returns aligned space after its region header.
  void* fetchNewBlock() {
    // mmap is automatically page aligned
    void* byte_block = mmap(nullptr, REGION_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (byte_block == MAP_FAILED) {
      throw std::bad_alloc{};
    }
    regions_ = new (byte_block) MmapRegion{regions_};
    return static_cast<std::byte*>(byte_block) + REGION_HEADER_SIZE;
  }
  // Just using max alignment possible to mimick malloc
  static constexpr std::size_t ALIGNMENT = alignof(std::max_align_t);
  static constexpr std::size_t HEADER_SIZE = sizeof(FreeNodeHeader);
  static constexpr std::size_t REGION_SIZE = 4096;
  static constexpr std::size_t REGION_HEADER_SIZE = (sizeof(MmapRegion) + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
  static constexpr std::size_t BLOCK_CAPACITY = REGION_SIZE - REGION_HEADER_SIZE - HEADER_SIZE;
  FreeNodeHeader* head_ = nullptr;
  MmapRegion* regions_ = nullptr;
};
