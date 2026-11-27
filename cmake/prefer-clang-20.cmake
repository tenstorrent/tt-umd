# Toolchain file for the CMake presets: build with clang-20 when it is installed.
#
# clang-20 is what tt-metal and the main CI jobs use. Compilers differ in the warnings they report
# (e.g. -Wconversion), and warnings are errors, so local builds should match it. It is only a preference:
# a compiler given through CC/CXX or -DCMAKE_<LANG>_COMPILER wins, and without clang-20 the build falls
# back to CMake's default compiler instead of failing, since UMD supports gcc too.

if(NOT CMAKE_C_COMPILER AND NOT CMAKE_CXX_COMPILER AND NOT DEFINED ENV{CC} AND NOT DEFINED ENV{CXX})
    find_program(TT_UMD_CLANG_20 clang-20)
    find_program(TT_UMD_CLANGXX_20 clang++-20)
    if(TT_UMD_CLANG_20 AND TT_UMD_CLANGXX_20)
        set(CMAKE_C_COMPILER "${TT_UMD_CLANG_20}")
        set(CMAKE_CXX_COMPILER "${TT_UMD_CLANGXX_20}")
    elseif(NOT PROJECT_NAME STREQUAL "CMAKE_TRY_COMPILE")
        # CMake reads this file several times per configure, including in its compiler checks; warn once.
        get_property(already_warned GLOBAL PROPERTY TT_UMD_CLANG_20_WARNED)
        if(NOT already_warned)
            set_property(
                GLOBAL
                PROPERTY
                    TT_UMD_CLANG_20_WARNED
                        TRUE
            )
            message(
                WARNING
                "clang-20 not found, building with the default compiler. CI builds with clang-20, whose warnings may differ from this compiler's."
            )
        endif()
    endif()
endif()
