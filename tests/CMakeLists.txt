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

add_executable(memory_allocators_tests arena_test.cpp)

target_link_libraries(
  memory_allocators_tests
  PRIVATE
    memory_allocators::memory_allocators
    Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(memory_allocators_tests)
