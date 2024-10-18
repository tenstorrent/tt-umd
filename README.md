# UMD
## About
Usermode Driver for Tenstorrent AI Accelerators

## Dependencies
Required Ubuntu dependencies:
```
sudo apt install -y libhwloc-dev cmake ninja-build
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

## As a submodule/external project
If your project has CMake support, simply add this repo as a subdirectory:
```
add_subdirectory(<path to umd>)
```
You can then use `libdevice.so` by linking against the `umd::device` target wheverever is needed.
```
target_link_libraries(tt_metal PUBLIC umd::device)
```
