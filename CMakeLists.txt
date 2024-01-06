cmake_minimum_required(VERSION 3.12)
 
project(async_coro)

add_subdirectory(async_coro)

set(gtest_force_shared_crt ON CACHE BOOL "Force use dynamic libs in tests" FORCE)

add_subdirectory(googletest)
add_subdirectory(tests)