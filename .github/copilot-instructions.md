# Copilot Instructions for tt-umd

## Project Overview

tt-umd (Tenstorrent User Mode Driver) is a C++17 library that provides user-space access to Tenstorrent AI accelerator hardware. It supports Wormhole and Blackhole chip architectures.

## Build Verification

The environment is pre-configured with dependencies via `copilot-setup-steps.yml`.
To rebuild after making changes:

```bash
# Use GCC (set these env vars before cmake, or use clang-17 if available)
export CMAKE_C_COMPILER=/usr/bin/gcc
export CMAKE_CXX_COMPILER=/usr/bin/g++

cmake -B build -G Ninja -DTT_UMD_BUILD_TESTS=ON -DTT_UMD_ENABLE_CLANG_TIDY=OFF
cmake --build build
```

**Important**:
- Always use cmake/ninja to build. Do NOT compile files directly with g++ (dependencies like fmt are managed by CPM and require cmake).
- Do NOT modify `CMakeLists.txt`.

## Code Style Guidelines

- **C++ Standard**: C++17
- **Formatting**: clang-format (config in `.clang-format`)
- **Linting**: clang-tidy (config in `.clang-tidy`)

Follow the existing style in the file you're modifying.

## When Fixing Static Analysis Issues

1. Make minimal, targeted changes to fix the specific issue
2. Follow existing code patterns in the surrounding code
3. If a fix requires architectural changes, skip it
4. Prefer safe, conservative fixes over clever ones

### Common Issues and Fixes

**Virtual method call during construction**
`Call to virtual method 'X' during construction bypasses virtual dispatch`

Consider:
1. Use fully-qualified call: `ClassName::method()` to make intent explicit
2. Mark method/class `final` only if it's truly not meant to be extended

**Null pointer dereference**
- Add null check before dereferencing
- Use early return pattern

**Uninitialized variable**
- Initialize at declaration

**Dead store**
- Remove the unused assignment, or use the value

**Division by zero**
- Add check before division: `if (divisor != 0) { ... }`
- Use ternary with default: `divisor != 0 ? x / divisor : default_value`
- Assert if zero is a programming error: `TT_ASSERT(divisor != 0)`
- Use `std::max(divisor, 1)` if 1 is a safe minimum

