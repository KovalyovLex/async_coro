cmake_minimum_required(VERSION 3.16)

option(SERVER_ENABLE_TLS "Enable TLS support via OpenSSL" ON)
option(SERVER_ENABLE_HTTP2 "Enable HTTP/2 support" OFF)

# server lib
file(GLOB_RECURSE server_src LIST_DIRECTORIES false CONFIGURE_DEPENDS "src/**.cpp")

add_library(server_lib ${server_src})

target_include_directories(server_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_property(TARGET server_lib PROPERTY CXX_STANDARD 20)
target_link_libraries(server_lib PUBLIC async_coro)

if (WIN32)
    target_link_libraries(server_lib PRIVATE wepoll "-lWs2_32")
    target_link_libraries(server_lib PUBLIC ws2_32)
endif()

if(SERVER_ENABLE_TLS)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(server_lib PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()
