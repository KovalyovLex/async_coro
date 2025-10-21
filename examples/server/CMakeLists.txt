cmake_minimum_required(VERSION 3.16)

project(async_coro_server LANGUAGES CXX)

option(SERVER_ENABLE_TLS "Enable TLS support via OpenSSL" ON)
option(SERVER_ENABLE_HTTP2 "Enable HTTP/2 support" OFF)


file(GLOB_RECURSE server_src LIST_DIRECTORIES false CONFIGURE_DEPENDS "src/**.h" "src/**.cpp")

add_executable(server_example ${server_src})

target_include_directories(server_example PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_property(TARGET server_example PROPERTY CXX_STANDARD 20)
target_link_libraries(server_example PRIVATE async_coro)

if(SERVER_ENABLE_TLS)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(server_example PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

