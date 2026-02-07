cmake_minimum_required(VERSION 3.31)

project(async_coro)

# all available options for library
option(ASYNC_CORO_ASAN_ENABLED "Enable address sanitizer for all async_coro targets" OFF)
option(ASYNC_CORO_TSAN_ENABLED "Enable thread sanitizer for all async_coro targets" OFF)
option(ASYNC_CORO_NO_EXCEPTIONS "Disable exceptions support for async_coro library" FALSE)
option(ASYNC_CORO_TESTS_ENABLED "Enable async_coro tests in config" ON)


if (ASYNC_CORO_ASAN_ENABLED)
  if (WIN32)
    add_compile_options(/fsanitize=address)
    add_link_options(/fsanitize=address)
  else()
    add_compile_options(-fsanitize=address)
    add_link_options(-fsanitize=address)
  endif()
endif()

if (ASYNC_CORO_TSAN_ENABLED)
  if (NOT WIN32)
    add_compile_options(-fsanitize=thread)
    add_link_options(-fsanitize=thread)

    if (CMAKE_CXX_COMPILER_ID STREQUAL GNU)
      add_compile_options(-Wno-error=tsan)
    endif()
  else()
    message(WARNING "Option ASYNC_CORO_TSAN_ENABLED was enabled but windows platform has no support for thread sanitizers.")
  endif()
endif()

# setup main lib project
add_subdirectory(async_coro)

if (ASYNC_CORO_TESTS_ENABLED)
  include(CTest)

  add_subdirectory(tests)
endif()
