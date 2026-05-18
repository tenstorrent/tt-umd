file(
    GLOB_RECURSE MOCK_SOURCES
    CONFIGURE_DEPENDS
    "*.cpp"
    "*.cc"
    "*.cxx"
    "*.c"
    "*.src"
)

add_library(tt-umd-mock STATIC ${MOCK_SOURCES})
add_library(${PROJECT_NAME}::tt-umd-mock ALIAS tt-umd-mock)

target_include_directories(
    tt-umd-mock
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../src/common>
        # Needed by the copied grendel_implementation.cpp (includes blackhole/*.h).
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../src/firmware/riscv>
)

target_link_libraries(
    tt-umd-mock
    PUBLIC
        tt-umd-api
    PRIVATE
        fmt::fmt-header-only
        spdlog::spdlog_header_only
        "$<BUILD_INTERFACE:tt-logger::tt-logger>"
        "$<BUILD_INTERFACE:yaml-cpp::yaml-cpp>"
)

install(
    TARGETS
        tt-umd-mock
    EXPORT ${PROJECT_NAME}Targets
    ARCHIVE
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
        COMPONENT umd-dev
)

set_target_properties(
    tt-umd-mock
    PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY
            ${PROJECT_BINARY_DIR}/lib
)
