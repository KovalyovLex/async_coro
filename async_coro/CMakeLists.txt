cmake_minimum_required(VERSION 3.12)
 
project(async_coro)

file(GLOB_RECURSE async_coro_src LIST_DIRECTORIES false CONFIGURE_DEPENDS "src/**.h" "src/**.cpp")

add_library(async_coro ${async_coro_src})
target_include_directories(async_coro PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/../concurrentqueue)

set_property(TARGET async_coro PROPERTY CXX_STANDARD 20)

target_compile_options(async_coro PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic -Werror>
)
