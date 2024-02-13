cmake_minimum_required(VERSION 3.12)
 
project(async_coro_tests)

set(gtest_force_shared_crt ON CACHE BOOL "Force use dynamic libs in tests" FORCE)
add_subdirectory(googletest)

include(CTest)
include(GoogleTest)

add_subdirectory(async_coro)

file(GLOB_RECURSE tests_src LIST_DIRECTORIES false CONFIGURE_DEPENDS "tests/src/**.h" "tests/src/**.cpp")

add_executable(async_coro_tests ${tests_src})
target_include_directories(async_coro_tests PRIVATE "tests/src")
set_property(TARGET async_coro_tests PROPERTY CXX_STANDARD 20)
target_link_libraries(async_coro_tests gtest async_coro)

# enable full warnings checks in our targets
target_compile_options(async_coro PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /permissive->
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic -Werror -std=c++20>
)
target_compile_options(async_coro_tests PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /permissive->
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic -Werror -std=c++20>
)

# configure tests by different lists
file(GLOB_RECURSE simple_tests_src LIST_DIRECTORIES false CONFIGURE_DEPENDS "tests/src/simple_tests/**.cpp")
file(GLOB_RECURSE long_tests_src LIST_DIRECTORIES false CONFIGURE_DEPENDS "tests/src/long_runnung_tests/**.cpp")

gtest_add_tests(TARGET      async_coro_tests
                SOURCES     ${simple_tests_src}
                TEST_LIST   simple_tests_list
)
gtest_add_tests(TARGET      async_coro_tests
                SOURCES     ${long_tests_src}
                TEST_LIST   long_tests_list
)
set_tests_properties(${long_tests_list} PROPERTIES TIMEOUT 20)
