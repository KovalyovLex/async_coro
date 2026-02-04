cmake_minimum_required(VERSION 3.31)

# keep debug symbols in binary for dladdr resolution
if (NOT WIN32)
  add_compile_options(-g -fno-omit-frame-pointer -fvisibility=default)
  add_link_options(-g -fvisibility=default -rdynamic)
endif()

include(FetchContent)

# setup googletest
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
    GIT_SHALLOW TRUE
)

set(gtest_force_shared_crt ON CACHE BOOL "Force use dynamic libs in tests" FORCE)

FetchContent_MakeAvailable(googletest)

include(GoogleTest)

if (TARGET gtest_main)
  set_target_properties(gtest_main PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
if (TARGET gmock_main)
  set_target_properties(gmock_main PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

# configure tests by different lists
file(GLOB_RECURSE common_src LIST_DIRECTORIES false CONFIGURE_DEPENDS "src/common/*")
file(GLOB_RECURSE simple_tests_src LIST_DIRECTORIES false CONFIGURE_DEPENDS "src/simple_tests/*")
file(GLOB_RECURSE long_tests_src LIST_DIRECTORIES false CONFIGURE_DEPENDS "src/long_runnung_tests/*")

set(all_targets)

if (ANDROID)
  find_package(junit-gtest REQUIRED CONFIG)
  find_library(
        # Sets the name of the path variable.
        log-lib

        # Specifies the name of the NDK library that
        # you want CMake to locate.
        log)

  add_library(async_coro_tests SHARED ${common_src} ${simple_tests_src} ${long_tests_src})

  target_link_libraries(async_coro_tests junit-gtest::junit-gtest ${log-lib})

  target_include_directories(async_coro_tests PRIVATE "src/common" "src/simple_tests" "src/long_runnung_tests")

  set(all_targets async_coro_tests)

else()
  add_executable(tests_simple ${common_src} ${simple_tests_src})
  add_executable(tests_long ${common_src} ${long_tests_src})
  
  set(all_targets tests_simple tests_long)

  target_include_directories(tests_simple PRIVATE "src/common" "src/simple_tests")
  target_include_directories(tests_long PRIVATE "src/common" "src/long_runnung_tests")
endif()

foreach(tar IN LISTS all_targets)
  set_property(TARGET ${tar} PROPERTY CXX_STANDARD 20)
  target_link_libraries(${tar} async_coro gmock)

  # enable full warnings checks in our targets
  target_compile_options(${tar} PRIVATE
      $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /permissive->
      $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic -Werror -std=c++20>
  )
endforeach()


if (NOT ANDROID)
  gtest_add_tests(TARGET      tests_simple
                  SOURCES     ${simple_tests_src}
                  TEST_LIST   simple_tests_list
                  EXTRA_ARGS  "--gtest_repeat=10"
  )
  gtest_add_tests(TARGET      tests_long
                  SOURCES     ${long_tests_src}
                  TEST_LIST   long_tests_list
  )

  set_tests_properties(${long_tests_list} PROPERTIES TIMEOUT 120)
endif()
