#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "umd::device" for configuration "Debug"
set_property(TARGET umd::device APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(umd::device PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libdevice.so"
  IMPORTED_SONAME_DEBUG "libdevice.so"
  )

list(APPEND _cmake_import_check_targets umd::device )
list(APPEND _cmake_import_check_files_for_umd::device "${_IMPORT_PREFIX}/lib/libdevice.so" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
