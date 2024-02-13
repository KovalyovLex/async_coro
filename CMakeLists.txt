cmake_minimum_required(VERSION 3.12)
 
project(async_coro_tests)

set(gtest_force_shared_crt ON CACHE BOOL "Force use dynamic libs in tests" FORCE)
add_subdirectory(googletest)

include(CTest)
include(GoogleTest)

add_subdirectory(async_coro)

file(GLOB_RECURSE tests_src LIST_DIRECTORIES false CONFIGURE_DEPENDS "tests/src/**.h" "tests/src/**.cpp")

add_executable(async_coro_tests ${tests_src})
set_property(TARGET async_coro_tests PROPERTY CXX_STANDARD 20)
target_link_libraries(async_coro_tests gtest async_coro)
target_compile_options(async_coro PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic -Werror>
)


gtest_add_tests(TARGET      async_coro_tests
                TEST_SUFFIX .noArgs
                TEST_LIST   noArgsTests
)
set_tests_properties(${noArgsTests}   PROPERTIES TIMEOUT 10)
