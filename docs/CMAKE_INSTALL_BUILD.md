# Building Components from UMD Install Artifacts

This document explains how to build UMD examples, tools, and Python bindings using only the installed UMD artifacts (without the full source tree).

## Overview

The UMD project supports two build modes:

1. **Source Build** (default): Build everything from source in one go
2. **Install Build**: Build device library first, install it, then build client components from install

## Quick Start

### Step 1: Build and Install Device Library

```bash
# Build the device library
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/path/to/install
cmake --build build --target device

# Install the device library and headers
cmake --install build --prefix /path/to/install
```

### Step 2: Build Components from Install

Your custom component can be built using umd install artifacts.
To demonstrate that ability, we have an option of building some of our components as if they were external.
Each component (examples, tools, nanobind) can now be built independently:

#### Examples
```bash
cd examples/
cmake . -B build -G Ninja -DCMAKE_PREFIX_PATH=/path/to/install
cmake --build build
```

#### Tools
```bash
cd tools/
cmake . -B build -G Ninja -DCMAKE_PREFIX_PATH=/path/to/install
cmake --build build
```

#### Python Bindings
```bash
cd nanobind/
cmake . -B build -G Ninja -DCMAKE_PREFIX_PATH=/path/to/install
cmake --build build
```

## Install Structure

When you install UMD to `/path/to/install`, it creates:

```
/path/to/install/
├── lib/
│   ├── libdevice.so                    # Main UMD library
│   └── cmake/
│       └── umd/                        # CMake config files
│           ├── umdConfig.cmake
│           ├── umdConfigVersion.cmake
│           └── umdTargets.cmake
├── include/
│   └── umd/                            # Public headers
│       ├── device/
│       └── common/
└── bin/                                # (if tools are installed)
```

## Specifying Install Location

There are several ways to tell CMake where to find the installed UMD package:

### Method 1: CMAKE_PREFIX_PATH (Recommended)
```bash
cmake . -DCMAKE_PREFIX_PATH=/path/to/install
```

### Method 2: umd_DIR (Most Specific)
```bash
cmake . -Dumd_DIR=/path/to/install/lib/cmake/umd
```

### Method 3: Environment Variable
```bash
export CMAKE_PREFIX_PATH=/path/to/install
cmake .
```
