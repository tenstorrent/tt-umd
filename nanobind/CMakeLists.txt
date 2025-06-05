# Define the Python interface target
add_library(nanobind_libdevice MODULE cluster.cpp)

# Link the Python interface target to libdevice and nanobind
target_link_libraries(
    nanobind_libdevice
    PRIVATE
        libdevice
        nanobind::nanobind
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

# Ensure the Python interface target uses C++17 (required by nanobind)
target_compile_features(nanobind_libdevice PRIVATE cxx_std_17)

# Install the Python module to a test directory
install(TARGETS nanobind_libdevice LIBRARY DESTINATION ${CMAKE_BINARY_DIR}/python)
