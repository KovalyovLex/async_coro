cmake_minimum_required(VERSION 3.16)

project(async_coro_server LANGUAGES CXX)

add_subdirectory(lib)

file(GLOB_RECURSE server_src LIST_DIRECTORIES false CONFIGURE_DEPENDS "src/**.cpp")

add_executable(server_example ${server_src})

target_include_directories(server_example PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_property(TARGET server_example PROPERTY CXX_STANDARD 20)
target_link_libraries(server_example PRIVATE server_lib)

# tests

file(GLOB_RECURSE tests_src LIST_DIRECTORIES false CONFIGURE_DEPENDS "tests/src/**.h" "tests/src/**.cpp")

add_executable(server_example_tests ${tests_src})
target_link_libraries(server_example_tests server_lib gtest)

target_include_directories(server_example_tests PRIVATE "tests/src")
set_property(TARGET server_example_tests PROPERTY CXX_STANDARD 20)

target_compile_options(server_example_tests PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /permissive->
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic -Werror -std=c++20>
)

gtest_add_tests(TARGET      server_example_tests
                SOURCES     ${tests_src}
                TEST_LIST   tests_list
                EXTRA_ARGS  "--gtest_repeat=10"
)

set_tests_properties(${tests_list} PROPERTIES TIMEOUT 120)
