cmake_minimum_required(VERSION 3.16)

add_library(tt_kmd_lib STATIC)

target_sources(tt_kmd_lib PRIVATE src/tt_kmd_lib.c)

target_include_directories(
    tt_kmd_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/api>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
