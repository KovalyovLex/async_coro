cmake_minimum_required(VERSION 3.16)

if (WIN32)
    file(GLOB_RECURSE wepoll_srcs LIST_DIRECTORIES false CONFIGURE_DEPENDS "wepoll/**.c", "wepoll/**.cpp")

    add_library(wepoll ${wepoll_srcs})

    target_include_directories(wepoll PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/wepoll
    )
endif()
