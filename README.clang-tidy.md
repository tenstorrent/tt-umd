# Integration of clang-tidy and clangd into UMD

This document describes the clang-tidy integration added to the UMD project for static code analysis and improved code quality.

## Overview

Clang-tidy is a clang-based C++ "linter" tool that provides static analysis to find bugs, performance issues, and style violations. This integration enables automated code quality checks during the build process.

### 1. CMake Integration

A new build option `TT_UMD_ENABLE_CLANG_TIDY` has been added to `CMakeLists.txt` that allows enabling clang-tidy checks during compilation:



When enabled, the build system will:
- Search for `clang-tidy` or `clang-tidy-17` executable
- Configure CMake to run clang-tidy on each source file during compilation using the project-specific configuration file `.clang-tidy-build`
- Enable real-time analysis through clangd in VSCode using the `.clang-tidy` configuration for immediate feedback during development

### 2. VSCode Configuration

The `.vscode/default.settings.json` contains configuration that allows the integration of clangd and clang-tody in the IDE itself.

#### clangd Integration
- **clangd.path**: Set to `clangd-17` to use a specific version
- **clangd.arguments**: Configured with optimal settings:
  - `--compile-commands-dir=${workspaceFolder}/build`: Use build directory for compilation database
  - `--header-insertion=never`: Disable automatic header insertion
  - `--enable-config`: Enable clangd configuration files

#### IntelliSense Configuration
- **C_Cpp.default.intelliSenseMode**: Set to `clang-x64` for consistency
- **C_Cpp.intelliSenseEngine**: Disabled to avoid conflicts with clangd

### 3. Configuration Files

The project uses two seprate configuration files: 
- `.clang-tidy-build` for build-time static analysis configuration (which will be used on the CI)
- `.clang-tidy` for real-time analysis integrated with clangd in VSCode

## Usage

### Building with clang-tidy

To enable clang-tidy both during the build process (applies the rules from `.clang-tidy-build`) and with clangd in the IDE (applies the rules from `.clang-tidy`) use the command:

```bash
cmake -B build -G Ninja -DTT_UMD_BUILD_TESTS=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DTT_UMD_ENABLE_CLANG_TIDY=ON
```

To enable clang-tidy with clangd only in the IDE (applies the rules from `.clang-tidy`):

```bash
# Build (clang-tidy will run automatically)
cmake --build build -DTT_UMD_BUILD_TESTS=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=1
```

Note: clang-tidy and clangd depend on the flag `CMAKE_EXPORT_COMPILE_COMMANDS` which generates the compilation database `compile_commands.json`.

Note: The flag `TT_UMD_BUILD_TESTS` isn't necessary, but it's almost always used, see the general `README.md` for more information.

### VSCode Setup

1. Install the clangd extension for VS Code
2. Ensure `clangd-17` is available in your PATH
3. The workspace settings will automatically configure clangd to work with the project

## Requirements

- **clang-tidy**: Version 17 recommended (clang-tidy-17)
- **clangd**: Version 17 recommended for VS Code integration
- **CMake**: Version 3.10 or higher

## Troubleshooting

### Common Issues

1. **Clang-tidy not found**: Install clang-tidy-17 or ensure it's in your PATH
2. **VS Code conflicts**: Disable C/C++ IntelliSense if using clangd

### Performance Considerations

- Clang-tidy analysis adds compilation time
- Use `TT_UMD_ENABLE_CLANG_TIDY=OFF` or omit the flag completely for faster development builds
