#pragma once

// Linked list of owned mmap regions for cleanup.
struct MmapRegion {
  MmapRegion* next;
};
