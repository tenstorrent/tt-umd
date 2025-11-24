# Check if there is ttexalens_private project already in cpmcache.
# If it is not, we should check if user has access to private repository.

set(TTEXALENS_PRIVATE_GIT_REPOSITORY "git@yyz-gitlab.local.tenstorrent.com:tenstorrent/tt-exalens-private.git")
set(TTEXALENS_PRIVATE_GIT_TAG "7433a053a54e7d05c85ea984be4f96e355300953")
option(DOWNLOAD_TTEXALENS_PRIVATE "Download tt-lens private repository" OFF)

if(DOWNLOAD_TTEXALENS_PRIVATE)
    if(NOT EXISTS "$ENV{CPM_SOURCE_CACHE}/ttexalens_private")
        # Trying to download private repository which will provide additional functionality that is not required
        # We do this by creating fake source directory and running CMake in it

        message(STATUS "Checking access for tt-lens-private repository...")

        file(
            COPY
                "${CMAKE_CURRENT_LIST_DIR}/ttexalens_private_check.cmake"
            DESTINATION "${CMAKE_BINARY_DIR}/tt_exalens_private_check"
        )

        file(
            WRITE
            "${CMAKE_BINARY_DIR}/tt_exalens_private_check/CMakeLists.txt"
            "cmake_minimum_required(VERSION 3.16)\ncmake_policy(VERSION 3.16)\ninclude(\${CMAKE_CURRENT_LIST_DIR}/ttexalens_private_check.cmake)"
        )

        message(STATUS "Checking for git-lfs...")
        find_program(GIT_LFS_EXECUTABLE NAMES git-lfs)
        message(STATUS "git-lfs executable: ${GIT_LFS_EXECUTABLE}")

        if(GIT_LFS_EXECUTABLE MATCHES "NOTFOUND")
            # git-lfs is not installed
            message(FATAL_ERROR "git-lfs is not installed. Please install git-lfs.")
            set(DOWNLOAD_TTEXALENS_PRIVATE OFF)
        else()
            execute_process(
                COMMAND
                    ${CMAKE_COMMAND} -B build -Wno-dev
                    -DTTEXALENS_PRIVATE_GIT_REPOSITORY=${TTEXALENS_PRIVATE_GIT_REPOSITORY}
                    -DTTEXALENS_PRIVATE_GIT_TAG=${TTEXALENS_PRIVATE_GIT_TAG}
                WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/tt_exalens_private_check"
                RESULT_VARIABLE TTEXALENS_PRIVATE_CHECK_RESULT
                OUTPUT_VARIABLE TTEXALENS_PRIVATE_CHECK_OUTPUT
                ERROR_QUIET
            )

            if(TTEXALENS_PRIVATE_CHECK_RESULT EQUAL "0")
                execute_process(
                    COMMAND
                        ${CMAKE_COMMAND} --build build
                    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/tt_exalens_private_check"
                    RESULT_VARIABLE TTEXALENS_PRIVATE_CHECK_RESULT
                    OUTPUT_VARIABLE TTEXALENS_PRIVATE_CHECK_OUTPUT
                )
            endif()

            # Check if we succeeded in cloning the private repository
            if(NOT TTEXALENS_PRIVATE_CHECK_RESULT EQUAL "0")
                message(
                    WARNING
                    "tt-lens-private project check failed with error code ${TTEXALENS_PRIVATE_CHECK_RESULT}. Continuing without it."
                )
                set(DOWNLOAD_TTEXALENS_PRIVATE OFF)
            endif()
        endif()
    endif()
endif()

if(DOWNLOAD_TTEXALENS_PRIVATE)
    CPMAddPackage(
        NAME ttexalens_private
        GIT_REPOSITORY ${TTEXALENS_PRIVATE_GIT_REPOSITORY}
        GIT_TAG ${TTEXALENS_PRIVATE_GIT_TAG}
    )
else()
    # Since we don't have ttexalens_private project, we need to create empty jtag interface library
    add_library(ttexalens_jtag_lib INTERFACE)
endif()
