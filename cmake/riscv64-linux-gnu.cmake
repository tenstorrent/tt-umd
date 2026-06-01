# CMake toolchain for cross-compiling tt-umd to a riscv64 Linux host.
#
# Used by the ubuntu-24.04-riscv-temp1 (cross-compile) CI experiment: an x86 build
# host runs the GNU riscv64-linux-gnu toolchain to emit riscv64 binaries, with no
# emulation. The matching riscv64 system libraries (hwloc) come from the image's
# cross sysroot.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR riscv64)

set(CMAKE_C_COMPILER riscv64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER riscv64-linux-gnu-g++)

# Ubuntu multiarch: riscv64 libraries live under /usr/lib/riscv64-linux-gnu.
set(CMAKE_LIBRARY_ARCHITECTURE riscv64-linux-gnu)
string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT " -L/usr/lib/riscv64-linux-gnu")
string(APPEND CMAKE_SHARED_LINKER_FLAGS_INIT " -L/usr/lib/riscv64-linux-gnu")

# Run host-native build tools, but resolve target libraries/headers/packages
# against the riscv64 sysroot as well as the host multiarch tree.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
