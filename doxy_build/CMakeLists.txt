cmake_minimum_required(VERSION 3.16)
project(tt_device_doxy CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(tt_device STATIC tt_device.cpp)
target_include_directories(tt_device PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(stub)
add_subdirectory(mock)
