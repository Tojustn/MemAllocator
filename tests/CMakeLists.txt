if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(STATUS "Allocator tests are Linux-only; skipping on ${CMAKE_SYSTEM_NAME}")
  return()
endif()

include(FetchContent)

find_package(Catch2 3 CONFIG QUIET)

if(NOT Catch2_FOUND)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.16.1
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(Catch2)
endif()

add_executable(memory_allocators_tests arena_test.cpp freelist_test.cpp pool_test.cpp threadsafepool_test.cpp)

target_link_libraries(
  memory_allocators_tests
  PRIVATE
    memory_allocators::memory_allocators
    Catch2::Catch2WithMain
)

if(MEMORY_ALLOCATORS_ENABLE_TSAN)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    find_program(SETARCH_EXECUTABLE setarch REQUIRED)

    target_compile_options(
      memory_allocators_tests
      PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
        -g
    )

    target_link_options(
      memory_allocators_tests
      PRIVATE
        -fsanitize=thread
    )

    set_property(
      TARGET memory_allocators_tests
      PROPERTY CROSSCOMPILING_EMULATOR
        "${SETARCH_EXECUTABLE};${CMAKE_SYSTEM_PROCESSOR};-R"
    )
  else()
    message(FATAL_ERROR "ThreadSanitizer requires GCC or Clang")
  endif()
endif()

include(Catch)
catch_discover_tests(memory_allocators_tests)
