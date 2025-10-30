# Check if this is being built as a standalone project by running cmake from this dir
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # This is a root project build from install artifacts
    cmake_minimum_required(VERSION 3.16)
    project(tt_umd_nanobind)

    message(STATUS "Building nanobind as standalone project from UMD install artifacts")

    include(${PROJECT_SOURCE_DIR}/../cmake/example_client.cmake)
endif()

# https://cmake.org/cmake/help/latest/module/FindPython.html
# https://github.com/wjakob/nanobind/blob/master/docs/building.rst
# Use Development.Module to support manylinux
# Python module library should not dynamically link libpython.so or use Development.Embed
find_package(Python REQUIRED COMPONENTS Development.Module)

# Define the Python interface target
nanobind_add_module(nanobind_tt_umd MODULE py_api_module.cpp py_api_basic_types.cpp py_api_topology_discovery.cpp py_api_telemetry.cpp py_api_tt_device.cpp py_api_cluster.cpp py_api_warm_reset.cpp)

# Set properties for the Python module
set_target_properties(
    nanobind_tt_umd
    PROPERTIES
        OUTPUT_NAME
            "tt_umd" # Name of the Python module
)

# The problem that RPATH is solving is the following:
# Some third party python package which uses tt_umd gets installed on the system. The artifacts of this package will end up in the same folder.
# When the third party package tries to include tt_umd module, it will correctly find it. The module will then include the python bindings .so file
# which is in the same folder and will be correctly found. However, the .so file itself requires libdevice.so (the umd library) file. The system tries to
# find this in the default system lib directory, but it is not there. The RunPATH variable will say to this lib loading system that it should look for the
# libdevice.so file in the same folder as the tt_umd module, which is where it is located.
set_target_properties(
    nanobind_tt_umd
    PROPERTIES
        BUILD_WITH_INSTALL_RPATH
            true
        INSTALL_RPATH
            "$ORIGIN"
)

# Link the Python interface target to libdevice and nanobind
target_link_libraries(
    nanobind_tt_umd
    PRIVATE
        umd::device
        fmt::fmt-header-only
)

# Install the Python module
install(
    TARGETS
        nanobind_tt_umd
    LIBRARY
        DESTINATION .
        COMPONENT python
)

# Install device library to root for Python package (co-located with the module)
# Only when building Python bindings to avoid conflicts with main install
if(TT_UMD_BUILD_PYTHON)
    install(
        TARGETS
            device
        LIBRARY
            DESTINATION .
            COMPONENT python
    )
endif()
