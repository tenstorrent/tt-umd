# Check if this is being built as a standalone project by running cmake from this dir
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # This is a root project build from install artifacts
    cmake_minimum_required(VERSION 3.16)
    project(tt_umd_examples)

    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    message(STATUS "Building examples as standalone project from UMD install artifacts")

    include(${PROJECT_SOURCE_DIR}/../cmake/example_client.cmake)
endif()

# Add all example subdirectories
add_subdirectory(tt_device_example)
add_subdirectory(reset_notification_example)
