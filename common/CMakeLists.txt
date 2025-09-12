add_library(umd_common INTERFACE)
add_library(${PROJECT_NAME}::Common ALIAS umd_common)

target_sources(
    umd_common
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/assert.hpp>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/backtrace.hpp>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/gtest_initializer.hpp> # FIXME: this should be tucked away with the tests
)

target_include_directories(
    umd_common
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/common>
)
