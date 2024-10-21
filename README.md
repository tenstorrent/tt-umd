# UMD
## About
Usermode Driver for Tenstorrent AI Accelerators

## Dependencies
Required Ubuntu dependencies:
```
sudo apt install -y libhwloc-dev cmake ninja-build
```

Suggested third-party dependency is Clang 17:
```
wget https://apt.llvm.org/llvm.sh
chmod u+x llvm.sh
sudo ./llvm.sh 17
```

## Build flow

To build `libdevice.so`: 
```
cmake -B build -G Ninja
cmake --build build
```

Tests are build separatelly for each architecture.
Specify the `ARCH_NAME` environment variable as `grayskull`,  `wormhole_b0` or `blackhole` before building.
You also need to configure cmake to enable tests, hence the need to run cmake configuration step again.
To build tests:
```
cmake -B build -G Ninja -DTT_UMD_BUILD_TESTS=ON
cmake --build build
```

To build with GCC, set these environment variables before invoking `cmake`:
```
export CMAKE_C_COMPILER=/usr/bin/gcc
export CMAKE_CXX_COMPILER=/usr/bin/g++
```

## Build debian dev package
```
cmake --build build --target package

# Generates umd-dev-x.y.z-Linux.deb
```

# Integration
UMD can be consumed by downstream projects in multiple ways.

## From Source (CMake)
You can link `libdevice.so` by linking against the `umd::device` target.

### Using CPM Package Manager
```
CPMAddPackage(
  NAME umd
  GITHUB_REPOSITORY tenstorrent/tt-umd
  GIT_TAG v0.1.0
  VERSION 0.1.0
)
```

### As a submodule/external project
```
add_subdirectory(<path to umd>)
```

## From Prebuilt Binaries

### Ubuntu
```
apt install ./umd-dev-x.y.z-Linux.deb 
```

