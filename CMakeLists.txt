cmake_minimum_required(VERSION 3.12)
 
project(async_coro)

add_subdirectory(async_coro)

file(GLOB_RECURSE tests_src LIST_DIRECTORIES false CONFIGURE_DEPENDS "tests/**.h" "tests/**.cpp")

add_executable(async_coro_tests ${tests_src})
