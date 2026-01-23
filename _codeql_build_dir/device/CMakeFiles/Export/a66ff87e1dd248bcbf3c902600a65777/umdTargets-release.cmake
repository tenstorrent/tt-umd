#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "umd::device" for configuration "Release"
set_property(TARGET umd::device APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(umd::device PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libdevice.so"
  IMPORTED_SONAME_RELEASE "libdevice.so"
  )

list(APPEND _cmake_import_check_targets umd::device )
list(APPEND _cmake_import_check_files_for_umd::device "${_IMPORT_PREFIX}/lib/libdevice.so" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
