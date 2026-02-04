cmake_minimum_required(VERSION 3.16)
 
project(async_coro)

option(ASYNC_CORO_ASAN_ENABLED "Enable address sanitizer for all targets" OFF)
option(ASYNC_CORO_TSAN_ENABLED "Enable thread sanitizer for all targets" OFF)
option(ASYNC_CORO_TESTS_ENABLED "Enable tests in config" ON)


if (ASYNC_CORO_ASAN_ENABLED)
  if (WIN32)
    add_compile_options(/fsanitize=address)
    add_link_options(/fsanitize=address)
  else()
    add_compile_options(-fsanitize=address)
    add_link_options(-fsanitize=address)
  endif()
endif()

if (ASYNC_CORO_TSAN_ENABLED AND NOT WIN32)
  add_compile_options(-fsanitize=thread)
  add_link_options(-fsanitize=thread)

  if (CMAKE_CXX_COMPILER_ID STREQUAL GNU)
    add_compile_options(-Wno-error=tsan)
  endif()
endif()

# setup main lib project
add_subdirectory(async_coro)

if (ASYNC_CORO_TESTS_ENABLED)
  add_subdirectory(tests)
endif()
