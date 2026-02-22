cmake_minimum_required(VERSION 3.16)

if (WIN32)
    include(FetchContent)

    FetchContent_Populate(
        wepoll
        GIT_REPOSITORY https://github.com/piscisaureus/wepoll.git
        GIT_TAG v1.5.8
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
        SOURCE_DIR wepoll-src
    )

    file(GLOB_RECURSE wepoll_srcs LIST_DIRECTORIES false CONFIGURE_DEPENDS "${wepoll_SOURCE_DIR}/**.c")

    add_library(wepoll ${wepoll_srcs})

    target_include_directories(wepoll PUBLIC
        ${wepoll_SOURCE_DIR}
    )
endif()
