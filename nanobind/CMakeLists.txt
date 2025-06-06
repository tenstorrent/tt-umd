find_package(Python REQUIRED COMPONENTS Development)

# Define the Python interface target
nanobind_add_module(nanobind_libdevice MODULE cluster.cpp)

# Link the Python interface target to libdevice and nanobind
target_link_libraries(
    nanobind_libdevice
    PRIVATE
        umd::device
        fmt::fmt-header-only
)

# Include device API
target_include_directories(
    nanobind_libdevice
    INTERFACE
        ${PROJECT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
        "$<TARGET_PROPERTY:umd::device,INCLUDE_DIRECTORIES>"
)

# Set properties for the Python module
set_target_properties(
    nanobind_libdevice
    PROPERTIES
        OUTPUT_NAME
            "libdevice" # Name of the Python module
        PREFIX
            "" # Remove the default 'lib' prefix for Python modules
)
