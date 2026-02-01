cmake_minimum_required(VERSION 3.16)

option(SERVER_ENABLE_TLS "Enable TLS support via OpenSSL" ON)
option(SERVER_ENABLE_DEFLATE_GZIP "Enable deflate/gzip compression for content" ON)
option(SERVER_ENABLE_BROTLI "Enable brotli compression for content" ON)
option(SERVER_ENABLE_ZSTD "Enable zstd compression for content" ON)
option(SERVER_ENABLE_HTTP2 "Enable HTTP/2 support" OFF)

include(FetchContent)

if (NOT DEFINED TRACY_ENABLE)
    set(TRACY_ENABLE OFF CACHE BOOL "Disable tracy by default")
endif()

# setup tracy
FetchContent_Declare(
    tracy
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG v0.12.2
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(tracy)

# server lib
file(GLOB_RECURSE server_src LIST_DIRECTORIES false CONFIGURE_DEPENDS "src/**.cpp")

add_library(server_lib ${server_src})

target_include_directories(server_lib 
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set_property(TARGET server_lib PROPERTY CXX_STANDARD 20)

target_link_libraries(server_lib PUBLIC async_coro Tracy::TracyClient)

if (WIN32)
    target_link_libraries(server_lib PRIVATE wepoll)
    target_link_libraries(server_lib PUBLIC ws2_32)
endif()

if(SERVER_ENABLE_TLS)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(server_lib PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(server_lib PUBLIC SERVER_ENABLE_OPENSSL=1)
else()
    target_compile_definitions(server_lib PUBLIC SERVER_ENABLE_OPENSSL=0)
endif()

if(SERVER_ENABLE_DEFLATE_GZIP)
    target_compile_definitions(server_lib PUBLIC SERVER_ENABLE_DEFLATE_GZIP=1)

    FetchContent_Declare(
        ZLIB
        URL https://zlib.net/zlib-1.3.1.tar.gz
        URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
        DOWNLOAD_EXTRACT_TIMESTAMP NO
        FIND_PACKAGE_ARGS
    )

    FetchContent_MakeAvailable(ZLIB)

    find_package(ZLIB REQUIRED)

    if (TARGET ZLIB::ZLIB)
        message(STATUS "Using predefined ZLIB::ZLIB target")

        set(zlib_targets ZLIB::ZLIB)
    else()
        set(zlib_targets zlib)
    endif()

    target_link_libraries(server_lib PUBLIC ${zlib_targets})
else()
    target_compile_definitions(server_lib PUBLIC SERVER_ENABLE_DEFLATE_GZIP=0)
endif()

if(SERVER_ENABLE_BROTLI)
    target_compile_definitions(server_lib PUBLIC SERVER_ENABLE_BROTLI=1)

    if (TARGET brotli::brotlienc)
        message(STATUS "Using predefined brotli::brotlienc/brotli::brotlidec targets")

        set(brotli_targets brotli::brotlienc brotli::brotlidec)
    else()
        set(brotli_targets brotlienc brotlidec)

        if(NOT TARGET brotlienc)
            message(STATUS "Going to fetch brotli from source")

            if (NOT DEFINED BROTLI_BUILD_TOOLS)
                set(BROTLI_BUILD_TOOLS OFF CACHE BOOL "Disable brotli tools build")
            endif()

            FetchContent_Declare(
                brotli
                GIT_REPOSITORY https://github.com/google/brotli.git
                GIT_TAG v1.2.0
                GIT_SHALLOW TRUE
                GIT_PROGRESS TRUE
            )

            # using static lib for brotli
            set(OLD_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
            set(BUILD_SHARED_LIBS OFF)
            
            FetchContent_MakeAvailable(brotli)
            
            set(BUILD_SHARED_LIBS ${OLD_BUILD_SHARED_LIBS})
        else()
            message(STATUS "Using predefined brotlienc and brotlidec targets")
        endif()
    endif()

    target_link_libraries(server_lib PUBLIC ${brotli_targets})
else()
    target_compile_definitions(server_lib PUBLIC SERVER_ENABLE_BROTLI=0)
endif()

if(SERVER_ENABLE_ZSTD)
    target_compile_definitions(server_lib PUBLIC SERVER_ENABLE_ZSTD=1)

    if (TARGET zstd::libzstd)
        message(STATUS "Using predefined zstd::libzstd target")

        set(zlib_targets zstd::libzstd)
    else()
        set(zlib_targets zstd)

        if(NOT TARGET zstd)
            message(STATUS "Going to fetch zstd from source")

            set(ZSTD_BUILD_STATIC ON)
            set(ZSTD_BUILD_SHARED OFF)
            set(ZSTD_FRAMEWORK OFF)
            set(ZSTD_BUILD_PROGRAMS OFF)
            set(ZSTD_BUILD_TESTS OFF)
            set(ZSTD_BUILD_CONTRIB OFF)
            
            FetchContent_Declare(
                zstd
                GIT_REPOSITORY https://github.com/facebook/zstd.git
                GIT_TAG v1.5.7
                GIT_SHALLOW TRUE
                GIT_PROGRESS TRUE
            )
            FetchContent_MakeAvailable(zstd)

            set(zlib_targets libzstd)
        else()
             message(STATUS "Using predefined zstd target")
        endif()
    endif()

    target_link_libraries(server_lib PUBLIC ${zlib_targets})
else()
    target_compile_definitions(server_lib PUBLIC SERVER_ENABLE_ZSTD=0)
endif()
