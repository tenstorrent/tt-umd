# https://cmake.org/cmake/help/latest/module/FindPython.html
# https://github.com/wjakob/nanobind/blob/master/docs/building.rst
# Use Development.Module to support manylinux
# Python module library should not dynamically link libpython.so or use Development.Embed
find_package(Python REQUIRED COMPONENTS Development.Module)

# Define the Python interface target
nanobind_add_module(nanobind_libdevice MODULE cluster.cpp)

# Set properties for the Python module
set_target_properties(
    nanobind_libdevice
    PROPERTIES
        OUTPUT_NAME
            "libdevice" # Name of the Python module
)

# Link the Python interface target to libdevice and nanobind
target_link_libraries(
    nanobind_libdevice
    PRIVATE
        umd::device
        fmt::fmt-header-only
)
