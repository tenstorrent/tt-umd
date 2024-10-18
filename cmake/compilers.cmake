include_guard(GLOBAL)

function(FIND_AND_SET_CLANG17)
    find_program(CLANGPP_17 clang++-17)
    find_program(CLANG_17 clang-17)

    if(NOT CLANGPP_17 OR NOT CLANG_17)
        message(
            FATAL_ERROR
            "Clang-17 not found. Make sure you have clang-17 and clang++-17 installed and in your PATH"
        )
    endif()

    set(CMAKE_CXX_COMPILER "${CLANGPP_17}" PARENT_SCOPE)
    set(CMAKE_C_COMPILER "${CLANG_17}" PARENT_SCOPE)
endfunction()

function(CHECK_COMPILERS)
    message(STATUS "Checking compilers")

    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        if(
            CMAKE_CXX_COMPILER_VERSION
                VERSION_LESS
                "17.0.0"
            OR CMAKE_CXX_COMPILER_VERSION
                GREATER_EQUAL
                "18.0.0"
        )
            message(WARNING "Only Clang-17 is tested right now")
        endif()
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "12.0.0")
            message(FATAL_ERROR "GCC-12 or higher is required")
        elseif(CMAKE_CXX_COMPILER_VERSION GREATER_EQUAL "13.0.0")
            message(WARNING "Only GCC-12 is tested right now")
        endif()
    else()
        message(
            FATAL_ERROR
            "Unsupported compiler: ${CMAKE_CXX_COMPILER_ID} ! Only Clang and GCC are supported"
        )
    endif()
endfunction()

if(DEFINED ENV{CMAKE_C_COMPILER} AND DEFINED ENV{CMAKE_CXX_COMPILER})
    message(STATUS "Setting C and C++ compiler from environment variables")
    set(CMAKE_C_COMPILER $ENV{CMAKE_C_COMPILER})
    set(CMAKE_CXX_COMPILER $ENV{CMAKE_CXX_COMPILER})
endif()

if(CMAKE_CXX_COMPILER AND CMAKE_C_COMPILER)
    message(STATUS "Using specifed C++ compiler: ${CMAKE_CXX_COMPILER}")
    message(STATUS "Using specifed C compiler: ${CMAKE_C_COMPILER}")
else()
    message(STATUS "No C or C++ compiler specified, defaulting to Clang-17")
    FIND_AND_SET_CLANG17()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(
        STATUS
        "CRITICAL WARNING: Forcing LLVM version of the standard library for Clang"
    )
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
endif()
