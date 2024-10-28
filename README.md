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

# Pre-commit Hook Integration for Formatting and Linting

As part of maintaining consistent code formatting across the project, we have integrated the [pre-commit](https://pre-commit.com/) framework into our workflow. The pre-commit hooks will help automatically check and format code before commits are made, ensuring that we adhere to the project's coding standards.

## What is Pre-commit?

Pre-commit is a framework for managing and maintaining multi-language pre-commit hooks. It helps catch common issues early by running a set of hooks before code is committed, automating tasks like:

- Formatting code (e.g., fixing trailing whitespace, enforcing end-of-file newlines)
- Running linters (e.g., `clang-format`, `black`, `flake8`)
- Checking for merge conflicts or other common issues.

For more details on pre-commit, you can visit the [official documentation](https://pre-commit.com/).

## How to Set Up Pre-commit Locally

To set up pre-commit on your local machine, follow these steps:

1. **Install Pre-commit**:
   Ensure you have Python installed, then run:
   ```bash
   pip install pre-commit
   ```  
2. **Install the Git Hook Scripts**:
   In your local repository, run the following command to install the pre-commit hooks:
   ```bash
   pre-commit install
   ```
   This command will configure your local Git to run the defined hooks automatically before each commit.
3. **Run Pre-commit Hooks Manually**:
   You can also run the hooks manually against all files at any time with:
   ```bash
   pre-commit run --all-files
   ```
## Why You Should Use Pre-commit
By setting up pre-commit locally, you can help maintain the quality of the codebase and ensure that commits consistently meet the project's formatting standards. This saves time during code reviews and reduces the likelihood of code formatting issues slipping into the repository.  
  
Since the hooks run automatically before each commit, you don't need to remember to manually format or check your code, making it easier to maintain consistency.  
  
We strongly encourage all developers to integrate pre-commit into their workflow.
