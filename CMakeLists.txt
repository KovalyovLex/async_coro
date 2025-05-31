cmake_minimum_required(VERSION 3.12)
 
project(async_coro_tests)

option(ASAN_ENABLED "Enable address sanitizer for all targets" OFF)
option(TSAN_ENABLED "Enable thread sanitizer for all targets" OFF)


if (ASAN_ENABLED)
  if (WIN32)
    add_compile_options(/fsanitize=address)
    add_link_options(/fsanitize=address)
  else()
    add_compile_options(-fsanitize=address)
    add_link_options(-fsanitize=address)
  endif()
endif()

if (TSAN_ENABLED)
  add_compile_options(-fsanitize=thread -Wno-error=tsan)
  add_link_options(-fsanitize=thread)
endif()

set(gtest_force_shared_crt ON CACHE BOOL "Force use dynamic libs in tests" FORCE)
add_subdirectory(googletest)

include(CTest)
include(GoogleTest)

add_subdirectory(async_coro)

file(GLOB_RECURSE tests_src LIST_DIRECTORIES false CONFIGURE_DEPENDS "tests/src/**.h" "tests/src/**.cpp")

if (ANDROID)
  find_package(googletest REQUIRED CONFIG)
  find_package(junit-gtest REQUIRED CONFIG)
  find_library(
        # Sets the name of the path variable.
        log-lib

        # Specifies the name of the NDK library that
        # you want CMake to locate.
        log)

  add_library(async_coro_tests SHARED ${tests_src})

  target_link_libraries(async_coro_tests async_coro googletest::gtest junit-gtest::junit-gtest ${log-lib})
else()
  add_executable(async_coro_tests ${tests_src})
  target_link_libraries(async_coro_tests async_coro gtest)
endif()

target_include_directories(async_coro_tests PRIVATE "tests/src")
set_property(TARGET async_coro_tests PROPERTY CXX_STANDARD 20)

# enable full warnings checks in our targets
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
set_tests_properties(${long_tests_list} PROPERTIES TIMEOUT 120)
