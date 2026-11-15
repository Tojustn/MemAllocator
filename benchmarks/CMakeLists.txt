add_executable(allocator_bench bench.cpp)
target_link_libraries(allocator_bench PRIVATE memory_allocators::memory_allocators)

add_executable(allocator_thread_bench multithreadbench.cpp)
target_link_libraries(allocator_thread_bench PRIVATE memory_allocators::memory_allocators)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(allocator_bench PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(allocator_thread_bench PRIVATE -Wall -Wextra -Wpedantic)
endif()
