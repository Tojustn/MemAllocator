cmake_minimum_required(VERSION 3.20)

project(
    memory_allocators
    VERSION 0.1.0
    DESCRIPTION "Memory allocator implementations and benchmarks"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(memory_allocators INTERFACE)
add_library(memory_allocators::memory_allocators ALIAS memory_allocators)

target_compile_features(memory_allocators INTERFACE cxx_std_20)

target_include_directories(
    memory_allocators
    INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

option(MEMORY_ALLOCATORS_BUILD_TESTS "Build the test suite" ON)
option(MEMORY_ALLOCATORS_BUILD_BENCHMARKS "Build benchmarks" OFF)

if(MEMORY_ALLOCATORS_BUILD_TESTS AND EXISTS "${PROJECT_SOURCE_DIR}/tests/CMakeLists.txt")
    include(CTest)
    add_subdirectory(tests)
endif()

if(MEMORY_ALLOCATORS_BUILD_BENCHMARKS AND EXISTS "${PROJECT_SOURCE_DIR}/benchmarks/CMakeLists.txt")
    add_subdirectory(benchmarks)
endif()
