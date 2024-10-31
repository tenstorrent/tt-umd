# Only perform the check if Clang is the compiler
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    include(CheckCXXCompilerFlag)

    check_cxx_compiler_flag(
        "-stdlib=libc++"
        HAS_LIBCPP
    )

    if(HAS_LIBCPP)
        message(STATUS "libc++ is available")
    else()
        message(FATAL_ERROR "libc++ was not detected! Please ensure that libc++ is installed and available.")
    endif()
endif()
