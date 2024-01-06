cmake_minimum_required(VERSION 3.12)
 
project(async_coro_tests)

include(CTest)
include(GoogleTest)

file(GLOB_RECURSE tests_src LIST_DIRECTORIES false CONFIGURE_DEPENDS "src/**.h" "src/**.cpp")

add_executable(async_coro_tests ${tests_src})
set_property(TARGET async_coro_tests PROPERTY CXX_STANDARD 20)
target_link_libraries(async_coro_tests gtest async_coro)

gtest_add_tests(TARGET      async_coro_tests
                TEST_SUFFIX .noArgs
                TEST_LIST   noArgsTests
)
set_tests_properties(${noArgsTests}   PROPERTIES TIMEOUT 10)